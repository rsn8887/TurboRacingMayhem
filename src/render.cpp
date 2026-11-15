#include "render.h"
#include <tuple>
#ifdef __SWITCH__
#include <switch.h>
#endif
#include "sprites.h"
#include "cars.h"
#include "text.h"
#include "painter.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>
#include <map>

namespace {
struct Proj {
    double x1, y1, w1, z1;
    double x2, y2, w2, z2;
    double rx1, rx2;          // road centre relative to the camera (world units)
    double clip;
};
// per thread (strips are rendered in parallel). Kept on the heap: big thread-local arrays would need a big
// per-thread TLS block, which some platforms (Switch) don't give their threads.
thread_local std::vector<Proj> projBuf;
thread_local std::vector<std::array<double, 2>> g_tyrePts;
thread_local bool g_nightLights = false;   // rainy night: cars drive with their lights on   // rear tyre contact points of the car drawn last
thread_local std::vector<double> ceilBuf;   // rows above this are hidden behind nearer tunnel ceilings

inline double fogAt(const Theme& th, double z) {
    return th.fogDensity > 0 ? std::min(0.92, 1 - std::exp(-th.fogDensity * z)) : 0.0;
}

// A car in the shadow of a bridge: darken it by the same 42% as the road there, on top of its fog. Returns the
// fog amount and colour that give lerp(lerp(c, fogColor, fog), black, 0.42) in a single blend.
inline void shadeUnderBridge(double& fog, Color& fogColor) {
    const double k = 0.58;                                   // light left in the shadow
    double F = 1 - k * (1 - fog);
    fogColor = F > 0 ? shade(fogColor, k * fog / F) : rgb(0, 0, 0);
    fog = F;
}

unsigned hashRand(unsigned x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
double frand(unsigned seed) { return (hashRand(seed) & 0xFFFF) / 65535.0; }

// Background layers scaled to a view's size, made once and kept (for the current stage only, so memory stays
// small). Area-filtered from the layer bitmap (wrapping sideways, so the copies tile without a seam).
const Bitmap& scaledLayer(int stage, const Bitmap& src, int W, int H) {
    static std::mutex m;
    static int cachedStage = -1;
    static std::map<std::tuple<const Bitmap*, int, int>, std::unique_ptr<Bitmap>> cache;
    std::lock_guard<std::mutex> lock(m);
    if (stage != cachedStage) { cache.clear(); cachedStage = stage; }
    auto key = std::make_tuple(&src, W, H);
    auto it = cache.find(key);
    if (it != cache.end()) return *it->second;
    auto out = std::make_unique<Bitmap>(W, H);
    const double fx = double(src.w) / W, fy = double(src.h) / H;
    for (int y = 0; y < H; ++y) {
        double y0 = y * fy, y1 = (y + 1) * fy;
        for (int x = 0; x < W; ++x) {
            double x0 = x * fx, x1 = (x + 1) * fx;
            double a = 0, r = 0, g = 0, b = 0, wsum = 0;
            for (int sy = int(std::floor(y0)); sy < int(std::ceil(y1)); ++sy) {
                if (sy < 0 || sy >= src.h) continue;
                double wy = std::min(y1, sy + 1.0) - std::max(y0, double(sy));
                for (int sx = int(std::floor(x0)); sx < int(std::ceil(x1)); ++sx) {
                    double wx = std::min(x1, sx + 1.0) - std::max(x0, double(sx));
                    Color c = src.px[size_t(sy) * src.w + size_t(((sx % src.w) + src.w) % src.w)];
                    double wgt = wx * wy;
                    wsum += wgt;
                    if (!(c >> 24)) continue;                       // transparent: adds only to the weight
                    double ca = (c >> 24) / 255.0;
                    a += ca * wgt; r += ((c >> 16) & 255) * ca * wgt; g += ((c >> 8) & 255) * ca * wgt; b += (c & 255) * ca * wgt;
                }
            }
            if (wsum <= 0 || a <= 0) continue;
            Color c = (uint32_t(std::min(255.0, a / wsum * 255) + 0.5) << 24) | (uint32_t(r / a + 0.5) << 16) |
                      (uint32_t(g / a + 0.5) << 8) | uint32_t(b / a + 0.5);
            out->px[size_t(y) * W + x] = c;
        }
    }
    out->computeBounds();
    const Bitmap& ref = *out;
    cache[key] = std::move(out);
    return ref;
}

void drawBackground(Canvas& cv, const Rect& vp, const Theme& th, const Car& car, double horizonY, double s, int stage) {
    int hy = int(horizonY);
    for (int y = vp.y; y < vp.y + vp.h; ++y) {
        Color c;
        if (y < hy) {
            // the sky stays deep overhead and brightens faster near the horizon, where a glow (city light at
            // night) lifts it further
            double t = double(y - vp.y) / std::max(1, hy - vp.y);
            c = lerpColor(th.skyTop, th.skyBottom, std::pow(t, 1.35));
            double g = std::clamp((t - 0.72) / 0.28, 0.0, 1.0);
            if (g > 0) c = lerpColor(c, lerpColor(th.skyBottom, th.horizonGlow ? th.horizonGlow : th.night ? rgb(255, 140, 210) : rgb(255, 255, 255), 0.3), 0.55 * g * g);
        }
        else c = th.fogDensity > 0 ? lerpColor(th.horizonGround, th.fogColor, 0.6) : th.horizonGround;
        cv.span(vp.x, vp.x + vp.w, y, c);
    }
    for (const BgLayer& L : gfx.bg[stage]) {
        // every layer is 1024 layout units wide, whatever resolution its bitmap has. It is drawn from a copy
        // scaled to this view's size once (made on first use), so every frame takes the fast unscaled path.
        double k = 1024.0 / L.bmp.w;
        int W = std::max(1, int(std::lround(1024 * s))), H = std::max(1, int(std::lround(L.bmp.h * k * s)));
        const Bitmap& B = scaledLayer(stage, L.bmp, W, H);
        double bottom = horizonY + L.yOff * s;
        double off = std::fmod(car.bgOffset * L.parallax * 2.0 * s, double(W));
        if (off < 0) off += W;
        int y = int(std::floor(bottom)) - H;
        for (int x = -int(std::floor(off)); x < vp.w; x += W)
            cv.blitScaled(B, double(vp.x + x), double(y), double(W), double(H));
    }
}

// Fraction of [z0,z1] covered by the "light" half of stripes with the given length. Filtering each
// scanline over the depth range it really covers acts like mip-mapping for the stripes: far away,
// where many stripes fall into one row, they blend to an even average instead of flickering (moire).
inline double lightFrac(double z0, double z1, double len) {
    auto F = [len](double z) {
        double period = 2 * len;
        double n = std::floor(z / period);
        return n * len + std::min(z - n * period, len);
    };
    if (z1 - z0 < 1e-3) return (int(std::floor(z0 / len)) & 1) == 0 ? 1.0 : 0.0;
    return std::clamp((F(z1) - F(z0)) / (z1 - z0), 0.0, 1.0);
}

// fraction of [z0,z1] that falls on the first 'on' units of each 'period' (thin lines across the ground)
inline double dutyFrac(double z0, double z1, double period, double on) {
    auto F = [=](double z) {
        double n = std::floor(z / period);
        return n * on + std::min(z - n * period, on);
    };
    if (z1 - z0 < 1e-3) return std::fmod(std::fmod(z0, period) + period, period) < on ? 1.0 : 0.0;
    return std::clamp((F(z1) - F(z0)) / (z1 - z0), 0.0, 1.0);
}

// bright, fully saturated colour for a hue 0..1 (the rainbow road)
inline Color hueColor(double h) {
    h = (h - std::floor(h)) * 6;
    int i = int(h);
    double f = h - i;
    auto C = [](double v) { return int(std::lround(60 + 195 * v)); };   // never darker than 60: bright pastel-neon
    switch (i % 6) {
    case 0:  return rgb(C(1), C(f), C(0));
    case 1:  return rgb(C(1 - f), C(1), C(0));
    case 2:  return rgb(C(0), C(1), C(f));
    case 3:  return rgb(C(0), C(1 - f), C(1));
    case 4:  return rgb(C(f), C(0), C(1));
    default: return rgb(C(1), C(0), C(1 - f));
    }
}

inline double tunnelDark(double z) { return std::min(0.75, 0.12 + 0.63 * (1 - std::exp(-z * 0.000045))); }
// how much a car at track distance 'dist' is lit by the nearest row of tunnel lamps (0..1): a short bright
// spell each time it passes under one
double tunnelLampLight(double dist) {
    const double spacing = TUNNEL_LAMP_EVERY * SEG_LEN;
    double d = std::fmod(dist, spacing);
    if (d < 0) d += spacing;
    d = std::min(d, spacing - d);                       // distance to the nearest lamp row
    double k = d / (SEG_LEN * 1.6);
    return std::exp(-k * k);
}
// cars inside a tunnel are drawn darker (blended towards black), brighter under a lamp
double tunnelCarShade(double dist, double viewDepth) {
    double base = std::min(0.8, 0.5 + tunnelDark(viewDepth) * 0.45);
    return std::max(0.05, base - 0.42 * tunnelLampLight(dist));
}

const Color TUNNEL_BLACK = rgb(4, 4, 7);

// rainy night: the headlight brightening of the road at depth z (see drawRoadRow)
inline double rainLit(double z) { return 1.1 * std::pow(std::clamp(1 - z / (45 * SEG_LEN), 0.0, 1.0), 1.6); }
// how dark a car at depth z is in a tunnel: in the rainy night's brightly lit tunnel exactly as dark as the road
// around it, elsewhere the usual tunnel gloom with a flash under each row of lamps
inline double carTunnelShade(const Theme& th, double dist, double z) {
    // (in the rainy night, still a little darker between the rows of lamps and brightest right under each one)
    return th.rain ? std::min(0.85, tunnelDark(z) * (1 - std::min(1.0, rainLit(z))) + 0.32 * (1 - tunnelLampLight(dist)))
                   : tunnelCarShade(dist, z);
}

// one scanline of road. hw = half width of the road on screen (already includes the width factor w)
// outside = the camera is not in this tunnel: the ground beside the tunnel is then the grey concrete of the
// tunnel structure (seen beyond the entrance wall from up a hill), not the black of the inside
void drawRoadRow(Canvas& cv, const Rect& vp, const Theme& th, int y, double cx, double hw, double w, const Segment& seg,
                 double worldZ, double z, double span, double trackLen, bool outside = false) {
    const bool tunnel = seg.tunnel;
    const bool dirt = seg.surface == SURF_DIRT;
    const double lf = lightFrac(worldZ - span / 2, worldZ + span / 2, STRIPE_LEN);   // filtered stripe
    auto LD = [lf](Color l, Color d) { return lerpColor(d, l, lf); };
    const double detail = std::clamp(1 - span / 120, 0.0, 1.0);    // fine detail fades out with distance
    double fog = tunnel && th.rain ? 0.0 : fogAt(th, z);              // the tunnel lights cut through the night: no haze
    double dark = tunnel ? tunnelDark(z) : seg.shadow ? 0.42 : 0;     // in the shadow of a bridge
    // rainy night: the player's headlights light up the wet road just ahead, falling off into the dark
    // (in the tunnel too, just as bright: the road near the car looks the same in and out of the tunnel)
    const double lit = th.rain ? rainLit(z) : 0;
    const double darkHere = tunnel && th.rain ? dark * (1 - std::min(1.0, lit)) : dark;   // the headlights beat the gloom
    auto F = [&](Color c) {
        if (lit > 0) c = lerpColor(c, shade(lerpColor(c, rgb(200, 196, 180), 0.25), 2.4), std::min(1.0, lit) * (seg.shadow && !tunnel ? 0.4 : 1));
        if (fog > 0) c = lerpColor(c, th.fogColor, fog);
        return darkHere > 0 ? lerpColor(c, TUNNEL_BLACK, darkHere) : c;
    };
    const double k = 1.0 / w;    // converts standard-road distances into u for this width
    auto spanU = [&](double u0, double u1, Color c) { cv.spanAA(cx + u0 * hw, cx + u1 * hw, y, c); };
    Color lane = F(th.lane);

    // --- verge
    if (tunnel) {
        Color around = TUNNEL_BLACK;
        if (outside) { around = rgb(146, 146, 144); if (fog > 0) around = lerpColor(around, th.fogColor, fog); }   // like the portal
        cv.span(vp.x, vp.x + vp.w, y, around);
        spanU(-TUNNEL_SIDE, TUNNEL_SIDE, F(LD(rgb(118, 116, 112), rgb(108, 106, 102))));   // concrete walkway
    } else {
        Color grass = LD(th.grassLight, th.grassDark);
        cv.span(vp.x, vp.x + vp.w, y, F(grass));
        if (th.grid) {                                      // glowing grid on the ground (future)
            double across = dutyFrac(worldZ - span / 2, worldZ + span / 2, 1200, 90);
            if (across > 0.01) cv.span(vp.x, vp.x + vp.w, y, F(lerpColor(grass, th.grid, 0.75 * across)));
            Color line = F(lerpColor(grass, th.grid, 0.8));
            const double gw = 0.035;                        // line width (road half widths)
            for (int n = 0; n < 40; ++n) {
                double u = 1.6 + n * 0.9;
                if (cx - u * hw < vp.x && cx + u * hw > vp.x + vp.w) break;
                spanU(-u - gw, -u + gw, line);
                spanU(u - gw, u + gw, line);
            }
        }
    }

    // --- dirt road: ragged edges, ruts, grainy rows, no markings
    if (dirt) {
        double grain = 0.5 + (hashNoise(std::floor(worldZ / 37), 3) - 0.5) * detail;
        double jl = (hashNoise(std::floor(worldZ / 90), 1) - 0.5) * 0.10 * k * detail;
        double jr = (hashNoise(std::floor(worldZ / 90), 2) - 0.5) * 0.10 * k * detail;
        Color d = shade(LD(th.dirtLight, th.dirtDark), 0.93 + 0.14 * grain);
        spanU(-1.04 - jl, 1.04 + jr, F(shade(d, 0.86)));          // loose edge
        spanU(-0.98 - jl, 0.98 + jr, F(d));
        for (double r : {-0.5, 0.5}) {                              // wheel ruts
            spanU(r - 0.16, r + 0.16, F(shade(d, 0.86)));
            spanU(r - 0.08, r + 0.08, F(shade(d, 0.78)));
        }
        if (detail > 0.5 && hashNoise(std::floor(worldZ / 53), 7) > 0.8) spanU(-0.03, 0.03, F(shade(th.grassDark, 0.9)));   // grass in the middle
        return;
    }

    // --- edges / kerbs
    Color road = F(LD(th.roadLight, th.roadDark));
    Kerb kerb = tunnel ? KERB_RUMBLE : seg.kerb;
    switch (kerb) {
    case KERB_RUMBLE:
        spanU(-1 - 0.15 * k, 1 + 0.15 * k, F(LD(th.rumbleA, th.rumbleB)));
        break;
    case KERB_ZEBRA: {
        double zf = lightFrac(worldZ - span / 2, worldZ + span / 2, 110 * SPEED_SCALE);
        spanU(-1 - 0.12 * k, 1 + 0.12 * k, F(lerpColor(th.rumbleB, th.rumbleA, zf)));
        break;
    }
    case KERB_SOLID:
        spanU(-1 - 0.40 * k, 1 + 0.40 * k, F(shade(th.shoulder, 0.94 + 0.06 * lf)));   // gravel shoulder
        spanU(-1 - 0.07 * k, 1 + 0.07 * k, F(rgb(236, 236, 236)));                      // painted edge
        break;
    case KERB_GUARDRAIL:
        spanU(-RAIL_POS - 0.05, RAIL_POS + 0.05, F(shade(th.shoulder, 0.94 + 0.06 * lf)));
        spanU(-1 - 0.05 * k, 1 + 0.05 * k, F(rgb(236, 236, 236)));
        break;
    case KERB_NONE:
        spanU(-1 - 0.04 * k, 1 + 0.04 * k, F(shade(LD(th.grassLight, th.grassDark), 0.8)));
        break;
    }
    const double px = hw;                                    // screen pixels per road half width
    switch (th.road) {
    case ROAD_CHECKER: {                                     // square tiles: 8 across a standard road
        const int cols = std::max(2, 2 * int(std::lround(4 * w)));
        const double tile = 2 * ROAD_WIDTH * w / cols;
        double rowF = lightFrac(worldZ - span / 2, worldZ + span / 2, tile);   // share of "even" rows
        Color A = LD(th.roadLight, th.roadDark), B = th.roadAlt;
        Color even = lerpColor(B, A, rowF), odd = lerpColor(A, B, rowF);
        if (2 * px / cols < 1.5) { spanU(-1.0, 1.0, F(lerpColor(A, B, 0.5))); break; }   // too small: average
        for (int i = 0; i < cols; ++i)
            spanU(-1.0 + 2.0 * i / cols, -1.0 + 2.0 * (i + 1) / cols, F((i & 1) ? odd : even));
        break;
    }
    case ROAD_RAINBOW: {                                     // wavy bands of colour flowing down the road
        const int bands = 7;
        const double flow = worldZ / 5200;
        double fade = std::clamp((span - 300) / 900, 0.0, 1.0);      // far away the colours blend
        Color avg = rgb(196, 160, 210);
        double light = 0.9 + 0.1 * lf;
        double edge[bands + 1];
        for (int j = 0; j <= bands; ++j)
            edge[j] = j == 0 ? -1.0 : j == bands ? 1.0
                     : -1.0 + 2.0 * j / bands + 0.09 * std::sin(worldZ / 900 + j * 1.7);
        if (2 * px / bands < 1.5) fade = 1;
        for (int j = 0; j < bands; ++j) {
            Color c = shade(hueColor(flow + j * 0.11 + 0.05 * std::sin(worldZ / 1300)), light);
            spanU(edge[j], edge[j + 1], F(lerpColor(c, avg, fade)));
        }
        break;
    }
    case ROAD_COBBLE: {                                      // rows of stones, every other row shifted by half
        const double SL = ROAD_WIDTH / 9;                    // stone length along the road
        const int across = std::max(4, int(std::lround(16 * w)));
        Color base = LD(th.roadLight, th.roadDark);
        double joint = dutyFrac(worldZ - span / 2, worldZ + span / 2, SL, SL * 0.16) * detail;
        spanU(-1.0, 1.0, F(lerpColor(base, th.roadAlt, 0.25 + 0.6 * joint)));
        if (detail < 0.15 || 2 * px / across < 3) break;    // far away: just the averaged colour
        double row = std::floor(worldZ / SL);
        double shift = std::fmod(row, 2.0) != 0 ? 0.5 : 0.0;
        double sw = 2.0 / across, gap = sw * 0.14;
        for (int i = -1; i <= across; ++i) {
            double u0 = -1.0 + (i + shift) * sw + gap / 2, u1 = u0 + sw - gap;
            u0 = std::max(u0, -1.0); u1 = std::min(u1, 1.0);
            if (u1 <= u0) continue;
            double v = 0.88 + 0.24 * hashNoise(row * 3.1, i * 7.7);
            spanU(u0, u1, F(lerpColor(lerpColor(base, th.roadAlt, 0.6 * joint), shade(base, v), detail)));
        }
        break;
    }
    default:
        spanU(-1.0, 1.0, road);
        break;
    }
    // subtle darker edge on the asphalt next to the kerbs (fake ambient occlusion)
    spanU(-1.0, -1 + 0.03 * k, F(shade(road, 0.85)));
    spanU(1 - 0.03 * k, 1.0, F(shade(road, 0.85)));

    double zm = std::fmod(worldZ, trackLen);
    if (zm < 0) zm += trackLen;
    if (zm < 360) {                                         // checkered start/finish line
        int row = int(zm / 90);
        for (int i = 0; i < 16; ++i)
            spanU(-1.0 + i * 0.125, -1.0 + (i + 1) * 0.125, F(((i + row) & 1) ? rgb(24, 24, 28) : rgb(240, 240, 240)));
        return;
    }
    if (kerb == KERB_RUMBLE || kerb == KERB_ZEBRA) {
        spanU(-1 + 0.06 * k, -1 + 0.10 * k, lane);
        spanU(1 - 0.10 * k, 1 - 0.06 * k, lane);
    }
    if (lf > 0.02) {                                        // dashed lane markers (test1 vs test2.png), faded by coverage
        lane = withAlpha(lane, int(255 * lf));
        double lw = 0.012 * k;
        if (w < 0.85) {                                     // narrow: two lanes
            spanU(-lw, lw, lane);
        } else if (w > 1.25) {                              // wide: six lanes
            for (int i = -2; i <= 2; ++i) spanU(i / 3.0 - lw, i / 3.0 + lw, lane);
        } else {                                            // standard: four lanes
            for (int i = -1; i <= 1; ++i) spanU(i * 0.5 - lw, i * 0.5 + lw, lane);
        }
    }
}

// tunnel ceiling scanline: dark, like the night-time tunnels in Lotus
void drawCeilingRow(Canvas& cv, const Rect& vp, const Theme& th, int y, double cx, double hw, double worldZ, double z) {
    (void)worldZ;
    (void)th; (void)cx; (void)hw; (void)z;
    cv.span(vp.x, vp.x + vp.w, y, TUNNEL_BLACK);         // the inside of a tunnel is black; only the lamps show
}

// a vertical wall running along the road (tunnel barrier or guardrail), seen side-on for one segment.
// x/y = screen positions of the wall foot at the near (1) and far (2) end, h = wall height on screen
template <class ColorFn>
void drawSideWall(Canvas& cv, double xa, double ya, double ha, double xb, double yb, double hb, double clipBottom, ColorFn col) {
    if (std::fabs(xb - xa) < 0.01) return;
    double xl = std::min(xa, xb), xr = std::max(xa, xb);
    int c0 = std::max(cv.clip.x, int(std::floor(xl))), c1 = std::min(cv.clip.x + cv.clip.w, int(std::ceil(xr)));
    for (int x = c0; x < c1; ++x) {
        double t = std::clamp((x + 0.5 - xa) / (xb - xa), 0.0, 1.0);
        double yf = ya + (yb - ya) * t, hh = ha + (hb - ha) * t;
        col(x, yf - hh, yf, t, clipBottom);
    }
}

// the rock/concrete face around a tunnel entrance (a vertical plane at depth z)
void drawPortal(Canvas& cv, const Rect& vp, const Theme& th, double horizonY, double focal, double depth,
                double camY, double roadX, double roadY, double groundY, double z, double clipBottom) {
    // a plain flat grey wall with a rectangular opening the size of the tunnel. Looked down on from a hill, the
    // tunnel's roof (and the hill it runs into) fills everything above the wall up to the horizon: all grey.
    double sc = depth / z * focal;                     // screen pixels per world unit at this depth
    double openHW = TUNNEL_SIDE * ROAD_WIDTH;          // half width of the opening (world)
    double fog = fogAt(th, z);
    const double wallTop = PORTAL_HEIGHT * 0.8;
    double yWallTop = horizonY - (groundY + wallTop - camY) * sc;
    double yOpenTop = horizonY - (groundY + TUNNEL_HEIGHT - camY) * sc;
    int yTop = std::max({vp.y, cv.clip.y, int(std::ceil(std::min(yWallTop, horizonY) - 0.5))});
    int yBot = std::min({int(std::ceil(std::min(roadY, clipBottom) - 0.5)), vp.y + vp.h, cv.clip.y + cv.clip.h});
    double xl = roadX - openHW * sc, xr = roadX + openHW * sc;
    Color c = rgb(146, 146, 144);
    if (fog > 0) c = lerpColor(c, th.fogColor, fog);
    for (int y = yTop; y < yBot; ++y) {
        if (y + 0.5 < yOpenTop) cv.span(vp.x, vp.x + vp.w, y, c);           // above the opening: full width
        else { cv.spanAA(vp.x, xl, y, c); cv.spanAA(xr, vp.x + vp.w, y, c); }   // beside it
    }
}

// inside face around a tunnel exit (seen from inside the tunnel)
void drawExitFace(Canvas& cv, const Rect& vp, const Theme& th, double xCenter, double hw, double yCeil, double yRoad,
                  double z, double clipBottom) {
    (void)z; (void)th;
    Color c = TUNNEL_BLACK;
    int y0 = std::max(vp.y, int(std::ceil(yCeil - 0.5)));
    int y1 = std::min(int(std::ceil(std::min(yRoad, clipBottom) - 0.5)), vp.y + vp.h);
    for (int y = y0; y < y1; ++y) {
        cv.spanAA(vp.x, xCenter - hw, y, c);
        cv.spanAA(xCenter + hw, vp.x + vp.w, y, c);
    }
}

// bottom = where the car's wheels are on screen, groundY = where its shadow falls (differs while airborne)
void drawCarSprite(Canvas& cv, const Car& c, double cx, double bottom, double groundY, double w, double clip, double fog,
                   Color fogColor, double shadowAlpha = 1.0, double viewAngle = 0, double viewElev = CAR_ELEV_DEG[CAR_ELEV_LEVEL],
                   unsigned frame = 0) {
    // yaw frame: which side of the car we see. Its own steering (up to 15 degrees at full lock) plus the
    // angle we look at it from - a car to our left shows a little of its right side and vice versa. The
    // viewing angle is toned down (0.55) so cars beside the road don't swing round too far.
    // A drift swings the tail out: the car points further into the bend than its steering alone would show.
    double yawDeg = c.steerVis * 15.0 * (1 - 0.45 * std::fabs(c.driftVis)) + c.driftVis * 14.0 + viewAngle * 0.55;
    int yawIdx = std::clamp(int(std::lround(yawDeg / CAR_YAW_STEP)), -(CAR_YAWS - 1), CAR_YAWS - 1);
    if (c.crashTimer > 0) {      // spinning: sweep through the yaw frames, mirrored every other half turn
        double ph = c.crashTimer * 6.0;
        yawIdx = int(std::lround(std::sin(ph * M_PI) * (CAR_YAWS - 1)));
        bottom -= std::sin(c.crashTimer / 1.5 * M_PI) * w * (CAR_BODY_PX / CAR_BMP_W) * 0.35;
    }
    // elevation frame: the angle we look down at the car from (viewElev: low for distant cars) plus its own
    // pitch - nose up on climbs / take-offs shows more roof, nose down on descents / when falling shows less
    double elevDeg = viewElev;
    if (c.crashTimer <= 0) elevDeg += std::atan(c.pitchVis) * 180 / M_PI * 1.6;
    double h = w * CAR_BMP_H / CAR_BMP_W;
    if (c.landTimer > 0) bottom += w * (CAR_BODY_PX / CAR_BMP_W) * 0.025 * (c.landTimer / 0.25);         // suspension squash on landing
    // shadow: cast by the car's own 3D shape and seen from the same angle (it turns with the car), always at
    // the car's ground position - so when the car jumps it stays on the road (fainter the higher it flies)
    int elevIdx = carElevIndex(elevDeg);
    double sa = 0.62 * shadowAlpha * (1 - fog);
    double groundTop = groundY - h * double(CAR_ANCHOR_Y) / CAR_BMP_H;
    // The shadow reaches a little below the wheels. 'clip' is where nearer road starts, which for a car on
    // a flat road lies only somewhat below it and jumps as the car moves through a segment: clipping the shadow
    // there made its bottom edge flicker. So unless the car is hidden behind a crest, let it reach past.
    double body = w * CAR_BODY_PX / CAR_BMP_W;
    double shadowClip = groundY <= clip + 1 ? std::max(clip, groundY + body * 0.35) : clip;
    drawCarShadow(cv, c.type, yawIdx, elevIdx, cx - w / 2, groundTop, w, h, shadowClip, sa);
    double top = bottom - h * double(CAR_ANCHOR_Y) / CAR_BMP_H;
    drawCar(cv, c.type, CAR_COLORS[c.color], yawIdx, elevIdx, c.braking, cx - w / 2, top, w, h, clip, fog, fogColor);
    if (g_nightLights)                                  // tail lights glow red, even far off in the dark
        drawCarTailLights(cv, c.type, CAR_COLORS[c.color], yawIdx, elevIdx, cx - w / 2, top, w, h, clip, fog * 0.15, fogColor,
                          &gfx.lampGlow);
    carTyres(c.type, yawIdx, elevIdx, cx - w / 2, top, w, h, g_tyrePts);   // for the tyre smoke drawn after it
    // nitrous: a flame out of every exhaust pipe, exactly where the pipe is in this frame
    if (c.boostVis > 0.05 && c.crashTimer <= 0) {
        thread_local std::vector<std::array<double, 2>> ex;
        carExhausts(c.type, yawIdx, elevIdx, cx - w / 2, top, w, h, ex);
        double size = body * (ex.size() >= 4 ? 0.30 : 0.40) * c.boostVis;
        for (size_t k = 0; k < ex.size(); ++k) {
            if (ex[k][1] > clip) continue;
            double flick = 0.75 + 0.5 * frand(frame * 7 + unsigned(k) * 3 + unsigned(c.type) * 11);
            double sz = size * flick, x = ex[k][0], y = ex[k][1];
            cv.blitScaled(gfx.flame, x - sz * 0.75, y - sz * 0.45, sz * 1.5, sz * 0.9, false, clip, 0, 0, 0.55 * c.boostVis);
            cv.blitScaled(gfx.flame, x - sz / 2, y - sz / 2, sz, sz, false, clip, 0, 0, c.boostVis);
        }
    }
}

// Tyre smoke (or dust) from the rear tyres. 'tyres' are the screen points where they touch the road (from
// drawCarSprite), bw the car's body width on screen.
void drawTyreSmoke(Canvas& cv, const Bitmap& pb, const std::vector<std::array<double, 2>>& tyres, double cx, double groundY,
                   double bw, double clip, double t, unsigned seed, double fog, Color fogColor) {
    std::array<double, 2> fallback[2] = {{cx - bw * 0.38, groundY}, {cx + bw * 0.38, groundY}};
    const std::array<double, 2>* pts = tyres.size() >= 2 ? tyres.data() : fallback;
    const int nt = tyres.size() >= 2 ? int(tyres.size()) : 2;
    const int per = 4;
    for (int k = 0; k < nt; ++k) {
        const double side = pts[k][0] < cx ? -1 : 1;
        for (int i = 0; i < per; ++i) {
            double ph = t * 3.0 + double(i) / per + k * 0.5 / per;
            double age = ph - std::floor(ph);
            unsigned sd = unsigned(std::floor(ph)) * 31 + unsigned(i) * 7 + unsigned(k) * 53 + seed;
            double x = pts[k][0] + (frand(sd) - 0.5) * bw * 0.1 + side * bw * 0.12 * age;
            double y = pts[k][1] - bw * 0.04 - bw * 0.27 * age;
            double sz = bw * (0.16 + 0.26 * age);
            cv.blitScaled(pb, x - sz / 2, y - sz * 0.6, sz, sz * 0.85, false, clip, fog, fogColor, (1 - age) * 0.9);
        }
    }
}

// Drift sparks: the car scrapes along the road with its tail out, and showers of sparks shoot from under the
// rear tyres and the floor between them - most from the outer side the tail swings to - out sideways and back
// towards the camera, arcing down onto the road. Each spark is a short motion streak, white hot at the head and
// cooling through yellow and orange to red; a flickering glow marks where the car scrapes the road.
void drawDriftSparks(Canvas& cv, const std::vector<std::array<double, 2>>& tyres, double cx, double groundY, double bw,
                     double clip, double t, unsigned seed, double driftVis, double dim) {
    if (bw < 3) return;
    std::array<double, 2> fallback[2] = {{cx - bw * 0.38, groundY}, {cx + bw * 0.38, groundY}};
    const std::array<double, 2>* pts = tyres.size() >= 2 ? tyres.data() : fallback;
    std::array<double, 2> L = pts[0], R = pts[1];
    if (L[0] > R[0]) std::swap(L, R);
    const double outer = driftVis > 0 ? -1 : 1;           // the tail swings out opposite to the turn
    const double strength = std::clamp(std::fabs(driftVis), 0.0, 1.0);
    const double vis = (1 - dim) * strength;
    if (vis < 0.05) return;
    const double th = std::max(1.0, bw * 0.013);         // streak thickness
    const Rect keep = cv.clip;
    auto dot = [&](double x, double y, double sz, Color c, double a) {
        if (y > clip) return;
        cv.rectBlend(int(std::floor(x - sz / 2)), int(std::floor(y - sz / 2)), std::max(1, int(sz + 0.5)), std::max(1, int(sz + 0.5)), c, a);
    };
    // glow where the floor scrapes the road
    for (int k = 0; k < 2; ++k) {
        const std::array<double, 2>& p = (k == 0) == (outer < 0) ? L : R;
        double fl = 0.6 + 0.4 * frand(unsigned(t * 60) * 13 + unsigned(k) * 7 + seed);
        double sz = bw * (k == 0 ? 0.2 : 0.11) * fl;   // a flat hot spot on the road under the tyre
        cv.blitScaled(gfx.spark, p[0] - sz / 2, p[1] - sz * 0.2, sz, sz * 0.35, false, clip, 0, 0, std::min(1.0, 0.9 * vis * fl));
    }
    const int N = bw > 60 ? 64 : bw > 20 ? 24 : 8;        // fewer for distant cars
    for (int i = 0; i < N; ++i) {
        const double life = 0.32 + 0.22 * frand(unsigned(i) * 97 + seed);
        double ph = t / life + double(i) / N;
        double cyc = std::floor(ph), age = ph - cyc;
        unsigned sd = unsigned(cyc) * 131 + unsigned(i) * 17 + seed;
        double r1 = frand(sd), r2 = frand(sd + 1), r3 = frand(sd + 2), r4 = frand(sd + 3);
        // where it starts: 70% at the outer tyre, the rest along the floor between the tyres
        double u = r1 < 0.7 ? (outer < 0 ? 0.0 : 1.0) + (r4 - 0.5) * 0.15 : r4;
        u = std::clamp(u, 0.0, 1.0);
        double ox = L[0] + (R[0] - L[0]) * u, oy = L[1] + (R[1] - L[1]) * u - bw * 0.01;
        // velocity (body widths per second): out to the swinging side, back towards us, a little up
        double vx = (outer * (0.8 + 2.4 * r2) + (r3 - 0.5) * 1.2) * bw;
        double vy = (-(0.3 + 1.1 * r3) + 0.9 * r2) * bw;
        const double g = 5.5 * bw;
        auto pos = [&](double tt) { return std::array<double, 2>{ox + vx * tt, oy + vy * tt + 0.5 * g * tt * tt}; };
        double ts = age * life;
        std::array<double, 2> head = pos(ts), tail = pos(std::max(0.0, ts - 0.045));
        // colour: white hot, then yellow, orange, red as it cools
        Color c = age < 0.25 ? lerpColor(rgb(255, 255, 235), rgb(255, 225, 90), age / 0.25)
                : age < 0.6  ? lerpColor(rgb(255, 225, 90), rgb(255, 130, 30), (age - 0.25) / 0.35)
                             : lerpColor(rgb(255, 130, 30), rgb(200, 40, 10), (age - 0.6) / 0.4);
        double a = vis * (age > 0.7 ? (1 - age) / 0.3 : 1.0);
        double dx = head[0] - tail[0], dy = head[1] - tail[1];
        int steps = std::max(1, int(std::max(std::fabs(dx), std::fabs(dy))));
        for (int k = 0; k <= steps; ++k) {
            double f = double(k) / steps;                  // 0 tail .. 1 head
            dot(tail[0] + dx * f, tail[1] + dy * f, th, c, a * (0.25 + 0.75 * f));
        }
        dot(head[0], head[1], th * 1.8, rgb(255, 250, 220), a * 0.9);   // hot head
    }
    cv.setClip(keep);
}

// Wall sparks: the car's flank grinds along a tunnel wall, so sparks spray from that side - from the rear
// corner up the side of the body - bounce off the wall back across and behind the car and arc down onto the
// road. Same white-hot-to-red streaks as the drift sparks; a bright flare marks the contact point.
void drawWallSparks(Canvas& cv, const std::vector<std::array<double, 2>>& tyres, double cx, double groundY, double bw,
                    double clip, double t, unsigned seed, int side, double strength) {
    if (bw < 3 || strength < 0.05) return;
    std::array<double, 2> fallback[2] = {{cx - bw * 0.38, groundY}, {cx + bw * 0.38, groundY}};
    const std::array<double, 2>* pts = tyres.size() >= 2 ? tyres.data() : fallback;
    std::array<double, 2> T = pts[0];                    // the rear tyre on the wall side
    for (int k = 1; k < int(std::max<size_t>(2, tyres.size())); ++k)
        if ((pts[k][0] - T[0]) * side > 0) T = pts[k];
    const double ex = T[0] + side * bw * 0.1;            // the flank, just outside the tyre
    const double th = std::max(1.0, bw * 0.013);
    auto dot = [&](double x, double y, double sz, Color c, double a) {
        if (y > clip) return;
        cv.rectBlend(int(std::floor(x - sz / 2)), int(std::floor(y - sz / 2)), std::max(1, int(sz + 0.5)), std::max(1, int(sz + 0.5)), c, a);
    };
    // the flare where metal meets the wall
    double fl = 0.6 + 0.4 * frand(unsigned(t * 60) * 13 + seed);
    double fh = bw * 0.3 * fl, fw = bw * 0.14 * fl;
    cv.blitScaled(gfx.spark, ex - fw / 2, T[1] - bw * 0.2 - fh / 2, fw, fh, false, clip, 0, 0, std::min(1.0, strength * fl));
    const int N = bw > 60 ? 56 : bw > 20 ? 22 : 8;
    for (int i = 0; i < N; ++i) {
        const double life = 0.28 + 0.2 * frand(unsigned(i) * 89 + seed);
        double ph = t / life + double(i) / N;
        double cyc = std::floor(ph), age = ph - cyc;
        unsigned sd = unsigned(cyc) * 139 + unsigned(i) * 23 + seed;
        double r1 = frand(sd), r2 = frand(sd + 1), r3 = frand(sd + 2);
        double ox = ex + (r1 - 0.5) * bw * 0.04, oy = T[1] - bw * (0.03 + 0.3 * r1);   // along the flank
        double vx = -side * (0.2 + 1.6 * r2) * bw;                                      // off the wall, across
        double vy = (-(0.4 + 1.0 * r3) + 1.1 * r2) * bw;                                // up, then back at us
        const double g = 5.5 * bw;
        auto pos = [&](double tt) { return std::array<double, 2>{ox + vx * tt, oy + vy * tt + 0.5 * g * tt * tt}; };
        double ts = age * life;
        std::array<double, 2> head = pos(ts), tail = pos(std::max(0.0, ts - 0.045));
        Color c = age < 0.25 ? lerpColor(rgb(255, 255, 235), rgb(255, 225, 90), age / 0.25)
                : age < 0.6  ? lerpColor(rgb(255, 225, 90), rgb(255, 130, 30), (age - 0.25) / 0.35)
                             : lerpColor(rgb(255, 130, 30), rgb(200, 40, 10), (age - 0.6) / 0.4);
        double a = strength * (age > 0.7 ? (1 - age) / 0.3 : 1.0);
        double dx = head[0] - tail[0], dy = head[1] - tail[1];
        int steps = std::max(1, int(std::max(std::fabs(dx), std::fabs(dy))));
        for (int k = 0; k <= steps; ++k) {
            double f = double(k) / steps;
            dot(tail[0] + dx * f, tail[1] + dy * f, th, c, a * (0.25 + 0.75 * f));
        }
        dot(head[0], head[1], th * 1.8, rgb(255, 250, 220), a * 0.9);
    }
}

// Puddle splash: water thrown up from the rear tyres in two wide fans - a mist of spray at the tyres and drops
// (round, not streaks) that fly up and out and fall back, pale and translucent, some catching the street light.
void drawSplash(Canvas& cv, const std::vector<std::array<double, 2>>& tyres, double cx, double groundY, double bw,
                double clip, double t, unsigned seed, double strength, double dim) {
    if (bw < 3 || strength < 0.05) return;
    std::array<double, 2> fallback[2] = {{cx - bw * 0.38, groundY}, {cx + bw * 0.38, groundY}};
    const std::array<double, 2>* pts = tyres.size() >= 2 ? tyres.data() : fallback;
    std::array<double, 2> L = pts[0], R = pts[1];
    if (L[0] > R[0]) std::swap(L, R);
    const double vis = strength * (1 - dim);
    // the spray: soft white fans rising from the tyres, leaning outwards
    for (int k = 0; k < 2; ++k) {
        const auto& o = k ? R : L;
        double side = k ? 1 : -1;
        for (int j = 0; j < 3; ++j) {
            double fl = 0.8 + 0.2 * std::sin(t * 23 + j * 2 + k);
            double w = bw * (0.42 + 0.18 * j) * fl, h = bw * (0.4 + 0.16 * j) * fl;
            double x = o[0] + side * bw * (0.04 + 0.1 * j) - w / 2, y = o[1] - h * 0.9 - bw * 0.04 * j;
            cv.blitScaled(gfx.smoke, x, y, w, h, false, clip, 0.5, rgb(190, 214, 245), (0.62 - 0.14 * j) * vis);
        }
    }
    // drops
    const int N = bw > 60 ? 140 : bw > 20 ? 56 : 16;
    for (int i = 0; i < N; ++i) {
        const double life = 0.36 + 0.24 * frand(unsigned(i) * 71 + seed);
        double ph = t / life + double(i) / N;
        double cyc = std::floor(ph), age = ph - cyc;
        unsigned sd = unsigned(cyc) * 113 + unsigned(i) * 29 + seed;
        double r1 = frand(sd), r2 = frand(sd + 1), r3 = frand(sd + 2);
        const std::array<double, 2>& o = (i & 1) ? R : L;
        double side = (i & 1) ? 1 : -1;
        double vx = side * (0.3 + 2.0 * r1) * bw, vy = -(1.3 + 2.3 * r2) * bw;
        const double g = 7.0 * bw, ts = age * life;
        double x = o[0] + vx * ts, y = o[1] - bw * 0.04 + vy * ts + 0.5 * g * ts * ts;
        if (y > clip) continue;
        double sz = std::max(1.0, bw * (0.014 + 0.02 * r3));
        Color c = r3 < 0.25 ? rgb(255, 214, 160) : r3 < 0.6 ? rgb(150, 190, 245) : rgb(215, 230, 250);   // some catch the lamp light
        double a = vis * 0.75 * (age > 0.6 ? (1 - age) / 0.4 : 1.0);
        cv.rectBlend(int(x - sz / 2), int(y - sz / 2), int(std::ceil(sz)), int(std::ceil(sz)), c, a);
    }
}

void hudPanel(UiCanvas& cv, double x, double y, double w, double h) {
    cv.vgradRect(x, y, w, h, rgb(14, 18, 50), rgb(2, 2, 10), 0.6);
    cv.rectBlend(x, y, w, 1, rgb(160, 190, 255), 0.45);
    cv.rectBlend(x, y + h - 1, w, 1, rgb(160, 190, 255), 0.25);
}

// the HUD is laid out in layout units (640x360 for the full screen)
void drawHud(Canvas& canvas, const Rect& vpPx, const Race& race, int idx, int frame) {
    UiCanvas cv{canvas};
    const Rect vp{vpPx.x / RENDER_SCALE, vpPx.y / RENDER_SCALE, vpPx.w / RENDER_SCALE, vpPx.h / RENDER_SCALE};
    const Car& c = race.cars[idx];
    const double f = vp.h >= 300 ? 1.0 : 0.66;          // text scale for split screen
    const double pad = 8 * f;
    const Color yellow = rgb(255, 220, 40), white = rgb(255, 255, 255);
    const double cx = vp.x + vp.w / 2.0;
    const double top = vp.y + pad;

    TextStyle label = styleUI(9.5 * f, yellow);
    TextStyle val = styleDisplay(15 * f, white, rgb(170, 200, 255));
    TextStyle small = styleUI(10 * f, white);

    // time left (centre)
    hudPanel(cv, int(cx - 44 * f), vp.y, int(88 * f), int(48 * f));
    text.draw("TIME", cx, top - 3 * f, label, ALIGN_CENTER);
    int t = int(std::ceil(c.timeLeft));
    bool low = c.timeLeft < 10 && !c.finished;
    TextStyle big = low ? styleDisplay(30 * f, rgb(255, 120, 100), rgb(200, 0, 0)) : styleDisplay(30 * f, rgb(255, 250, 160), rgb(255, 130, 20));
    if (!(low && !c.timedOut && (frame / 15) % 2)) text.draw(std::to_string(t), cx, top + 5 * f, big, ALIGN_CENTER);

    // lap + position (left)
    int lap = std::clamp(int(std::floor(c.dist / race.track.length)) + 1, 1, race.laps);
    hudPanel(cv, vp.x, vp.y, int(112 * f), int(48 * f));
    text.draw("LAP", vp.x + pad, top, label);
    text.draw(std::to_string(lap) + "/" + std::to_string(race.laps), vp.x + pad + 34 * f, top - 3 * f, val);
    text.draw("POS", vp.x + pad, top + 19 * f, label);
    text.draw(std::to_string(race.position(idx)) + "/" + std::to_string(race.cars.size()), vp.x + pad + 34 * f, top + 16 * f, val);

    // lap times (right)
    double rx = vp.x + vp.w - pad;
    hudPanel(cv, int(vp.x + vp.w - 128 * f), vp.y, int(128 * f), int(48 * f));
    double cur = (race.phase == RacePhase::Countdown || c.cpPassed < 0) ? 0 : (c.finished ? c.lastLap : race.time - c.lapStart);
    text.draw("LAP", rx - 74 * f, top, label);
    text.draw(formatTime(cur), rx, top - 1 * f, small, ALIGN_RIGHT);
    if (c.bestLap > 0) {
        text.draw("BEST", rx - 74 * f, top + 19 * f, label);
        text.draw(formatTime(c.bestLap), rx, top + 18 * f, styleUI(10 * f, rgb(110, 230, 255)), ALIGN_RIGHT);
    }

    // speedometer (bottom right)
    int kmh = int(c.speed / MAX_SPEED * KMH_AT_MAX + 0.5);
    double by = vp.y + vp.h - pad;
    hudPanel(cv, int(vp.x + vp.w - 118 * f), int(by - 34 * f), int(118 * f), int(34 * f + pad));
    text.draw("KM/H", rx, by - 13 * f, label, ALIGN_RIGHT);
    text.draw(std::to_string(kmh), rx - 34 * f, by - 32 * f, styleDisplay(26 * f, white, rgb(150, 190, 255)), ALIGN_RIGHT);

    // gear + rev bar + nitrous bar (bottom left)
    hudPanel(cv, vp.x, int(by - 50 * f), int(150 * f), int(50 * f + pad));
    bool man = race.manual[std::max(0, c.player)];
    text.draw("GEAR", vp.x + pad, by - 13 * f, label);
    text.draw(std::string(1, char('1' + c.gear)), vp.x + pad + 34 * f, by - 19 * f, styleDisplay(16 * f, white, rgb(170, 200, 255)));
    text.draw(man ? "MANUAL" : "AUTO", vp.x + pad + 52 * f, by - 13 * f, styleUI(9 * f, rgb(170, 180, 200)));
    int segs = 14;
    double sw = 8 * f, gap = 2 * f, sh = 9 * f;
    double bx = vp.x + pad, byy = by - 30 * f;
    for (int i = 0; i < segs; ++i) {
        double fr = double(i) / segs;
        Color col = fr < 0.6 ? rgb(60, 230, 80) : fr < 0.85 ? rgb(255, 220, 40) : rgb(255, 60, 40);
        bool litSeg = c.rpm > fr;
        int x = int(bx + i * (sw + gap)), y = int(byy), w = std::max(1, int(sw)), h = std::max(2, int(sh));
        if (litSeg) cv.vgradRect(x, y, w, h, shade(col, 1.25), shade(col, 0.6));
        else cv.vgradRect(x, y, w, h, rgb(50, 54, 70), rgb(24, 26, 36));
    }
    // nitrous: drains while boosting, refills slowly and faster while drifting; flashes while in use. It can
    // only be fired from a full bar, so while it refills the bar is dimmed and the label greyed out.
    {
        bool flash = c.boosting && (frame / 4) % 2;
        bool charging = !c.boosting && c.nitro < 1.0;
        // full and ready: the bar pulses with a soft halo and a shine sweeps along it, the label glows
        bool ready = !c.boosting && c.nitro >= 1.0;
        double pulse = ready ? 0.5 + 0.5 * std::sin(frame * 0.1) : 0;
        double ny = byy - 15 * f, nh = 7 * f;
        Color label = c.boosting ? rgb(140, 240, 255) : charging ? rgb(130, 134, 150) : lerpColor(yellow, rgb(255, 255, 215), pulse);
        text.draw("N2O", bx, ny - 2.5 * f, styleUI(8.5 * f, label));
        double gx = bx + 26 * f, gw = segs * (sw + gap) - gap - 26 * f;
        int n = 12;
        double cw = (gw + gap) / n;
        if (ready)
            for (int k = 1; k <= 3; ++k)
                cv.rectBlend(int(gx - 1.5 * k * f), int(ny - 1.5 * k * f), int(gw + 3 * k * f), int(nh + 3 * k * f),
                             rgb(90, 220, 255), (0.06 + 0.14 * pulse) * (4 - k) / 3);
        const double sweep = ready ? std::fmod(frame * 0.35, n + 14.0) - 2 : -99;   // shine position (segments)
        for (int i = 0; i < n; ++i) {
            double fr = double(i) / n;
            bool on = c.nitro > fr + 0.5 / n;
            Color col = lerpColor(rgb(80, 230, 255), rgb(70, 100, 255), fr);
            if (flash) col = shade(col, 1.35);
            if (charging) col = shade(col, 0.5);
            if (ready) {
                col = shade(col, 1.0 + 0.3 * pulse);
                double shine = std::max(0.0, 1 - std::fabs(i - sweep) / 1.5);
                if (shine > 0) col = lerpColor(col, rgb(255, 255, 255), 0.7 * shine);
            }
            int x = int(gx + i * cw), y = int(ny), w = std::max(1, int(cw - gap)), h = std::max(2, int(nh));
            if (on) cv.vgradRect(x, y, w, h, shade(col, 1.25), shade(col, 0.6));
            else cv.vgradRect(x, y, w, h, rgb(50, 54, 70), rgb(24, 26, 36));
        }
    }

    // countdown lights
    auto lights = [&](int lit, bool green) {
        double r = 13 * f, gap2 = r * 2.6;
        double ly = vp.y + vp.h * 0.30;
        cv.vgradRect(int(cx - gap2 * 1.75), int(ly - r - 5 * f), int(gap2 * 3.5), int(r * 2 + 10 * f), rgb(40, 40, 50), rgb(10, 10, 14), 0.9);
        for (int i = 0; i < 3; ++i) {
            const Bitmap& b = green ? gfx.lightGreen : (i < lit ? gfx.lightRed : gfx.lightOff);
            cv.blitScaled(b, cx - gap2 + i * gap2 - r, ly - r, r * 2, r * 2);
        }
        return ly + r;
    };
    double belowGo = 0;   // while "GO!" shows, messages go below it
    if (race.phase == RacePhase::Countdown && race.countdown <= 3.5) {
        lights(race.countdown > 3 ? 0 : race.countdown > 2 ? 1 : race.countdown > 1 ? 2 : 3, false);
    } else if (race.phase != RacePhase::Countdown && race.time < 1.5 && !race.attract) {
        double y = lights(3, true);
        text.draw("GO!", cx, y + 6 * f, styleDisplay(30 * f, rgb(200, 255, 140), rgb(30, 200, 60)), ALIGN_CENTER);
        belowGo = y + 44 * f;
    }

    // messages
    if (c.msgTimer > 0 && !c.msg.empty() && ((frame / 8) % 4 != 0 || c.msgTimer > 50)) {
        TextStyle ms = c.timedOut ? styleDisplay(30 * f, rgb(255, 140, 120), rgb(220, 0, 0))
                     : c.finished ? styleDisplay(30 * f, rgb(255, 250, 160), rgb(255, 130, 20))
                     : c.msg == "TURBO START!" ? styleDisplay(28 * f, rgb(255, 245, 150), rgb(255, 110, 20))
                     : c.msg == "TOO EARLY!" ? styleDisplay(24 * f, rgb(255, 170, 150), rgb(200, 30, 20))
                                  : styleDisplay(26 * f, white, rgb(140, 200, 255));
        double my = std::max(vp.y + vp.h * 0.38, belowGo);
        text.draw(c.msg, cx, my, ms, ALIGN_CENTER);
        if (c.msg == "CHECKPOINT" || c.msg.rfind("LAP", 0) == 0 || c.msg == "FINAL LAP")
            text.draw("EXTENDED TIME +" + std::to_string(int(c.lastBonus)), cx, my + 34 * f, styleUI(12 * f, yellow), ALIGN_CENTER);
    }
}
} // namespace

std::string formatTime(double t, bool hundredths) {
    if (t < 0) t = 0;
    int m = int(t / 60), s = int(t) % 60, h = int(t * 100) % 100;
    char buf[32];
    if (hundredths) std::snprintf(buf, sizeof buf, "%d:%02d.%02d", m, s, h);
    else std::snprintf(buf, sizeof buf, "%d:%02d", m, s);
    return buf;
}

void renderView(Canvas& cv, const Rect& vp, const Race& race, int carIndex, const ViewOptions& opt) {
    const Track& tr = race.track;
    const Theme& th = tr.theme;
    const Car& car = race.cars[carIndex];
    const int N = tr.count();
    projBuf.resize(DRAW_DIST);
    ceilBuf.resize(DRAW_DIST);
    Proj* const proj = projBuf.data();
    double* const ceilClip = ceilBuf.data();

    const double focal = vp.h * (320.0 / 360.0);
    // nitrous widens the view a little: the road stretches away and rushes past (the car stays the same size)
    const double depth = 1.0 / std::tan((FIELD_OF_VIEW + 14 * car.boostVis) / 2 * M_PI / 180);
    const double s = focal / 320.0;
    double horizonY = vp.y + vp.h * HORIZON_FRAC;
    const double bottomY = vp.y + vp.h - vp.h * 0.03;
    // big jumps: the camera pulls back a little (so the flying car stays on screen), rises a bit with
    // the car, and tilts down so the road under the car - with its shadow - stays at the bottom
    const double camBack = focal * CAMERA_HEIGHT * depth / (bottomY - horizonY) * (1 + car.air * 0.00035);

    // jumps: the camera rises a little with the car and tilts down (horizon moves up) so the road under
    // the car - and its shadow - stays in view while the car flies above it
    const double kGround = depth / camBack * focal;          // screen pixels per world unit at the car
    const double camRise = car.air * 0.3;
    horizonY = bottomY - (CAMERA_HEIGHT + camRise) * kGround;

    // screen shake (crash/bumps/off-road)
    double shakeAmt = car.shake * 5 + (car.offroad && car.speed > 500 ? 1.5 : 0);
    double shakeX = (frand(opt.frame * 3 + carIndex) - 0.5) * 2 * shakeAmt * s;
    double shakeY = (frand(opt.frame * 7 + 11 + carIndex) - 0.5) * 2 * shakeAmt * s;
    horizonY += shakeY;

    cv.setClip(vp);

    const double camZ = tr.wrap(car.dist - camBack);
    const double groundAtCar = tr.heightAt(car.dist);
    const double camY = groundAtCar + CAMERA_HEIGHT + camRise;          // the camera follows a jump part of the way
    const double camX = car.x * ROAD_WIDTH;
    const double cx = vp.x + vp.w / 2.0 + shakeX;
    const Segment& base = tr.find(camZ);
    const double basePct = (camZ - base.index * SEG_LEN) / SEG_LEN;

    // how far the tunnel the camera is in reaches (0 = the camera is outside): only from inside is a tunnel exit
    // seen as a black frame around a bright hole, and only from inside is the ground around the road black
    int insideRun = 0;
    while (insideRun < DRAW_DIST && tr.segs[(base.index + insideRun) % N].tunnel) ++insideRun;
    // deep in a tunnel whose exit lies beyond the draw distance: the far end is black, not sky - only a real
    // exit (drawn as a bright hole) may show the outside
    g_nightLights = th.rain;
    if (insideRun >= DRAW_DIST) cv.rect(vp.x, vp.y, vp.w, vp.h, TUNNEL_BLACK);
    else drawBackground(cv, vp, th, car, horizonY, s, tr.stage);

    double x = 0, dx = -base.curve * basePct;
    double maxy = vp.y + vp.h;
    for (int n = 0; n < DRAW_DIST; ++n) {
        int idx = base.index + n;
        bool looped = idx >= N;
        const Segment& seg = tr.segs[idx % N];
        Proj& p = proj[n];
        p.z1 = seg.index * SEG_LEN + (looped ? tr.length : 0) - camZ;
        p.z2 = p.z1 + SEG_LEN;
        double s1 = depth / std::max(p.z1, 1.0), s2 = depth / std::max(p.z2, 1.0);
        p.rx1 = x - camX;
        p.rx2 = x + dx - camX;
        p.x1 = cx + s1 * (x - camX) * focal;
        p.y1 = horizonY - s1 * (seg.y1 - camY) * focal;
        p.w1 = s1 * ROAD_WIDTH * seg.w1 * focal;
        p.x2 = cx + s2 * (x + dx - camX) * focal;
        p.y2 = horizonY - s2 * (seg.y2 - camY) * focal;
        p.w2 = s2 * ROAD_WIDTH * seg.w2 * focal;
        x += dx;
        dx += seg.curve;
        p.clip = maxy;
        if (p.z1 <= 1 || p.y2 >= p.y1 || p.y2 >= maxy) continue;

        // fill the scanlines covered by this segment (perspective-correct z per row, like the zMap in main.lua)
        double yTop = std::max(p.y2, double(vp.y));
        double yBot = std::min(p.y1, maxy);
        int r0 = int(std::ceil(yTop - 0.5)), r1 = int(std::ceil(yBot - 0.5));
        double iz1 = 1 / p.z1, iz2 = 1 / p.z2;
        for (int y = r0; y < r1; ++y) {
            double t = (p.y1 - (y + 0.5)) / (p.y1 - p.y2);
            t = std::clamp(t, 0.0, 1.0);
            double z = 1 / (iz1 + (iz2 - iz1) * t);
            double rcx = p.x1 + (p.x2 - p.x1) * t;
            double hw = p.w1 + (p.w2 - p.w1) * t;
            double wf = seg.w1 + (seg.w2 - seg.w1) * t;
            // depth range covered by this scanline (for the stripe filter)
            double ta = std::clamp((p.y1 - y) / (p.y1 - p.y2), 0.0, 1.0), tb = std::clamp((p.y1 - (y + 1)) / (p.y1 - p.y2), 0.0, 1.0);
            double za = 1 / (iz1 + (iz2 - iz1) * ta), zb = 1 / (iz1 + (iz2 - iz1) * tb);
            drawRoadRow(cv, vp, th, y, rcx, hw, wf, seg, camZ + z, z, std::fabs(zb - za), tr.length, n >= insideRun);
        }
        maxy = p.y2;
    }

    // Seen from outside, a tunnel shows its dark inside only through the opening in its entrance wall; any other
    // part of it in view (rising above the wall in a climbing bend, say) is the grey outside of the structure.
    // opening[n] = the screen rectangle of the opening of the tunnel segment n belongs to (outside tunnels only).
    struct OpenRect { double x0, y0, x1, y1; bool on; };
    thread_local std::vector<OpenRect> openRect;
    openRect.assign(DRAW_DIST, OpenRect{0, 0, 0, 0, false});
    {
        OpenRect cur{0, 0, 0, 0, false};
        for (int n = std::max(1, insideRun); n < DRAW_DIST; ++n) {
            const Segment& seg = tr.segs[(base.index + n) % N];
            if (!seg.tunnel) { cur.on = false; continue; }
            if (!tr.segs[(base.index + n + N - 1) % N].tunnel) {      // an entrance
                const Proj& p = proj[n];
                if (p.z1 > 1) {
                    double sc = depth / p.z1 * focal, hwO = TUNNEL_SIDE * ROAD_WIDTH * sc;
                    cur = {p.x1 - hwO, horizonY - (seg.y1 + TUNNEL_HEIGHT - camY) * sc, p.x1 + hwO, std::min(p.y1, p.clip), true};
                } else cur = {0, 0, 0, 0, true};                        // behind the near plane: shows nothing
            }
            openRect[n] = cur;
        }
    }
    const Color TUNNEL_OUTSIDE = rgb(146, 146, 144);                   // the concrete of the portal and roof
    auto outsideColor = [&](double z) {
        double f = fogAt(th, z);
        return f > 0 ? lerpColor(TUNNEL_OUTSIDE, th.fogColor, f) : TUNNEL_OUTSIDE;
    };
    auto openRectClip = [&](const OpenRect& o) {                         // the openRect, within the current clip
        int x0 = std::max(cv.clip.x, int(std::floor(o.x0))), y0 = std::max(cv.clip.y, int(std::floor(o.y0)));
        int x1 = std::min(cv.clip.x + cv.clip.w, int(std::ceil(o.x1))), y1 = std::min(cv.clip.y + cv.clip.h, int(std::ceil(o.y1)));
        return Rect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
    };

    // tunnel ceilings, front to back (each scanline belongs to the nearest ceiling slice)
    double ceilMin = vp.y;
    for (int n = 0; n < DRAW_DIST; ++n) {
        const Segment& seg = tr.segs[(base.index + n) % N];
        const Proj& p = proj[n];
        ceilClip[n] = ceilMin;
        if (!seg.tunnel || p.z1 <= 1) continue;
        double s1 = depth / p.z1, s2 = depth / p.z2;
        double cy1 = horizonY - s1 * (seg.y1 + TUNNEL_HEIGHT - camY) * focal;
        double cy2 = horizonY - s2 * (seg.y2 + TUNNEL_HEIGHT - camY) * focal;
        if (cy2 <= cy1) continue;
        int r0 = std::max({int(std::ceil(cy1 - 0.5)), int(ceilMin), vp.y});
        int r1 = std::min(int(std::ceil(std::min(cy2, p.clip) - 0.5)), vp.y + vp.h);
        double iz1 = 1 / p.z1, iz2 = 1 / p.z2;
        for (int y = r0; y < r1; ++y) {
            double t = std::clamp((y + 0.5 - cy1) / (cy2 - cy1), 0.0, 1.0);
            double z = 1 / (iz1 + (iz2 - iz1) * t);
            double rcx = p.x1 + (p.x2 - p.x1) * t;
            double hw = TUNNEL_SIDE * ROAD_WIDTH * focal * depth / z;
            if (openRect[n].on) {        // outside: grey, except what shows through the opening
                const OpenRect& o = openRect[n];
                Color g = outsideColor(z);
                if (y + 0.5 < o.y0 || y + 0.5 >= o.y1) cv.span(vp.x, vp.x + vp.w, y, g);
                else { cv.spanAA(vp.x, o.x0, y, g); cv.spanAA(o.x0, o.x1, y, TUNNEL_BLACK); cv.spanAA(o.x1, vp.x + vp.w, y, g); }
            } else
                drawCeilingRow(cv, vp, th, y, rcx, hw, camZ + z, z);
        }
        ceilMin = std::max(ceilMin, double(r1));     // whole rows, so thin far slices leave no gaps
    }

    // cars per segment offset (relative to the camera segment)
    struct CarRef { int n; double pct; int idx; };
    std::vector<CarRef> carRefs;
    for (int i = 0; i < int(race.cars.size()); ++i) {
        if (i == carIndex && opt.drawViewCar) continue;
        const Car& c = race.cars[i];
        double rel = tr.wrap(c.dist) - base.index * SEG_LEN;
        if (rel < 0) rel += tr.length;
        int n = int(rel / SEG_LEN);
        if (n < 1 || n >= DRAW_DIST) continue;
        carRefs.push_back({n, rel / SEG_LEN - n, i});
    }
    std::sort(carRefs.begin(), carRefs.end(), [](const CarRef& a, const CarRef& b) {
        return a.n != b.n ? a.n > b.n : a.pct > b.pct;
    });
    size_t ci = 0;
    // tumbleweeds (desert), drawn in the same far-to-near pass as the cars
    struct WeedRef { int n; double pct; int idx; };
    std::vector<WeedRef> weedRefs;
    for (int i = 0; i < int(race.weeds.size()); ++i) {
        double rel = tr.wrap(race.weeds[i].dist) - base.index * SEG_LEN;
        if (rel < 0) rel += tr.length;
        int n = int(rel / SEG_LEN);
        if (n < 1 || n >= DRAW_DIST) continue;
        weedRefs.push_back({n, rel / SEG_LEN - n, i});
    }
    std::sort(weedRefs.begin(), weedRefs.end(), [](const WeedRef& a, const WeedRef& b) {
        return a.n != b.n ? a.n > b.n : a.pct > b.pct;
    });
    size_t wi = 0;

    // Tunnel lamps are collected while the walls are drawn and drawn last, each one only if no nearer tunnel
    // wall (or the wall around a tunnel entrance) is in front of it. Drawing them in the normal far-to-near
    // order would let the next wall pieces cut a hard edge into their glow.
    struct Lamp { double hx0, hx1, hy0, hy1, lx, ly, size, dark, alpha; Rect clip; double clipBottom; int n; bool done = false; };
    std::vector<Lamp> lamps;
    struct WallQuad { double xa, yaTop, yaBot, xb, ybTop, ybBot; int n; };   // a wall piece, near (a) to far (b)
    std::vector<WallQuad> wallQuads;
    struct Opening { double xl, xr, yTop, yBot; int n; };                  // a tunnel entrance seen from outside
    std::vector<Opening> openings;
    auto lampHidden = [&](const Lamp& L) {
        for (const WallQuad& q : wallQuads) {
            if (q.n >= L.n) continue;
            double lo = std::min(q.xa, q.xb), hi = std::max(q.xa, q.xb);
            if (L.lx <= lo || L.lx >= hi) continue;
            double t = (L.lx - q.xa) / (q.xb - q.xa);
            double top = q.yaTop + (q.ybTop - q.yaTop) * t, bot = q.yaBot + (q.ybBot - q.yaBot) * t;
            if (L.ly > top && L.ly < bot) return true;
        }
        for (const Opening& o : openings)
            if (o.n < L.n && (L.lx < o.xl || L.lx > o.xr || L.ly < o.yTop || L.ly > o.yBot)) return true;
        return false;
    };
    auto drawLamp = [&](Lamp& L) {
        L.done = true;
        if (lampHidden(L)) return;
        Rect keep = cv.clip;
        cv.setClip(L.clip);
        Color housing = lerpColor(rgb(70, 70, 76), TUNNEL_BLACK, L.dark * 0.8);
        for (int x = int(std::floor(L.hx0)); x <= int(std::ceil(L.hx1)); ++x) cv.vspanAA(x, L.hy0, L.hy1, housing, L.clipBottom);
        cv.blitScaled(gfx.lampGlow, L.lx - L.size / 2, L.ly - L.size / 2, L.size, L.size, false, L.clipBottom, 0, 0, L.alpha);
        cv.setClip(keep);
    };
    auto drawLamps = [&] {
        for (Lamp& L : lamps) if (!L.done) drawLamp(L);   // far to near
        lamps.clear();
    };
    // Sprites (checkpoint gantries, signs) and cars nearer than a lamp must cover it, so a lamp whose glow
    // overlaps one of them is drawn early, at its proper depth, just before that object.
    struct Occluder { double x0, y0, x1, y1; };
    auto flushLampsBehind = [&](const Occluder& o) {
        for (Lamp& L : lamps) {
            if (L.done) continue;
            double r = L.size / 2;
            if (L.lx + r > o.x0 && L.lx - r < o.x1 && L.ly + r > o.y0 && L.ly - r < o.y1) drawLamp(L);
        }
    };
    // concrete tunnel barriers, guardrails and tunnel lamps of one segment
    auto drawWalls = [&](int n) {
        const Proj& p = proj[n];
        const Segment& seg = tr.segs[(base.index + n) % N];
        if (!seg.tunnel && seg.kerb != KERB_GUARDRAIL) return;
        if (p.z2 <= 1) return;
        double z1 = std::max(p.z1, 40.0);
        double s1 = depth / z1 * focal, s2 = depth / p.z2 * focal;
        double t1 = (z1 - p.z1) / SEG_LEN;              // clamp the near end to the near plane
        double x1 = cx + s1 * (p.rx1 + (p.rx2 - p.rx1) * t1);   // perspective-correct near end
        double y1 = horizonY - s1 * (seg.y1 + (seg.y2 - seg.y1) * t1 - camY);
        double w1 = seg.w1 + (seg.w2 - seg.w1) * t1;
        Rect keepClip = cv.clip;
        if (seg.tunnel && openRect[n].on) {
            Color g = outsideColor(p.z1);
            for (int sgn : {-1, 1}) {
                double off = sgn * TUNNEL_SIDE * ROAD_WIDTH;
                double xa = x1 + off * w1 * s1, xb = p.x2 + off * seg.w2 * s2;
                drawSideWall(cv, xa, y1, TUNNEL_HEIGHT * s1, xb, p.y2, TUNNEL_HEIGHT * s2, p.clip,
                    [&](int x, double top, double bot, double, double clipB) { cv.vspanAA(x, top, bot, g, clipB); });
            }
            cv.setClip(openRectClip(openRect[n]));
        }
        if (seg.tunnel) {
            // the walls above the barriers, up to the ceiling: solid black, so in bends they hide whatever is
            // behind them (the tunnel further on, its lamps, cars)
            for (int sgn : {-1, 1}) {
                double off = sgn * TUNNEL_SIDE * ROAD_WIDTH;
                double xa = x1 + off * w1 * s1, xb = p.x2 + off * seg.w2 * s2;
                drawSideWall(cv, xa, y1, TUNNEL_HEIGHT * s1, xb, p.y2, TUNNEL_HEIGHT * s2, p.clip,
                    [&](int x, double top, double bot, double, double clipB) { cv.vspanAA(x, top, bot, TUNNEL_BLACK, clipB); });
                wallQuads.push_back({xa, y1 - TUNNEL_HEIGHT * s1, y1, xb, p.y2 - TUNNEL_HEIGHT * s2, p.y2, n});
            }
            for (int sgn : {-1, 1}) {
                double off = sgn * TUNNEL_SIDE * ROAD_WIDTH;
                double xa = x1 + off * w1 * s1, xb = p.x2 + off * seg.w2 * s2;
                drawSideWall(cv, xa, y1, TUNNEL_WALL_H * s1, xb, p.y2, TUNNEL_WALL_H * s2, p.clip,
                    [&](int x, double top, double bot, double t, double clipB) {
                        double z = z1 + (p.z2 - z1) * t;
                        double d = tunnelDark(z);
                        double hgt = bot - top;
                        Color body = lerpColor(rgb(150, 148, 142), TUNNEL_BLACK, d);
                        Color lip = lerpColor(rgb(196, 194, 188), TUNNEL_BLACK, d);
                        Color foot = lerpColor(rgb(96, 94, 90), TUNNEL_BLACK, d);
                        cv.vspanAA(x, top, bot, body, clipB);
                        cv.vspanAA(x, top, top + hgt * 0.14, lip, clipB);              // rounded top edge catches the light
                        cv.vspanAA(x, bot - hgt * 0.22, bot, foot, clipB);             // grime at the bottom
                    });
            }
            // lamps every TUNNEL_LAMP_EVERY segments: one on each wall at mid height and one in the middle of
            // the ceiling, each a small dark housing with a lit lens and its glow
            const Segment& before = tr.segs[(base.index + n + N - 1) % N];
            if (seg.index % TUNNEL_LAMP_EVERY == 0 && p.z1 > 1 && before.tunnel) {
                double sc = depth / p.z1 * focal;
                double d = tunnelDark(p.z1);
                double a = 1.0 - std::min(0.6, p.z1 / 60000);
                auto add = [&](double lx, double ly, double hw, double hh, double size) {
                    lamps.push_back({lx - hw, lx + hw, ly - hh, ly + hh, lx, ly, size, d, a, cv.clip, p.clip, n});
                };
                double wallY = horizonY - sc * (seg.y1 + TUNNEL_HEIGHT * 0.5 - camY);
                for (int sgn : {-1, 1}) {
                    // the fitting sits flat on the wall (exactly where the wall is drawn), the lens just in front
                    double wallX = p.x1 + sgn * TUNNEL_SIDE * ROAD_WIDTH * seg.w1 * sc;
                    double lensX = wallX - sgn * 25 * sc;
                    double inner = lensX - sgn * 30 * sc;             // the fitting reaches from the wall to here
                    lamps.push_back({std::min(wallX, inner), std::max(wallX, inner), wallY - 150 * sc, wallY + 150 * sc,
                                     lensX, wallY, 360 * sc, d, a, cv.clip, p.clip, n});
                }
                double ceilY = horizonY - sc * (seg.y1 + TUNNEL_HEIGHT - 60 - camY);
                add(p.x1, ceilY, 260 * sc, 45 * sc, 460 * sc);                          // long ceiling fitting
            }
            cv.setClip(keepClip);
        } else {   // guardrail: W-beam on posts
            for (int sgn : {-1, 1}) {
                double off = sgn * RAIL_POS * ROAD_WIDTH;
                double xa = x1 + off * w1 * s1, xb = p.x2 + off * seg.w2 * s2;
                double fogA = fogAt(th, z1);
                auto F = [&](Color c) { return fogA > 0 ? lerpColor(c, th.fogColor, fogA) : c; };
                // posts at the start of every second segment
                if (seg.index % 2 == 0 && p.z1 > 1) {
                    double pw = std::max(1.0, 60 * s1);
                    for (int x = int(xa - pw / 2); x < int(xa + pw / 2) + 1; ++x)
                        cv.vspanAA(x, y1 - RAIL_HEIGHT * 0.95 * s1, y1, F(rgb(70, 72, 80)), p.clip);
                }
                drawSideWall(cv, xa, y1 - 0.45 * RAIL_HEIGHT * s1, 0.55 * RAIL_HEIGHT * s1,
                             xb, p.y2 - 0.45 * RAIL_HEIGHT * s2, 0.55 * RAIL_HEIGHT * s2, p.clip,
                    [&](int x, double top, double bot, double, double clipB) {
                        double hgt = bot - top;
                        cv.vspanAA(x, top, bot, F(shade(th.rail, 0.82)), clipB);
                        cv.vspanAA(x, top, top + hgt * 0.30, F(shade(th.rail, 1.12)), clipB);   // upper ridge highlight
                        cv.vspanAA(x, top + hgt * 0.45, top + hgt * 0.58, F(shade(th.rail, 0.55)), clipB);   // groove
                        cv.vspanAA(x, bot - hgt * 0.12, bot, F(shade(th.rail, 0.6)), clipB);
                    });
            }
        }
    };

    // A tunnel seen from outside and above (coming down a hill): a solid concrete roof over it, so its dark
    // inside never shows. Drawn after the segment's walls and cars, which it covers.
    auto drawRoof = [&](int n) {
        const Proj& p = proj[n];
        const Segment& seg = tr.segs[(base.index + n) % N];
        if (!seg.tunnel || n < insideRun || p.z1 <= 1) return;
        const double roofH = TUNNEL_HEIGHT + 120;
        if (camY <= std::min(seg.y1, seg.y2) + roofH) return;          // looking at it from below: no roof to see
        double s1 = depth / p.z1 * focal, s2 = depth / p.z2 * focal;
        double ya = horizonY - s1 * (seg.y1 + roofH - camY), yb = horizonY - s2 * (seg.y2 + roofH - camY);
        if (yb >= ya) return;
        const double side = TUNNEL_SIDE * 1.1 * ROAD_WIDTH;
        double hwa = side * seg.w1 * s1, hwb = side * seg.w2 * s2;
        double fog = fogAt(th, p.z1);
        auto F = [&](Color c) { return fog > 0 ? lerpColor(c, th.fogColor, fog) : c; };
        Color slab = F(rgb(146, 146, 144)), edge = F(rgb(124, 124, 120)), seam = F(rgb(136, 136, 132));
        bool seamRow = seg.index % 4 == 0;                              // joints between the precast sections
        int y0 = std::max({vp.y, cv.clip.y, int(std::ceil(yb - 0.5))});
        double bottom = std::min(ya, p.clip);
        if (!tr.segs[(base.index + n + N - 1) % N].tunnel)             // the entrance: the portal wall is in front
            bottom = std::min(bottom, horizonY - s1 * (seg.y1 + PORTAL_HEIGHT * 0.8 - camY));
        int y1 = std::min({int(std::ceil(bottom - 0.5)), vp.y + vp.h, cv.clip.y + cv.clip.h});
        for (int y = y0; y < y1; ++y) {
            double t = (y + 0.5 - yb) / (ya - yb);                     // 0 far .. 1 near
            double xc = p.x2 + (p.x1 - p.x2) * t, hw = hwb + (hwa - hwb) * t;
            cv.spanAA(xc - hw, xc + hw, y, seamRow && t > 0.85 ? seam : slab);
            cv.spanAA(xc - hw, xc - hw * 0.9, y, edge);                // parapets along both sides
            cv.spanAA(xc + hw * 0.9, xc + hw, y, edge);
        }
    };

    // A tunnel that runs on past the draw distance gets a black end cap across it at the last drawn segment, so
    // the background never shows at its far end as if there were an exit (from inside, or through the entrance)
    {
        const int n = DRAW_DIST - 1;
        const Proj& p = proj[n];
        const Segment& seg = tr.segs[(base.index + n) % N];
        if (seg.tunnel && p.z2 > 1) {
            double s2 = depth / p.z2 * focal;
            double hw = TUNNEL_SIDE * ROAD_WIDTH * seg.w2 * s2 + 1;
            double yCeil = horizonY - s2 * (seg.y2 + TUNNEL_HEIGHT - camY) - 1;
            int y0 = std::max(vp.y, int(std::floor(yCeil)));
            int y1 = std::min(vp.y + vp.h, int(std::ceil(std::min(p.y2 + 1, p.clip))));
            for (int y = y0; y < y1; ++y) cv.spanAA(p.x2 - hw, p.x2 + hw, y, TUNNEL_BLACK);
        }
    }

    // sprites back to front
    for (int n = DRAW_DIST - 1; n >= 1; --n) {
        const Proj& p = proj[n];
        const Segment& seg = tr.segs[(base.index + n) % N];
        if (seg.tunnel && p.z2 > 1) {
            const Segment& next = tr.segs[(base.index + n + 1) % N];
            const Segment& prev = tr.segs[(base.index + n - 1 + N) % N];
            if (!next.tunnel && n < insideRun) {          // exit hole seen from inside
                double s2 = depth / p.z2;
                double cy2 = horizonY - s2 * (seg.y2 + TUNNEL_HEIGHT - camY) * focal;
                drawExitFace(cv, vp, th, p.x2, TUNNEL_SIDE * ROAD_WIDTH * s2 * focal, cy2, p.y2, p.z2, p.clip);
            }
            if (!prev.tunnel && p.z1 > 1)   // entrance: flat grey wall
            {
                drawPortal(cv, vp, th, horizonY, focal, depth, camY, p.x1, p.y1, seg.y1, p.z1, p.clip);
                double sc = depth / p.z1 * focal;     // lamps inside are only seen through the opening
                openings.push_back({p.x1 - TUNNEL_SIDE * ROAD_WIDTH * seg.w1 * sc, p.x1 + TUNNEL_SIDE * ROAD_WIDTH * seg.w1 * sc,
                                    horizonY - sc * (seg.y1 + TUNNEL_HEIGHT - camY), p.y1, n});
            }
        }
        int topClip = std::max(vp.y, int(std::ceil(ceilClip[n])));
        cv.setClip({vp.x, topClip, vp.w, vp.y + vp.h - topClip});
        drawWalls(n);
        if (p.z1 > 1) {
            double sc = depth / p.z1;
            double fog = fogAt(th, p.z1);
            for (const SpriteInst& si : seg.sprites) {
                const Bitmap& bmp = th.rain && si.type == SPR_BRIDGE ? gfx.bridgeNight
                                  : th.rain && si.type == SPR_BRIDGE_PILLAR ? gfx.bridgePillarNight : gfx.sprite[si.type];
                double dh = SPRITE_INFO[si.type].worldH * sc * focal;
                double dw = dh * bmp.w / bmp.h;
                if (si.type == SPR_GANTRY_START || si.type == SPR_GANTRY_CHECK || si.type == SPR_BRIDGE) dw *= seg.w1;   // spans the road
                double sx = p.x1 + sc * si.offset * ROAD_WIDTH * focal;
                if (si.type != SPR_BRIDGE && (sx + dw / 2 < vp.x || sx - dw / 2 > vp.x + vp.w)) continue;
                bool flip = SPRITE_INFO[si.type].mirrorRight && si.offset > 0;
                if (!lamps.empty()) flushLampsBehind({sx - dw / 2, p.y1 - dh, sx + dw / 2, p.y1});
                // street lamps keep shining in the dark distance (their light, not the post, cuts through the murk)
                const bool lights = si.type == SPR_RAIN_BUILDING1 || si.type == SPR_RAIN_BUILDING2;
                // rainy night: the city sinks into the dark with distance. Buildings fade fully to black between 120
                // and 200 segments ahead (and are not drawn beyond); their lit windows fade more slowly than the
                // walls, but on the same curve, so no window ever floats where its wall has gone.
                double sink = th.rain ? std::clamp((p.z1 / SEG_LEN - 120) / 80, 0.0, 1.0) : 0;
                if (lights && sink >= 1) continue;
                double f = si.type == SPR_PUDDLE ? fog : si.type == SPR_RAIN_LAMP ? fog * 0.2 : th.rain && (si.type == SPR_BRIDGE || si.type == SPR_BRIDGE_PILLAR) ? fog * 0.9
                         : lights ? std::max(fog * 0.9, sink) : fog;
                // rainy night: things close by are caught in the headlights - lit up, the nearer the brighter
                Color fc = th.fogColor;
                if (th.rain && si.type != SPR_PUDDLE) {
                    double lit = 0.42 * std::pow(std::clamp(1 - p.z1 / (45 * SEG_LEN), 0.0, 1.0), 1.6);
                    if (lit > f) { f = lit; fc = rgb(236, 226, 200); }
                }
                if (!(th.rain && f > 0.86) || lights)       // (lost in the dark: not worth drawing)
                    cv.blitScaled(bmp, sx - dw / 2, p.y1 - dh, dw, dh, flip, p.clip, f, fc);
                if (lights && f > 0.3)                      // the windows glow on as the wall darkens
                    cv.blitScaled(gfx.rainLights[si.type == SPR_RAIN_BUILDING2], sx - dw / 2, p.y1 - dh, dw, dh, flip, p.clip,
                                  std::max(fog * 0.6, sink), th.fogColor);
                if (si.type == SPR_PUDDLE && dw > 24) {     // raindrops landing in the water: rings that spread
                    double ph0 = race.time * 1.7 + seg.index * 0.37;
                    for (int k = 0; k < 4; ++k) {
                        double ph = ph0 + k * 0.25, age = ph - std::floor(ph);
                        unsigned sd = unsigned(std::floor(ph)) * 97 + unsigned(k) * 13 + unsigned(seg.index) * 7;
                        double rx = sx + (frand(sd) - 0.5) * dw * 0.7, ry = p.y1 - dh * (0.3 + 0.4 * frand(sd + 1));
                        double rr = dw * (0.02 + 0.07 * age), a = 0.7 * (1 - age) * (1 - f);
                        for (int q = 0; q < 20; ++q) {
                            double an = q * M_PI / 10;
                            double px = rx + std::cos(an) * rr, py = ry + std::sin(an) * rr * 0.3;
                            if (py < p.clip) cv.rectBlend(int(px), int(py), std::max(1, int(s)), std::max(1, int(s * 0.7)), rgb(200, 225, 255), a);
                        }
                    }
                }
                if (si.type == SPR_BRIDGE && dw > 4)        // the deck runs on out to both screen edges
                {
                    // the image has a 2-unit transparent margin each side (of 644): step by the deck's own width, a
                    // hair less, so the copies join without a seam
                    const double step = dw * 640.0 / 644.0 - 0.6;
                    for (int sd : {-1, 1})
                        for (double tx = sx - dw / 2 + sd * step; tx + dw > vp.x && tx < vp.x + vp.w; tx += sd * step)
                            cv.blitScaled(bmp, tx, p.y1 - dh, dw, dh, false, p.clip, f, fc);
                }
            }
        }
        while (wi < weedRefs.size() && weedRefs[wi].n > n) ++wi;
        while (wi < weedRefs.size() && weedRefs[wi].n == n) {
            const WeedRef& r = weedRefs[wi++];
            const auto& w = race.weeds[r.idx];
            double z = p.z1 + r.pct * SEG_LEN;
            if (z <= 1) continue;
            double sc = depth / z * focal;
            double gx = p.x1 + (p.x2 - p.x1) * r.pct + w.x * ROAD_WIDTH * sc;
            double gy = p.y1 + (p.y2 - p.y1) * r.pct;
            double size = w.size * sc;
            if (size < 1.5 || gx + size < vp.x || gx - size > vp.x + vp.w) continue;
            double fog = fogAt(th, z);
            // soft shadow on the road, smaller and fainter the higher it flies
            double lift = std::clamp(1 - w.y / 2500, 0.3, 1.0);
            cv.blitScaled(gfx.smoke, gx - size * 0.5 * lift, gy - size * 0.12, size * lift, size * 0.22, false, p.clip, 1.0, rgb(40, 28, 16), 0.45 * lift);
            const int F = Graphics::TUMBLE_FRAMES;
            int fr = int(std::floor(w.roll / (2 * M_PI / F)));
            fr = ((fr % F) + F) % F;
            double by = gy - w.y * sc;
            cv.blitScaled(gfx.tumbleweed[fr], gx - size / 2, by - size, size, size, false, p.clip, fog, th.fogColor);
        }
        while (ci < carRefs.size() && carRefs[ci].n == n) {
            const CarRef& r = carRefs[ci++];
            const Car& c = race.cars[r.idx];
            double z = p.z1 + r.pct * SEG_LEN;
            if (z <= 1) continue;
            double sc = depth / z;
            double rcx = p.x1 + (p.x2 - p.x1) * r.pct;
            double ry = p.y1 + (p.y2 - p.y1) * r.pct;
            double w = CAR_SPRITE_W * sc * focal;
            double bodyW = CAR_WORLD_W * sc * focal;
            double lift = c.air * sc * focal;
            double fogC = fogAt(th, z);
            Color fogCol = th.fogColor;
            if (c.inTunnel) { fogC = carTunnelShade(th, c.dist, z); fogCol = TUNNEL_BLACK; }
            else if (tr.find(c.dist).shadow) shadeUnderBridge(fogC, fogCol);   // in a bridge's shadow
            double carX = rcx + sc * c.x * ROAD_WIDTH * focal;
            // viewing angle from the camera: horizontal screen offset / focal length = tan(angle)
            // the focal length in pixels is depth * focal (screen x = world x * depth / z * focal)
            double viewAngle = -std::atan((carX - cx) / (depth * focal)) * 180 / M_PI;
            // how steeply we look down at it, scaled so that a car at the player's own distance gets the normal
            // chase view: farther cars are seen flatter (more from behind), nearer ones more from above
            double ratio = (ry - horizonY) / (bottomY - horizonY);
            double viewElev = std::atan(std::tan(CAR_ELEV_DEG[CAR_ELEV_LEVEL] * M_PI / 180) * ratio) * 180 / M_PI;
            if (!lamps.empty()) flushLampsBehind({carX - w / 2, ry - lift - w * CAR_BMP_H / CAR_BMP_W, carX + w / 2, ry});
            drawCarSprite(cv, c, carX, ry - lift, ry, w, p.clip, fogC, fogCol, std::clamp(1 - c.air / 2400, 0.35, 1.0), viewAngle,
                          viewElev, unsigned(opt.frame));
            if (c.splash > 0 && bodyW > 4)                     // driving through a puddle
                drawSplash(cv, g_tyrePts, carX, ry, bodyW, p.clip, race.time, unsigned(r.idx) * 101, std::min(1.0, c.splash / 0.25), fogC * 0.5);
            if (c.drifting && !c.airborne && bodyW > 4)       // CPU drivers drift too
                drawDriftSparks(cv, g_tyrePts, carX, ry, bodyW, p.clip, race.time, unsigned(r.idx) * 101, c.driftVis,
                                c.inTunnel ? 0 : fogC);   // sparks glow even in the dark of a tunnel
            if (c.onDirt && !c.airborne && c.speed > 2000 * SPEED_SCALE) {     // dust cloud behind cars on dirt
                for (int i = 0; i < 3; ++i) {
                    double age = std::fmod(race.time * 2.5 + i / 3.0 + r.idx * 0.37, 1.0);
                    double sz = bodyW * (0.5 + age * 0.9);
                    cv.blitScaled(gfx.dust, carX - sz / 2 + (i - 1) * bodyW * 0.3, ry - sz * 0.75 - age * bodyW * 0.2, sz, sz * 0.75,
                                  false, p.clip, fogC, fogCol, (1 - age) * 0.6);
                }
            }
        }
        drawRoof(n);
        cv.setClip(vp);
    }
    drawWalls(0);
    drawLamps();
    cv.setClip(vp);

    // the viewpoint car
    if (opt.drawViewCar) {
        double sc = depth / camBack;
        double w = CAR_SPRITE_W * sc * focal;
        double groundScreen = bottomY + shakeY;                    // the road under the car (camera pitch keeps it here)
        double by = groundScreen - car.air * sc * focal;           // the car itself, up in the air
        double bounce = (car.speed > 100 && !car.airborne) ? std::sin(race.time * 30 + carIndex) * 0.6 * s : 0;
        if (car.offroad && car.speed > 500) bounce += (frand(opt.frame * 13 + carIndex) - 0.5) * 3 * s;
        double tint = 0;
        Color tintCol = TUNNEL_BLACK;
        if (car.inTunnel) tint = carTunnelShade(th, car.dist, 0);   // dark, lit up briefly under each row of lamps
        else if (tr.find(car.dist).shadow) { tint = 0; tintCol = th.fogColor; shadeUnderBridge(tint, tintCol); }
        drawCarSprite(cv, car, cx, by + bounce, groundScreen, w, 1e9, tint, tintCol, std::clamp(1 - car.air / 2400, 0.35, 1.0),
                      0, CAR_ELEV_DEG[CAR_ELEV_LEVEL], unsigned(opt.frame));

        double bw = CAR_WORLD_W * sc * focal, bh = bw * 0.45;
        // dust / tyre smoke (soft alpha puffs drifting up and back)
        bool dust = (car.offroad || (car.onDirt && !car.airborne)) && car.speed > 400;
        const bool wallSparks = car.wallScrape > 0 && car.speed > 1500 * SPEED_SCALE;    // grinding along a tunnel wall
        bool smoke = !wallSparks && car.skid > 0.3 && (car.speed > 2000 * SPEED_SCALE || car.startBog > 0);   // wheelspin on the grid too
        if (car.splash > 0)
            drawSplash(cv, g_tyrePts, cx, by, bw, 1e9, race.time, unsigned(carIndex) * 101, std::min(1.0, car.splash / 0.25), 0);
        const bool drift = car.drifting && !car.airborne && !dust;
        if (drift)                                          // drifting: a shower of sparks instead of smoke
            drawDriftSparks(cv, g_tyrePts, cx, by, bw, 1e9, race.time, unsigned(carIndex) * 101, car.driftVis, 0);
        else if (dust || smoke)
            drawTyreSmoke(cv, dust ? gfx.dust : gfx.smoke, g_tyrePts, cx, by, bw, 1e9, race.time, unsigned(carIndex) * 101,
                          tint, TUNNEL_BLACK);
        if (wallSparks)
            drawWallSparks(cv, g_tyrePts, cx, by, bw, 1e9, race.time, unsigned(carIndex) * 101 + 7, car.wallSide,
                           std::min(1.0, car.wallScrape / 0.15));
        else if (car.bumpTimer > 0) {
            for (int i = 0; i < 5; ++i) {
                unsigned sd = opt.frame * 17 + i * 13;
                double px = cx + (frand(sd) - 0.5) * bw * 0.9;
                double py = by - bh * (0.6 + frand(sd + 3) * 0.6);
                double sz = 10 * s * (0.5 + frand(sd + 5));
                cv.blitScaled(gfx.spark, px - sz / 2, py - sz / 2, sz, sz);
            }
        }
    }

    // ---- ambient flair, in front of everything: confetti (Rio), gusts of leaves (autumn forest), rain
    {
        // camera-space particles: screen position of a point X (world units across), Y (height above the road
        // at the camera), Z (ahead of the camera)
        const double kz = depth * focal;
        auto scr = [&](double X, double Y, double Z, double& sx, double& sy) {
            sx = cx + X / Z * kz;
            sy = horizonY + (CAMERA_HEIGHT + camRise - Y) / Z * kz;
        };
        // confetti: a burst from the cannons that the car drives through - pieces shoot out across the road and
        // towards us, then flutter down slowly, turning over and over
        double age = race.time - car.confettiT;
        if (tr.stage == 7 && age >= 0 && age < 4.5) {
            static const Color cols[] = {rgb(255, 50, 140), rgb(0, 190, 255), rgb(255, 225, 0), rgb(150, 70, 255), rgb(0, 215, 120),
                                         rgb(255, 120, 30), rgb(255, 255, 255)};
            double zRelBase = car.confettiZ - (car.dist - camBack);
            for (int i = 0; i < 420; ++i) {
                double r0 = frand(i * 11 + 1), r1 = frand(i * 11 + 2), r2 = frand(i * 11 + 3), r3 = frand(i * 11 + 4), r4 = frand(i * 11 + 5);
                double side = (i & 1) ? 1 : -1;
                double delay = r4 * 0.35;                  // the cannons keep firing for a moment
                double a = age - delay;
                if (a < 0) continue;
                // launch from the cannon mouth (about 2400 high, just off the road), with drag towards a slow fall
                double k = 1.3, e = (1 - std::exp(-k * a)) / k;
                double vx = -side * (1200 + 5200 * r0), vz = -(2500 + 9000 * r1), vy = -600 + 1700 * r2;
                double X = side * 1.55 * ROAD_WIDTH + vx * e + std::sin(a * (3 + r3 * 4) + r0 * 6) * 140;
                double Zw = zRelBase + vz * e;
                double Y = 1500 + vy * e - 420 * a;
                if (Y < 0) continue;
                // follow the road through bends: offset by the road centre at that distance
                if (Zw < 380) continue;                  // fades out just before it reaches the camera
                int n = int(Zw / SEG_LEN);
                if (n < 1 || n >= DRAW_DIST - 1) continue;
                const Proj& pp = proj[n];
                double pct = Zw / SEG_LEN - n;
                double roadX = (pp.x1 + (pp.x2 - pp.x1) * pct - cx) / kz * Zw;   // road centre, in camera space
                double sx, sy;
                scr(X + roadX, Y + (tr.heightAt(car.dist - camBack + Zw) - groundAtCar), Zw, sx, sy);
                if (sy > pp.clip) continue;
                double sz = std::min(75 / Zw * kz, 13.0 * s);
                double spin = std::cos(a * (6 + 10 * r3) + r0 * 6.28);
                double w = std::max(1.0, sz * std::max(0.15, std::fabs(spin))), h = std::max(1.0, sz * 0.6);
                Color c = cols[i % 7];
                if (spin < 0) c = shade(c, 0.7);         // the back of the paper is darker
                double fade = std::min(1.0, (4.5 - age) / 1.0) * std::min(1.0, (Zw - 380) / 500);
                cv.rectBlend(int(sx - w / 2), int(sy - h / 2), int(std::ceil(w)), int(std::ceil(h)), c, 0.95 * fade);
            }
        }
        // autumn leaves: a gust shakes them from the old oaks as the car comes by - they swirl out across the road
        // and towards us, tumbling and fluttering, and sink to the ground, where they lie on the road for a while
        // (until the car has passed them, or they fade after a while)
        const double LEAF_LIFE = 16.0;
        if (tr.stage == 11 && age >= 0 && age < LEAF_LIFE) {
            double zRelBase = car.confettiZ - (car.dist - camBack);
            for (int i = 0; i < 200; ++i) {
                double r0 = frand(i * 13 + 1), r1 = frand(i * 13 + 2), r2 = frand(i * 13 + 3), r3 = frand(i * 13 + 4), r4 = frand(i * 13 + 5);
                double side = (i & 1) ? 1 : -1;
                double a = age - r4 * 0.6;               // the gust keeps shaking leaves loose for a moment
                if (a < 0) continue;
                const double k = 1.1;
                double vx = -side * (900 + 3800 * r0), vz = -(1000 + 4500 * r1), vy = 100 + 500 * r2;
                double Y0 = 1500 + 1300 * r3;
                auto E = [&](double t) { return (1 - std::exp(-k * t)) / k; };
                auto height = [&](double t) { return Y0 + vy * E(t) - 900 * t; };
                // when it lands (height falls through 0): found once by bisection, after which it lies still
                double lo = 0, hi = 12;
                for (int it = 0; it < 14; ++it) { double m = (lo + hi) / 2; (height(m) > 0 ? lo : hi) = m; }
                bool landed = a >= hi;
                double ta = landed ? hi : a;
                double e = E(ta);
                double X = side * (1.75 * ROAD_WIDTH + 600 * (r2 - 0.5)) + vx * e + std::sin(ta * (1.6 + r3 * 2) + r0 * 6) * 380;
                double Zw = zRelBase + vz * e + std::cos(ta * (1.3 + r2) + r1 * 6) * 250;
                double Y = landed ? 0 : height(ta);
                if (Zw < 380) continue;                  // passed: behind the camera
                int n = int(Zw / SEG_LEN);
                if (n < 1 || n >= DRAW_DIST - 1) continue;
                const Proj& pp = proj[n];
                double pct = Zw / SEG_LEN - n;
                double roadX = (pp.x1 + (pp.x2 - pp.x1) * pct - cx) / kz * Zw;
                double sx, sy;
                scr(X + roadX, Y + (tr.heightAt(car.dist - camBack + Zw) - groundAtCar), Zw, sx, sy);
                if (sy > pp.clip) continue;
                double sz = std::min(150 / Zw * kz, 26.0 * s);
                double fade = std::min(1.0, (LEAF_LIFE - age) / 2.0) * std::min(1.0, (Zw - 380) / 500);
                if (landed) {                            // lying flat on the road: seen at a low angle, no tumbling
                    double wv = sz * (0.7 + 0.3 * r0), hv = sz * 0.3;
                    cv.blitScaled(gfx.leaf[r2 < 0.55 ? 0 : 1], sx - wv / 2, sy - hv, wv, hv, r1 < 0.5, pp.clip, fogAt(th, Zw) * 0.6,
                                  th.fogColor, fade);
                } else {
                    double flip = std::cos(a * (3 + 5 * r3) + r0 * 6.28);      // tumbling: it turns edge-on
                    double wv = sz * std::max(0.2, std::fabs(flip));
                    cv.blitScaled(gfx.leaf[r2 < 0.55 ? 0 : 1], sx - wv / 2, sy - sz / 2, wv, sz, flip < 0, 1e9, fogAt(th, Zw) * 0.6,
                                  th.fogColor, fade);
                }
            }
        }
        // rain: long, slanted streaks falling past the camera, and drops bursting on the road ahead
        if (th.rain && !car.inTunnel) {
            const double T = race.time + carIndex * 3.1;
            const int NR = 170;
            for (int i = 0; i < NR; ++i) {
                double r0 = frand(i * 7 + 101), r1 = frand(i * 7 + 102), r2 = frand(i * 7 + 103);
                double sp = 1.4 + 0.9 * r1;                              // screen heights per second
                double ph = std::fmod(T * sp + r0, 1.0);
                double len = (14 + 22 * r2) * s, x0 = vp.x + std::fmod(r2 * 1.7 + r0 * 0.3 + T * 0.02, 1.0) * (vp.w + 80 * s) - 40 * s;
                double y0 = vp.y - len + ph * (vp.h + len);
                double slant = 0.18 + 0.04 * std::sin(T * 0.5);         // blown a little by the wind
                Color c = r1 < 0.2 ? rgb(230, 200, 160) : rgb(176, 186, 204);   // some catch the street light
                double a = 0.18 + 0.22 * r2;
                if (y0 + len < cv.clip.y || y0 > cv.clip.y + cv.clip.h) continue;   // not in this strip
                // four short pieces, stepping sideways: a slanted streak, brighter at its lower end
                const int pieces = 4;
                double pl = len / pieces;
                for (int k = 0; k < pieces; ++k) {
                    double f = (k + 0.5) / pieces;
                    cv.rectBlend(int(x0 - slant * len * f), int(y0 + pl * k), std::max(1, int(s * 0.8)), int(std::ceil(pl)), c,
                                 a * (0.4 + 0.6 * f));
                }
            }
            // drops bursting on the road: little splashes below the horizon, nearer ones bigger
            for (int i = 0; i < 60; ++i) {
                double r0 = frand(i * 5 + 501), r1 = frand(i * 5 + 502);
                double life = 0.25, ph = std::fmod(T / life + r0, 1.0);
                unsigned cyc = unsigned(std::floor(T / life + r0));
                double u = frand(cyc * 17 + i * 3 + 7), v = frand(cyc * 31 + i * 5 + 11);
                double yy = horizonY + (vp.y + vp.h - horizonY) * (0.08 + 0.92 * v * v);
                if (yy >= vp.y + vp.h) continue;
                double xx = vp.x + u * vp.w, sz = (1 + 5 * v * v) * s * (0.5 + ph);
                double a = 0.35 * (1 - ph) * (0.4 + r1 * 0.6);
                cv.rectBlend(int(xx - sz), int(yy), std::max(1, int(2 * sz)), std::max(1, int(s * 0.8)), rgb(190, 200, 215), a);
                cv.rectBlend(int(xx - 1), int(yy - sz * 1.2), std::max(1, int(s)), std::max(1, int(sz)), rgb(190, 200, 215), a * 0.7);
            }
        }
    }

    if (opt.hud && car.player >= 0) drawHud(cv, vp, race, carIndex, opt.frame);
    cv.resetClip();
}

// ---------------------------------------------------------- parallel render ----
// The view is split into horizontal strips that are rendered at the same time: every thread runs the
// whole scene, clipped to its strip, on its own canvas; the strips are then copied together. The HUD
// (which queues text) is drawn once afterwards on the main thread.
namespace {
class RenderPool {
public:
    RenderPool() {
        unsigned hw = std::thread::hardware_concurrency();
        int n = int(std::clamp(hw ? hw : 1u, 1u, 4u));
        if (const char* e = std::getenv("TURBO_THREADS")) n = std::clamp(std::atoi(e), 1, 8);   // debug / tuning
#ifdef __SWITCH__
        n = 3;                                   // games get cores 0-2: the main thread + one worker on 1 and 2
#endif
        canvases_.resize(size_t(n - 1));
        for (int i = 0; i < n - 1; ++i) {
            canvases_[size_t(i)] = std::make_unique<Canvas>();
            threads_.emplace_back([this, i] { loop(i); });
        }
    }
    ~RenderPool() {
        { std::lock_guard<std::mutex> l(m_); quit_ = true; ++gen_; }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
    }
    int strips() const { return int(threads_.size()) + 1; }
    Canvas& canvas(int i) { return *canvases_[size_t(i)]; }
    // run job(k) for k = 1..strips()-1 on the workers while the caller does strip 0, then wait
    void run(const std::function<void(int)>& job) {
        {
            std::lock_guard<std::mutex> l(m_);
            job_ = &job;
            pending_ = int(threads_.size());
            ++gen_;
        }
        cv_.notify_all();
        job(0);
        std::unique_lock<std::mutex> l(m_);
        done_.wait(l, [this] { return pending_ == 0; });
        job_ = nullptr;
    }

private:
    void loop(int i) {
#ifdef __SWITCH__
        int core = 1 + i % 2;                    // spread the workers over the free cores
        svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, 1u << core);
#endif
        unsigned seen = 0;
        for (;;) {
            const std::function<void(int)>* job;
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [&] { return gen_ != seen; });
                seen = gen_;
                if (quit_) return;
                job = job_;
            }
            (*job)(i + 1);
            {
                std::lock_guard<std::mutex> l(m_);
                if (--pending_ == 0) done_.notify_one();
            }
        }
    }
    std::vector<std::thread> threads_;
    std::vector<std::unique_ptr<Canvas>> canvases_;
    std::mutex m_;
    std::condition_variable cv_, done_;
    const std::function<void(int)>* job_ = nullptr;
    unsigned gen_ = 0;
    int pending_ = 0;
    bool quit_ = false;
};
RenderPool& pool() { static RenderPool p; return p; }
} // namespace

