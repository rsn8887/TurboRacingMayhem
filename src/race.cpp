#include "race.h"
#include "sprites.h"
#include "audio.h"
#include "cars.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

const int POINTS[10] = {20, 15, 12, 10, 8, 6, 4, 3, 2, 1};

namespace {
std::mt19937 rng(777);
double rnd(double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); }

const double GEAR_TOP[NUM_GEARS] = {0.22, 0.40, 0.60, 0.80, 1.00};
const double LANES[4] = {-0.75, -0.25, 0.25, 0.75};
constexpr double CAR_LEN = 360;
constexpr double CAR_HALF = CAR_WORLD_W / ROAD_WIDTH / 2;   // half width in road units

const char* AI_NAMES[] = {
    "R.VALENTE", "K.NAKAMURA", "T.LINDQVIST", "M.DUBOIS", "S.OKAFOR", "J.HARTMANN",
    "L.MORETTI", "D.KOWALSKI", "A.FERREIRA", "P.OBRIEN", "E.SVENSSON", "C.MARQUEZ",
    "H.TANAKA", "B.NOVAK", "G.BIANCHI", "N.PETROVA", "F.ADEYEMI", "V.LAURENT", "Y.SATO", "I.KARLSSON",
};

std::string ordinal(int n) {
    const char* suf = (n % 100 >= 11 && n % 100 <= 13) ? "TH" : n % 10 == 1 ? "ST" : n % 10 == 2 ? "ND" : n % 10 == 3 ? "RD" : "TH";
    return std::to_string(n) + suf;
}
} // namespace

double gearTop(int g) { return GEAR_TOP[g] * MAX_SPEED; }

// Longitudinal acceleration (fractions of MAX_SPEED per second) from engine, drag, rolling resistance,
// surface and gravity along the slope. 'power' scales the engine (gearbox / throttle).
static double carAccel(const CarStats& st, double u, double power, bool dirt, bool offroad, double slope) {
    double P = ENGINE_POWER * st.accel;
    double ueq = DRAG_BALANCE * st.top;
    double drag = (P / ueq - ROLLING) / (ueq * ueq);
    double trac = TRACTION * (dirt ? 0.72 : 1.0) * std::min(1.0, st.accel);
    double drive = power > 0 ? std::min(trac, P * power / std::max(u, 0.04)) : -ENGINE_BRAKE;
    double res = drag * u * u + ROLLING;
    if (dirt) {   // loose surface: extra drag so the car tops out at st.dirtTop
        double dt = st.dirtTop * st.top;
        res += std::max(0.0, (P / dt - ROLLING) / (dt * dt) - drag) * u * u;
    }
    if (offroad) res += OFFROAD_ROLL + OFFROAD_DRAG * u * u;   // grows with speed: no wall at low speed
    // gravity along the slope; capped because the pseudo-3D hills are far steeper than real roads -
    // heavy cars lose a lot of speed on long climbs
    double a = drive - res - std::clamp(HILL_G * st.weight * slope, -0.09, 0.055);
    // but with the throttle down, no surface or hill can hold a car at a standstill (grass + dirt + a climb
    // would otherwise take more than the tyres can give)
    if (power > 0) a = std::max(a, CRAWL_ACCEL * power * (1 - u / CRAWL_SPEED));
    return a;
}

// Highest speed (world units / s) the sport car can hold at track distance z: the top speed, the cornering
// limit (steering grip just holds the line against the centrifugal drift: 2 grip = 2 sp^2 curve C slide / grip)
// and the top speed on dirt.
double referenceCap(const Track& track, double z) {
    const CarStats& st = CAR_STATS[0];
    const Segment& s = track.find(z);
    bool dirt = s.surface == SURF_DIRT;
    double c = st.top;
    if (std::fabs(s.curve) > 0.05) {
        double g = (dirt ? DIRT_GRIP : 1.0) * st.grip, slide = dirt ? DIRT_SLIDE : 1.0;
        c = std::min(c, 0.95 * g / std::sqrt(CENTRIFUGAL * std::fabs(s.curve) * slide));
    }
    if (dirt) c = std::min(c, st.dirtTop * st.top);
    return c * MAX_SPEED;
}

// Time a perfect driver in the sport car needs from z0 to z1 (track distance, may be negative = on the grid),
// starting at speed v0 (world units / s): full throttle, braking just in time for bends, at the cornering limit
// (steering grip against the centrifugal drift), the top speed on dirt and the real acceleration up and down
// hills. Returns the time; vEnd gets the speed at z1.
double referenceTime(const Track& track, double z0, double z1, double v0, double* vEnd) {
    const CarStats& st = CAR_STATS[0];
    const double dz = SEG_LEN / 2;
    const int n = std::max(1, int(std::ceil((z1 - z0) / dz)));
    std::vector<double> cap(size_t(n) + 1), v(size_t(n) + 1);
    for (int i = 0; i <= n; ++i) cap[size_t(i)] = referenceCap(track, z0 + i * dz);
    v[0] = std::min(v0, cap[0]);
    for (int i = 0; i < n; ++i) {                                 // accelerate as hard as the car can
        double z = z0 + i * dz, vi = std::max(v[size_t(i)], 50.0);
        const Segment& s = track.find(z);
        double a = carAccel(st, vi / MAX_SPEED, 1.0, s.surface == SURF_DIRT, false, track.groundSlope(z)) * MAX_SPEED;
        v[size_t(i) + 1] = std::min(cap[size_t(i) + 1], std::sqrt(std::max(0.0, vi * vi + 2 * a * dz)));
    }
    for (int i = n - 1; i >= 0; --i)                              // brake just in time
        v[size_t(i)] = std::min(v[size_t(i)], std::sqrt(v[size_t(i) + 1] * v[size_t(i) + 1] + 2 * BRAKE_DECEL * MAX_SPEED * dz));
    double t = 0;
    for (int i = 0; i < n; ++i) t += dz / std::max(50.0, 0.5 * (v[size_t(i)] + v[size_t(i) + 1]));
    if (vEnd) *vEnd = v[size_t(n)];
    return t;
}

