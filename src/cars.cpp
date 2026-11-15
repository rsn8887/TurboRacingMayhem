// Car types, their sprites (loaded from the assets) and drawing with per-car paint colours
#include "cars.h"
#include "sprites.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>

// generic look-alikes only: no badges, names or logos
const CarStats CAR_STATS[CAR_TYPES] = {
    // name        top   accel grip  dirtTop weight
    {"SPORT",      1.00, 1.00, 1.00, 0.78,  1.00},
    {"COUPE",      0.97, 1.08, 1.08, 0.78,  0.90},   // rear-engined, rounded tail
    {"SUPERCAR",   1.04, 1.05, 0.92, 0.72,  0.95},   // wide mid-engined wedge
    {"SUV",        0.90, 0.92, 0.94, 0.90,  1.30},
    {"PICKUP",     0.88, 0.96, 0.90, 0.92,  1.35},
    {"SEDAN",      0.92, 0.94, 1.02, 0.80,  1.10},
};


namespace {
CarSprites sprites[CAR_TYPES];               // grey frames + paint masks, recoloured while drawing
std::map<Color, std::unique_ptr<std::array<Color, 256>>> palettes;

// grey value (0..255, the body is painted at 170) -> colour of the paint, for one car colour
const Color* palette(Color col) {
    col |= 0xFF000000u;
    auto it = palettes.find(col);
    if (it != palettes.end()) return it->second->data();
    auto p = std::make_unique<std::array<Color, 256>>();
    for (int g = 0; g < 256; ++g) {
        double v = g / 170.0;
        (*p)[size_t(g)] = v <= 1 ? shade(col, v) : lerpColor(col, rgb(255, 255, 255), std::min(1.0, (v - 1) / 0.5));
    }
    const Color* d = p->data();
    palettes[col] = std::move(p);
    return d;
}

void prepare(CarFrame& f) {
    f.bmp.buildMips();
    if (f.mask.w != f.bmp.w || f.mask.h != f.bmp.h) { f.mask.w = f.bmp.w; f.mask.h = f.bmp.h; f.mask.px.assign(size_t(f.bmp.w) * f.bmp.h, 0); }
    f.mask.buildMips();
}

void drawCarFrame(Canvas& cv, const CarFrame& f, const Color* pal, double x, double y, double w, double h, bool flipX,
                  double clipBottom, double fog, Color fogColor, double alpha = 1.0) {
    if (f.bmp.w <= 0) return;
    const double fw = f.frameW, fh = f.frameH;
    double kx = w / fw, ky = h / fh;
    double ox = flipX ? fw - f.ox - f.bmp.w * f.scale : f.ox;
    cv.blitScaled(f.bmp, x + ox * kx, y + f.oy * ky, f.bmp.w * f.scale * kx, f.bmp.h * f.scale * ky, flipX, clipBottom, fog,
                  fogColor, alpha, pal ? &f.mask : nullptr, pal);
}
} // namespace

CarSprites& carSprites(int type) { return sprites[std::clamp(type, 0, CAR_TYPES - 1)]; }

void prepareCarSprites() {
    for (auto& cs : sprites)
        for (int s = 0; s < CAR_YAWS; ++s) {
            for (int e = 0; e < CAR_ELEV; ++e) {
                prepare(cs.car[s][e]);
                for (auto& o : cs.brake[s][e]) prepare(o);
            }
        }
    for (auto& cs : sprites) {
        for (auto& row : cs.shadow)
            for (auto& f : row) f.bmp.buildMips();
        for (auto& row : cs.glare)
            for (auto& v : row)
                for (auto& f : v) prepare(f);
    }
    for (int i = 0; i < NUM_CAR_COLORS; ++i) palette(CAR_COLORS[i]);   // ready before any (multi-threaded) drawing
}

