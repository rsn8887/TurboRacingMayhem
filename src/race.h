// Race simulation: cars, physics, AI opponents, checkpoints/time limit, championship
#pragma once
#include "track.h"
#include "input.h"
#include <string>
#include <vector>

struct Driver {
    std::string name;
    int color = 0;
    double skill = 0.85;       // fraction of MAX_SPEED the AI aims for
    int points = 0;
    int player = -1;           // 0/1 for humans, -1 for AI
    int type = 0;              // CarType
};

struct Car {
    int driver = 0;            // index into the driver table
    int player = -1;           // 0/1 human, -1 AI
    int color = 0;
    int type = 0;              // CarType (see cars.h)
    double pitchVis = 0;       // smoothed nose-up/down for the sprite (slope or flight path)
    double dist = 0;           // continuous distance from start line (negative on the grid)
    double x = 0;              // lateral position in road half-widths
    double vx = 0;             // lateral velocity (road half-widths per second) - gives the car sideways inertia
    double speed = 0;
    int gear = 0;
    double rpm = 0.15, throttle = 0;
    double steerVis = 0;       // smoothed steering for sprite frame
    bool braking = false, offroad = false;
    double skid = 0;
    // AI
    double skill = 0.85, targetX = 0, laneTimer = 0;
    // AI personality so the field spreads out instead of driving as a pack
    double paceFreq = 0.1, pacePhase = 0, corner = 1.0, accelK = 1.0, mistake = 0;
    double nitroWait = 0;        // CPU: how long it waits with a full bar before it fires the nitro
    bool driftRolled = false;    // CPU: already decided whether to drift through the current bend
    bool autopilot = false;
    // race state
    int cpPassed = -1;         // number of half-laps passed (0 = crossed start)
    bool finished = false, timedOut = false;
    bool outOfTime = false;     // the clock hit zero: no more throttle, but the car may still roll through a checkpoint
    double finishTime = 0;
    double timeLeft = 0;
    double lapStart = 0, lastLap = 0, bestLap = 0;
    double crashTimer = 0, bumpTimer = 0, shake = 0;
    double bgOffset = 0;
    // vertical motion (jumps)
    double alt = 0, vy = 0, air = 0;   // absolute altitude, vertical speed, height above the road
    bool airborne = false;
    double airTime = 0, landTimer = 0;
    bool inTunnel = false;
    // drift (see DRIFT_* in config.h): driftDir -1 / +1 while drifting, driftVis the smoothed swing of the tail
    bool drifting = false;
    int driftDir = 0;
    double driftTime = 0, driftVis = 0, driftCounter = 0, driftIdle = 0, brakeTap = 0;   // brakeTap: how long the brake is held
    bool brakeWas = false;
    // nitrous: bar 0..1, boosting while the button is held; boostVis is the smoothed effect (camera, flames)
    double nitro = 1.0, boostVis = 0;
    bool boosting = false;
    double gasDownAt = -1;       // countdown time when the gas went down (and is still held), -1 = not held
    double turboStart = 0;       // > 0: turbo start push still running (s)
    double startBog = 0;         // > 0: wheelspin after a too-early start (s)
    bool onDirt = false;
    double lastBonus = 0;
    std::string msg; double msgTimer = 0; double msgFlash = 0;
};

struct Options {
    int players = 1;
    bool manual[2] = {false, false};
    int carType[2] = {0, 2};
    int laps = 3;
    bool sound = true;
    int startStage = 0;
    bool championship = true;
    int difficulty = 1;       // CPU drivers: 0 easy, 1 medium, 2 hard (see DIFFICULTY in race.cpp)
};

enum class RacePhase { Countdown, Racing, Over };

class Race {
public:
    Track track;
    std::vector<Car> cars;
    std::vector<int> order;    // car indices by race position
    int laps = 3;
    int humans = 0;
    bool attract = false;
    RacePhase phase = RacePhase::Countdown;
    double countdown = 4.0;
    double time = 0;           // race clock (since green light)
    double overTimer = 0;
    bool manual[2] = {false, false};

    void start(int stage, const std::vector<Driver>& drivers, const Options& opt, bool attractMode = false);
    void update(double dt, const PlayerInput in[2]);
    int  position(int carIndex) const;      // 1-based
    int  humanCar(int player) const;
    bool done() const { return phase == RacePhase::Over && overTimer > 4.0; }
    double delta(double a, double b) const;  // wrapped distance b-a in [-L/2, L/2]
    double legBonus(int leg) const { return leg ? bonusB_ : bonusA_; }   // 0: start->checkpoint, 1: checkpoint->finish

private:
    double bonusA_ = 30, bonusB_ = 30;
    int difficulty_ = 1;
    void updateHuman(Car& c, const PlayerInput& in, double dt);
    void updateAI(Car& c, double dt);
    void physicsCommon(Car& c, double dt);
    void collisions(double dt);
    void progress(Car& c);
    void sortOrder();
};

// championship points for places 1..10
extern const int POINTS[10];
std::vector<Driver> makeDrivers(int players);
double gearTop(int g);
// time a perfect driver needs from z0 to z1 (see race.cpp); sets the time limits
double referenceTime(const Track& track, double z0, double z1, double v0, double* vEnd = nullptr);
double referenceCap(const Track& track, double z);