std::vector<Driver> makeDrivers(int players) {
    std::vector<Driver> d;
    for (int p = 0; p < players; ++p) {
        Driver h; h.name = p == 0 ? "PLAYER 1" : "PLAYER 2"; h.color = p; h.player = p; h.skill = 1.0;
        d.push_back(h);
    }
    int ai = NUM_CARS - players;
    for (int i = 0; i < ai; ++i) {
        Driver a;
        a.name = AI_NAMES[i];
        a.color = 2 + i % (NUM_CAR_COLORS - 2);
        a.skill = 0.79 + 0.21 * double(i) / (ai - 1);     // wide spread of opponents (the best are close to you)
        a.type = (i * 5 + 1) % CAR_TYPES;                  // a mix of sports cars, SUVs, pickups and sedans
        d.push_back(a);
    }
    return d;
}

double Race::delta(double a, double b) const {
    double L = track.length;
    double d = std::fmod(b - a, L);
    if (d > L / 2) d -= L;
    if (d < -L / 2) d += L;
    return d;
}

// How the difficulty changes the CPU drivers (medium = as they always were): their pace on the straights,
// the speed they carry into bends, how hard they accelerate out of them, and how often they make mistakes.
// nitro: how eagerly a CPU driver fires a full bar (x); drift: chance to drift through a sharp bend
// turbo: chance that a CPU driver gets a turbo start
struct Difficulty { double pace, corner, accel, mistakes, nitro, drift, turbo; };
constexpr Difficulty DIFFICULTY[3] = {
    {0.90, 0.92, 0.85, 1.8, 0.35, 0.10, 0.10},   // easy
    {1.00, 1.00, 1.00, 1.0, 1.00, 0.22, 0.25},   // medium
    {1.05, 1.06, 1.15, 0.5, 1.70, 0.35, 0.45},   // hard
};

void Race::start(int stage, const std::vector<Driver>& drivers, const Options& opt, bool attractMode) {
    attract = attractMode;
    difficulty_ = attract ? 1 : std::clamp(opt.difficulty, 0, 2);
    track.build(stage);
    laps = attract ? 999 : opt.laps;
    manual[0] = opt.manual[0]; manual[1] = opt.manual[1];
    cars.clear();
    phase = attract ? RacePhase::Racing : RacePhase::Countdown;
    countdown = 4.0;
    time = 0;
    overTimer = 0;

    // grid: AI sorted fastest first (on pole), as after qualifying, so the field strings out from the start
    // instead of the fast cars queueing behind slow ones; humans at the back like in Lotus
    std::vector<int> grid;
    for (int i = 0; i < int(drivers.size()); ++i) if (drivers[i].player < 0) grid.push_back(i);
    std::sort(grid.begin(), grid.end(), [&](int a, int b) { return drivers[a].skill > drivers[b].skill; });
    for (int i = 0; i < int(drivers.size()); ++i) if (drivers[i].player >= 0) grid.push_back(i);

    {   // time limits: from the time a perfect driver needs on this very track (bends, gravel, hills), plus slack for
        // traffic and mistakes; the first leg includes the standing start from the back of the grid
        double vcp = 0, gridZ = -GRID_FRONT - (NUM_CARS - 1) * GRID_SLOT;
        double tA = referenceTime(track, gridZ, track.checkpointZ, 0, &vcp);
        double tB = referenceTime(track, track.checkpointZ, track.length, vcp);
        bonusA_ = std::round(tA * TIME_SLACK_START) - 8;   // the clock starts at bonusA_ + 8
        bonusB_ = std::round(tB * TIME_SLACK);
    }
    humans = 0;
    for (int g = 0; g < int(grid.size()); ++g) {
        const Driver& d = drivers[grid[g]];
        Car c;
        c.driver = grid[g];
        c.player = attract ? -1 : d.player;
        c.color = d.color;
        c.type = d.type;
        c.skill = std::min(1.0, d.skill + 0.008 * stage);
        c.paceFreq = rnd(0.02, 0.08);      // slow swings: a driver has long fast and slow spells
        c.pacePhase = rnd(0, 6.283);
        c.corner = rnd(0.88, 1.04) + (c.skill - 0.9) * 1.3;   // better drivers carry much more speed into bends
        c.accelK = rnd(0.9, 1.1) * (0.8 + 1.0 * (c.skill - 0.79));   // and get out of them quicker
        if (d.player < 0) {
            c.corner *= DIFFICULTY[difficulty_].corner;
            c.accelK *= DIFFICULTY[difficulty_].accel;
            c.nitro = rnd(0.3, 1.0);   // staggered, so the field doesn't all fire at once
            c.nitroWait = rnd(0.5, 1.5) * AI_NITRO_WAIT / DIFFICULTY[difficulty_].nitro;
        }
        int col = g % 2;                       // pole on the left, then alternating sides
        c.dist = -GRID_FRONT - g * GRID_SLOT;
        if (attract) c.dist += SEG_LEN * 40;
        c.x = col ? 0.42 : -0.42;
        c.targetX = c.x;
        c.laneTimer = rnd(2, 6);
        c.timeLeft = bonusA_ + 8;
        c.cpPassed = c.dist >= 0 ? 0 : -1;
        c.alt = track.heightAt(c.dist);
        if (attract) c.speed = MAX_SPEED * c.skill * 0.8;
        if (c.player >= 0) humans++;
        cars.push_back(c);
    }
    sortOrder();
}

