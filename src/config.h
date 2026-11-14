// Turbo Racing Mayhem - pseudo-3D arcade racer in C++/SDL2
#pragma once
#include <cstdint>

#define GAME_TITLE   "Turbo Racing Mayhem"
#ifndef GAME_VERSION
#define GAME_VERSION "1.4.0"     // set by CMake from project(... VERSION)
#endif

// Internal framebuffer (16:9, scales 2x to 720p and 3x to 1080p)
// Layout units: menus, HUD and text are laid out on a 640x360 grid. The game itself is rendered at
// RENDER_SCALE times that, i.e. 1280x720 (native on Nintendo Switch, 2x on a 2560x1440 Retina window).
constexpr int SCREEN_W = 640;
constexpr int SCREEN_H = 360;
#ifndef TURBO_RENDER_SCALE
#define TURBO_RENDER_SCALE 2          // cmake -DTURBO_RENDER_SCALE=1 renders at 640x360 (for slow machines)
#endif
constexpr int RENDER_SCALE = TURBO_RENDER_SCALE;
constexpr int CANVAS_W = SCREEN_W * RENDER_SCALE;   // 1280
constexpr int CANVAS_H = SCREEN_H * RENDER_SCALE;   // 720

constexpr double STEP = 1.0 / 60.0;     // fixed simulation step

// ---- World / projection ---------------------------------------------------
constexpr double SEG_LEN       = 200.0;   // length of one road segment
constexpr double SPEED_SCALE   = 1.6;     // world speed multiplier: higher = road rushes past faster
constexpr double STRIPE_LEN    = 400.0 * SPEED_SCALE;   // light/dark alternation (like the zOffset trick in main.lua)
constexpr double ROAD_WIDTH    = 2000.0;  // half width of the road (4 lanes, as in test1.png)
constexpr double CAMERA_HEIGHT = 1000.0;
constexpr double FIELD_OF_VIEW = 100.0;   // degrees
constexpr int    DRAW_DIST     = 300;     // segments drawn ahead
constexpr double HORIZON_FRAC  = 0.47;    // horizon position in the viewport (0=top)

