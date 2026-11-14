// Turbo Racing Mayhem - a pseudo-3D arcade racer in the style of Outrun / Lotus Esprit Turbo Challenge
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "audio.h"
#include "canvas.h"
#include "input.h"
#include "race.h"
#include "render.h"
#include "sprites.h"
#include "cars.h"
#include "assets.h"
#include "text.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace {

enum class State { Title, Racing, Paused, Results, Standings };

struct ResultRow { int driver; int car; bool dnf; int points; };

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SDL_Texture* upscaled = nullptr;   // integer-upscaled copy for non-integer window sizes
    int upscaleK = 0;
    Canvas cv;
    UiCanvas gui{cv};          // menus / overlays in layout units
    bool running = true;
    bool fullscreen = false;

    State state = State::Title;
    Options opt;
    std::vector<Driver> drivers;
    int stage = 0;
    Race race;
    Race attract;
    int attractStage = 0;
    double attractTimer = 0;
    int frame = 0;
    int menuSel = 0;
    int pauseSel = 0;
    std::vector<ResultRow> results;
    bool anyQualified = false, anyFinished = false;
    double stateTime = 0;

    // ------------------------------------------------------------ setup ----
    bool init(bool headless) {
        Uint32 flags = SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS;
        if (!headless) flags |= SDL_INIT_AUDIO;
        if (SDL_Init(flags) != 0) { SDL_Log("SDL_Init: %s", SDL_GetError()); return false; }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#ifdef __SWITCH__
        window = SDL_CreateWindow(GAME_TITLE, 0, 0, 1280, 720, SDL_WINDOW_FULLSCREEN);
#else
        window = SDL_CreateWindow(GAME_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W * 2, SCREEN_H * 2,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (headless ? SDL_WINDOW_HIDDEN : 0));
#endif
        if (!window) { SDL_Log("SDL_CreateWindow: %s", SDL_GetError()); return false; }
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
        if (!renderer) { SDL_Log("SDL_CreateRenderer: %s", SDL_GetError()); return false; }
        SDL_RenderSetLogicalSize(renderer, SCREEN_W, SCREEN_H);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CANVAS_W, CANVAS_H);
        if (!headless) audio.init();
        input.init();
        if (!text.init()) return false;
        if (!headless) showMessage("LOADING", "");
        std::string dir = findAssetsDir(), err;
        if (!loadAssets(dir, gfx, err)) {
            std::string msg = "Could not load the game graphics from '" + dir + "':\n" + err +
                              "\n\nRun turbo_assetgen to create the assets folder.";
            SDL_Log("%s", msg.c_str());
            if (!headless) {
                showMessage("CANNOT LOAD GRAPHICS", msg + "\n\nPress any button to quit.");
#ifndef __SWITCH__
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GAME_TITLE, msg.c_str(), window);
#else
                waitForButton();
#endif
            }
            return false;
        }
        startAttract(0);
        return true;
    }

    // a plain screen with a title and a few lines of text (loading / error messages)
    void showMessage(const std::string& title, const std::string& body) {
        text.beginFrame(renderer, frame);
        cv.resetClip();
        cv.fill(rgb(6, 8, 26));
        text.draw(title, SCREEN_W / 2.0, 120, styleDisplay(28, rgb(255, 250, 150), rgb(255, 120, 20)), ALIGN_CENTER);
        size_t start = 0;
        for (int line = 0; start <= body.size() && !body.empty(); ++line) {
            size_t end = body.find('\n', start);
            std::string l = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
            text.draw(l, SCREEN_W / 2.0, 175 + line * 14, styleUI(8.5, rgb(220, 224, 240)), ALIGN_CENTER);
            if (end == std::string::npos) break;
            start = end + 1;
        }
        present();
    }
    void waitForButton() {
        for (Uint64 t0 = SDL_GetTicks64(); SDL_GetTicks64() - t0 < 120000;) {
#ifdef __SWITCH__
            if (!appletMainLoop()) return;
#endif
            SDL_Event e;
            while (SDL_PollEvent(&e))
                if (e.type == SDL_QUIT || e.type == SDL_KEYDOWN || e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_JOYBUTTONDOWN) return;
            SDL_Delay(16);
        }
    }

    void shutdown() {
        audio.shutdown();
        text.shutdown();
        if (upscaled) SDL_DestroyTexture(upscaled);
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void startAttract(int st) {
        attractStage = st;
        attractTimer = 0;
        Options o; o.players = 0;
        attract.start(st, makeDrivers(0), o, true);
    }


    int attractCar() const {
        int best = 0;
        for (int i = 0; i < int(attract.cars.size()); ++i)
            if (attract.cars[i].skill > attract.cars[best].skill) best = i;
        return best;
    }

    void newGame() {
        drivers = makeDrivers(opt.players);
        for (auto& d : drivers) if (d.player >= 0) d.type = opt.carType[d.player];
        stage = opt.championship ? 0 : opt.startStage;
        startRace();
    }

    void startRace() {
        race.start(stage, drivers, opt);
        state = State::Racing;
        stateTime = 0;
    }

    // ------------------------------------------------------------- menu ----
    enum Item { IT_START, IT_MODE, IT_STAGE, IT_PLAYERS, IT_CAR1, IT_GEARS1, IT_CAR2, IT_GEARS2, IT_LAPS, IT_DIFFICULTY, IT_SOUND, IT_QUIT };
    std::vector<Item> menuItems() const {
        std::vector<Item> v = {IT_START, IT_MODE};
        if (!opt.championship) v.push_back(IT_STAGE);
        v.push_back(IT_PLAYERS);
        v.push_back(IT_CAR1);
        v.push_back(IT_GEARS1);
        if (opt.players > 1) { v.push_back(IT_CAR2); v.push_back(IT_GEARS2); }
        v.push_back(IT_LAPS);
        v.push_back(IT_DIFFICULTY);
        v.push_back(IT_SOUND);
#ifndef __SWITCH__
        v.push_back(IT_QUIT);
#endif
        return v;
    }
    std::pair<std::string, std::string> itemText(Item it) const {
        switch (it) {
        case IT_START:   return {opt.championship ? "START CHAMPIONSHIP" : "START RACE", ""};
        case IT_MODE:    return {"MODE", opt.championship ? "CHAMPIONSHIP" : "SINGLE RACE"};
        case IT_STAGE:   return {"STAGE", STAGE_NAMES[opt.startStage]};
        case IT_PLAYERS: return {"PLAYERS", opt.players > 1 ? "2 SPLIT SCREEN" : "1"};
        case IT_CAR1:    return {"CAR P1", CAR_STATS[opt.carType[0]].name};
        case IT_CAR2:    return {"CAR P2", CAR_STATS[opt.carType[1]].name};
        case IT_GEARS1:  return {"GEARS P1", opt.manual[0] ? "MANUAL" : "AUTOMATIC"};
        case IT_GEARS2:  return {"GEARS P2", opt.manual[1] ? "MANUAL" : "AUTOMATIC"};
        case IT_LAPS:    return {"LAPS", std::to_string(opt.laps)};
        case IT_DIFFICULTY: { static const char* const D[] = {"EASY", "MEDIUM", "HARD"}; return {"DIFFICULTY", D[opt.difficulty]}; }
        case IT_SOUND:   return {"SOUND", opt.sound ? "ON" : "OFF"};
        case IT_QUIT:    return {"QUIT", ""};
        }
        return {"", ""};
    }
    void changeItem(Item it, int d) {
        switch (it) {
        case IT_MODE: opt.championship = !opt.championship; break;
        case IT_STAGE: opt.startStage = (opt.startStage + d + NUM_STAGES) % NUM_STAGES; startAttract(opt.startStage); break;
        case IT_PLAYERS: opt.players = opt.players == 1 ? 2 : 1; break;
        case IT_CAR1: opt.carType[0] = (opt.carType[0] + d + CAR_TYPES) % CAR_TYPES; break;
        case IT_CAR2: opt.carType[1] = (opt.carType[1] + d + CAR_TYPES) % CAR_TYPES; break;
        case IT_GEARS1: opt.manual[0] = !opt.manual[0]; break;
        case IT_GEARS2: opt.manual[1] = !opt.manual[1]; break;
        case IT_LAPS: opt.laps = std::clamp(opt.laps + d, 1, 9); break;
        case IT_DIFFICULTY: opt.difficulty = (opt.difficulty + d + 3) % 3; break;
        case IT_SOUND: opt.sound = !opt.sound; audio.setEnabled(opt.sound); break;
        default: return;
        }
        audio.play(SFX_MENU_MOVE);
    }

    void updateTitle(double dt) {
        PlayerInput none[2];
        attract.update(dt, none);
        attractTimer += dt;
        if (attractTimer > 45 && opt.championship) startAttract((attractStage + 1) % NUM_STAGES);
        auto items = menuItems();
        menuSel = std::clamp(menuSel, 0, int(items.size()) - 1);
        MenuInput& m = input.menu;
        if (m.up)   { menuSel = (menuSel + int(items.size()) - 1) % int(items.size()); audio.play(SFX_MENU_MOVE); }
        if (m.down) { menuSel = (menuSel + 1) % int(items.size()); audio.play(SFX_MENU_MOVE); }
        Item it = items[menuSel];
        if (m.left) changeItem(it, -1);
        if (m.right) changeItem(it, +1);
        if (m.confirm) {
            if (it == IT_START) { audio.play(SFX_MENU_SELECT); newGame(); }
            else if (it == IT_QUIT) running = false;
            else changeItem(it, +1);
        }
#ifndef __SWITCH__
        if (m.back && !m.confirm) running = false;
#endif
    }

    // ------------------------------------------------------------- race ----
    void updateRace(double dt) {
        stateTime += dt;
        if (input.menu.pause || input.menu.background) { state = State::Paused; pauseSel = 0; audio.silenceLoops(); audio.setTunnel(0); return; }
        PlayerInput in[2];
        for (int p = 0; p < opt.players; ++p) in[p] = input.player(p, opt.players);
        race.update(dt, in);
        double tunnel = 0;
        for (int p = 0; p < opt.players; ++p) { int ci = race.humanCar(p); if (ci >= 0 && race.cars[ci].inTunnel) tunnel = 1; }
        audio.setTunnel(tunnel);
        for (int p = 0; p < 2; ++p) {
            int ci = race.humanCar(p);
            if (ci < 0) { audio.setEngine(p, 0, 0, false); audio.setSkid(p, 0); audio.setOffroad(p, 0); continue; }
            const Car& c = race.cars[ci];
            audio.setEngine(p, c.rpm, c.throttle, true);
            audio.setSkid(p, c.skid);
            double rumble = c.offroad ? std::min(1.0, c.speed / (MAX_SPEED * 0.3)) : 0;
            if (c.onDirt && !c.airborne) rumble = std::max(rumble, 0.45 * std::min(1.0, c.speed / (MAX_SPEED * 0.5)));
            audio.setOffroad(p, rumble);
        }
        updateOpponentSounds(race, race.humanCar(0));
        if (race.done()) finishRace();
    }

    // The engines of the opponents nearest to the listener (player 1's car): louder the closer they are,
    // panned to where they are, and Doppler-shifted - higher while a car closes in, lower as it pulls away.
    int oppVoice[Audio::OPP_VOICES] = {-1, -1, -1, -1, -1};
    void updateOpponentSounds(const Race& r, int listener) {
        if (listener < 0) { for (int v = 0; v < Audio::OPP_VOICES; ++v) audio.setOpponent(v, 0, 0, 0, 1); return; }
        const Car& L = r.cars[listener];
        const double HEAR = 9000;                   // world units: engines further away are not heard
        const double SOUND = MAX_SPEED * 0.55;     // "speed of sound": much lower than real, so the effect is clear
        struct Cand { int ci; double dist, dz, dx; };
        std::vector<Cand> near;
        for (int i = 0; i < int(r.cars.size()); ++i) {
            if (i == listener || r.cars[i].player >= 0) continue;
            double dz = r.delta(L.dist, r.cars[i].dist), dx = (r.cars[i].x - L.x) * ROAD_WIDTH;
            double dist = std::sqrt(dz * dz + dx * dx);
            if (dist < HEAR) near.push_back({i, dist, dz, dx});
        }
        std::sort(near.begin(), near.end(), [](const Cand& a, const Cand& b) { return a.dist < b.dist; });
        if (near.size() > size_t(Audio::OPP_VOICES)) near.resize(Audio::OPP_VOICES);
        // keep each car on the voice it already has (no jumps in the sound), give free voices to new cars
        int assign[Audio::OPP_VOICES];
        for (int v = 0; v < Audio::OPP_VOICES; ++v) {
            assign[v] = -1;
            for (size_t k = 0; k < near.size(); ++k) if (near[k].ci == oppVoice[v]) assign[v] = int(k);
        }
        for (size_t k = 0; k < near.size(); ++k) {
            bool used = false;
            for (int v = 0; v < Audio::OPP_VOICES; ++v) used |= assign[v] == int(k);
            for (int v = 0; v < Audio::OPP_VOICES && !used; ++v) if (assign[v] < 0) { assign[v] = int(k); used = true; }
        }
        for (int v = 0; v < Audio::OPP_VOICES; ++v) {
            if (assign[v] < 0) { oppVoice[v] = -1; audio.setOpponent(v, 0, 0, 0, 1); continue; }
            const Cand& c = near[size_t(assign[v])];
            const Car& o = r.cars[size_t(c.ci)];
            oppVoice[v] = c.ci;
            double d = std::max(c.dist, 300.0);
            double vol = std::clamp(1.0 / (1 + (d / 2800) * (d / 2800)) - 0.03, 0.0, 1.0);
            double pan = std::clamp(c.dx / (std::fabs(c.dz) + 600), -1.0, 1.0);
            double closing = -(c.dz / d) * (o.speed - L.speed);            // > 0: the distance is shrinking
            double pitch = std::clamp(SOUND / (SOUND - closing), 0.7, 1.45);
            static const double TONE[CAR_TYPES] = {0.6, 0.7, 0.9, 0.2, 0.1, 0.4};   // sport, coupe, super, SUV, pickup, sedan
            audio.setOpponent(v, o.rpm, vol, pan, pitch, TONE[std::clamp(o.type, 0, CAR_TYPES - 1)]);
        }
    }

    void finishRace() {
        audio.silenceLoops();
        audio.setTunnel(0);
        results.clear();
        std::vector<int> ord = race.order;
        std::stable_partition(ord.begin(), ord.end(), [&](int ci) { return !race.cars[ci].timedOut; });
        anyQualified = false;
        anyFinished = false;
        int place = 0;
        for (int ci : ord) {
            const Car& c = race.cars[ci];
            ResultRow r{c.driver, ci, c.timedOut, 0};
            if (!r.dnf) {
                if (c.player >= 0) anyFinished = true;
                if (place < 10) r.points = POINTS[place];
                if (c.player >= 0 && place < QUALIFY_POS) anyQualified = true;
            }
            place++;
            if (opt.championship) drivers[c.driver].points += r.points;
            results.push_back(r);
        }
        state = State::Results;
        stateTime = 0;
    }

    void updatePause() {
        MenuInput& m = input.menu;
        if (m.up || m.down) { pauseSel ^= 1; audio.play(SFX_MENU_MOVE); }
        if (m.pause && !m.confirm) { state = State::Racing; return; }
        if (m.confirm) {
            if (pauseSel == 0) state = State::Racing;
            else { state = State::Title; startAttract(stage); }
            audio.play(SFX_MENU_SELECT);
        }
    }

    void updateResults() {
        if (stateTime > 1.0 && input.menu.confirm) {
            audio.play(SFX_MENU_SELECT);
            if (opt.championship) { state = State::Standings; stateTime = 0; }
            else { state = State::Title; startAttract(stage); }
        }
    }

    bool championshipContinues() const { return anyQualified && stage < NUM_STAGES - 1; }

    void updateStandings() {
        if (stateTime > 1.0 && input.menu.confirm) {
            audio.play(SFX_MENU_SELECT);
            if (championshipContinues()) { stage++; startRace(); }
            else { state = State::Title; startAttract(0); }
        }
    }

    // ----------------------------------------------------------- render ----
    // small helpers for the menu screens
    static TextStyle ui(double size, Color c) { return styleUI(size, c); }
    static Rect toPx(Rect r) { return {r.x * RENDER_SCALE, r.y * RENDER_SCALE, r.w * RENDER_SCALE, r.h * RENDER_SCALE}; }
    void panel(int x, int y, int w, int h, Color top = rgb(16, 20, 60), Color bottom = rgb(4, 4, 16), double a = 0.78) {
        gui.vgradRect(x, y, w, h, top, bottom, a);
        gui.rectBlend(x, y, w, 1, rgb(170, 200, 255), 0.5);
        gui.rectBlend(x, y + h - 1, w, 1, rgb(0, 0, 0), 0.6);
    }
    void colorChip(int x, int y, Color c) {
        gui.vgradRect(x, y, 12, 9, shade(c, 1.3), shade(c, 0.6));
        gui.rectBlend(x, y, 12, 1, rgb(255, 255, 255), 0.5);
    }

    void drawLogo(double y) {
        TextStyle st = styleDisplay(38, rgb(255, 250, 150), rgb(240, 40, 70));
        st.outline = rgb(30, 8, 40);
        st.outlineW = 2.5;
        text.draw("TURBO RACING MAYHEM", SCREEN_W / 2.0, y + 6, st, ALIGN_CENTER);
    }

    void renderTitle() {
        ViewOptions vo; vo.hud = false; vo.frame = frame;
        renderViewParallel(cv, toPx({0, 0, SCREEN_W, SCREEN_H}), attract, attractCar(), vo);
        drawLogo(14);
        TextStyle sub = ui(13, rgb(255, 255, 255));
        sub.color2 = rgb(160, 210, 255);
        text.draw("PSEUDO 3D ARCADE RACER", SCREEN_W / 2.0, 72, sub, ALIGN_CENTER);
        auto items = menuItems();
        // rows shrink for long menus (single race + 2 players) so the panel stays above the hint lines
        const int n = int(items.size()), y0 = 100;
        int rowH = std::min(21, (SCREEN_H - 46 - y0 - 14) / n);
        const bool driftTip = rowH >= 17;
        if (!driftTip) rowH = std::min(21, (SCREEN_H - 34 - y0 - 14) / n);
        int w = 400, h = n * rowH + 14;
        int x0 = SCREEN_W / 2 - w / 2;
        panel(x0, y0, w, h);
        for (int i = 0; i < int(items.size()); ++i) {
            bool sel = i == menuSel;
            auto [label, value] = itemText(items[i]);
            int y = y0 + 8 + i * rowH;
            if (sel) {
                gui.vgradRect(x0 + 6, y - 1, w - 12, rowH - 2, rgb(255, 210, 60), rgb(200, 90, 20), 0.35);
                gui.rectBlend(x0 + 6, y - 1, 3, rowH - 2, rgb(255, 230, 80), 0.9);
            }
            TextStyle ls = ui(13, sel ? rgb(255, 230, 80) : rgb(230, 232, 245));
            if (value.empty()) { text.draw(label, SCREEN_W / 2.0, y, ls, ALIGN_CENTER); continue; }
            text.draw(label, x0 + 22, y, ls);
            if (sel) value = "<  " + value + "  >";
            text.draw(value, x0 + w - 22, y, ui(13, sel ? rgb(255, 255, 255) : rgb(140, 205, 255)), ALIGN_RIGHT);
        }
        // car preview with stats when a CAR row is selected
        if (items[menuSel] == IT_CAR1 || items[menuSel] == IT_CAR2) {
            int pl = items[menuSel] == IT_CAR1 ? 0 : 1;
            int type = opt.carType[pl];
            int px = x0 + w + 6, pw = SCREEN_W - px - 4, py = y0;
            panel(px, py, pw, 150);
            const CarStats& st = CAR_STATS[type];
            text.draw(st.name, px + pw / 2.0, py + 4, ui(11, rgb(255, 230, 80)), ALIGN_CENTER);
            int steer = int(std::lround(std::sin(frame * 0.03) * (CAR_YAWS - 1)));   // show it swaying left/right
            int pitch = (frame / 90) % 4 == 1 ? CAR_ELEV - 1 : (frame / 90) % 4 == 3 ? 1 : CAR_ELEV_LEVEL;   // uphill/downhill
            double cw = pw - 8, ch = cw * CAR_BMP_H / CAR_BMP_W;
            const double R = RENDER_SCALE;
            drawCar(cv, type, CAR_COLORS[pl], steer, pitch, (frame / 45) % 3 == 0, (px + 4) * R, (py + 16) * R, cw * R, ch * R);
            const char* names[4] = {"SPEED", "ACCEL", "GRIP", "DIRT"};
            double vals[4] = {(st.top - 0.85) / 0.2, (st.accel - 0.88) / 0.2, (st.grip - 0.86) / 0.24, (st.dirtTop - 0.7) / 0.24};
            for (int i = 0; i < 4; ++i) {
                int by = py + 18 + int(ch) + i * 11;
                text.draw(names[i], px + 5, by - 2, ui(7.5, rgb(200, 205, 225)));
                int bx = px + 40, bw = pw - 46;
                gui.rect(bx, by + 1, bw, 5, rgb(30, 32, 44));
                gui.vgradRect(bx, by + 1, int(bw * std::clamp(vals[i], 0.08, 1.0)), 5, rgb(120, 255, 140), rgb(30, 160, 60));
            }
        }
        TextStyle hint = ui(9.5, rgb(225, 228, 240));
        if (driftTip) text.draw("TAP THE BRAKE IN A BEND TO DRIFT  -  DRIFTING REFILLS THE NITRO", SCREEN_W / 2.0, SCREEN_H - 42, hint, ALIGN_CENTER);
        text.draw("P1: ARROWS, SPACE NITRO, Z/X GEARS    P2: WASD, SHIFT NITRO, Q/E    PADS: A/B, X/Y NITRO, LB/RB", SCREEN_W / 2.0, SCREEN_H - 30, hint, ALIGN_CENTER);
        text.draw("v" GAME_VERSION "  -  by rsn8887", SCREEN_W / 2.0, SCREEN_H - 16, ui(8.5, rgb(170, 175, 200)), ALIGN_CENTER);
    }

    void renderRace() {
        ViewOptions vo; vo.frame = frame;
        if (opt.players == 1) {
            renderViewParallel(cv, toPx({0, 0, SCREEN_W, SCREEN_H}), race, race.humanCar(0), vo);
        } else {
            renderViewParallel(cv, toPx({0, 0, SCREEN_W, SCREEN_H / 2 - 1}), race, race.humanCar(0), vo);
            renderViewParallel(cv, toPx({0, SCREEN_H / 2 + 1, SCREEN_W, SCREEN_H / 2 - 1}), race, race.humanCar(1), vo);
            gui.vgradRect(0, SCREEN_H / 2 - 1, SCREEN_W, 2, rgb(200, 200, 220), rgb(20, 20, 30));
        }
        if (race.phase == RacePhase::Countdown && race.countdown > 3.0) {
            std::string s = std::string(opt.championship ? "RACE " + std::to_string(stage + 1) + "  -  " : "") + STAGE_NAMES[stage];
            panel(0, SCREEN_H / 2 - 30, SCREEN_W, 60, rgb(10, 10, 40), rgb(0, 0, 0), 0.7);
            text.draw(s, SCREEN_W / 2.0, SCREEN_H / 2.0 - 24, styleDisplay(24, rgb(255, 245, 150), rgb(255, 120, 20)), ALIGN_CENTER);
            text.draw(std::to_string(race.laps) + " LAPS   -   FINISH TOP " + std::to_string(QUALIFY_POS) + " TO QUALIFY",
                      SCREEN_W / 2.0, SCREEN_H / 2.0 + 8, ui(11, rgb(255, 255, 255)), ALIGN_CENTER);
        }
        if (state == State::Paused) {
            gui.rectBlend(0, 0, SCREEN_W, SCREEN_H, rgb(0, 0, 10), 0.55);
            text.draw("PAUSED", SCREEN_W / 2.0, 110, styleDisplay(40, rgb(255, 250, 150), rgb(255, 120, 20)), ALIGN_CENTER);
            const char* it[2] = {"RESUME", "QUIT TO TITLE"};
            for (int i = 0; i < 2; ++i)
                text.draw(it[i], SCREEN_W / 2.0, 185 + i * 26, ui(15, i == pauseSel ? rgb(255, 230, 80) : rgb(230, 232, 245)), ALIGN_CENTER);
        }
    }

    void renderResults() {
        ViewOptions vo; vo.hud = false; vo.frame = frame;
        renderViewParallel(cv, toPx({0, 0, SCREEN_W, SCREEN_H}), race, race.humanCar(0) >= 0 ? race.humanCar(0) : 0, vo);
        gui.rectBlend(0, 0, SCREEN_W, SCREEN_H, rgb(5, 5, 25), 0.55);
        text.draw("RACE RESULTS", SCREEN_W / 2.0, 6, styleDisplay(22, rgb(255, 250, 150), rgb(255, 120, 20)), ALIGN_CENTER);
        text.draw(STAGE_NAMES[stage], SCREEN_W / 2.0, 32, ui(10, rgb(180, 210, 255)), ALIGN_CENTER);
        panel(12, 48, SCREEN_W - 24, 10 * 23 + 8);
        for (int i = 0; i < int(results.size()); ++i) {
            const ResultRow& r = results[i];
            const Car& c = race.cars[r.car];
            int col = i / 10, row = i % 10;
            int x = 22 + col * 308, y = 54 + row * 23;
            bool human = c.player >= 0;
            if (human) gui.vgradRect(x - 4, y - 2, 296, 20, rgb(255, 210, 60), rgb(160, 80, 0), 0.3);
            text.draw(std::to_string(i + 1), x + 16, y, ui(12, rgb(150, 200, 255)), ALIGN_RIGHT);
            colorChip(x + 24, y + 4, CAR_COLORS[c.color]);
            text.draw(drivers[r.driver].name, x + 42, y, ui(12, human ? rgb(255, 230, 80) : rgb(235, 236, 248)));
            std::string tm = r.dnf ? "TIME OUT" : c.finished ? formatTime(c.finishTime) : "RUNNING";
            text.draw(tm, x + 170, y, ui(11, r.dnf ? rgb(255, 120, 110) : rgb(200, 204, 220)));
            if (opt.championship && r.points > 0) text.draw("+" + std::to_string(r.points), x + 286, y, ui(12, rgb(90, 255, 130)), ALIGN_RIGHT);
        }
        std::string verdict;
        if (opt.championship) verdict = anyQualified ? (stage < NUM_STAGES - 1 ? "QUALIFIED FOR THE NEXT RACE!" : "CHAMPIONSHIP COMPLETE!") : "NOT QUALIFIED - GAME OVER";
        else verdict = anyFinished ? "RACE COMPLETE" : "TIME OUT";
        bool good = opt.championship ? anyQualified : anyFinished;
        text.draw(verdict, SCREEN_W / 2.0, 292, good ? styleDisplay(20, rgb(200, 255, 170), rgb(40, 200, 80)) : styleDisplay(20, rgb(255, 160, 140), rgb(220, 20, 20)), ALIGN_CENTER);
        for (int p = 0; p < opt.players; ++p) {
            int ci = race.humanCar(p);
            if (ci >= 0 && race.cars[ci].bestLap > 0)
                text.draw("P" + std::to_string(p + 1) + " BEST LAP  " + formatTime(race.cars[ci].bestLap), SCREEN_W / 2.0 + (opt.players > 1 ? (p ? 150 : -150) : 0), 318, ui(11, rgb(110, 230, 255)), ALIGN_CENTER);
        }
        if (stateTime > 1.0 && (frame / 30) % 2) text.draw("PRESS ENTER / A", SCREEN_W / 2.0, 338, ui(11, rgb(255, 255, 255)), ALIGN_CENTER);
    }

    void renderStandings() {
        ViewOptions vo; vo.hud = false; vo.frame = frame;
        renderViewParallel(cv, toPx({0, 0, SCREEN_W, SCREEN_H}), race, race.humanCar(0) >= 0 ? race.humanCar(0) : 0, vo);
        gui.rectBlend(0, 0, SCREEN_W, SCREEN_H, rgb(25, 5, 15), 0.55);
        bool last = !championshipContinues();
        text.draw(last ? "FINAL STANDINGS" : "CHAMPIONSHIP STANDINGS", SCREEN_W / 2.0, 6, styleDisplay(22, rgb(255, 250, 150), rgb(255, 120, 20)), ALIGN_CENTER);
        text.draw("AFTER RACE " + std::to_string(stage + 1) + " OF " + std::to_string(NUM_STAGES), SCREEN_W / 2.0, 32, ui(10, rgb(200, 205, 225)), ALIGN_CENTER);
        std::vector<int> idx(drivers.size());
        for (int i = 0; i < int(idx.size()); ++i) idx[i] = i;
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return drivers[a].points > drivers[b].points; });
        panel(30, 48, SCREEN_W - 60, 10 * 23 + 8, rgb(50, 14, 40), rgb(10, 2, 10));
        for (int i = 0; i < int(idx.size()); ++i) {
            const Driver& d = drivers[idx[i]];
            int col = i / 10, row = i % 10;
            int x = 42 + col * 292, y = 54 + row * 23;
            if (d.player >= 0) gui.vgradRect(x - 4, y - 2, 274, 20, rgb(255, 210, 60), rgb(160, 80, 0), 0.3);
            text.draw(std::to_string(i + 1), x + 16, y, ui(12, rgb(150, 200, 255)), ALIGN_RIGHT);
            colorChip(x + 24, y + 4, CAR_COLORS[d.color]);
            text.draw(d.name, x + 42, y, ui(12, d.player >= 0 ? rgb(255, 230, 80) : rgb(235, 236, 248)));
            text.draw(std::to_string(d.points) + " PTS", x + 262, y, ui(12, rgb(90, 255, 130)), ALIGN_RIGHT);
        }
        if (last) {
            const Driver& champ = drivers[idx[0]];
            std::string s = anyQualified ? (champ.player >= 0 ? champ.name + " IS THE CHAMPION!" : champ.name + " WINS THE TITLE") : "GAME OVER";
            bool won = champ.player >= 0 && anyQualified;
            text.draw(s, SCREEN_W / 2.0, 292, won ? styleDisplay(22, rgb(255, 250, 150), rgb(255, 120, 20)) : styleDisplay(20, rgb(255, 160, 140), rgb(220, 20, 20)), ALIGN_CENTER);
        } else {
            text.draw("NEXT:  " + std::string(STAGE_NAMES[stage + 1]), SCREEN_W / 2.0, 292, styleDisplay(20, rgb(200, 255, 170), rgb(40, 200, 80)), ALIGN_CENTER);
        }
        if (stateTime > 1.0 && (frame / 30) % 2) text.draw("PRESS ENTER / A", SCREEN_W / 2.0, 330, ui(11, rgb(255, 255, 255)), ALIGN_CENTER);
    }

    void render() {
        text.beginFrame(renderer, frame);
        switch (state) {
        case State::Title: renderTitle(); break;
        case State::Racing:
        case State::Paused: renderRace(); break;
        case State::Results: renderResults(); break;
        case State::Standings: renderStandings(); break;
        }
    }

    void present() {
        SDL_UpdateTexture(texture, nullptr, cv.px.data(), CANVAS_W * 4);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        // Scale the 1280x720 image to the window so that every game pixel ends up the same size:
        // whole-number scales are drawn with nearest neighbour; for any other scale the image is first
        // blown up by the next whole number (sharp square pixels), then smoothly filtered down to the
        // window size - pixels stay uniform squares with soft (not uneven) edges.
        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(renderer, &ow, &oh);
        double scale = std::min(ow / double(CANVAS_W), oh / double(CANVAS_H));
        int k = int(std::ceil(scale - 1e-3));
        bool whole = std::fabs(scale - std::round(scale)) < 1e-3 && scale >= 1;
        if (whole || k < 2) {
            SDL_SetTextureScaleMode(texture, whole ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        } else {
            if (!upscaled || upscaleK != k) {
                if (upscaled) SDL_DestroyTexture(upscaled);
                upscaled = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, CANVAS_W * k, CANVAS_H * k);
                upscaleK = k;
                if (upscaled) SDL_SetTextureScaleMode(upscaled, SDL_ScaleModeLinear);
            }
            if (upscaled) {
                SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
                SDL_SetRenderTarget(renderer, upscaled);
                SDL_RenderCopy(renderer, texture, nullptr, nullptr);
                SDL_SetRenderTarget(renderer, nullptr);
                SDL_RenderCopy(renderer, upscaled, nullptr, nullptr);
            } else {
                SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
                SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            }
        }
        text.flush(renderer);                 // crisp TTF text at the real output resolution
        if (shotFile) {
            int w = 0, h = 0;
            SDL_GetRendererOutputSize(renderer, &w, &h);
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
            SDL_SaveBMP(s, shotFile);
            SDL_FreeSurface(s);
            shotFile = nullptr;
        }
        SDL_RenderPresent(renderer);
    }

    void tick(double dt) {
        stateTime += (state == State::Racing) ? 0 : dt;
        switch (state) {
        case State::Title: updateTitle(dt); break;
        case State::Racing: updateRace(dt); break;
        case State::Paused: updatePause(); break;
        case State::Results: updateResults(); break;
        case State::Standings: updateStandings(); break;
        }
        // keep the attract race alive for the menu background
        frame++;
    }

    void run() {
        Uint64 last = SDL_GetPerformanceCounter();
        double acc = 0;
        while (running) {
#ifdef __SWITCH__
            if (!appletMainLoop()) break;
#endif
            SDL_Event e;
            while (SDL_PollEvent(&e)) input.handleEvent(e, state == State::Title ? 1 : opt.players);
            if (input.quitRequested) break;
            if (input.toggleFullscreen) {
                input.toggleFullscreen = false;
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            }
            Uint64 now = SDL_GetPerformanceCounter();
            double frameDt = double(now - last) / SDL_GetPerformanceFrequency();
            last = now;
            acc += std::min(frameDt, 0.25);
            while (acc >= STEP) {
                tick(STEP);
                acc -= STEP;
                input.beginFrame();                  // menu edges are consumed by one tick
            }
            render();
            present();
        }
    }

    // ------------------------------------------- automated screenshots ----
    const char* shotFile = nullptr;
    void saveShot(const char* file) { shotFile = file; render(); present(); }
};

} // namespace