int Race::humanCar(int player) const {
    for (int i = 0; i < int(cars.size()); ++i) if (cars[i].player == player) return i;
    return -1;
}

int Race::position(int idx) const {
    for (int i = 0; i < int(order.size()); ++i) if (order[i] == idx) return i + 1;
    return int(order.size());
}

void Race::sortOrder() {
    order.resize(cars.size());
    for (int i = 0; i < int(cars.size()); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const Car& A = cars[a]; const Car& B = cars[b];
        if (A.finished != B.finished) return A.finished;
        if (A.finished) return A.finishTime < B.finishTime;
        return A.dist > B.dist;
    });
}

void Race::update(double dt, const PlayerInput in[2]) {
    if (phase == RacePhase::Countdown) {
        double before = countdown;
        countdown -= dt;
        for (int t = 3; t >= 1; --t)
            if (before > t && countdown <= t) audio.play(SFX_BEEP_LOW);
        for (auto& c : cars)               // rev the engine on the grid; note when the gas goes down
            if (c.player >= 0) {
                c.throttle = in[c.player].throttle;
                c.rpm += ((0.15 + c.throttle * 0.85) - c.rpm) * std::min(1.0, dt * 6);
                if (c.throttle <= 0.5) c.gasDownAt = -1;
                else if (c.gasDownAt < 0) c.gasDownAt = std::max(0.0, countdown);
            }
        if (countdown <= 0) {
            audio.play(SFX_BEEP_HIGH);
            phase = RacePhase::Racing;
            // turbo start: the gas went down in the last moment before green -> the car leaps off the line with
            // a short nitro-strength push. Held down any earlier -> the wheels spin for a moment.
            bool heard = false;
            for (auto& c : cars) {
                bool turbo = false;
                if (c.player >= 0) {
                    if (c.gasDownAt >= 0 && c.gasDownAt <= TURBO_START_WINDOW) turbo = true;
                    else if (c.gasDownAt > TURBO_START_WINDOW) {
                        c.startBog = START_BOG_TIME;
                        c.msg = "TOO EARLY!"; c.msgTimer = 1.3;
                    }
                } else if (!attract) {
                    turbo = rnd(0, 1) < DIFFICULTY[difficulty_].turbo * (0.6 + 0.4 * c.skill);
                }
                if (turbo) {
                    c.speed = std::max(c.speed, TURBO_START_SPEED * MAX_SPEED);
                    c.turboStart = TURBO_START_TIME;
                    if (c.player >= 0) {
                        c.msg = "TURBO START!"; c.msgTimer = 1.5;
                        if (!heard) { audio.play(SFX_NITRO); heard = true; }
                    }
                }
            }
        }
        return;
    }
    time += dt;
    for (auto& c : cars) {
        if (c.player >= 0 && !c.autopilot) updateHuman(c, in[c.player], dt);
        else updateAI(c, dt);
        physicsCommon(c, dt);
    }
    collisions(dt);
    for (auto& c : cars) progress(c);
    sortOrder();

    if (!attract && humans > 0) {
        bool allDone = true;
        for (auto& c : cars) if (c.player >= 0 && !c.finished && !c.timedOut) allDone = false;
        if (allDone) phase = RacePhase::Over;
    }
    if (phase == RacePhase::Over) overTimer += dt;
}