// Strip boundaries are moved every frame so that all threads take about as long: the rows near the horizon
// (scenery, cars) cost far more than sky or near road. Each strip's time in the last frame gives its cost per
// row; the new boundaries split the summed cost evenly (half-way, to stay steady).
namespace {
struct StripPlan { std::vector<double> b; };                 // boundaries, 0..1 of the view height
std::map<long long, StripPlan> stripPlans;                   // per viewport (split screen has two)

void rebalance(StripPlan& P, const std::vector<double>& ms) {
    const int n = int(ms.size());
    double total = 0;
    for (double t : ms) total += t;
    if (total <= 0) return;
    std::vector<double> nb(size_t(n) + 1);
    nb[0] = 0; nb[size_t(n)] = 1;
    int k = 0;
    double acc = 0;                                          // cost of the strips before strip k
    for (int j = 1; j < n; ++j) {
        double target = total * j / n;
        while (k < n - 1 && acc + ms[size_t(k)] < target) acc += ms[size_t(k++)];
        double h = P.b[size_t(k) + 1] - P.b[size_t(k)];
        double frac = ms[size_t(k)] > 0 ? (target - acc) / ms[size_t(k)] : 0.5;
        nb[size_t(j)] = P.b[size_t(k)] + h * std::clamp(frac, 0.0, 1.0);
    }
    const double minH = 0.04;                                // keep every strip a few rows high
    for (int j = 1; j < n; ++j) {
        double v = 0.5 * P.b[size_t(j)] + 0.5 * nb[size_t(j)];
        P.b[size_t(j)] = std::clamp(v, P.b[size_t(j) - 1] + minH, 1 - minH * (n - j));
    }
}
} // namespace

