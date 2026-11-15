// Game graphics: scenery sprites, effects and background layers. The game loads them from PNG files in the
// assets folder (see assets.h); the asset tool generates those files procedurally (Graphics::generate).
#pragma once
#include "canvas.h"
#include "track.h"
#include "cars.h"
#include <vector>

constexpr int NUM_CAR_COLORS = 10;

struct BgLayer {
    Bitmap bmp;
    double parallax;   // how fast it scrolls with curves (0 = fixed)
    int yOff;          // bottom of bitmap relative to horizon (positive = below)
};

struct Graphics {
    Bitmap sprite[SPR_COUNT];
    std::vector<BgLayer> bg[NUM_STAGES];
    Bitmap dust, smoke, spark;
    Bitmap lightRed, lightGreen, lightOff;     // start lights
    Bitmap lampGlow;                           // tunnel lights
    Bitmap flame;                              // nitrous exhaust flame (seen from behind)
    static constexpr int TUMBLE_FRAMES = 8;
    Bitmap tumbleweed[TUMBLE_FRAMES];          // a rolling tumbleweed, turned 45 degrees per frame (desert)
    Bitmap leaf[2];                            // gusts of autumn leaves: red-orange, golden
    Bitmap bridgeNight, bridgePillarNight;     // the road bridge and its pillars in the rainy night city
    Bitmap rainLights[2];                      // the lit windows of the rainy-night buildings (drawn at full brightness)

    void generate();                           // asset tool only; needs the text layer (fonts)
};

extern Graphics gfx;
extern const Color CAR_COLORS[NUM_CAR_COLORS];
extern const char* const SPRITE_NAMES[SPR_COUNT];    // file / atlas names