void Race::updateHuman(Car& c, const PlayerInput& inRaw, double dt) {
    PlayerInput in = inRaw;
    if (c.timedOut) { in.throttle = 0; in.brake = 0.25; in.steer = 0; }
    else if (phase == RacePhase::Racing) {
        if (!c.outOfTime) {
            c.timeLeft -= dt;
            if (c.timeLeft <= 0) {
                c.timeLeft = 0; c.outOfTime = true;
                c.msg = "TIME OUT"; c.msgTimer = 99;
                audio.play(SFX_TIMEOUT);
            }
        }
        if (c.outOfTime) {
            // out of time: the engine is cut, but the car coasts on (and can still be steered). Rolling
            // through a checkpoint adds time and the race goes on; coming to a stop ends it.
            in.throttle = 0;
            in.brake = 0;
            if (c.speed < MAX_SPEED * 0.015 && !c.airborne) c.timedOut = true;
        }
    }
    const Segment& seg = track.find(c.dist);
    double sp = c.speed / MAX_SPEED;
    c.throttle = in.throttle;
    c.braking = in.brake > 0.1;

    if (c.crashTimer > 0) {
        c.crashTimer -= dt;
        c.speed = std::max(0.0, c.speed - CRASH_DECEL * MAX_SPEED * dt);
        c.vx = 0;
        double safe = std::clamp(c.x, -0.8, 0.8);
        c.x += (safe - c.x) * std::min(1.0, dt * 2.5);
        c.steerVis = 0;
        c.skid = 0;
        c.offroad = false;
        c.drifting = false;
        c.boosting = false;
        c.turboStart = c.startBog = 0;
        c.driftVis += (0 - c.driftVis) * std::min(1.0, dt * 6);
        c.boostVis += (0 - c.boostVis) * std::min(1.0, dt * 4);
        c.rpm += (0.15 - c.rpm) * dt * 3;
        return;
    }

    const double w = track.widthAt(c.dist);
    const bool air = c.airborne && c.air > 40;
    const bool dirt = seg.surface == SURF_DIRT && !air;
    const CarStats& st = CAR_STATS[c.type];
    const bool racing = phase == RacePhase::Racing && !c.outOfTime && !c.timedOut;

    // ---- drift: a brake tap while steering hard at speed swings the tail out (OutRun 2 style). It starts as you
    // let go of a short brake press (holding the brake just brakes), and ends when you countersteer, straighten
    // up for a while, slow down too much, leave the road or take off.
    bool brakeDown = in.brake > 0.3;
    bool tapped = false;
    if (brakeDown) c.brakeTap += dt;
    else {
        tapped = c.brakeWas && c.brakeTap < DRIFT_TAP;
        c.brakeTap = 0;
    }
    c.brakeWas = brakeDown;
    if (!c.drifting && racing && tapped && !air && !c.offroad && sp > DRIFT_MIN_SPEED && std::fabs(in.steer) > 0.5) {
        c.drifting = true;
        c.driftDir = in.steer > 0 ? 1 : -1;
        c.driftTime = c.driftCounter = c.driftIdle = 0;
    }
    if (c.drifting) {
        c.driftTime += dt;
        c.driftCounter = in.steer * c.driftDir < -0.4 ? c.driftCounter + dt : 0;
        c.driftIdle = std::fabs(in.steer) < 0.15 ? c.driftIdle + dt : 0;
        if (!racing || sp < DRIFT_MIN_SPEED * 0.75 || c.driftCounter > 0.25 || c.driftIdle > 0.7 || c.offroad || air) {
            c.drifting = false;
            if (c.driftTime >= 1.0 && (c.msgTimer <= 0 || c.msg.rfind("DRIFT", 0) == 0)) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "DRIFT %.1f s", c.driftTime);
                c.msg = buf;
                c.msgTimer = 1.4;
            }
        }
    }
    c.driftVis += ((c.drifting ? c.driftDir : 0) - c.driftVis) * std::min(1.0, dt * 6);

    // ---- nitrous: it can only be fired with a full bar; then it burns while the button is held (with the
    // throttle down) until the bar is empty. It refills slowly, and faster while drifting.
    bool want = in.nitro && racing && in.throttle > 0.1;
    bool boost = want && (c.boosting ? c.nitro > 0 : c.nitro >= 1.0);
    if (boost && !c.boosting) audio.play(SFX_NITRO);
    c.boosting = boost;
    if (boost) c.nitro = std::max(0.0, c.nitro - dt / NITRO_TIME);
    else c.nitro = std::min(1.0, c.nitro + dt / NITRO_RECHARGE);
    if (c.drifting) c.nitro = std::min(1.0, c.nitro + dt * DRIFT_NITRO);
    // turbo start push (braking cancels it), or wheelspin after a too-early start
    if (c.turboStart > 0) c.turboStart = in.brake > 0.3 ? 0 : c.turboStart - dt;
    if (c.startBog > 0) c.startBog -= dt;
    const bool push = boost || c.turboStart > 0;
    c.boostVis += ((push ? 1.0 : 0.0) - c.boostVis) * std::min(1.0, dt * 4);

    // steering + centrifugal force (as in Jake Gordon's / Outrun style pseudo 3D); little grip in the air,
    // less grip and more sliding on dirt
    // the car's sideways velocity follows the steering with a delay (it is heavy); in the air it just
    // keeps drifting the way it was going
    double grip = (dirt ? DIRT_GRIP : 1.0) * st.grip;
    double wanted = in.steer * 2.0 * std::min(1.0, sp * 1.8) * grip;
    if (c.drifting)   // more bite, and the car keeps turning into the drift unless you countersteer
        wanted = std::clamp(in.steer + DRIFT_PULL * c.driftDir, -1.0, 1.0) * 2.0 * std::min(1.0, sp * 1.8) * grip * DRIFT_STEER;
    if (!air) c.vx += (wanted - c.vx) * std::min(1.0, dt * LATERAL_RESP * (0.6 + 0.4 * grip));
    c.x += c.vx * dt;
    if (!air) c.x -= dt * 2.0 * sp * sp * seg.curve * CENTRIFUGAL * (dirt ? DIRT_SLIDE : 1.0) / st.grip * (c.drifting ? DRIFT_HOLD : 1.0);
    c.steerVis += (in.steer - c.steerVis) * std::min(1.0, dt * 8);

    // gearbox
    bool man = manual[std::max(0, c.player)];
    if (man) {
        if (in.shiftUp && c.gear < NUM_GEARS - 1) c.gear++;
        if (in.shiftDown && c.gear > 0) c.gear--;
    }
    if (!man) {   // automatic: shift points scale with the car's top speed
        while (c.gear < NUM_GEARS - 1 && c.speed > 0.96 * gearTop(c.gear) * st.top) c.gear++;
        while (c.gear > 0 && c.speed < 0.86 * gearTop(c.gear - 1) * st.top) c.gear--;
    }
    double rpm = c.speed / (gearTop(c.gear) * st.top);
    c.offroad = !air && std::fabs(c.x) > w * (dirt ? 1.06 : 1.12);
    if (air) {
        c.speed -= c.speed * 0.01 * dt;                 // wheels off the ground: only air drag
    } else {
        // engine power available in this gear: full in automatic; in manual it bogs down at low revs,
        // and the rev limiter cuts in at the top of each gear
        double power = in.throttle;
        if (man) power *= std::clamp(0.35 + rpm * 0.9, 0.35, 1.0);
        const double limiter = push ? NITRO_TOP : 1.0;      // nitrous pushes past the rev limiter
        if (rpm >= limiter) power = 0;
        double a = carAccel(st, sp, power, dirt, c.offroad, track.groundSlope(c.dist));
        if (c.startBog > 0 && a > 0) a *= START_BOG_POWER;   // spinning wheels: the grip is gone
        if (push && rpm < limiter) a += NITRO_ACCEL;
        if (c.drifting) a -= DRIFT_SCRUB;
        if (in.throttle > 0.05 && rpm >= 1.0) a = std::max(a, -0.01);   // sitting on the limiter, not braking
        a -= BRAKE_DECEL * in.brake;
        if (man && rpm > 1.02 && c.gear < NUM_GEARS - 1) a -= 0.15;  // over-rev engine braking (manual downshift)
        c.speed += a * MAX_SPEED * dt;
    }
    c.speed = std::clamp(c.speed, 0.0, MAX_SPEED * OVERSPEED);
    c.x = std::clamp(c.x, -w - 1.6, w + 1.6);

    double targetRpm = std::max(0.15, std::min(push || c.startBog > 0 ? 1.18 : 1.08, c.speed / (gearTop(c.gear) * st.top)));
    if (c.speed < 200) targetRpm = std::max(targetRpm, 0.15 + in.throttle * 0.6);
    if (air) targetRpm = std::min(1.08, targetRpm + 0.25 * in.throttle);   // free-spinning wheels
    c.rpm += (targetRpm - c.rpm) * std::min(1.0, dt * 10);

    double corner = std::fabs(seg.curve) * sp * sp;
    c.skid = (!c.offroad && !air && corner > 2.0 && std::fabs(in.steer) > 0.4) ? std::min(1.0, (corner - 2.0) / 2.5 + 0.3) : 0;
    if (c.startBog > 0 && in.throttle > 0.1) c.skid = 1.0;      // spinning wheels: smoke and screech
    if (!c.offroad && !air && in.brake > 0.5 && sp > 0.55) c.skid = std::max(c.skid, 0.6);
    if (c.drifting && !air) c.skid = 1.0;
    if (dirt) c.skid = 0;     // no tyre squeal on loose ground (the gravel rumble plays instead)

    auto crash = [&]() {
        if (c.speed > MAX_SPEED * 0.12) {
            c.crashTimer = 1.5;
            c.speed *= 0.35;
            c.shake = 1.0;
            audio.play(SFX_CRASH);
        } else {
            c.speed = 0;
            c.x += (c.x > 0 ? -0.06 : 0.06);
        }
    };

    // tunnel walls and guardrails: the tunnel portal face is a crash, the walls/rails make you scrape along
    double wall = track.wallAt(c.dist);
    if (wall > 0 && !(air && c.air > RAIL_HEIGHT && !seg.tunnel)) {
        double limit = wall - CAR_HALF * 0.9;
        if (seg.tunnel && std::fabs(c.x) > wall + 0.12) { crash(); c.x = std::clamp(c.x, -limit, limit); return; }
        if (!seg.tunnel && std::fabs(c.x) > wall + 0.25) {
            // already outside the guardrail (came in from the verge): stay outside
        } else if (std::fabs(c.x) > limit) {
            c.x = std::clamp(c.x, -limit, limit);
            c.vx = -c.vx * 0.3;
            c.x -= (c.x > 0 ? 1 : -1) * 0.02;
            c.speed *= std::pow(0.45, dt);
            if (c.bumpTimer <= 0) audio.play(SFX_BUMP);
            c.bumpTimer = 0.2;
            c.shake = std::max(c.shake, 0.3);
            c.skid = 0.9;
        }
    }

    // roadside obstacles (you can jump over them!) - check every segment crossed this step
    if (c.air > 250) return;
    int crossed = int(c.speed * dt / SEG_LEN) + 1;
    for (int j = 0; j < crossed; ++j)
    for (const auto& s : track.find(c.dist + j * SEG_LEN).sprites) {
        bool hit = false;
        if (s.type == SPR_GANTRY_START || s.type == SPR_GANTRY_CHECK) {
            hit = std::fabs(c.x) > 1.22 * w && std::fabs(c.x) < 1.50 * w;
        } else if (SPRITE_INFO[s.type].hitFrac > 0) {
            const Bitmap& b = gfx.sprite[s.type];
            double sw = SPRITE_INFO[s.type].worldH * b.w / b.h / ROAD_WIDTH * SPRITE_INFO[s.type].hitFrac;
            hit = std::fabs(c.x - s.offset) < sw / 2 + CAR_HALF * 0.8;
        }
        if (!hit) continue;
        crash();
        return;
    }
}