void renderViewParallel(Canvas& cv, const Rect& vp, const Race& race, int carIndex, const ViewOptions& opt) {
    RenderPool& P = pool();
    const int n = P.strips();
    if (n <= 1) { renderView(cv, vp, race, carIndex, opt); return; }
    ViewOptions o = opt;
    o.hud = false;
    const bool hud = opt.hud && race.cars[carIndex].player >= 0;
    StripPlan& plan = stripPlans[(long long)vp.y << 32 | vp.h];
    if (plan.b.size() != size_t(n) + 1) {
        plan.b.resize(size_t(n) + 1);
        for (int k = 0; k <= n; ++k) plan.b[size_t(k)] = double(k) / n;
    }
    auto stripY = [&](int k) { return vp.y + int(std::lround(vp.h * plan.b[size_t(k)])); };
    std::vector<double> ms(size_t(n), 0.0);
    std::function<void(int)> job = [&](int k) {
        auto t0 = std::chrono::steady_clock::now();
        Canvas& c = k == 0 ? cv : P.canvas(k - 1);
        int y0 = stripY(k), y1 = stripY(k + 1);
        c.band = {vp.x, y0, vp.w, y1 - y0};
        renderView(c, vp, race, carIndex, o);
        if (hud) {                                          // the HUD's panels per strip; its text once (strip 0)
            textMuted = k != 0;
            c.setClip(vp);
            drawHud(c, vp, race, carIndex, opt.frame);
            textMuted = false;
        }
        c.band = {0, 0, c.w, c.h};
        c.resetClip();
        if (k > 0)
            for (int y = y0; y < y1; ++y)
                std::copy_n(&c.px[size_t(y) * c.w + vp.x], vp.w, &cv.px[size_t(y) * cv.w + vp.x]);
        ms[size_t(k)] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    };
    P.run(job);
    rebalance(plan, ms);
}