// test mode --pilot: drives player 1 like a good human would, through the normal controls (the clock runs):
// a speed target from the perfect-driver profile (with some caution), steering against the centrifugal drift
// back to the middle of its lane, and a sidestep around cars just ahead
PlayerInput pilotInput(const Race& r, int idx, double caution) {
    const Car& c = r.cars[size_t(idx)];
    const Track& t = r.track;
    PlayerInput in;
    double target = 1e9;
    for (double d = 0; d < 90 * SEG_LEN; d += SEG_LEN / 2) {
        double cap = referenceCap(t, c.dist + d) * caution;
        target = std::min(target, std::sqrt(cap * cap + 2 * BRAKE_DECEL * MAX_SPEED * d * 0.8));
    }
    in.throttle = c.speed < target ? 1 : 0;
    in.brake = c.speed > target * 1.03 ? 1 : 0;
    // on the grid: wait, then (in FX mode) time the gas for a turbo start
    if (r.phase == RacePhase::Countdown)
        in.throttle = std::getenv("TURBO_PILOT_FX") && r.countdown <= TURBO_START_WINDOW * 0.6 ? 1 : 0;
    const Segment& s = t.find(c.dist);
    const CarStats& st = CAR_STATS[c.type];
    bool dirt = s.surface == SURF_DIRT;
    double sp = c.speed / MAX_SPEED;
    double drift = 2 * sp * sp * s.curve * CENTRIFUGAL * (dirt ? DIRT_SLIDE : 1.0) / st.grip;
    double auth = 2 * std::min(1.0, sp * 1.8) * (dirt ? DIRT_GRIP : 1.0) * st.grip;
    double w = t.widthAt(c.dist), xt = 0;
    for (size_t k = 0; k < r.cars.size(); ++k) {
        if (int(k) == idx) continue;
        const Car& o = r.cars[k];
        double dd = o.dist - c.dist;
        if (dd > 0 && dd < 8 * SEG_LEN && std::fabs(o.x - c.x) < 0.7) xt = (o.x > 0 ? -0.55 : 0.55) * w;
    }
    // before a jump: line up straight in the middle (no sideways motion to carry through the air)
    for (int j : t.jumps_) {
        double ahead = t.wrap(j * SEG_LEN - t.wrap(c.dist));
        if (ahead < 45 * SEG_LEN) { xt = 0; drift *= 0.3; }
    }
    in.steer = std::clamp((drift + 2.5 * (xt - c.x) - 0.3 * c.vx) / std::max(auth, 0.1), -1.0, 1.0);
    // TURBO_PILOT_FX=1: also use nitro on long straights and drift into the sharper bends (a brake tap on entry)
    static const bool fx = std::getenv("TURBO_PILOT_FX") != nullptr;
    if (fx) {
        if (target >= CAR_STATS[c.type].top * MAX_SPEED * 0.9 && (c.nitro >= 1.0 || c.boosting) && std::fabs(s.curve) < 1) in.nitro = true;
        if (c.drifting) { in.throttle = 1; in.brake = 0; }        // drifting: flat out through the bend
        double bendAhead = 0;
        for (int k = 2; k < 12; ++k) bendAhead = std::max(bendAhead, std::fabs(t.find(c.dist + k * SEG_LEN).curve));
        if (!c.drifting && sp > 0.55 && bendAhead >= 3.5 && std::fabs(s.curve) >= 1.5 && !dirt && !c.braking) {
            in.brake = 1;                                            // the tap (one frame; letting go starts the drift)
            in.steer = s.curve > 0 ? 1 : -1;
        }
        if (c.drifting && std::fabs(s.curve) < 1.0) in.steer = -c.driftDir;   // bend over: countersteer out of it
    }
    return in;
}