void Race::updateAI(Car& c, double dt) {
    if (phase != RacePhase::Racing && !c.autopilot && phase != RacePhase::Over) return;
    double sp = c.speed / MAX_SPEED;
    const CarStats& st = CAR_STATS[c.type];
    double target = c.skill * MAX_SPEED * (0.5 + 0.5 * st.top);
    if (c.autopilot) target = MAX_SPEED * ((c.player >= 0 && !c.finished) ? c.skill : 0.7);   // (unfinished human on autopilot = test mode)
    c.timeLeft = std::max(0.0, c.timeLeft);

    // personality: each driver's pace drifts slowly up and down, and now and then someone makes a
    // mistake and lifts for a moment - this spreads the field out over a lap
    if (!c.autopilot) {
        const Difficulty& df = DIFFICULTY[difficulty_];
        target *= df.pace * (1 + 0.06 * std::sin(time * c.paceFreq * 6.283 + c.pacePhase));
        if (c.mistake > 0) { c.mistake -= dt; target *= 0.75; }
        else if (rnd(0, 1) < dt * (0.02 + 0.06 * (1 - c.skill)) * df.mistakes) c.mistake = rnd(0.8, 2.2);   // weaker drivers err more
    }
    // slow down for upcoming bends (look further ahead at higher speed)
    double maxC = 0;
    for (int k = 0; k < 36; k += 2) maxC = std::max(maxC, std::fabs(track.find(c.dist + k * SEG_LEN).curve));
    bool dirtAhead = track.find(c.dist).surface == SURF_DIRT || track.find(c.dist + 10 * SEG_LEN).surface == SURF_DIRT;
    double slide = dirtAhead ? DIRT_SLIDE / DIRT_GRIP : 1.0;
    const double cornerK = c.drifting ? AI_DRIFT_CORNER : 1.0;
    if (maxC > 0.1) target = std::min(target, MAX_SPEED * std::min(1.0, cornerK * c.corner / (CENTRIFUGAL * slide * maxC)));
    if (dirtAhead) target = std::min(target, MAX_SPEED * st.dirtTop * (0.75 + 0.25 * c.skill));
    // hills: slower uphill, faster downhill
    double slope = track.slopeAt(c.dist + 3 * SEG_LEN);
    target *= std::clamp(1 - slope * 0.5 * st.weight, 0.65, 1.12);

    // road width now and a little ahead (narrow sections squeeze the lanes together)
    const double wAI = std::min(track.widthAt(c.dist), track.widthAt(c.dist + 8 * SEG_LEN));
    double lanes[4];
    for (int i = 0; i < 4; ++i) lanes[i] = LANES[i] * wAI;

    // traffic
    auto laneFree = [&](double lane, const Car* self) {
        for (auto& o : cars) {
            if (&o == self) continue;
            double dz = delta(self->dist, o.dist);
            if (dz > -5 * SEG_LEN && dz < 22 * SEG_LEN && std::fabs(o.x - lane) < 0.42) return false;
        }
        return true;
    };
    const Car* blocker = nullptr;
    double bestDz = 1e18;
    for (auto& o : cars) {
        if (&o == &c) continue;
        double dz = delta(c.dist, o.dist);
        if (dz > 0 && dz < 20 * SEG_LEN && std::fabs(o.x - c.x) < 0.42 && dz < bestDz) { bestDz = dz; blocker = &o; }
    }
    if (blocker && blocker->speed < target * 0.98) {
        double bestLane = c.targetX, bestD = 1e9;
        for (double lane : lanes) {
            if (std::fabs(lane - blocker->x) < 0.42) continue;
            if (!laneFree(lane, &c)) continue;
            double d = std::fabs(lane - c.x);
            if (d < bestD) { bestD = d; bestLane = lane; }
        }
        if (bestD < 1e9) c.targetX = bestLane;
        else target = std::min(target, blocker->speed * 0.98);
        if (bestDz < 4 * SEG_LEN && std::fabs(blocker->x - c.x) < 0.38) target = std::min(target, blocker->speed * 0.9);
    }
    c.laneTimer -= dt;
    if (c.laneTimer <= 0) {
        c.laneTimer = rnd(3, 9);
        double lane = lanes[int(rnd(0, 3.999))];
        if (laneFree(lane, &c)) c.targetX = lane;
    }
    c.targetX = std::clamp(c.targetX, -0.78 * wAI, 0.78 * wAI);
    double dx = c.targetX - c.x;
    double step = dt * 0.9 * (0.3 + sp);
    c.x += std::clamp(dx, -step, step);

    // ---- nitro and drift, like the player (not for cars on autopilot after the finish)
    const bool aiRacing = phase == RacePhase::Racing && !c.autopilot && !c.finished;
    const Difficulty& dfc = DIFFICULTY[difficulty_];
    const double curNow = track.find(c.dist).curve;
    bool dirtHere = track.find(c.dist).surface == SURF_DIRT;
    // drift: decide once per sharp bend (better drivers drift more); it ends when the bend does
    if (std::fabs(curNow) < 0.5) c.driftRolled = false;
    if (!c.drifting && !c.driftRolled && aiRacing && std::fabs(curNow) >= AI_DRIFT_MIN) {
        c.driftRolled = true;
        if (!c.airborne && !dirtHere && sp > DRIFT_MIN_SPEED && c.mistake <= 0 &&
            rnd(0, 1) < dfc.drift * (0.5 + 0.5 * c.skill)) {
            c.drifting = true;
            c.driftDir = curNow > 0 ? 1 : -1;
            c.driftTime = 0;
        }
    }
    if (c.drifting) {
        c.driftTime += dt;
        if (!aiRacing || std::fabs(curNow) < 1.0 || curNow * c.driftDir < 0 || sp < DRIFT_MIN_SPEED * 0.75 ||
            c.airborne || dirtHere || c.mistake > 0)
            c.drifting = false;
    }
    c.driftVis += ((c.drifting ? c.driftDir : 0) - c.driftVis) * std::min(1.0, dt * 6);
    // nitro: only from a full bar, after a short wait, on a clear stretch (no sharp bend or gravel ahead, nobody
    // right in front); a bend coming up cuts it. Drivers near a human player are keener, to keep it a fight.
    bool nearHuman = false;
    for (auto& o : cars)
        if (o.player >= 0 && std::fabs(delta(c.dist, o.dist)) < 40 * SEG_LEN) nearHuman = true;
    const bool clear = maxC < 1.2 && !dirtAhead && !(blocker && bestDz < 10 * SEG_LEN) && !c.airborne;
    if (c.boosting) {
        c.nitro -= dt / NITRO_TIME;
        if (c.nitro <= 0 || !aiRacing || maxC > 2.0 || dirtAhead) { c.nitro = std::max(0.0, c.nitro); c.boosting = false; }
    } else {
        c.nitro = std::min(1.0, c.nitro + dt / NITRO_RECHARGE);
        if (aiRacing && c.nitro >= 1.0 && clear && c.mistake <= 0) {
            c.nitroWait -= dt * (nearHuman ? 2.0 : 1.0);
            if (c.nitroWait <= 0) {
                c.boosting = true;
                c.nitroWait = rnd(0.5, 1.5) * AI_NITRO_WAIT / dfc.nitro;
                for (auto& o : cars)   // you hear it when it goes off right next to you
                    if (o.player >= 0 && std::fabs(delta(c.dist, o.dist)) < 12 * SEG_LEN) { audio.play(SFX_NITRO); break; }
            }
        }
    }
    if (c.drifting) c.nitro = std::min(1.0, c.nitro + dt * DRIFT_NITRO);
    if (c.turboStart > 0) c.turboStart -= dt;
    const bool aiPush = c.boosting || c.turboStart > 0;
    c.boostVis += ((aiPush ? 1.0 : 0.0) - c.boostVis) * std::min(1.0, dt * 4);
    if (aiPush) target *= NITRO_TOP;

    // what the wheel would show: lane changes plus steering into the bend (as much as a driver needs to hold
    // the line against the centrifugal push at this speed), so cars in a bend are seen turned into it
    double bend = c.airborne ? 0 : sp * track.find(c.dist).curve * CENTRIFUGAL / st.grip;
    c.steerVis += (std::clamp(dx * 5 + bend, -1.0, 1.0) - c.steerVis) * std::min(1.0, dt * 4);

    // same force model as the player's car (AI drivers just have a slightly different right foot)
    double gslope = c.airborne ? 0 : track.groundSlope(c.dist);
    bool dirtNow = track.find(c.dist).surface == SURF_DIRT;
    if (c.airborne) c.speed -= c.speed * 0.01 * dt;
    else if (c.speed < target) c.speed += (carAccel(st, sp, 1.0, dirtNow, false, gslope) * c.accelK + (aiPush ? NITRO_ACCEL : 0)) * MAX_SPEED * dt;
    else c.speed = std::max(target, c.speed + (carAccel(st, sp, 0, dirtNow, false, gslope) - BRAKE_DECEL * 0.5) * MAX_SPEED * dt);
    c.braking = c.speed > target * 1.04 && slope > -0.05;
    c.speed = std::clamp(c.speed, 0.0, MAX_SPEED * OVERSPEED);
    c.gear = std::min(NUM_GEARS - 1, int(sp * 5));
    c.rpm = std::max(0.15, std::min(1.0, c.speed / gearTop(c.gear)));
    if (c.drifting && !c.airborne) c.speed = std::max(0.0, c.speed - DRIFT_SCRUB * MAX_SPEED * dt);
    c.throttle = c.braking ? 0 : 0.8;
    c.skid = c.drifting && !c.airborne ? 1.0 : 0;
    c.offroad = false;
}

