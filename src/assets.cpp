#include "assets.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>

namespace { template <class T> std::string num(T v) { std::ostringstream o; o << v; return o.str(); } }

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"   // sprintf in stb_image_write's HDR writer (macOS)
#endif
#include "stb_image.h"
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if !defined(__SWITCH__)
#include <filesystem>
#endif

namespace {
const char* const CAR_FILE[CAR_TYPES] = {"sport", "coupe", "supercar", "suv", "pickup", "sedan"};
const char* const EFFECT_NAMES[] = {"dust", "smoke", "spark", "lamp_glow", "light_red", "light_green", "light_off", "flame",
                                    "tumbleweed0", "tumbleweed1", "tumbleweed2", "tumbleweed3", "tumbleweed4", "tumbleweed5",
                                    "tumbleweed6", "tumbleweed7", "leaf_a", "leaf_b", "bridge_night", "bridge_pillar_night",
                                    "rain_lights1", "rain_lights2"};
constexpr int NUM_EFFECTS = 22;
constexpr int PAD = 1;     // gap between pictures in an atlas: exactly the 1-pixel magenta line between them
// every picture in an atlas is framed by a 1-pixel magenta rectangle just outside its area, to show where it
// ends (neighbouring pictures share the line between them); this exact colour is never used inside a picture
const Color FRAME = rgb(255, 0, 255);

// ------------------------------------------------------------------ PNG ----
bool writePng(const std::string& path, const Bitmap& b) {
    std::vector<unsigned char> rgba(size_t(b.w) * b.h * 4);
    for (size_t i = 0; i < b.px.size(); ++i) {
        Color c = b.px[i];
        unsigned a = c >> 24;
        rgba[i * 4 + 0] = a ? (c >> 16) & 255 : 0;     // fully transparent pixels are stored black
        rgba[i * 4 + 1] = a ? (c >> 8) & 255 : 0;
        rgba[i * 4 + 2] = a ? c & 255 : 0;
        rgba[i * 4 + 3] = (unsigned char)a;
    }
    return stbi_write_png(path.c_str(), b.w, b.h, 4, rgba.data(), b.w * 4) != 0;
}
bool writeGreyPng(const std::string& path, int w, int h, const std::vector<unsigned char>& v) {
    return stbi_write_png(path.c_str(), w, h, 1, v.data(), w) != 0;
}
// the whole file, read through SDL: works for plain files, the Switch romfs and the assets inside an Android APK
bool readFile(const std::string& path, std::vector<unsigned char>& out) {
    SDL_RWops* f = SDL_RWFromFile(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    unsigned char buf[65536];
    size_t got;
    while ((got = SDL_RWread(f, buf, 1, sizeof(buf))) > 0) out.insert(out.end(), buf, buf + got);
    SDL_RWclose(f);
    return true;
}
unsigned char* loadImage(const std::string& path, int& w, int& h, int channels) {
    std::vector<unsigned char> file;
    if (!readFile(path, file) || file.empty()) return nullptr;
    int n = 0;
    return stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &n, channels);
}
bool readPng(const std::string& path, Bitmap& out) {
    int w = 0, h = 0;
    unsigned char* d = loadImage(path, w, h, 4);
    if (!d) return false;
    out = Bitmap(w, h);
    for (size_t i = 0; i < out.px.size(); ++i)
        out.px[i] = (Color(d[i * 4 + 3]) << 24) | (Color(d[i * 4]) << 16) | (Color(d[i * 4 + 1]) << 8) | Color(d[i * 4 + 2]);
    stbi_image_free(d);
    return true;
}
bool readGreyPng(const std::string& path, int& w, int& h, std::vector<unsigned char>& out) {
    unsigned char* d = loadImage(path, w, h, 1);
    if (!d) return false;
    out.assign(d, d + size_t(w) * h);
    stbi_image_free(d);
    return true;
}

// ---------------------------------------------------------------- atlas ----
struct Rect4 { int x, y, w, h; };
// shelf packing: tallest first, rows left to right
std::vector<Rect4> pack(const std::vector<std::pair<int, int>>& sizes, int maxW, int& W, int& H) {
    std::vector<int> order(sizes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = int(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return sizes[size_t(a)].second > sizes[size_t(b)].second; });
    std::vector<Rect4> pos(sizes.size());
    int x = PAD, y = PAD, rowH = 0;
    W = 0;
    for (int i : order) {
        auto [w, h] = sizes[size_t(i)];
        if (x + w + PAD > maxW && x > PAD) { x = PAD; y += rowH + PAD; rowH = 0; }
        pos[size_t(i)] = {x, y, w, h};
        x += w + PAD;
        rowH = std::max(rowH, h);
        W = std::max(W, x);
    }
    H = y + rowH + PAD;
    return pos;
}
void blitInto(Bitmap& dst, const Bitmap& src, int x, int y) {
    for (int yy = 0; yy < src.h; ++yy)
        for (int xx = 0; xx < src.w; ++xx) {
            Color c = src.px[size_t(yy) * src.w + xx];
            if ((c >> 24) && (c & 0x00FFFFFFu) == (FRAME & 0x00FFFFFFu)) c -= 0x00010000u;   // keep the frame colour unique
            dst.px[size_t(y + yy) * dst.w + x + xx] = c;
        }
}
void frameAround(Bitmap& dst, int x, int y, int w, int h) {
    for (int xx = x - 1; xx <= x + w; ++xx) { dst.set(xx, y - 1, FRAME); dst.set(xx, y + h, FRAME); }
    for (int yy = y - 1; yy <= y + h; ++yy) { dst.set(x - 1, yy, FRAME); dst.set(x + w, yy, FRAME); }
}
Bitmap cutOut(const Bitmap& atlas, int x, int y, int w, int h) {
    Bitmap b(std::max(1, w), std::max(1, h));
    for (int yy = 0; yy < h; ++yy)
        for (int xx = 0; xx < w; ++xx) b.px[size_t(yy) * b.w + xx] = atlas.get(x + xx, y + yy);
    return b;
}

// a named list of pictures -> one PNG + one text file
struct Item { std::string line; const Bitmap* bmp; };   // line = text before the coordinates
bool writeAtlas(const std::string& base, const std::string& title, const std::vector<Item>& items,
                const std::vector<std::string>& extra, int maxW, std::string& err,
                const std::function<std::string(size_t)>& suffix = nullptr) {
    std::vector<std::pair<int, int>> sizes;
    for (auto& it : items) sizes.push_back({it.bmp->w, it.bmp->h});
    int W = 0, H = 0;
    auto pos = pack(sizes, maxW, W, H);
    Bitmap atlas(W, H);
    for (size_t i = 0; i < items.size(); ++i) {
        blitInto(atlas, *items[i].bmp, pos[i].x, pos[i].y);
        frameAround(atlas, pos[i].x, pos[i].y, pos[i].w, pos[i].h);
    }
    if (!writePng(base + ".png", atlas)) { err = "cannot write " + base + ".png"; return false; }
    std::ofstream f(base + ".txt");
    if (!f) { err = "cannot write " + base + ".txt"; return false; }
    f << "# " << title << "\n";
    for (auto& e : extra) f << e << "\n";
    for (size_t i = 0; i < items.size(); ++i) {
        f << items[i].line << "  " << pos[i].x << " " << pos[i].y << " " << pos[i].w << " " << pos[i].h;
        if (suffix) f << "  " << suffix(i);
        f << "\n";
    }
    return true;
}

// read "<name...> x y w h [more]" lines; '#' starts a comment
std::vector<std::vector<std::string>> readLines(const std::string& path) {
    std::vector<std::vector<std::string>> out;
    std::vector<unsigned char> file;
    readFile(path, file);
    std::istringstream f(std::string(file.begin(), file.end()));
    std::string line;
    while (std::getline(f, line)) {
        if (auto c = line.find('#'); c != std::string::npos) line.resize(c);
        std::istringstream ss(line);
        std::vector<std::string> t;
        for (std::string w; ss >> w;) t.push_back(w);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

// The assets are made for 1280x720. A game built for 640x360 halves them while loading (a quarter of the
// memory; they would only ever be drawn shrunk anyway).
int g_shrink = 1;
Bitmap shrunk(Bitmap b) { return g_shrink > 1 ? b.downsampled(g_shrink) : b; }
void shrinkFrame(CarFrame& f) {
    if (g_shrink <= 1) return;
    const int k = g_shrink;
    Bitmap b = f.bmp.downsampled(k);
    std::vector<unsigned char> m(size_t(b.w) * b.h, 0);
    if (f.mask.px.size() == size_t(f.bmp.w) * f.bmp.h)
        for (int y = 0; y < b.h; ++y)
            for (int x = 0; x < b.w; ++x) {
                unsigned sum = 0;
                for (int yy = 0; yy < k; ++yy)
                    for (int xx = 0; xx < k; ++xx) sum += f.mask.px[size_t(y * k + yy) * f.bmp.w + x * k + xx];
                m[size_t(y) * b.w + x] = (unsigned char)(sum / (k * k));
            }
    f.bmp = std::move(b);
    f.mask.w = f.bmp.w; f.mask.h = f.bmp.h; f.mask.px = std::move(m);
    f.ox = int(std::floor(double(f.ox) / k)); f.oy = int(std::floor(double(f.oy) / k));
    f.frameW /= k; f.frameH /= k;
}

bool makeDir(const std::string& d) {
#if !defined(__SWITCH__)
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return !ec;
#else
    (void)d;
    return true;
#endif
}
// a file in the assets folder; an empty folder means the root of the APK's assets (Android)
std::string inDir(const std::string& dir, const std::string& rel) { return dir.empty() ? rel : dir + "/" + rel; }

bool exists(const std::string& p) {
    SDL_RWops* f = SDL_RWFromFile(p.c_str(), "rb");
    if (!f) return false;
    SDL_RWclose(f);
    return true;
}

// scenery groups: atlas name -> sprites
struct Group { const char* file; std::vector<SpriteType> sprites; };
const std::vector<Group>& groups() {
    static const std::vector<Group> g = {
        {"trees", {SPR_PALM, SPR_PINE, SPR_PINE_SNOW, SPR_BUSH, SPR_CACTUS}},
        {"rocks", {SPR_ROCK, SPR_SNOWMAN}},
        {"props", {SPR_LAMP, SPR_BUILDING1, SPR_BUILDING2}},
        {"signs", {SPR_BILLBOARD1, SPR_BILLBOARD2, SPR_BILLBOARD3, SPR_TURN_LEFT, SPR_TURN_RIGHT, SPR_GANTRY_START,
                   SPR_GANTRY_CHECK, SPR_SIGN_JUMP, SPR_SIGN_NARROW, SPR_SIGN_TUNNEL, SPR_SIGN_DIRT}},
        // stages 7-11
        {"future", {SPR_HOLO_PYLON, SPR_SCIFI_TOWER, SPR_UFO, SPR_HOLO_SIGN}},
        {"brazil", {SPR_TROPICAL, SPR_CARNIVAL, SPR_FAVELA}},
        {"paris", {SPR_PARIS_LAMP, SPR_HAUSSMANN, SPR_CAFE, SPR_TREE_ROUND}},
        {"germany", {SPR_FACHWERK, SPR_AUTOBAHN_SIGN, SPR_WIND_TURBINE}},
        // 1.4: more roadside variety
        {"flair1", {SPR_DEAD_TREE, SPR_WINDPUMP, SPR_DINER, SPR_BEACH_HUT, SPR_BEACH_HUT2, SPR_LIFEGUARD, SPR_BUILDING3,
                    SPR_CHALET, SPR_CABIN, SPR_MOTEL_SIGN, SPR_DOME}},
        {"flair2", {SPR_FAVELA2, SPR_CONFETTI_CANNON, SPR_HAUSSMANN2, SPR_MORRIS, SPR_FACHWERK2, SPR_FACHWERK3, SPR_CHURCH,
                    SPR_VILLA2, SPR_VILLA3, SPR_VINES}},
        {"stage12", {SPR_BRIDGE, SPR_BRIDGE_PILLAR, SPR_AUTUMN_TREE1, SPR_AUTUMN_TREE2, SPR_OAK}},
        {"stage13", {SPR_PUDDLE, SPR_RAIN_LAMP, SPR_RAIN_BUILDING1, SPR_RAIN_BUILDING2}},
        {"italy", {SPR_CYPRESS, SPR_VILLA, SPR_OLIVE}},
    };
    return g;
}
Bitmap* effect(Graphics& g, int i) {
    Bitmap* e[] = {&g.dust, &g.smoke, &g.spark, &g.lampGlow, &g.lightRed, &g.lightGreen, &g.lightOff, &g.flame,
                   &g.tumbleweed[0], &g.tumbleweed[1], &g.tumbleweed[2], &g.tumbleweed[3], &g.tumbleweed[4],
                   &g.tumbleweed[5], &g.tumbleweed[6], &g.tumbleweed[7], &g.leaf[0], &g.leaf[1], &g.bridgeNight,
                   &g.bridgePillarNight, &g.rainLights[0], &g.rainLights[1]};
    return e[i];
}
} // namespace

// ================================================================= save ====
bool saveAssets(const std::string& dir, const Graphics& g0, const CarSprites cars[CAR_TYPES], std::string& err) {
    Graphics& g = const_cast<Graphics&>(g0);    // only read
    if (!makeDir(inDir(dir, "scenery")) || !makeDir(inDir(dir, "backgrounds")) || !makeDir(inDir(dir, "cars"))) {
        err = "cannot create folders in " + dir;
        return false;
    }
    for (const Group& gr : groups()) {
        std::vector<Item> items;
        for (SpriteType t : gr.sprites) items.push_back({SPRITE_NAMES[t], &g.sprite[t]});
        if (!writeAtlas(inDir(dir, "scenery/") + gr.file, std::string("scenery atlas: name  x y w h"), items, {}, 2048, err)) return false;
    }
    {
        std::vector<Item> items;
        for (int i = 0; i < NUM_EFFECTS; ++i) items.push_back({EFFECT_NAMES[i], effect(g, i)});
        if (!writeAtlas(inDir(dir, "effects"), "effects atlas: name  x y w h", items, {}, 1024, err)) return false;
    }
    for (int st = 0; st < NUM_STAGES; ++st) {
        std::vector<Item> items;
        const auto& L = g.bg[st];
        for (size_t i = 0; i < L.size(); ++i) items.push_back({"layer", &L[i].bmp});
        // one layer per row, back to front; each layer spans 1024 layout units, whatever its pixel width
        auto suffix = [&](size_t i) { return num(L[i].parallax) + " " + num(L[i].yOff); };
        if (!writeAtlas(inDir(dir, "backgrounds/stage") + std::to_string(st + 1),
                        std::string("background layers of ") + STAGE_NAMES[st] + ", back to front: layer  x y w h  parallax  yOffset",
                        items, {}, 1, err, suffix))
            return false;
    }
    for (int t = 0; t < CAR_TYPES; ++t) {
        const CarSprites& cs = cars[t];
        std::vector<Item> items;
        std::vector<const CarFrame*> frames;
        auto add = [&](const std::string& kind, int s, int e, const CarFrame& f) {
            items.push_back({kind + " " + std::to_string(s) + " " + std::to_string(e), &f.bmp});
            frames.push_back(&f);
        };
        for (int s = 0; s < CAR_YAWS; ++s)
            for (int e = 0; e < CAR_ELEV; ++e) {
                add("car", s, e, cs.car[s][e]);
                for (const CarFrame& o : cs.brake[s][e]) add("brake", s, e, o);
            }
        for (int s = -(CAR_YAWS - 1); s < CAR_YAWS; ++s)
            for (int e = 0; e < CAR_ELEV; ++e)
                for (const CarFrame& o : cs.glare[s + CAR_YAWS - 1][e]) add("glare", s, e, o);
        for (int s = -(CAR_YAWS - 1); s < CAR_YAWS; ++s)
            for (int e = 0; e < CAR_SHADOW_ELEV; ++e) add("shadow", s, e, cs.shadow[s + CAR_YAWS - 1][e]);
        auto suffix = [&](size_t i) {
            const CarFrame& f = *frames[i];
            std::string sc = num(f.scale);
            return std::to_string(f.ox) + " " + std::to_string(f.oy) + " " + sc;
        };
        std::string base = inDir(dir, "cars/") + CAR_FILE[t];
        std::vector<std::string> extra = {
            "# frame <w> <h>: size of a whole frame; the car stands with its rear wheels on row " + std::to_string(CAR_ANCHOR_Y) +
                " (at full size)",
            "frame " + std::to_string(CAR_BMP_W) + " " + std::to_string(CAR_BMP_H),
            "# car/brake <yaw step> <elevation>  x y w h  ox oy scale   (yaw step * " + num(CAR_YAW_STEP) +
                " deg to the right, mirrored for the left; brake = lit brake lights drawn over the car)",
            "# glare <yaw step -8..8> <elevation>: reflections on the glass, drawn over the car (never mirrored)",
            "# shadow <yaw step -8..8> <shadow elevation 0-2 = car elevations 1, 3, 5>: the shadow on the road",
            "# exhaust <yaw step> <elevation>  x y  x y ...: the exhaust pipes in that car frame, as fractions of the whole",
            "#   frame (0..1); the nitrous flames come out there",
            "# tyres <yaw step> <elevation>  x y  x y: where the rear tyres touch the road (tyre smoke), same units",
        };
        for (int s = 0; s < CAR_YAWS; ++s)
            for (int e = 0; e < CAR_ELEV; ++e) {
                std::string l = "exhaust " + std::to_string(s) + " " + std::to_string(e);
                char buf[32];
                for (const auto& p : cs.exhaust[s][e]) {
                    std::snprintf(buf, sizeof buf, "  %.4f %.4f", p[0], p[1]);
                    l += buf;
                }
                extra.push_back(l);
            }
        for (int s = 0; s < CAR_YAWS; ++s)
            for (int e = 0; e < CAR_ELEV; ++e) {
                std::string l = "tyres " + std::to_string(s) + " " + std::to_string(e);
                char buf[32];
                for (const auto& p : cs.tyres[s][e]) {
                    std::snprintf(buf, sizeof buf, "  %.4f %.4f", p[0], p[1]);
                    l += buf;
                }
                extra.push_back(l);
            }
        if (!writeAtlas(base, std::string("car atlas: ") + CAR_FILE[t], items, extra, 4096, err, suffix)) return false;
        // paint mask in the same layout
        auto lines = readLines(base + ".txt");
        Bitmap probe;
        if (!readPng(base + ".png", probe)) { err = "cannot re-read " + base + ".png"; return false; }
        std::vector<unsigned char> mask(size_t(probe.w) * probe.h, 0);
        size_t k = 0;
        for (auto& l : lines) {
            if (l[0] != "car" && l[0] != "brake" && l[0] != "shadow" && l[0] != "glare") continue;
            const CarFrame& f = *frames[k++];
            int x = std::stoi(l[3]), y = std::stoi(l[4]);
            if (f.mask.px.size() == size_t(f.bmp.w) * f.bmp.h)
                for (int yy = 0; yy < f.bmp.h; ++yy)
                    std::copy_n(&f.mask.px[size_t(yy) * f.bmp.w], f.bmp.w, &mask[size_t(y + yy) * probe.w + x]);
        }
        if (!writeGreyPng(base + "_paint.png", probe.w, probe.h, mask)) { err = "cannot write " + base + "_paint.png"; return false; }
    }
    return true;
}

// ================================================================= load ====
bool loadAssets(const std::string& dir, Graphics& g, std::string& err) {
    auto fail = [&](const std::string& m) { err = m; return false; };
    // which resolution were the assets made for? (backgrounds are 1024 layout units wide)
    g_shrink = 1;
    for (auto& l : readLines(inDir(dir, "backgrounds/stage1.txt")))
        if (l[0] == "layer" && l.size() >= 5) { g_shrink = std::max(1, std::stoi(l[3]) / 1024 / RENDER_SCALE); break; }
    for (const Group& gr : groups()) {
        std::string base = inDir(dir, "scenery/") + gr.file;
        Bitmap atlas;
        if (!readPng(base + ".png", atlas)) return fail("missing or unreadable " + base + ".png");
        for (auto& l : readLines(base + ".txt")) {
            if (l.size() < 5) continue;
            for (int t = 0; t < SPR_COUNT; ++t)
                if (l[0] == SPRITE_NAMES[t]) {
                    g.sprite[t] = shrunk(cutOut(atlas, std::stoi(l[1]), std::stoi(l[2]), std::stoi(l[3]), std::stoi(l[4])));
                    g.sprite[t].buildMips();
                }
        }
    }
    for (int t = 0; t < SPR_COUNT; ++t)
        if (g.sprite[t].w == 0) return fail(std::string("sprite '") + SPRITE_NAMES[t] + "' not found in " + inDir(dir, "scenery"));
    {
        Bitmap atlas;
        if (!readPng(inDir(dir, "effects.png"), atlas)) return fail("missing or unreadable " + inDir(dir, "effects.png"));
        for (auto& l : readLines(inDir(dir, "effects.txt")))
            for (int i = 0; i < NUM_EFFECTS; ++i)
                if (l.size() >= 5 && l[0] == EFFECT_NAMES[i]) {
                    *effect(g, i) = cutOut(atlas, std::stoi(l[1]), std::stoi(l[2]), std::stoi(l[3]), std::stoi(l[4]));
                    effect(g, i)->buildMips();
                }
        for (int i = 0; i < NUM_EFFECTS; ++i)
            if (effect(g, i)->w == 0) return fail(std::string("effect '") + EFFECT_NAMES[i] + "' not found in effects.txt");
    }
    for (int st = 0; st < NUM_STAGES; ++st) {
        std::string base = inDir(dir, "backgrounds/stage") + std::to_string(st + 1);
        Bitmap atlas;
        if (!readPng(base + ".png", atlas)) return fail("missing or unreadable " + base + ".png");
        g.bg[st].clear();
        for (auto& l : readLines(base + ".txt")) {
            if (l[0] != "layer" || l.size() < 7) continue;
            BgLayer L{shrunk(cutOut(atlas, std::stoi(l[1]), std::stoi(l[2]), std::stoi(l[3]), std::stoi(l[4]))), std::stod(l[5]), std::stoi(l[6])};
            L.bmp.computeBounds();
            g.bg[st].push_back(std::move(L));
        }
    }
    for (int t = 0; t < CAR_TYPES; ++t) {
        std::string base = inDir(dir, "cars/") + CAR_FILE[t];
        Bitmap atlas;
        if (!readPng(base + ".png", atlas)) return fail("missing or unreadable " + base + ".png");
        int mw = 0, mh = 0;
        std::vector<unsigned char> paint;
        if (!readGreyPng(base + "_paint.png", mw, mh, paint) || mw != atlas.w || mh != atlas.h)
            return fail("missing or mismatched " + base + "_paint.png (it must be the same size as " + CAR_FILE[t] + ".png)");
        CarSprites& cs = carSprites(t);
        int fw = CAR_BMP_W, fh = CAR_BMP_H;
        for (int s = 0; s < CAR_YAWS; ++s)
            for (int e = 0; e < CAR_ELEV; ++e) { cs.car[s][e] = CarFrame(); cs.brake[s][e].clear(); cs.exhaust[s][e].clear(); cs.tyres[s][e].clear(); }
        for (auto& row : cs.glare)
            for (auto& v : row) v.clear();
        int found = 0;
        for (auto& l : readLines(base + ".txt")) {
            if (l[0] == "frame" && l.size() >= 3) { fw = std::stoi(l[1]); fh = std::stoi(l[2]); continue; }
            if (l[0] == "exhaust" && l.size() >= 3) {
                int s = std::stoi(l[1]), e = std::stoi(l[2]);
                if (s < 0 || s >= CAR_YAWS || e < 0 || e >= CAR_ELEV) continue;
                for (size_t i = 3; i + 1 < l.size(); i += 2) cs.exhaust[s][e].push_back({std::stod(l[i]), std::stod(l[i + 1])});
                continue;
            }
            if (l[0] == "tyres" && l.size() >= 3) {
                int s = std::stoi(l[1]), e = std::stoi(l[2]);
                if (s < 0 || s >= CAR_YAWS || e < 0 || e >= CAR_ELEV) continue;
                for (size_t i = 3; i + 1 < l.size(); i += 2) cs.tyres[s][e].push_back({std::stod(l[i]), std::stod(l[i + 1])});
                continue;
            }
            if ((l[0] != "car" && l[0] != "brake" && l[0] != "shadow" && l[0] != "glare") || l.size() < 10) continue;
            int s = std::stoi(l[1]), e = std::stoi(l[2]);
            int x = std::stoi(l[3]), y = std::stoi(l[4]), w = std::stoi(l[5]), h = std::stoi(l[6]);
            bool sh = l[0] == "shadow", signedYaw = sh || l[0] == "glare";
            if (s < (signedYaw ? -(CAR_YAWS - 1) : 0) || s >= CAR_YAWS || e < 0 || e >= (sh ? CAR_SHADOW_ELEV : CAR_ELEV)) continue;
            CarFrame f;
            f.bmp = cutOut(atlas, x, y, w, h);
            f.mask.w = f.bmp.w; f.mask.h = f.bmp.h;
            f.mask.px.assign(size_t(f.bmp.w) * f.bmp.h, 0);
            for (int yy = 0; yy < h; ++yy)
                for (int xx = 0; xx < w; ++xx)
                    if (x + xx < mw && y + yy < mh) f.mask.px[size_t(yy) * w + xx] = paint[size_t(y + yy) * mw + x + xx];
            f.ox = std::stoi(l[7]); f.oy = std::stoi(l[8]);
            f.scale = std::stod(l[9]);
            f.frameW = fw; f.frameH = fh;
            shrinkFrame(f);
            if (l[0] == "car") { cs.car[s][e] = std::move(f); ++found; }
            else if (l[0] == "brake") cs.brake[s][e].push_back(std::move(f));
            else if (l[0] == "glare") cs.glare[s + CAR_YAWS - 1][e].push_back(std::move(f));
            else cs.shadow[s + CAR_YAWS - 1][e] = std::move(f);
        }
        if (found != CAR_YAWS * CAR_ELEV)
            return fail(base + ".txt lists " + std::to_string(found) + " car frames, expected " + std::to_string(CAR_YAWS * CAR_ELEV));
    }
    prepareCarSprites();
    return true;
}

std::string findAssetsDir() {
    std::vector<std::string> tries;
    if (const char* e = SDL_getenv("TURBO_ASSETS")) tries.push_back(e);
#if defined(__SWITCH__)
    tries.push_back("romfs:/assets");
#endif
#if defined(__ANDROID__)
    tries.push_back("");                              // packed into the APK as its assets (see android/)
#endif
    if (char* b = SDL_GetBasePath()) {
        std::string base = b;
        SDL_free(b);
        tries.push_back(base + "assets");
        tries.push_back(base + "../assets");
        tries.push_back(base + "../Resources/assets");   // macOS app bundle
    }
    tries.push_back("assets");
    for (auto& d : tries)
        if (exists(inDir(d, "effects.png"))) return d;
    return tries.empty() ? "assets" : tries.back();
}