int main(int argc, char** argv) {
#ifdef __SWITCH__
    romfsInit();                             // the assets folder is packed into the .nro's romfs
#endif
#ifdef __ANDROID__
    // the Back button (on some handhelds also the B button) comes to the game as a key instead of closing the app
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
    // test mode: --shot out.bmp [--stage N] [--seconds S] [--players P] [--title] [--auto]
    const char* shot = nullptr;
    int shotStage = 0, shotPlayers = 1, shotDifficulty = 1;
    double shotSeconds = 3;
    bool shotTitle = false, shotSingle = false, shotAuto = false, bench = false, info = false, carsheet = false;
    double pilot = 0;   // --pilot <caution 0..1>: see pilotInput
    double telemetry = 0;
    int winW = 0, winH = 0;
    const char* audioTest = nullptr;      // debug: render the race sound into a WAV file
    double timeLeft = -1;
    int warp = -1, shotCar = -1, shotMenu = -1, bigF = -99, bigP = CAR_ELEV_LEVEL;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!std::strcmp(argv[i], "--stage") && i + 1 < argc) shotStage = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) shotSeconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--players") && i + 1 < argc) shotPlayers = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--difficulty") && i + 1 < argc) shotDifficulty = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--title")) shotTitle = true;
        else if (!std::strcmp(argv[i], "--single")) shotSingle = true;
        else if (!std::strcmp(argv[i], "--auto")) shotAuto = true;
        else if (!std::strcmp(argv[i], "--pilot") && i + 1 < argc) pilot = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--bench")) bench = true;
        else if (!std::strcmp(argv[i], "--audiotest") && i + 1 < argc) audioTest = argv[++i];
        else if (!std::strcmp(argv[i], "--time") && i + 1 < argc) timeLeft = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--window") && i + 2 < argc) { winW = std::atoi(argv[++i]); winH = std::atoi(argv[++i]); }
        else if (!std::strcmp(argv[i], "--warp") && i + 1 < argc) warp = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--info")) info = true;
        else if (!std::strcmp(argv[i], "--carsheet")) carsheet = true;
        else if (!std::strcmp(argv[i], "--telemetry") && i + 1 < argc) telemetry = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--carbig") && i + 2 < argc) { carsheet = true; bigF = std::atoi(argv[++i]); bigP = std::atoi(argv[++i]); }
        else if (!std::strcmp(argv[i], "--car") && i + 1 < argc) shotCar = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--menu") && i + 1 < argc) shotMenu = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--input") && i + 2 < argc) {
            input.overrideOn = true;
            input.overrideIn.throttle = std::atof(argv[++i]);
            input.overrideIn.steer = std::atof(argv[++i]);
        }
    }

    App app;
    if (!app.init(shot != nullptr)) return 1;
    if (winW > 0 && winH > 0) SDL_SetWindowSize(app.window, winW, winH);   // debug: test other window sizes

    if (shot && carsheet) {   // debug: every car type (rows) in steering frames + downhill/uphill + braking
        app.cv.fill(rgb(90, 90, 100));
        if (bigF > -99) {   // large view of one frame of every type
            for (int t = 0; t < CAR_TYPES; ++t)
                drawCar(app.cv, t, CAR_COLORS[t == 0 ? 0 : t + 1], bigF, bigP, false, (t % 3) * 213 * RENDER_SCALE,
                        (t / 3) * 180 * RENDER_SCALE, 212 * RENDER_SCALE, 154 * RENDER_SCALE);
        } else
        for (int t = 0; t < CAR_TYPES; ++t) {
            const Color cs = CAR_COLORS[t == 0 ? 0 : t + 1];
            int col = 0;
            const double R = RENDER_SCALE;
            for (int f = -(CAR_YAWS - 1); f < CAR_YAWS; f += 2) drawCar(app.cv, t, cs, f, CAR_ELEV_LEVEL, false, (2 + 70 * col++) * R, (2 + t * 52) * R, 68 * R, 49 * R);
            drawCar(app.cv, t, cs, 0, 0, false, (2 + 70 * col++) * R, (2 + t * 52) * R, 68 * R, 49 * R);              // flattest view
            drawCar(app.cv, t, cs, 0, CAR_ELEV - 1, false, (2 + 70 * col++) * R, (2 + t * 52) * R, 68 * R, 49 * R);   // steepest view
            drawCar(app.cv, t, cs, 0, CAR_ELEV_LEVEL, true, (2 + 70 * col++) * R, (2 + t * 52) * R, 68 * R, 49 * R);  // braking
        }
        text.beginFrame(app.renderer, 0);
        app.shotFile = shot;
        app.present();
        app.shutdown();
        return 0;
    }
    if (shot) {
        std::vector<double> frameMs;
        if (shotCar >= 0) app.opt.carType[0] = app.opt.carType[1] = shotCar;
        if (shotTitle) {
            app.opt.players = shotPlayers;
            app.opt.difficulty = std::clamp(shotDifficulty, 0, 2);
            if (shotSingle) app.opt.championship = false;
            if (shotMenu >= 0) app.menuSel = shotMenu;
            app.startAttract(shotStage);
            for (int i = 0; i < int(shotSeconds * 60); ++i) app.tick(STEP);
        } else {
            app.opt.players = shotPlayers;
            app.opt.difficulty = std::clamp(shotDifficulty, 0, 2);
            app.opt.championship = false;
            app.opt.startStage = shotStage;
            Uint64 s0 = SDL_GetPerformanceCounter();
            app.newGame();
            if (bench) std::printf("race start %.1f ms\n", (SDL_GetPerformanceCounter() - s0) * 1000.0 / SDL_GetPerformanceFrequency());
            if (info) {
                const Track& t = app.race.track;
                std::printf("stage %d: %d segments, checkpoint %d\n", shotStage, t.count(), int(t.checkpointZ / SEG_LEN));
                {
                    double vcp = 0, gridZ = -GRID_FRONT - (NUM_CARS - 1) * GRID_SLOT;
                    double tA = referenceTime(t, gridZ, t.checkpointZ, 0, &vcp);
                    double tB = referenceTime(t, t.checkpointZ, t.length, vcp);
                    std::printf("  time limits: to checkpoint %.0f s (perfect %.1f s, x%.2f), checkpoint to finish %.0f s (perfect %.1f s, x%.2f)\n",
                                app.race.legBonus(0) + 8, tA, (app.race.legBonus(0) + 8) / tA, app.race.legBonus(1), tB, app.race.legBonus(1) / tB);
                }
                for (int j : t.jumps_) std::printf("  jump crest %d\n", j);
                for (int j : t.narrows_) std::printf("  narrows at %d\n", j);
                for (int j : t.tunnels_) std::printf("  tunnel at %d\n", j);
                for (int j : t.dirts_) std::printf("  dirt at %d\n", j);
            }
            if (warp >= 0) {   // debug: start the human car(s) at a segment, already at speed
                app.race.phase = RacePhase::Racing;
                app.race.time = 10;   // past the start lights
                for (auto& c : app.race.cars)
                    if (c.player >= 0) {
                        c.dist = warp * SEG_LEN + c.player * 600;
                        c.x = c.player ? 0.4 : -0.1;
                        c.speed = MAX_SPEED * 0.97; c.gear = 4;
                        c.alt = app.race.track.heightAt(c.dist);
                        c.cpPassed = app.race.track.checkpointCount(c.dist);
                        if (timeLeft >= 0) c.timeLeft = timeLeft;
                    }
            }
            std::vector<float> wav;
            if (audioTest) { audio.initOffline(44100); audio.setEnabled(true); }
            for (int i = 0; i < int(shotSeconds * 60); ++i) {
                if (audioTest) { size_t at = wav.size(); wav.resize(at + 735 * 2); audio.renderOffline(&wav[at], 735); }
                Uint64 t0 = SDL_GetPerformanceCounter();
                if (shotAuto && app.race.phase == RacePhase::Racing)
                    for (auto& c : app.race.cars) if (c.player >= 0 && !c.autopilot) { c.autopilot = true; c.skill = 0.97; }
                int pIdx = pilot > 0 ? app.race.humanCar(0) : -1;
                int cpBefore = pIdx >= 0 ? app.race.cars[size_t(pIdx)].cpPassed : 0;
                double leftBefore = pIdx >= 0 ? app.race.cars[size_t(pIdx)].timeLeft : 0;
                if (pIdx >= 0) { input.overrideOn = true; input.overrideIn = pilotInput(app.race, pIdx, pilot); }
                app.tick(STEP);
                if (pIdx >= 0) {
                    const Car& c = app.race.cars[size_t(pIdx)];
                    if (c.cpPassed > cpBefore && c.cpPassed > 0)
                        std::printf("pilot: %s %d at t=%.1f s with %.1f s left\n", c.cpPassed % 2 ? "checkpoint" : "lap", (c.cpPassed + 1) / 2, i * STEP, leftBefore);
                    if (c.timedOut && !c.finished) { std::printf("pilot: TIMED OUT at t=%.1f s (half-laps %d)\n", i * STEP, c.cpPassed); break; }
                    if (c.finished) { std::printf("pilot: finished at t=%.1f s\n", i * STEP); break; }
                }
                if (bench) app.render();
                frameMs.push_back((SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency());
                if (telemetry > 0 && i % std::max(1, int(telemetry * 60)) == 0) {   // debug: player car log
                    const Car& c = app.race.cars[app.race.humanCar(0)];
                    std::printf("t=%6.2f seg=%5d kmh=%5.1f gear=%d air=%6.0f vy=%7.0f x=%5.2f vx=%5.2f nos=%.2f%s%s left=%5.1f%s%s\n", i * STEP,
                            int(c.dist / SEG_LEN), c.speed / MAX_SPEED * KMH_AT_MAX, c.gear + 1, c.air, c.vy, c.x, c.vx, c.nitro,
                            c.boosting ? " BOOST" : "", c.drifting ? " DRIFT" : "", c.timeLeft,
                            c.outOfTime ? " OUT-OF-TIME" : "", c.timedOut ? " TIMED-OUT" : "");
                }
            }
            if (audioTest) {     // 16-bit stereo WAV of what the race sounded like
                FILE* f = std::fopen(audioTest, "wb");
                if (f) {
                    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
                    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
                    uint32_t bytes = uint32_t(wav.size() * 2);
                    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
                    u32(16); u16(1); u16(2); u32(44100); u32(44100 * 4); u16(4); u16(16);
                    std::fwrite("data", 1, 4, f); u32(bytes);
                    for (float v : wav) { int16_t x = int16_t(std::tanh(v) * 30000.0f); std::fwrite(&x, 2, 1, f); }
                    std::fclose(f);
                }
            }
        }
        if (bench && !frameMs.empty()) {   // debug: frame time statistics (update + render)
            std::vector<double> v = frameMs;
            std::sort(v.begin(), v.end());
            double sum = 0; for (double x : v) sum += x;
            std::printf("frames %zu  avg %.2f ms  p50 %.2f  p99 %.2f  max %.2f\n", v.size(), sum / v.size(), v[v.size() / 2],
                        v[v.size() * 99 / 100], v.back());
        }
        if (info && app.state == State::Racing) {   // debug: how spread out is the field?
            std::printf("gaps (m between consecutive cars):");
            for (size_t k = 1; k < app.race.order.size(); ++k)
                std::printf(" %.0f", (app.race.cars[app.race.order[k - 1]].dist - app.race.cars[app.race.order[k]].dist) / 100);
            std::printf("\n");
            int hc = app.race.humanCar(0);
            if (hc >= 0) std::printf("player 1 position %d\n", app.race.position(hc));
        }
        app.saveShot(shot);
        app.shutdown();
        return 0;
    }

    audio.setEnabled(app.opt.sound);
    app.run();
    app.shutdown();
#ifdef __SWITCH__
    romfsExit();
#endif
    return 0;
}