void Race::physicsCommon(Car& c, double dt) {
    if (phase == RacePhase::Countdown) return;
    c.dist += c.speed * dt;
    c.bgOffset += track.curveAt(c.dist) * (c.speed / MAX_SPEED) * dt * 60;
    c.inTunnel = track.tunnelAt(c.dist);
    c.onDirt = track.find(c.dist).surface == SURF_DIRT;

    // vertical motion: on the road the car follows the ground (suspension + downforce can hold it down
    // up to TAKEOFF_ACCEL). When a crest or a kicker drops away faster than that, the car leaves the road
    // with its current vertical speed and flies a free-fall parabola under GRAVITY until it lands.
    double ground = track.heightAt(c.dist);
    double groundVy = c.speed * track.groundSlope(c.dist);
    if (!c.airborne) {
        double need = (groundVy - c.vy) / dt;          // vertical acceleration needed to stay on the road
        if (need < -TAKEOFF_ACCEL && c.speed > MAX_SPEED * 0.15) {
            c.airborne = true;                         // keep c.vy: launched along the lip of the ramp
            c.airTime = 0;
        } else {
            c.alt = ground;
            c.vy = groundVy;
        }
    }
    if (c.airborne) {
        c.vy -= GRAVITY * dt;
        c.alt += c.vy * dt;
        c.airTime += dt;
        if (c.alt <= ground) {
            double impact = groundVy - c.vy;           // vertical speed relative to the road
            c.landTimer = 0.3;
            if (c.player >= 0 && impact > 1500) {
                c.shake = std::max(c.shake, std::min(1.0, impact / 6000 + 0.15));
                audio.play(SFX_LAND);
                c.speed *= 1.0 - std::min(0.06, impact / 120000);   // a hard landing scrubs off some speed
            }
            c.airborne = false;
            c.alt = ground;
            c.vy = groundVy;
        }
    }
    c.air = std::max(0.0, c.alt - ground);
    double pv = c.airborne ? c.vy / std::max(c.speed, 2000.0 * SPEED_SCALE) : track.groundSlope(c.dist);
    c.pitchVis += (pv - c.pitchVis) * std::min(1.0, dt * 8);
    c.landTimer = std::max(0.0, c.landTimer - dt);
    if (c.msgTimer > 0) c.msgTimer -= dt;
    c.bumpTimer = std::max(0.0, c.bumpTimer - dt);
    c.shake = std::max(0.0, c.shake - dt * 2);
}