void drawCar(Canvas& cv, int type, Color color, int yawIdx, int elev, bool brake, double x, double y, double w, double h,
             double clipBottom, double fog, Color fogColor) {
    const CarSprites& cs = sprites[std::clamp(type, 0, CAR_TYPES - 1)];
    yawIdx = std::clamp(yawIdx, -(CAR_YAWS - 1), CAR_YAWS - 1);
    elev = std::clamp(elev, 0, CAR_ELEV - 1);
    bool flip = yawIdx < 0;
    int a = std::abs(yawIdx);
    const Color* pal = palette(color);
    drawCarFrame(cv, cs.car[a][elev], pal, x, y, w, h, flip, clipBottom, fog, fogColor);
    for (const CarFrame& o : cs.glare[yawIdx + CAR_YAWS - 1][elev]) drawCarFrame(cv, o, pal, x, y, w, h, false, clipBottom, fog, fogColor);
    if (brake)
        for (const CarFrame& o : cs.brake[a][elev]) drawCarFrame(cv, o, pal, x, y, w, h, flip, clipBottom, fog, fogColor);
}

void drawCarTailLights(Canvas& cv, int type, Color color, int yawIdx, int elev, double x, double y, double w, double h,
                       double clipBottom, double fog, Color fogColor, const Bitmap* glow) {
    const CarSprites& cs = sprites[std::clamp(type, 0, CAR_TYPES - 1)];
    yawIdx = std::clamp(yawIdx, -(CAR_YAWS - 1), CAR_YAWS - 1);
    elev = std::clamp(elev, 0, CAR_ELEV - 1);
    bool flip = yawIdx < 0;
    int a = std::abs(yawIdx);
    const Color* pal = palette(color);
    for (const CarFrame& o : cs.brake[a][elev]) {
        if (o.bmp.w <= 0) continue;
        const double kx = w / o.frameW, ky = h / o.frameH;
        double ox = flip ? o.frameW - o.ox - o.bmp.w * o.scale : o.ox;   // where the lights sit on screen
        double fx = x + ox * kx, fy = y + o.oy * ky, fw = o.bmp.w * o.scale * kx, fh = o.bmp.h * o.scale * ky;
        drawCarFrame(cv, o, pal, x, y, w, h, flip, clipBottom, fog, fogColor);
        if (glow && glow->w > 0) {   // a soft round red glow over each light, bigger relative to the car the further off
            double gw = std::max(fw * 0.32, 3.0), gh = std::max(fh * 1.8, 3.0);
            for (double gx : {fx + fw * 0.12, fx + fw * 0.88})
                cv.blitScaled(*glow, gx - gw / 2, fy + fh / 2 - gh / 2, gw, gh, false, clipBottom, 0.85, rgb(255, 40, 30), 0.85);
        }
    }
}

void carExhausts(int type, int yawIdx, int elev, double x, double y, double w, double h, std::vector<std::array<double, 2>>& out) {
    const CarSprites& cs = sprites[std::clamp(type, 0, CAR_TYPES - 1)];
    yawIdx = std::clamp(yawIdx, -(CAR_YAWS - 1), CAR_YAWS - 1);
    elev = std::clamp(elev, 0, CAR_ELEV - 1);
    out.clear();
    for (const auto& p : cs.exhaust[std::abs(yawIdx)][elev])          // turned left: the mirrored frame
        out.push_back({x + (yawIdx < 0 ? 1 - p[0] : p[0]) * w, y + p[1] * h});
}

void carTyres(int type, int yawIdx, int elev, double x, double y, double w, double h, std::vector<std::array<double, 2>>& out) {
    const CarSprites& cs = sprites[std::clamp(type, 0, CAR_TYPES - 1)];
    yawIdx = std::clamp(yawIdx, -(CAR_YAWS - 1), CAR_YAWS - 1);
    elev = std::clamp(elev, 0, CAR_ELEV - 1);
    out.clear();
    for (const auto& p : cs.tyres[std::abs(yawIdx)][elev])
        out.push_back({x + (yawIdx < 0 ? 1 - p[0] : p[0]) * w, y + p[1] * h});
}

void drawCarShadow(Canvas& cv, int type, int yawIdx, int elev, double x, double y, double w, double h, double clipBottom,
                   double alpha) {
    const CarSprites& cs = sprites[std::clamp(type, 0, CAR_TYPES - 1)];
    yawIdx = std::clamp(yawIdx, -(CAR_YAWS - 1), CAR_YAWS - 1);
    int se = std::clamp((std::clamp(elev, 0, CAR_ELEV - 1)) / 2, 0, CAR_SHADOW_ELEV - 1);   // elevations 1, 3, 5
    drawCarFrame(cv, cs.shadow[yawIdx + CAR_YAWS - 1][se], nullptr, x, y, w, h, false, clipBottom, 0, 0, alpha);
}