// ---- Car -------------------------------------------------------------------
constexpr double MAX_SPEED     = SEG_LEN / STEP * SPEED_SCALE;  // 19200 units/s (~ 290 km/h displayed)
constexpr double KMH_AT_MAX    = 290.0;
// Longitudinal physics (all rates in "fractions of MAX_SPEED per second"):
//   accel = min(traction, power / speed) - drag * speed^2 - rolling - brakes - gravity * slope
// Drag balances the engine just above top speed; the rev limiter caps the car at top speed.
// Tuned for a heavy feel: 0-100 km/h ~5 s, 0-200 ~11.5 s, 0-250 ~17 s, flat out after ~30 s.
constexpr double ENGINE_POWER  = 0.100;
constexpr double TRACTION      = 0.085;   // max accel the tyres can transmit (from a standstill)
constexpr double ROLLING       = 0.012;   // rolling resistance
constexpr double DRAG_BALANCE  = 1.08;    // drag equals engine power at this multiple of top speed
constexpr double ENGINE_BRAKE  = 0.065;   // extra slowing when off the throttle (engine braking + friction)
constexpr double BRAKE_DECEL   = 0.40;    // full brakes (~1.1 g)
constexpr double OFFROAD_DRAG  = 0.40;    // grass/sand drag (x speed^2): top speed ~100 km/h, slows you down gradually
constexpr double OFFROAD_ROLL  = 0.01;    // grass/sand rolling resistance (small, so you can always drive off again)
constexpr double CRAWL_ACCEL   = 0.03;    // with the throttle down you can always pull away, whatever the surface or slope
constexpr double CRAWL_SPEED   = 0.08;    // ... up to this speed (~23 km/h)
constexpr double CRASH_DECEL   = 0.55;    // sliding/spinning to a stop after a crash
constexpr double LATERAL_RESP  = 4.5;     // how quickly the car's sideways motion follows the steering (inertia)
constexpr double CENTRIFUGAL   = 0.22;
constexpr double DIRT_TOP      = 0.78;    // top speed on dirt (fraction of MAX_SPEED)
constexpr double DIRT_GRIP     = 0.7;     // steering response on dirt
constexpr double DIRT_SLIDE    = 1.45;    // extra centrifugal drift on dirt
constexpr double CAR_WORLD_W   = 720.0;   // width of a car body in world units (collisions)
constexpr double HILL_G        = 0.16;    // gravity along slopes (x car weight factor)
constexpr double OVERSPEED     = 1.15;    // downhill you can go this much faster than top speed
constexpr double GRAVITY       = 6000.0;  // free fall in world units / s^2: jumps hang ~1 s on smooth parabolas
constexpr double TAKEOFF_ACCEL = GRAVITY * 4.5;   // suspension + downforce keep you on the road over gentle crests
constexpr int    NUM_GEARS     = 5;
// Drift (OutRun 2 style): tap the brake while steering hard at speed and keep the throttle down. The car swings
// its tail out and holds a tighter line through bends with little loss of speed; it fills the nitro bar.
constexpr double DRIFT_MIN_SPEED = 0.45;  // fraction of MAX_SPEED needed to start a drift (~130 km/h)
constexpr double DRIFT_TAP       = 0.35;  // a brake press shorter than this (s) is a tap: letting go starts the drift
constexpr double DRIFT_HOLD      = 0.45;  // centrifugal push while drifting (x normal): the car holds its line
constexpr double DRIFT_STEER     = 1.35;  // extra steering authority while drifting
constexpr double DRIFT_PULL      = 0.30;  // the car keeps turning into the drift on its own (fraction of full lock)
constexpr double DRIFT_SCRUB     = 0.020; // speed lost while drifting (fraction of MAX_SPEED per second)
constexpr double DRIFT_NITRO     = 0.14;  // nitro gained per second of drifting (full bar = 1)
// Nitrous: hold the nitro button for a big push and a higher top speed until the bar is empty; it refills slowly
constexpr double NITRO_ACCEL     = 0.13;  // extra acceleration (fraction of MAX_SPEED per second)
constexpr double NITRO_TOP       = 1.15;  // the rev limiter lets you go this far past the gear's top speed
constexpr double NITRO_TIME      = 3.5;   // a full bar lasts this long (s)
constexpr double NITRO_RECHARGE  = 28.0;  // an empty bar refills in this long without drifting (s)
// turbo start: put the gas down in the last moment before the lights turn green
constexpr double TURBO_START_WINDOW = 0.35;  // the gas must go down at most this long before green (s)
constexpr double TURBO_START_SPEED  = 0.30;  // the car leaps off the line at this speed (x MAX_SPEED)
constexpr double TURBO_START_TIME   = 1.2;   // then it gets a nitro-strength push for this long (s)
constexpr double START_BOG_TIME     = 0.8;   // gas down too early: the wheels spin this long, with little drive (s)
constexpr double START_BOG_POWER    = 0.3;   // share of the acceleration left while they spin
// CPU drivers use the same nitro bar and drift (how often depends on the difficulty, see race.cpp)
constexpr double AI_NITRO_WAIT   = 9.0;   // average wait (s, Medium) with a full bar before firing on a clear stretch
constexpr double AI_DRIFT_MIN    = 2.0;   // a CPU car only drifts through bends at least this sharp
constexpr double AI_DRIFT_CORNER = 1.07;  // drifting lets a CPU car carry this much more speed through the bend
// time limits: this much longer than a perfect driver needs (the first leg, with the standing start from the back
// of the grid and the traffic, gets more)
constexpr double TIME_SLACK_START = 1.28;
constexpr double TIME_SLACK       = 1.15;

constexpr int    NUM_CARS      = 20;      // grid size (players + AI)
// staggered starting grid: consecutive cars alternate left/right and are GRID_SLOT apart, so every car has
// an empty slot in front of it on its own side of the road (2 * GRID_SLOT to the car ahead on the same side)
constexpr double GRID_SLOT     = SEG_LEN * 5.5;
constexpr double GRID_FRONT    = SEG_LEN * 1.5;   // pole position: this far behind the start line
constexpr int    QUALIFY_POS   = 10;      // finish in the top 10 to continue the championship

// ---- Colors ----------------------------------------------------------------
using Color = uint32_t;                   // 0xAARRGGBB
constexpr Color rgb(int r, int g, int b) { return 0xFF000000u | (uint32_t(r & 255) << 16) | (uint32_t(g & 255) << 8) | uint32_t(b & 255); }
constexpr Color TRANSPARENT = 0;