void Race::collisions(double) {
    for (int i = 0; i < int(cars.size()); ++i)
        for (int j = 0; j < int(cars.size()); ++j) {
            if (i == j) continue;
            Car& a = cars[i]; Car& b = cars[j];          // a behind b
            if (a.player < 0 && b.player < 0) continue;  // AI cars avoid each other on their own
            if (a.crashTimer > 0 || b.crashTimer > 0) continue;
            double dz = delta(a.dist, b.dist);
            double dx = b.x - a.x;
            if (dz < 0 || dz > CAR_LEN || std::fabs(dx) > CAR_HALF * 2 * 0.85) continue;
            if (std::fabs(a.alt - b.alt) > 400) continue;                 // one of them is flying over the other
            if (a.speed > b.speed) {
                double impact = a.speed - b.speed;
                a.speed = b.speed * (a.player >= 0 ? 0.8 : 0.92);
                a.dist = b.dist - CAR_LEN;
                if (b.player >= 0 && a.player < 0) b.speed = std::min(MAX_SPEED, b.speed + impact * 0.2);
                if (a.player >= 0 || b.player >= 0) {
                    if (a.bumpTimer <= 0) audio.play(SFX_BUMP);
                    a.bumpTimer = 0.3; b.bumpTimer = 0.3;
                    if (a.player >= 0) a.shake = 0.5;
                    if (b.player >= 0) b.shake = 0.4;
                }
            }
            // side push
            double push = (CAR_HALF * 2 * 0.85 - std::fabs(dx)) * 0.5;
            double s = dx >= 0 ? 1 : -1;
            a.x -= s * push * 0.5;
            b.x += s * push * 0.5;
            if (a.player < 0) a.targetX = std::clamp(a.x, -0.75, 0.75);
            if (b.player < 0) b.targetX = std::clamp(b.x, -0.75, 0.75);
        }
}

