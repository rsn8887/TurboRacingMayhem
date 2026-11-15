// Car models: six procedurally painted body types, each with steering and hill-pitch frames.
// Bases are painted once in a neutral grey with a "paint" mask, then recoloured per car colour on demand.
#pragma once
#include <array>
#include <vector>
#include <utility>
#include "canvas.h"
#include <cmath>

enum CarType { CAR_SPORT, CAR_COUPE, CAR_SUPER, CAR_SUV, CAR_PICKUP, CAR_SEDAN, CAR_TYPES };

// Sprite resolution: a 190 cm wide car is CAR_BODY_PX wide, a little more than it is ever drawn on screen,
// so cars are only ever scaled down (filtered), never blown up into uneven blocks.
constexpr double CAR_PX_PER_CM = 0.75 * RENDER_SCALE;
constexpr double CAR_BODY_PX = 190 * CAR_PX_PER_CM;                 // 285 px at 720p
constexpr int CAR_BMP_W = 280 * RENDER_SCALE, CAR_BMP_H = 204 * RENDER_SCALE;   // room for the yawed / elevated views
constexpr int CAR_ANCHOR_Y = 177 * RENDER_SCALE;    // bitmap row where the rear wheels touch the ground
constexpr double CAR_SPRITE_W = 720.0 * CAR_BMP_W / CAR_BODY_PX;   // world width of a whole bitmap (720 = the car)
// yaw: frames every CAR_YAW_STEP degrees from 0 to (CAR_YAWS-1)*CAR_YAW_STEP; negative yaws are mirrored
constexpr int CAR_YAWS = 9;
constexpr double CAR_YAW_STEP = 2.5;                // 0 .. 20 degrees
// viewing elevations the frames are rendered at (degrees above the car). The renderer picks the nearest one
// from where the car is seen from (far away = flat view) plus its own pitch (climbing = more roof visible).
constexpr int CAR_ELEV = 6;
constexpr double CAR_ELEV_DEG[CAR_ELEV] = {1.5, 4.5, 8, 12, 16, 20};
constexpr int CAR_ELEV_LEVEL = 3;                   // the normal chase view of the player's own car (12 degrees)
inline int carElevIndex(double deg) {               // nearest rendered elevation
    int best = 0;
    for (int i = 1; i < CAR_ELEV; ++i)
        if (std::fabs(CAR_ELEV_DEG[i] - deg) < std::fabs(CAR_ELEV_DEG[best] - deg)) best = i;
    return best;
}

struct CarStats {
    const char* name;
    double top;      // top speed factor
    double accel;    // acceleration factor
    double grip;     // steering grip factor (also less sliding)
    double dirtTop;  // top speed on dirt (fraction of MAX_SPEED)
    double weight;   // how strongly hills slow it down / speed it up
};
extern const CarStats CAR_STATS[CAR_TYPES];

// One frame, cropped to its visible pixels. (ox, oy) = where the crop sits in the full frameW x frameH frame
// (in frame pixels); scale = frame pixels per bitmap pixel (2 for the half-resolution shadows). The body is
// stored in neutral grey with a paint mask and recoloured per car while it is drawn, so all colours share
// the same frames.
struct CarFrame {
    Bitmap bmp;
    ByteMap mask;
    int ox = 0, oy = 0;
    int frameW = CAR_BMP_W, frameH = CAR_BMP_H;
    double scale = 1;
};
constexpr int CAR_SHADOW_ELEV = 3;                  // shadows for elevations 1, 3 and 5
struct CarSprites {
    CarFrame car[CAR_YAWS][CAR_ELEV];               // [yaw][elevation]
    std::vector<CarFrame> brake[CAR_YAWS][CAR_ELEV];   // lit brake lights, drawn on top of the car frame
    // reflections on the glass for yaw steps -(CAR_YAWS-1)..(CAR_YAWS-1) (index + CAR_YAWS-1): never mirrored,
    // so a highlight stays on the same side of the window when the car turns the other way
    std::vector<CarFrame> glare[2 * CAR_YAWS - 1][CAR_ELEV];
    // the car's shadow on the road for yaw steps -(CAR_YAWS-1)..(CAR_YAWS-1) (index + CAR_YAWS-1): not mirrored,
    // because the sun stays on the same side
    CarFrame shadow[2 * CAR_YAWS - 1][CAR_SHADOW_ELEV];
    // where the exhaust pipes are in each car frame (turned right), as fractions of the whole frame
    std::vector<std::array<double, 2>> exhaust[CAR_YAWS][CAR_ELEV];
    // where the rear tyres touch the road in each car frame (turned right), as fractions of the whole frame
    std::vector<std::array<double, 2>> tyres[CAR_YAWS][CAR_ELEV];
};

CarSprites& carSprites(int type);
void prepareCarSprites();      // after loading / generating: mipmaps, bounds, paint palettes

// draw a car so that its full (uncropped) frame covers x, y, w, h. yawIdx in -(CAR_YAWS-1)..(CAR_YAWS-1)
// (negative = turned left, mirrored), elevation index, brake lights on/off
// Night driving (rainy night): the rear lights drawn glowing - bright, barely fogged, with a red halo - so they
// shine far into the dark; 'fog' is the (reduced) fog to use for them
void drawCarTailLights(Canvas& cv, int type, Color color, int yawIdx, int elev, double x, double y, double w, double h,
                       double clipBottom, double fog, Color fogColor, const Bitmap* glow);
void drawCar(Canvas& cv, int type, Color color, int yawIdx, int elev, bool brake, double x, double y, double w, double h,
             double clipBottom = 1e9, double fog = 0, Color fogColor = 0);
// the screen positions of its exhaust pipes, for a car drawn with the same arguments (nitrous flames)
void carExhausts(int type, int yawIdx, int elev, double x, double y, double w, double h, std::vector<std::array<double, 2>>& out);
// the screen positions where its rear tyres touch the road (tyre smoke)
void carTyres(int type, int yawIdx, int elev, double x, double y, double w, double h, std::vector<std::array<double, 2>>& out);
// its shadow, in the same frame rectangle (put the frame's ground line where the shadow falls)
void drawCarShadow(Canvas& cv, int type, int yawIdx, int elev, double x, double y, double w, double h,
                   double clipBottom, double alpha);