void Race::progress(Car& c) {
    if (phase == RacePhase::Countdown) return;
    int cp = track.checkpointCount(c.dist);
    while (cp > c.cpPassed) {
        c.cpPassed++;
        int k = c.cpPassed;
        if (k == 0) { c.lapStart = time; continue; }
        if (k % 2 == 0) {                                   // crossed the start/finish line
            c.lastLap = time - c.lapStart;
            if (c.bestLap == 0 || c.lastLap < c.bestLap) c.bestLap = c.lastLap;
            c.lapStart = time;
            if (k / 2 >= laps && !c.finished) {
                c.finished = true;
                c.outOfTime = false;
                c.finishTime = time;
                if (c.player >= 0) {
                    int pos = 1;
                    for (auto& o : cars) if (&o != &c && o.finished) pos++;
                    c.msg = "FINISHED " + ordinal(pos);
                    c.msgTimer = 99;
                    c.autopilot = true;
                    c.targetX = std::clamp(c.x, -0.75, 0.75);
                    audio.play(SFX_CHECKPOINT);
                } else c.autopilot = true;
                continue;
            }
            if (c.player >= 0 && !c.timedOut) {
                c.outOfTime = false;                         // rolled through just in time
                c.lastBonus = bonusA_;
                c.timeLeft = std::min(99.0, c.timeLeft + bonusA_);
                int lap = k / 2 + 1;
                c.msg = lap == laps ? "FINAL LAP" : "LAP " + std::to_string(lap);
                c.msgTimer = 2.5;
                audio.play(SFX_CHECKPOINT);
            }
        } else if (c.player >= 0 && !c.timedOut && !c.finished) {   // mid-lap checkpoint
            c.outOfTime = false;
            c.lastBonus = bonusB_;
            c.timeLeft = std::min(99.0, c.timeLeft + bonusB_);
            c.msg = "CHECKPOINT";
            c.msgTimer = 2.5;
            audio.play(SFX_CHECKPOINT);
        }
    }
}
