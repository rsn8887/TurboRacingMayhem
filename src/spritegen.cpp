// Procedural scenery, effects and background generator (asset tool only)
#include "sprites.h"
#include "cars.h"
#include "painter.h"
#include "text.h"
#include <cmath>
#include <random>
#include <algorithm>
#include <array>
#include <functional>

namespace {
constexpr int SS = 4;
std::mt19937 rng(4242);
double rnd(double a, double b) { return std::uniform_real_distribution<double>(a, b)(rng); }
const Color WHITE = rgb(255, 255, 255), BLACK = rgb(0, 0, 0);
Color alpha(Color c, double a) { return withAlpha(c, int(a * 255)); }

// TTF text rendered at supersampled resolution, for baking into sprites
Bitmap ttf(const std::string& s, FontId f, double size, Color c, Color c2 = 0, Color outline = 0, double outlinePx = 0) {
    return text.bitmap(s, f, int(size * SS), c, c2, outline, int(outlinePx * SS));
}

// soft radial glow (alpha falloff) added on top of whatever is there
void glow(Painter& p, double cx, double cy, double rx, double ry, Color c, double strength) {
    p.ellipse(cx, cy, rx, ry, Paint([=](double x, double y) {
        double dx = (x - cx) / rx, dy = (y - cy) / ry;
        double d = std::max(0.0, 1 - std::sqrt(dx * dx + dy * dy));
        return alpha(c, std::min(1.0, d * d * strength));
    }));
}

// ------------------------------------------------------------------ cars ----
// -------------------------------------------------------------- scenery -----
Bitmap makePalm() {
    Painter p(130, 172, SS, RENDER_SCALE, 2, 2);   // wide enough for the longest fronds, plus margins for the outline
    // trunk: curved, cylindrically shaded, with ring notches
    auto cx = [](double t) { return 61 + 16 * t * t; };
    auto hw = [](double t) { return 6.0 - 2.6 * t; };
    std::vector<double> poly;
    for (int i = 0; i <= 30; ++i) { double t = i / 30.0; poly.push_back(cx(t) - hw(t)); poly.push_back(172 - t * 130); }
    for (int i = 30; i >= 0; --i) { double t = i / 30.0; poly.push_back(cx(t) + hw(t)); poly.push_back(172 - t * 130); }
    p.poly(poly, Paint([&](double x, double y) {
        double t = (172 - y) / 130;
        double nx = std::clamp((x - cx(t)) / hw(t), -0.98, 0.98);
        Color c = lit(rgb(156, 108, 62), nx, -0.1, std::sqrt(1 - nx * nx), 0.4, 0.2, 10);
        double ring = std::fmod(y + 2 * nx + 200, 7.0);
        if (ring < 1.6) c = shade(c, 0.62);
        else if (ring < 2.6) c = shade(c, 1.12);
        return c;
    }));
    double tx = cx(1), ty = 42;
    // fronds: spine + leaflets, back ones darker
    struct Fr { double ang, len; bool back; };
    std::vector<Fr> fr = {{-150, 36, true}, {-115, 30, true}, {-60, 32, true}, {-25, 38, true},
                          {-175, 44, false}, {-140, 40, false}, {-95, 26, false}, {-45, 40, false}, {-5, 46, false},
                          {160, 34, false}, {25, 34, false}};
    for (const Fr& f : fr) {
        double rad = f.ang * M_PI / 180;
        auto pt = [&](double t, double& x, double& y) {
            x = tx + std::cos(rad) * f.len * t;
            y = ty + std::sin(rad) * f.len * t * 0.55 + t * t * 30;
        };
        Color top = f.back ? rgb(40, 120, 40) : rgb(80, 185, 60), bot = f.back ? rgb(20, 80, 30) : rgb(30, 120, 40);
        for (double t = 0.08; t <= 1.0; t += 0.035) {
            double x, y, x2, y2;
            pt(t, x, y); pt(t + 0.03, x2, y2);
            double dx = x2 - x, dy = y2 - y, l = std::sqrt(dx * dx + dy * dy) + 1e-9;
            double nx = -dy / l, ny = dx / l;
            double len = 3 + 10 * std::sin(t * M_PI) * (0.8 + 0.2 * hashNoise(t * 50, f.ang));
            for (int s = -1; s <= 1; s += 2) {
                double ex = x + (nx * s * 0.7 + dx / l * 0.5) * len, ey = y + (ny * s * 0.7 + 0.55) * len;
                p.line(x, y, ex, ey, 1.5, s < 0 ? top : bot);
            }
        }
        for (double t = 0; t < 1.0; t += 0.05) {
            double x, y, x2, y2;
            pt(t, x, y); pt(t + 0.05, x2, y2);
            p.line(x, y, x2, y2, 1.8 - t, rgb(110, 150, 50));
        }
    }
    for (auto c : {std::pair<double, double>{tx - 3, ty + 3}, {tx + 3, ty + 4}, {tx, ty + 6}})
        p.ellipse(c.first, c.second, 3.2, 3.2, sphere(c.first, c.second, 3.2, 3.2, rgb(120, 76, 30), 0.4, 0.5, 16));
    return p.finish(rgb(18, 40, 18), 1.0);   // outline; the canvas is wide enough that it is never cut off
}

Bitmap makePine(bool snow) {
    Painter p(80, 160, SS, RENDER_SCALE, 3, 2);   // margins: the lowest branch tips and the outline reach past 0..80
    p.rect(35.5, 124, 9, 36, cylX(35.5, 44.5, rgb(110, 72, 40)));
    for (int i = 0; i < 5; ++i) {
        double top = 2 + i * 22, bottom = top + 44, half = 11 + i * 7.5;
        std::vector<double> pts = {40, top, 40 + half, bottom - 3};
        int teeth = 4 + i;
        for (int k = 0; k <= teeth * 2; ++k) {
            double x = 40 + half - (2 * half) * k / (teeth * 2.0);
            pts.push_back(x); pts.push_back(k % 2 ? bottom + 2 : bottom - 3);
        }
        pts.push_back(40 - half); pts.push_back(bottom - 3);
        p.poly(pts, Paint([=](double x, double y) {
            double wAt = half * std::clamp((y - top) / (bottom - top), 0.05, 1.0);
            double nx = std::clamp((x - 40) / wAt, -0.98, 0.98);
            Color c = lit(rgb(34, 120, 52), nx, -0.25, std::sqrt(1 - nx * nx), 0.38, 0.12, 8);
            double n = hashNoise(x * 1.7, y * 2.3);
            if (n > 0.85) c = shade(c, 1.25); else if (n < 0.12) c = shade(c, 0.75);
            return c;
        }));
        if (snow) {
            std::vector<double> cap = {40, top};
            double capB = top + (bottom - top) * 0.45;
            for (int k = 0; k <= 8; ++k) {
                double t = k / 8.0;
                double x = 40 + half * 0.55 * (1 - 2 * t);
                cap.push_back(x); cap.push_back(capB + std::sin(t * 9 + i) * 2.5 - std::fabs(1 - 2 * t) * 3);
            }
            p.poly(cap, Paint([=](double x, double y) {
                double wAt = half * 0.55 * std::clamp((y - top) / (capB - top), 0.05, 1.0);
                double nx = std::clamp((x - 40) / wAt, -0.98, 0.98);
                return lit(rgb(246, 250, 255), nx, -0.6, std::sqrt(1 - nx * nx), 0.62, 0.25, 12);
            }));
            p.rect(40 - half + 3, bottom - 2, 2 * half - 6, 1.6, rgb(230, 238, 252));
        }
    }
    return p.finish(rgb(10, 36, 22), 1.0);
}

Bitmap makeBush() {
    Painter p(96, 48, SS, RENDER_SCALE, 2, 2);   // margins keep the outline from being cut off
    std::vector<std::array<double, 3>> balls;
    for (int i = 0; i < 16; ++i) balls.push_back({rnd(16, 80), rnd(20, 36), rnd(9, 16)});
    std::sort(balls.begin(), balls.end(), [](auto& a, auto& b) { return a[1] < b[1]; });
    for (auto& b : balls) {
        Color base = rgb(40 + int(rnd(0, 30)), 130 + int(rnd(0, 40)), 50);
        p.ellipse(b[0], b[1], b[2], b[2] * 0.85, Paint([=](double x, double y) {
            Color c = sphere(b[0], b[1], b[2], b[2] * 0.85, base, 0.38, 0.15, 8)(x, y);
            double n = hashNoise(x * 2, y * 2);
            return n > 0.8 ? shade(c, 1.2) : n < 0.15 ? shade(c, 0.8) : c;
        }));
    }
    p.rect(0, 46, 96, 2, TRANSPARENT);
    for (int i = 0; i < 8; ++i) {   // a few flowers
        double x = rnd(18, 78), y = rnd(14, 34);
        p.ellipse(x, y, 1.3, 1.3, rnd(0, 1) < 0.5 ? rgb(255, 120, 160) : rgb(255, 240, 120));
    }
    Bitmap b = p.finish(rgb(12, 50, 18), 1.0);
    // (+2: the painter's top margin)
    for (int y = (46 + 2) * RENDER_SCALE; y < b.h; ++y) for (int x = 0; x < b.w; ++x) b.px[size_t(y) * b.w + x] = TRANSPARENT;
    b.buildMips();
    return b;
}

Bitmap makeCactus() {
    Painter p(64, 120, SS, RENDER_SCALE, 2, 3);   // margins: room for the flower and the outline on top
    Color g = rgb(56, 156, 76);
    auto column = [&](double x, double y, double w, double h) {
        Paint ribbed([=](double px_, double py) {
            double nx = std::clamp(2 * (px_ - x) / w - 1, -0.98, 0.98);
            Color c = lit(g, nx, 0, std::sqrt(1 - nx * nx), 0.4, 0.35, 16);
            double rib = std::fmod((nx + 1) * 2.5, 1.0);
            if (rib < 0.12) c = shade(c, 0.72);
            if (hashNoise(px_ * 3, py * 1.5) > 0.96) c = rgb(250, 245, 210);   // spines
            return c;
        });
        p.rect(x, y + w / 2, w, h - w / 2, ribbed);
        p.ellipse(x + w / 2, y + w / 2, w / 2, w / 2, Paint([=](double px_, double py) {
            return sphere(x + w / 2, y + w / 2, w / 2, w / 2, g, 0.4, 0.35, 16)(px_, py);
        }));
    };
    column(25, 0, 15, 120);
    column(6, 26, 11, 38);
    p.rect(11, 57, 16, 9, cylY(57, 66, g));
    column(47, 40, 11, 32);
    p.rect(37, 64, 14, 9, cylY(64, 73, g));
    p.ellipse(32.5, 3, 3, 2, rgb(255, 120, 170));      // a flower on top
    return p.finish(rgb(16, 56, 26), 1.0);
}

Bitmap makeRock() {
    Painter p(96, 56, SS, RENDER_SCALE, 2, 2);   // margins keep the outline from being cut off
    std::vector<double> pts = {4, 56, 9, 30, 22, 13, 44, 4, 66, 6, 84, 18, 93, 38, 92, 56};
    p.poly(pts, Paint([](double x, double y) {
        double nx = (x - 48) / 46, ny = (y - 34) / 32;
        Color c = lit(rgb(150, 138, 124), nx, ny, std::sqrt(std::max(0.05, 1 - nx * nx - ny * ny)), 0.4, 0.1, 6);
        double n = hashNoise(x * 3.1, y * 2.7);
        c = shade(c, 0.88 + 0.24 * n);
        return c;
    }));
    // facets / cracks
    p.line(44, 4, 50, 24, 1.0, alpha(rgb(60, 50, 44), 0.7));
    p.line(50, 24, 84, 18, 1.0, alpha(rgb(60, 50, 44), 0.6));
    p.line(50, 24, 40, 56, 1.0, alpha(rgb(60, 50, 44), 0.5));
    p.line(22, 13, 50, 24, 0.8, alpha(rgb(220, 210, 190), 0.6));
    return p.finish(rgb(40, 32, 28), 1.0);
}

Bitmap makeLamp() {
    Painter p(64, 200, SS, RENDER_SCALE, 6, 2);   // margins keep the outline from being cut off
    Color metal = rgb(150, 156, 170);
    p.rect(8, 22, 5, 168, cylX(8, 13, metal, 0.35, 0.7, 30));
    p.roundRect(4.5, 184, 12, 16, 2, cylX(4.5, 16.5, shade(metal, 0.7), 0.35, 0.6, 24));
    for (double t = 0; t < 1; t += 0.05) {                         // curved arm
        double a0 = M_PI * (1 - t * 0.5), a1 = M_PI * (1 - (t + 0.05) * 0.5);
        p.line(30 + 20 * std::cos(a0), 30 - 18 * std::sin(a0), 30 + 20 * std::cos(a1), 30 - 18 * std::sin(a1), 3.2, metal);
    }
    p.line(30, 12, 50, 12, 3.2, metal);
    p.roundRect(40, 9, 22, 7, 3, cylY(9, 16, rgb(70, 72, 84), 0.35, 0.7, 30));
    p.roundRect(42, 15, 18, 3, 1.5, rgb(255, 250, 200));
    glow(p, 51, 20, 16, 10, rgb(255, 240, 160), 1.2);
    return p.finish(rgb(18, 18, 24), 1.0);
}

Bitmap makeBillboard(int kind) {
    Painter p(160, 106, SS, RENDER_SCALE, 5, 2);   // margins keep the outline from being cut off
    Color metal = rgb(150, 150, 162);
    p.rect(25, 60, 9, 46, cylX(25, 34, metal, 0.35, 0.6, 24));
    p.rect(126, 60, 9, 46, cylX(126, 135, metal, 0.35, 0.6, 24));
    p.roundRect(0, 0, 160, 68, 3, vgrad(0, 68, rgb(245, 245, 250), rgb(150, 150, 165)));
    Color bg0 = kind == 0 ? rgb(230, 30, 50) : kind == 1 ? rgb(40, 100, 230) : rgb(255, 220, 40);
    Color bg1 = kind == 0 ? rgb(120, 0, 20) : kind == 1 ? rgb(10, 30, 110) : rgb(230, 130, 0);
    p.rect(4, 4, 152, 60, vgrad(4, 64, bg0, bg1));
    if (kind == 2)      // diagonal shine stripes, kept inside the board face
        for (int i = 0; i < 6; ++i)
            p.poly({-10.0 + i * 30, 64, 6.0 + i * 30, 64, 26.0 + i * 30, 4, 10.0 + i * 30, 4},
                   Paint([](double x, double) { return x >= 4 && x <= 156 ? alpha(WHITE, 0.12) : TRANSPARENT; }));
    Color ol = rgb(10, 10, 20);
    if (kind == 0) {
        p.imageCentered(ttf("TURBO", FONT_DISPLAY, 21, rgb(255, 255, 255), rgb(200, 210, 255), ol, 1.2), 80, 20);
        p.imageCentered(ttf("RACING", FONT_DISPLAY, 21, rgb(255, 240, 110), rgb(255, 150, 20), ol, 1.2), 80, 44);
    } else if (kind == 1) {
        p.imageCentered(ttf("SDL2", FONT_DISPLAY, 26, WHITE, rgb(150, 210, 255), ol, 1.2), 80, 22);
        p.imageCentered(ttf("POWERED", FONT_UI, 17, WHITE, rgb(190, 220, 255), ol, 1.0), 80, 47);
    } else {
        p.imageCentered(ttf("TURBO", FONT_DISPLAY, 30, rgb(40, 40, 50), rgb(0, 0, 0), WHITE, 1.2), 80, 28);
        p.imageCentered(ttf("> > > > >", FONT_UI, 14, rgb(220, 20, 20), 0, 0, 0), 80, 52);
    }
    p.rect(4, 4, 152, 10, alpha(WHITE, 0.16));
    return p.finish(rgb(16, 16, 22), 1.0);
}

Bitmap makeBuilding(int kind) {
    // night-city block: ledges every few floors, windows lit floor by floor (some dark, some with blinds or a
    // flicker of TV blue), lit windows on the shaded side too, rooftop water tank, aircraft light, a lit lobby
    // and its sign (NEON or HOTEL). Only hashed noise is used: no random numbers are drawn.
    int w = kind == 0 ? 92 : 124, h = kind == 0 ? 224 : 172;
    Painter p(w, h, SS, RENDER_SCALE, 2, 16);   // margins keep the outline and rooftop bits from being cut off
    Color wall0 = kind == 0 ? rgb(74, 74, 126) : rgb(104, 58, 104), wall1 = kind == 0 ? rgb(30, 30, 62) : rgb(50, 26, 56);
    double side = w * 0.18, fw = w - side;
    p.rect(0, 12, fw, h - 12, Paint([=](double x, double y) {                // front face, with subtle panel lines
        Color c = lerpColor(wall0, wall1, (y - 12) / (h - 12));
        if (std::fmod(x, 11.0) < 0.5) c = shade(c, 0.9);
        return c;
    }));
    p.poly({fw, 12, double(w), 4, double(w), double(h), fw, double(h)}, vgrad(4, h, shade(wall0, 0.58), shade(wall1, 0.58)));   // side face
    p.poly({0, 12, side, 4, double(w), 4, fw, 12}, shade(wall0, 1.3));         // roof
    // windows on the front: floors lit or dark as a whole, a few exceptions
    for (double y = 22; y < h - 22; y += 11) {
        double fl = hashNoise(y * 0.5 + kind * 13, 1.7);
        bool floorLit = fl < 0.6;
        for (double x = 7; x < fw - 8; x += 11) {
            double r = hashNoise(x * 7 + kind, y * 3);
            bool on = floorLit ? r < 0.82 : r < 0.12;
            if (on) {
                Color c = r < 0.55 ? rgb(255, 214, 110) : r < 0.75 ? rgb(255, 236, 190) : r < 0.82 ? rgb(140, 220, 255) : rgb(120, 160, 255);
                p.rect(x, y, 6, 7, vgrad(y, y + 7, c, shade(c, 0.72)));
                if (hashNoise(x, y * 1.3) < 0.3) p.rect(x, y, 6, 2.5, shade(c, 0.6));   // half-drawn blind
                glow(p, x + 3, y + 3.5, 6, 6, c, 0.3);
            } else p.rect(x, y, 6, 7, vgrad(y, y + 7, rgb(30, 28, 52), rgb(18, 16, 34)));
            p.rect(x - 0.5, y + 7, 7, 1, shade(wall0, 1.2));                         // sill
        }
        if (int(std::lround((y - 22) / 11)) % 5 == 4) p.rect(-1, y + 9, fw + 1, 1.6, shade(wall0, 1.45));   // ledge
    }
    // a column of windows on the shaded side face
    for (double y = 20; y < h - 22; y += 11) {
        double r = hashNoise(y, 5.0 + kind);
        double x0 = fw + side * 0.3;
        p.poly({x0, y + 1, x0 + side * 0.35, y - 0.5, x0 + side * 0.35, y + 6, x0, y + 7.5},
               r < 0.45 ? Paint(rgb(200, 160, 90)) : Paint(rgb(20, 18, 36)));
    }
    // rooftop: water tank on legs, an antenna with a red light
    double tx = fw * 0.62;
    p.rect(tx, 0, 12, 9, cylX(tx, tx + 12, rgb(96, 80, 96), 0.4, 0.2, 8));
    p.poly({tx - 1, 0.5, tx + 6, -4, tx + 13, 0.5}, rgb(70, 60, 76));
    for (double lx : {tx + 1.0, tx + 10.0}) p.rect(lx, 9, 1.2, 3, rgb(60, 56, 70));
    p.rect(w * 0.3, -8, 2, 16, rgb(150, 150, 170));
    p.ellipse(w * 0.3 + 1, -8.5, 1.6, 1.6, rgb(255, 40, 40));
    glow(p, w * 0.3 + 1, -8.5, 6, 6, rgb(255, 40, 40), 0.9);
    // lobby
    p.rect(0, h - 16, fw, 16, rgb(16, 14, 28));
    p.rect(fw * 0.3, h - 13, fw * 0.4, 13, vgrad(h - 13, h, rgb(255, 230, 170), rgb(220, 160, 90)));
    p.rect(fw * 0.25, h - 17, fw * 0.5, 3, kind == 0 ? rgb(60, 200, 255) : rgb(255, 80, 200));
    glow(p, fw / 2, h - 8, fw * 0.4, 10, rgb(255, 210, 140), 0.5);
    if (kind == 1) {
        p.roundRect(10, 26, fw - 20, 18, 3, vgrad(26, 44, rgb(255, 80, 200), rgb(180, 0, 120)));
        glow(p, fw / 2, 35, fw / 2 + 6, 16, rgb(255, 80, 200), 0.6);
        p.imageCentered(ttf("NEON", FONT_DISPLAY, 13, WHITE, rgb(255, 210, 255), rgb(120, 0, 80), 0.8), fw / 2, 35);
    } else {
        p.imageCentered(ttf("HOTEL", FONT_DISPLAY, 11, rgb(140, 255, 255), rgb(40, 180, 255), rgb(0, 30, 60), 0.8), fw / 2, 17);
    }
    return p.finish(rgb(8, 8, 20), 1.0);
}

Bitmap makeSnowman() {
    Painter p(60, 86, SS, RENDER_SCALE, 2, 2);   // margins keep the outline from being cut off
    Color s = rgb(248, 250, 255);
    p.ellipse(30, 64, 21, 20, sphere(30, 64, 21, 20, s, 0.62, 0.2, 10));
    p.ellipse(30, 35, 15, 14, sphere(30, 35, 15, 14, s, 0.62, 0.2, 10));
    p.ellipse(30, 14, 11, 10, sphere(30, 14, 11, 10, s, 0.62, 0.2, 10));
    p.rect(21, 0, 18, 7, cylX(21, 39, rgb(40, 40, 46), 0.4, 0.5, 20));
    p.rect(16, 6, 28, 2.5, rgb(30, 30, 36));
    p.ellipse(26, 12, 1.4, 1.4, BLACK); p.ellipse(34, 12, 1.4, 1.4, BLACK);
    p.poly({30, 14, 41, 16.5, 30, 17.5}, vgrad(14, 17.5, rgb(255, 160, 40), rgb(220, 90, 0)));
    p.roundRect(18, 22, 24, 5, 2, cylY(22, 27, rgb(220, 30, 40)));
    p.rect(34, 26, 5, 11, cylX(34, 39, rgb(220, 30, 40)));
    for (double y : {33.0, 40.0, 47.0}) p.ellipse(30, y, 1.6, 1.6, rgb(30, 30, 30));
    return p.finish(rgb(110, 120, 160), 1.0);
}

Bitmap makeTurnSign() {   // chevrons pointing right
    Painter p(96, 82, SS, RENDER_SCALE, 2, 2);   // margins keep the outline from being cut off
    Color metal = rgb(150, 150, 162);
    p.rect(19, 48, 6, 34, cylX(19, 25, metal, 0.35, 0.6, 24));
    p.rect(71, 48, 6, 34, cylX(71, 77, metal, 0.35, 0.6, 24));
    p.roundRect(0, 0, 96, 53, 3, rgb(20, 20, 24));
    p.rect(3, 3, 90, 47, vgrad(3, 50, rgb(255, 230, 60), rgb(240, 160, 0)));
    for (int i = 0; i < 3; ++i) {
        double x = 11 + i * 26;
        p.poly({x, 8, x + 12, 8, x + 26, 26.5, x + 12, 45, x, 45, x + 14, 26.5}, vgrad(8, 45, rgb(40, 40, 48), rgb(5, 5, 8)));
        p.poly({x, 8, x + 12, 8, x + 13, 9.5, x + 1.5, 9.5}, alpha(WHITE, 0.3));
    }
    p.rect(3, 3, 90, 8, alpha(WHITE, 0.2));
    return p.finish(rgb(10, 10, 12), 1.0);
}

// warning signs: 0 = jump, 1 = road narrows, 2 = tunnel
Bitmap makeWarningSign(int kind) {
    Painter p(96, 86, SS, RENDER_SCALE, 2, 2);   // margins keep the outline from being cut off
    Color metal = rgb(150, 150, 162);
    p.rect(19, 52, 6, 34, cylX(19, 25, metal, 0.35, 0.6, 24));
    p.rect(71, 52, 6, 34, cylX(71, 77, metal, 0.35, 0.6, 24));
    Color bg0 = kind == 2 ? rgb(60, 140, 255) : kind == 3 ? rgb(255, 190, 70) : rgb(255, 230, 60);
    Color bg1 = kind == 2 ? rgb(10, 50, 160) : kind == 3 ? rgb(200, 100, 20) : rgb(240, 150, 0);
    Color ink = kind == 2 ? WHITE : rgb(20, 20, 26);
    p.roundRect(0, 0, 96, 57, 4, rgb(20, 20, 24));
    p.roundRect(3, 3, 90, 51, 3, vgrad(3, 54, bg0, bg1));
    const char* word = kind == 0 ? "JUMP!" : kind == 1 ? "NARROW" : kind == 2 ? "TUNNEL" : "DIRT";
    p.imageCentered(ttf(word, FONT_DISPLAY, kind == 0 ? 17 : 14, ink, ink, 0, 0), 48, 14);
    if (kind == 0) {            // ramp with a flying car
        p.poly({14, 50, 50, 50, 50, 38}, ink);
        p.line(54, 36, 80, 30, 1.2, ink);
        p.roundRect(58, 24, 18, 7, 2, ink);
        p.ellipse(62, 32, 2.4, 2.4, ink); p.ellipse(72, 31, 2.4, 2.4, ink);
    } else if (kind == 1) {     // converging road edges
        p.line(22, 51, 40, 30, 3, ink); p.line(74, 51, 56, 30, 3, ink);
        p.line(40, 30, 40, 26, 3, ink); p.line(56, 30, 56, 26, 3, ink);
    } else if (kind == 3) {     // bumpy road with flying stones
        for (double x = 14; x < 82; x += 2)
            p.line(x, 46 + 3 * std::sin(x * 0.35), x + 2, 46 + 3 * std::sin((x + 2) * 0.35), 2.6, ink);
        for (auto q : {std::pair<double, double>{30, 36}, {44, 31}, {58, 35}, {70, 30}}) p.ellipse(q.first, q.second, 2.2, 1.8, ink);
    } else {                    // tunnel arch
        p.ellipse(48, 46, 22, 18, ink);
        p.rect(26, 46, 44, 6, ink);
        p.ellipse(48, 46, 15, 12, bg1);
        p.rect(33, 46, 30, 6, bg1);
        p.poly({44, 52, 52, 52, 49.5, 38, 46.5, 38}, ink);
    }
    p.rect(3, 3, 90, 8, alpha(WHITE, 0.2));
    return p.finish(rgb(10, 10, 12), 1.0);
}

Bitmap makeGantry(bool start) {
    Painter p(320, 130, SS, RENDER_SCALE, 2, 2);   // margins keep the outline from being cut off
    Color metal = rgb(140, 146, 162);
    for (double px_ : {0.0, 304.0}) {                               // truss towers
        p.rect(px_, 0, 3.5, 130, cylX(px_, px_ + 3.5, metal, 0.35, 0.6, 24));
        p.rect(px_ + 12.5, 0, 3.5, 130, cylX(px_ + 12.5, px_ + 16, metal, 0.35, 0.6, 24));
        for (double y = 40; y < 130; y += 12) {
            p.line(px_ + 2, y, px_ + 14, y + 12, 1.4, shade(metal, 0.8));
            p.line(px_ + 14, y, px_ + 2, y + 12, 1.4, shade(metal, 0.65));
        }
    }
    if (start) {
        p.rect(14, 3, 292, 36, rgb(20, 20, 24));
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 36; ++x) {
                bool w = (x + y) & 1;
                double xx = 16 + x * 8, yy = 5 + y * 8;
                p.rect(xx, yy, 8, 8, vgrad(yy, yy + 8, w ? rgb(255, 255, 255) : rgb(40, 40, 46), w ? rgb(190, 190, 200) : rgb(10, 10, 14)));
            }
        p.roundRect(104, 1, 112, 40, 4, vgrad(1, 41, rgb(240, 40, 50), rgb(130, 0, 20)));
        p.imageCentered(ttf("START", FONT_DISPLAY, 26, WHITE, rgb(255, 220, 140), rgb(40, 0, 10), 1.3), 160, 21);
    } else {
        p.rect(14, 3, 292, 34, vgrad(3, 37, rgb(255, 235, 80), rgb(235, 150, 0)));
        p.rect(14, 3, 292, 3, rgb(20, 20, 24));
        p.rect(14, 34, 292, 3, rgb(20, 20, 24));
        p.imageCentered(ttf("CHECKPOINT", FONT_DISPLAY, 22, rgb(30, 30, 40), BLACK, rgb(255, 250, 220), 1.0), 160, 20);
    }
    p.rect(14, 3, 292, 7, alpha(WHITE, 0.18));
    return p.finish(rgb(10, 10, 16), 1.0);
}

Bitmap makeLight(Color c, bool on) {
    Painter p(32, 32, SS, RENDER_SCALE, 2, 2);   // margins keep the outline from being cut off
    p.ellipse(16, 16, 15.5, 15.5, sphere(16, 16, 15.5, 15.5, rgb(40, 40, 46), 0.4, 0.4, 20));
    Color lc = on ? c : shade(c, 0.25);
    p.ellipse(16, 16, 11.5, 11.5, sphere(16, 16, 11.5, 11.5, lc, on ? 0.75 : 0.4, 0.8, 30));
    if (on) glow(p, 14, 13, 9, 8, WHITE, 0.6);
    return p.finish(rgb(0, 0, 0), 0.6);
}

Bitmap makePuff(Color c, double strength) {
    Bitmap b(24, 24);
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x) {
            double dx = (x + 0.5 - 12) / 12, dy = (y + 0.5 - 12) / 12;
            double d = dx * dx + dy * dy;
            if (d >= 1) continue;
            double a = std::pow(1 - d, 1.5) * strength * (0.75 + 0.25 * hashNoise(x, y));
            double lift = 1.0 + 0.25 * (-dx - dy);
            b.px[size_t(y) * 24 + x] = withAlpha(shade(c, lift), int(a * 255));
        }
    b.buildMips();
    return b;
}

// ----------------------------------------------------------- backgrounds ----
// smooth value noise (0..1) and a few octaves of it, for textures in the backgrounds
double vnoise(double x, double y) {
    double xi = std::floor(x), yi = std::floor(y), fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    double a = hashNoise(xi, yi), b = hashNoise(xi + 1, yi), c = hashNoise(xi, yi + 1), d = hashNoise(xi + 1, yi + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
double fbm(double x, double y, int oct = 4) {
    double s = 0, n = 0, a = 1;
    for (int i = 0; i < oct; ++i) { s += a * vnoise(x, y); n += a; x = x * 2.03 + 17.1; y = y * 2.01 + 9.3; a *= 0.5; }
    return s / n;
}

struct Ridge {
    double a[6], ph[6]; int f[6];
    explicit Ridge(int baseFreq) { for (int i = 0; i < 6; ++i) { f[i] = baseFreq * (i + 1) + int(rnd(0, 2)); a[i] = 1.0 / (i + 1.3); ph[i] = rnd(0, 6.283); } }
    double operator()(double x, double w) const {
        double s = 0, n = 0;
        for (int i = 0; i < 6; ++i) { s += a[i] * std::sin(6.283 * f[i] * x / w + ph[i]); n += a[i]; }
        return 0.5 + 0.5 * s / n;
    }
};

// silhouette with slope lighting, anti-aliased top edge and optional snow caps
// tex: 1 = bare rock (fine noise and gullies running down the slopes), 2 = wooded / grassy hills (speckled
// tree cover), 0 = plain. Every textured ridge also fades into a light haze towards its foot.
BgLayer ridgeLayer(int h, double minH, double maxH, int freq, Color top, Color bottom, double parallax, int yOff,
                   Color snow = 0, bool jagged = false, int tex = 1) {
    // drawn at full resolution: X, Y are bitmap pixels, x, y layout units (RENDER_SCALE pixels each)
    const int R = RENDER_SCALE, BW = 1024 * R, BH = h * R;
    BgLayer L{Bitmap(BW, BH), parallax, yOff};
    Ridge r(freq);
    auto height = [&](double x) {
        double v = r(x, 1024);
        if (jagged) v = 1 - std::fabs(2 * v - 1);
        return minH + (maxH - minH) * v;
    };
    for (int X = 0; X < BW; ++X) {
        double x = (X + 0.5) / R;
        double hh = height(x);
        double topY = h - hh;
        for (int Y = std::max(0, int(std::floor(topY * R))); Y < BH; ++Y) {
            double y = double(Y) / R;
            // facet lighting: compare heights left/right over a window that widens with depth,
            // so the faces of each peak are lit (left) or shaded (right) smoothly
            double depthPx = y - topY;
            double k = 2 + std::max(0.0, depthPx) * 0.9;
            double slope = (height(x + k) - height(x - k)) / (2 * k);
            double light = 1.0 + 0.24 * std::tanh(slope * 2.5);
            Color c = shade(lerpColor(top, bottom, y / h), light);
            if (tex == 1) {          // rock: mottled, with gullies running down from the crest
                double n = fbm(x * 0.09, y * 0.09, 4);
                double gully = std::pow(std::fabs(std::sin(x * 0.35 + 3 * fbm(x * 0.02, y * 0.05, 2))), 6) * std::min(1.0, depthPx / 12);
                c = shade(c, 0.9 + 0.2 * n - 0.12 * gully);
            } else if (tex == 2) {   // trees and meadows: speckles and soft patches
                double patch = fbm(x * 0.03, y * 0.06, 3);
                double speck = hashNoise(std::floor(x * 1.2), std::floor(y * 1.6));
                c = shade(c, 0.9 + 0.16 * patch + (speck > 0.82 ? -0.12 : speck < 0.1 ? 0.08 : 0));
            }
            if (tex) c = lerpColor(c, lerpColor(top, WHITE, 0.25), 0.18 * std::clamp((y - h * 0.6) / (h * 0.4), 0.0, 1.0));   // haze at the foot
            if (snow) {
                // snow cap thickness grows with the peak height, with a wavy lower edge
                double cap = std::clamp((hh - (minH + (maxH - minH) * 0.3)) / ((maxH - minH) * 0.7), 0.0, 1.0);
                double edge = cap * 0.5 * (0.8 + 0.2 * std::sin(x * 0.21) + 0.12 * std::sin(x * 0.77 + 1.3)) * hh;
                double dy = depthPx - edge;
                if (dy < 1.5) {
                    Color sc = shade(snow, std::clamp(0.86 + (light - 1) * 0.9, 0.7, 1.05));
                    c = lerpColor(sc, c, std::clamp(dy / 1.5, 0.0, 1.0));
                }
            }
            double cov = std::clamp(Y + 1 - topY * R, 0.0, 1.0);
            L.bmp.px[size_t(Y) * BW + X] = withAlpha(c, int(cov * 255));
        }
    }
    return L;
}

// layers drawn in layout pixels (sharp-edged windows, single-pixel stars): blown up to full resolution as
// exact RENDER_SCALE x RENDER_SCALE blocks
BgLayer upscaled(BgLayer L) {
    const int R = RENDER_SCALE;
    if (R == 1) return L;
    Bitmap b(L.bmp.w * R, L.bmp.h * R);
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x) b.px[size_t(y) * b.w + x] = L.bmp.px[size_t(y / R) * L.bmp.w + x / R];
    L.bmp = std::move(b);
    return L;
}

// Cumulus clouds, shaded per pixel: each cloud is a cluster of overlapping puffs with a flat base. The puff a
// pixel belongs to gives the surface normal: lit from the upper left, shaded cooler underneath, with a bright
// silver rim on the sunny edges and soft, slightly ragged outlines. The layer wraps around seamlessly.
BgLayer cloudLayer(int count, Color c, double parallax, int yOff) {
    const int R = RENDER_SCALE, LW = 1024, LH = 110, BW = LW * R, BH = LH * R;
    BgLayer L{Bitmap(BW, BH), parallax, yOff};
    struct Puff { double x, y, r; };
    struct Cloud { std::vector<Puff> puffs; double base, top, x0, x1; };
    std::vector<Cloud> clouds;
    for (int i = 0; i < count; ++i) {
        Cloud cl;
        double cx = rnd(0, LW), w = rnd(30, 80), base = rnd(72, 96);
        double hgt = w * rnd(0.3, 0.5);
        int n = 7 + int(w / 9);
        for (int k = 0; k < n; ++k) {                       // a broad base row and a taller, rounder middle
            double u = rnd(-1, 1);
            double r = w * (0.18 + 0.16 * (1 - u * u)) * rnd(0.75, 1.1);
            double y = base - r * 0.55 - hgt * (1 - u * u) * rnd(0.2, 0.75);
            cl.puffs.push_back({cx + u * w * 0.85, y, r});
        }
        cl.base = base; cl.top = 1e9; cl.x0 = 1e9; cl.x1 = -1e9;
        for (auto& p : cl.puffs) { cl.top = std::min(cl.top, p.y - p.r); cl.x0 = std::min(cl.x0, p.x - p.r); cl.x1 = std::max(cl.x1, p.x + p.r); }
        clouds.push_back(cl);
    }
    const Color shadowC = lerpColor(shade(c, 0.74), rgb(150, 164, 200), 0.35);
    const double lx = -0.42, ly = -0.72, lz = 0.55;      // light direction (towards the light)
    for (const Cloud& cl : clouds) {
        int Y0 = std::max(0, int(cl.top * R) - 2), Y1 = std::min(BH, int((cl.base + 2) * R));
        for (int X0 = int(std::floor(cl.x0 * R)) - 2; X0 < int(std::ceil(cl.x1 * R)) + 2; ++X0) {
            int X = ((X0 % BW) + BW) % BW;
            double x = (X0 + 0.5) / R;
            for (int Y = Y0; Y < Y1; ++Y) {
                double y = (Y + 0.5) / R;
                double best = 0; const Puff* bp = nullptr;
                for (const Puff& p : cl.puffs) {
                    double dx = (x - p.x) / p.r, dy = (y - p.y) / p.r;
                    double f = 1 - (dx * dx + dy * dy);
                    if (f > best) { best = f; bp = &p; }
                }
                if (!bp) continue;
                // ragged edge and the flat, soft base
                double edge = best + (fbm(x * 0.18, y * 0.18, 3) - 0.5) * 0.18;
                double baseFade = std::clamp((cl.base - y) / 3.0, 0.0, 1.0);
                double a = std::clamp(edge / 0.12, 0.0, 1.0) * baseFade;
                if (a <= 0) continue;
                // the surface normal, blended over all the puffs that overlap here (no seams between them)
                double sx = 0, sy = 0, sz = 0, sw = 0;
                for (const Puff& p : cl.puffs) {
                    double dx = (x - p.x) / p.r, dy = (y - p.y) / p.r, f = 1 - (dx * dx + dy * dy);
                    if (f <= 0) continue;
                    double wgt = f * f * f;
                    sx += dx * wgt; sy += dy * wgt; sz += std::sqrt(f) * wgt; sw += wgt;
                }
                double nl = std::sqrt(sx * sx + sy * sy + sz * sz);
                double nx = sx / nl, ny = sy / nl, nz = sz / nl;
                double lam = std::max(0.0, nx * lx + ny * ly + nz * lz);
                double under = std::clamp((y - (cl.base - (cl.base - cl.top) * 0.45)) / ((cl.base - cl.top) * 0.45), 0.0, 1.0);
                Color col = lerpColor(shadowC, c, std::clamp(0.3 + 0.75 * lam - 0.35 * under, 0.0, 1.0));
                if (best < 0.18 && lam > 0.5) col = lerpColor(col, WHITE, 0.5 * (1 - best / 0.18));   // silver lining
                col = shade(col, 0.97 + 0.06 * fbm(x * 0.3, y * 0.3, 2));
                Color& dst = L.bmp.px[size_t(Y) * BW + X];
                Color src = withAlpha(col, int(a * 255));
                dst = (dst >> 24) ? blendOver(dst, src) : src;
            }
        }
    }
    // the cloud bases sit a little lower than the old layer's single flat base, so the sky looks deeper
    L.yOff = yOff - 6;
    return L;
}

// A thin sheet of high, wispy cirrus streaks
BgLayer cirrusLayer(Color c, double parallax, int yOff) {
    const int R = RENDER_SCALE, LW = 1024, LH = 60, BW = LW * R, BH = LH * R;
    BgLayer L{Bitmap(BW, BH), parallax, yOff};
    for (int Y = 0; Y < BH; ++Y)
        for (int X = 0; X < BW; ++X) {
            double x = (X + 0.5) / R, y = (Y + 0.5) / R;
            // stretched noise, wrapped around the 1024 units
            // noise sampled around a circle (so the layer wraps), stretched sideways into long soft streaks
            double u = x / LW * 2 * M_PI;
            double cu = std::cos(u) * 9 + 50, su = std::sin(u) * 9 + 20;
            double n = fbm(cu + y * 0.02, su + y * 0.11, 3);
            double fine = fbm(cu * 3 + 7, su * 3 + y * 0.3, 2);        // fibres along the streaks
            double a = std::clamp((n - 0.5) * 3.0, 0.0, 1.0) * (0.75 + 0.25 * fine);
            a *= std::sin(M_PI * y / LH);                       // fade out at the top and bottom
            if (a > 0.01) L.bmp.px[size_t(Y) * BW + X] = withAlpha(c, int(a * 150));
        }
    return L;
}

// A full moon with soft grey seas and a halo (night)
BgLayer moonLayer(double x0, double y0, double r, double parallax, int yOff) {
    Painter p(1024, 170, 2);
    glow(p, x0, y0, r * 3.2, r * 3.2, rgb(200, 200, 255), 0.35);
    p.ellipse(x0, y0, r, r, Paint([=](double x, double y) {
        Color c = sphere(x0, y0, r, r, rgb(250, 246, 226), 0.75, 0.05, 6)(x, y);
        double n = fbm((x - x0) * 0.35 + 20, (y - y0) * 0.35, 3);
        return n > 0.55 ? shade(c, 0.82) : c;
    }));
    return BgLayer{p.finish(0, 0, false), parallax, yOff};
}

// Night skyline (drawn in layout pixels, then blown up for crisp windows): towers of mixed heights with
// setbacks, spires and slanted crowns, windows lit floor by floor, rooftop neon signs, water tanks and blinking
// aircraft lights. 'lit_' = the near row with bright windows; otherwise a far, hazy row with sparse lights.
BgLayer skylineLayer(int h, Color wall, bool lit_, double parallax, int yOff) {
    BgLayer L{Bitmap(1024, h), parallax, yOff};
    auto put = [&](int x, int y, Color c) { if (x >= 0 && x < 1024 && y >= 0 && y < h) L.bmp.px[size_t(y) * 1024 + x] = c; };
    auto add = [&](int x, int y, Color c) { if (x >= 0 && x < 1024 && y >= 0 && y < h) L.bmp.blend(x, y, c); };
    Color neon[] = {rgb(255, 60, 200), rgb(60, 230, 255), rgb(255, 210, 60), rgb(150, 90, 255)};
    int x = 0;
    while (x < 1024) {
        int bw = int(rnd(22, 58)), bh = int(rnd(h * 0.3, h * 0.95));
        if (x + bw > 1024) bw = 1024 - x;
        int style = int(rnd(0, 4));                       // 0 flat, 1 setback, 2 spire, 3 slanted crown
        int topY = h - bh;
        int setW = style == 1 ? bw / 4 : 0, setH = style == 1 ? int(bh * 0.18) : 0;
        Color face = wall;
        for (int y = topY; y < h; ++y) {
            bool upper = y < topY + setH;
            int xa = x + (upper ? setW : 0), xb = x + bw - (upper ? setW : 0);
            if (style == 3) {                             // slanted top
                int cut = std::max(0, (topY + 10 - y));
                xb = std::max(xa + 2, xb - cut * 2);
            }
            for (int xx = xa; xx < xb; ++xx) {
                double t = double(y - topY) / bh;
                Color c = lerpColor(shade(face, 1.25), face, t);
                if (xx == xa) c = shade(c, 1.35);
                if (xx >= xb - 2) c = shade(c, 0.72);
                if (!lit_) c = lerpColor(c, rgb(70, 40, 100), 0.25 * (1 - t));   // far: hazier at the top
                put(xx, y, c);
            }
        }
        // windows, lit by floors
        int wx0 = x + 3, wx1 = x + bw - 4;
        for (int y = topY + setH + 5; y < h - 3; y += lit_ ? 5 : 4) {
            double floorP = hashNoise(y * 0.7 + x, 3.1);
            bool floorLit = floorP < (lit_ ? 0.55 : 0.25);
            for (int wx = wx0; wx < wx1; wx += lit_ ? 4 : 3) {
                double r = hashNoise(wx * 1.3, y * 2.1 + x);
                bool on = floorLit ? r < 0.85 : r < (lit_ ? 0.1 : 0.04);
                if (lit_) {
                    Color wc = r < 0.6 ? rgb(255, 214, 120) : r < 0.85 ? rgb(130, 220, 255) : rgb(255, 160, 120);
                    Color dark = shade(wall, 0.55);
                    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) put(wx + dx, y + dy, on ? wc : dark);
                    if (on) { add(wx - 1, y, withAlpha(wc, 50)); add(wx + 2, y, withAlpha(wc, 50)); }
                } else if (on) put(wx, y, r < 0.02 ? rgb(255, 230, 160) : rgb(200, 150, 110));
            }
        }
        // crowns: spire with an aircraft light, or a lit crown / neon sign / water tank
        int cx = x + bw / 2;
        if (style == 2) {
            for (int y = topY - 14; y < topY; ++y) put(cx, y, shade(wall, 1.4));
            put(cx, topY - 15, rgb(255, 40, 40)); add(cx - 1, topY - 15, withAlpha(rgb(255, 40, 40), 110)); add(cx + 1, topY - 15, withAlpha(rgb(255, 40, 40), 110));
        } else if (lit_ && hashNoise(x, 7) < 0.4) {     // rooftop neon sign
            Color nc = neon[int(hashNoise(x, 9) * 4) % 4];
            int sw = std::min(bw - 6, 18), sx = cx - sw / 2, sy = topY - 6;
            for (int yy = sy; yy < sy + 4; ++yy) for (int xx = sx; xx < sx + sw; ++xx) put(xx, yy, (yy == sy || yy == sy + 3) ? shade(nc, 0.7) : nc);
            for (int xx = sx - 2; xx < sx + sw + 2; ++xx) { add(xx, sy - 1, withAlpha(nc, 70)); add(xx, sy + 4, withAlpha(nc, 70)); }
            put(sx + 2, sy + 4, shade(wall, 1.2)); put(sx + sw - 3, sy + 4, shade(wall, 1.2));
            for (int yy = sy + 4; yy < topY; ++yy) { put(sx + 2, yy, shade(wall, 1.2)); put(sx + sw - 3, yy, shade(wall, 1.2)); }
        } else if (hashNoise(x, 11) < 0.35) {           // a water tank on legs
            int tx = x + bw / 3;
            for (int yy = topY - 7; yy < topY - 2; ++yy) for (int xx = tx; xx < tx + 5; ++xx) put(xx, yy, shade(wall, 1.15));
            put(tx, topY - 2, shade(wall, 1.1)); put(tx + 4, topY - 2, shade(wall, 1.1));
            put(tx + 1, topY - 8, shade(wall, 1.15)); put(tx + 2, topY - 8, shade(wall, 1.15)); put(tx + 3, topY - 8, shade(wall, 1.15));
        }
        if (lit_ && style == 0 && hashNoise(x, 13) < 0.5) {   // a lit band around the crown
            Color nc = neon[int(hashNoise(x, 15) * 4) % 4];
            for (int xx = x + 1; xx < x + bw - 2; ++xx) { put(xx, topY + 1, nc); add(xx, topY, withAlpha(nc, 90)); add(xx, topY + 2, withAlpha(nc, 90)); }
        }
        x += bw + int(rnd(0, 6));
    }
    return L;
}

BgLayer sunLayer(Color c1, Color c2, int radius, bool stripes, double parallax, int yOff) {
    const int R = RENDER_SCALE;
    int hgt = radius * 3;
    BgLayer L{Bitmap(1024 * R, hgt * R), parallax, yOff};
    double cx = 512, cy = hgt / 2.0;
    for (int Y = 0; Y < hgt * R; ++Y)
        for (int X = int((cx - radius * 1.6) * R); X < int((cx + radius * 1.6) * R); ++X) {
            double dx = (X + 0.5) / R - cx, dy = (Y + 0.5) / R - cy;
            double d = std::sqrt(dx * dx + dy * dy) / radius;
            Color out = TRANSPARENT;
            if (d < 1) {
                double t = (dy / radius + 1) / 2;
                bool gap = stripes && t > 0.52 && std::fmod(dy * 0.42 + t * t * 9, 3.0) < (t - 0.45) * 2.6;
                if (!gap) out = withAlpha(lerpColor(c1, c2, t), int(std::clamp((1 - d) * radius * R, 0.0, 1.0) * 255));
            }
            double g = std::max(0.0, 1 - (d - 0.9) / 0.7);          // halo
            if (d >= 0.9 && g > 0) {
                Color halo = withAlpha(c1, int(g * g * 90));
                out = (out >> 24) ? blendOver(halo | 0xFF000000u, out) : halo;
            }
            if (out >> 24) L.bmp.px[size_t(Y) * L.bmp.w + X] = out;
        }
    return L;
}

BgLayer starLayer(double parallax) {
    BgLayer L{Bitmap(1024, 170), parallax, -60};
    for (int i = 0; i < 260; ++i) {
        int x = int(rnd(1, 1023)), y = int(rnd(1, 169));
        Color c = rnd(0, 1) < 0.2 ? rgb(255, 250, 210) : rgb(190, 190, 240);
        L.bmp.set(x, y, c);
        if (rnd(0, 1) < 0.15) {
            L.bmp.set(x - 1, y, withAlpha(c, 110)); L.bmp.set(x + 1, y, withAlpha(c, 110));
            L.bmp.set(x, y - 1, withAlpha(c, 110)); L.bmp.set(x, y + 1, withAlpha(c, 110));
        }
    }
    return L;
}

BgLayer treeLineLayer(Color c, double parallax, int yOff) {
    Painter p(1024, 64, 2);
    for (double x = -4; x < 1028; x += rnd(5, 8)) {
        double h = rnd(26, 60), w = rnd(8, 13);
        p.poly({x, 64, x + w / 2, 64 - h, x + w, 64}, hgrad(x, x + w, shade(c, 1.2), shade(c, 0.8)));
    }
    p.rect(0, 54, 1024, 10, c);
    return BgLayer{p.finish(0, 0, false), parallax, yOff};
}

BgLayer mesaLayer() {
    Painter p(1024, 72, 2);
    double x = 0;
    while (x < 1024) {
        double w = rnd(40, 140), h = rnd(25, 66);
        if (rnd(0, 1) < 0.6) {
            Color base = rgb(196, 96, 54);
            p.poly({x, 72, x + 10, 72 - h, x + w - 10, 72 - h, x + w, 72},
                   Paint([=](double px_, double py) {
                       Color c = lerpColor(base, shade(base, 0.7), (py - (72 - h)) / h);
                       if (px_ > x + w * 0.62) c = shade(c, 0.78);             // shadowed side
                       if (std::fmod(py, 9.0) < 1.2) c = shade(c, 0.88);        // strata
                       return c;
                   }));
            p.rect(x + 10, 72 - h, w - 20, 3, rgb(232, 140, 90));
        }
        x += w + rnd(10, 60);
    }
    return BgLayer{p.finish(0, 0, false), 0.2, 0};
}

// =========================================================== stages 7-11 ====
// (generated after everything above, so the original stages' graphics stay exactly the same)

// ------------------------------------------------------------- future -----
Bitmap makeHoloPylon() {   // street light of the future: a slim white mast with a glowing emitter reaching over the road
    Painter p(64, 220, SS, RENDER_SCALE, 6, 2);
    Color white = rgb(236, 238, 240), teal = rgb(30, 190, 190);
    p.roundRect(4, 202, 13, 18, 2, cylX(4, 17, rgb(200, 204, 210), 0.45, 0.6, 24));
    p.rect(8, 26, 5, 178, cylX(8, 13, white, 0.45, 0.8, 30));
    for (double y = 60; y < 200; y += 34) p.rect(7, y, 7, 2.2, teal);              // teal rings
    p.poly({10, 30, 54, 18, 56, 22, 12, 36}, vgrad(18, 36, rgb(250, 250, 252), rgb(200, 204, 212)));   // arm
    p.roundRect(36, 18, 26, 6, 3, cylY(18, 24, rgb(214, 218, 224), 0.45, 0.7, 30));
    p.roundRect(38, 23, 22, 3, 1.5, rgb(210, 255, 250));
    glow(p, 49, 26, 14, 8, rgb(120, 240, 230), 0.8);
    return p.finish(rgb(60, 80, 90), 1.0);
}

Bitmap makeScifiTower() {   // white tower with teal glass, a saucer deck and a spire
    Painter p(110, 260, SS, RENDER_SCALE, 3, 14);
    Color hull0 = rgb(248, 248, 246), hull1 = rgb(206, 210, 214), glass = rgb(40, 170, 180), gold = rgb(240, 196, 90);
    p.poly({18, 260, 32, 60, 78, 60, 92, 260}, Paint([=](double x, double y) {         // tapering main body
        double t = (y - 60) / 200;
        double half = 23 + t * 14, cx = 55;
        double nx = std::clamp((x - cx) / half, -0.98, 0.98);
        Color c = lit(lerpColor(hull0, hull1, t), nx, -0.2, std::sqrt(1 - nx * nx), 0.55, 0.5, 20);
        if (std::fmod(y - 60, 18.0) < 1.0) c = shade(c, 0.86);                      // floor lines
        return c;
    }));
    for (double x : {40.0, 55.0, 70.0})                                              // tall glass strips
        p.poly({x - 3, 70, x + 3, 70, x + 3.6, 252, x - 3.6, 252}, Paint([=](double px_, double py) {
            Color c = lerpColor(shade(glass, 1.35), shade(glass, 0.8), (py - 70) / 182);
            return std::fabs(px_ - x) < 0.6 ? shade(c, 1.4) : c;
        }));
    p.ellipse(55, 58, 48, 11, sphere(55, 58, 48, 11, rgb(236, 238, 240), 0.55, 0.8, 30));   // saucer deck
    p.rect(9, 61, 92, 1.6, alpha(rgb(60, 70, 80), 0.6));
    for (int i = 0; i < 9; ++i) p.ellipse(15 + i * 10, 63, 2.0, 1.2, gold);
    p.ellipse(55, 44, 18, 14, sphere(55, 44, 18, 14, glass, 0.5, 0.9, 40));          // glass dome
    p.rect(54, 2, 2, 32, cylX(54, 56, rgb(230, 232, 236)));                          // spire
    p.ellipse(55, 2, 2.5, 2.5, rgb(255, 60, 60));
    return p.finish(rgb(70, 80, 90), 1.0);
}

Bitmap makeUfo() {   // a flying saucer hovering over the verge, with a tractor beam
    Painter p(120, 150, SS, RENDER_SCALE, 2, 2);
    // the beam: a soft translucent cone down to the ground
    p.poly({48, 34, 72, 34, 104, 150, 16, 150}, Paint([](double x, double y) {
        double t = (y - 34) / 116, half = 12 + t * 44;
        double e = 1 - std::fabs(x - 60) / half;
        return alpha(rgb(150, 255, 200), std::clamp(e, 0.0, 1.0) * (0.28 - 0.12 * t));
    }));
    p.ellipse(60, 148, 44, 4, alpha(rgb(150, 255, 200), 0.35));
    p.ellipse(60, 30, 56, 13, sphere(60, 30, 56, 13, rgb(170, 176, 196), 0.45, 0.9, 40));   // hull
    p.ellipse(60, 33, 40, 7, alpha(rgb(30, 32, 50), 0.6));
    p.ellipse(60, 20, 22, 15, sphere(60, 20, 22, 15, rgb(110, 255, 170), 0.5, 1.0, 50));   // canopy
    for (int i = 0; i < 7; ++i) {
        double x = 18 + i * 14;
        Color c = (i & 1) ? rgb(255, 230, 80) : rgb(255, 60, 120);
        p.ellipse(x, 33, 2.4, 1.8, c);
        glow(p, x, 33, 6, 5, c, 0.8);
    }
    return p.finish(rgb(10, 12, 24), 1.0);
}

Bitmap makeHoloSign() {   // a sleek billboard of the future
    Painter p(160, 106, SS, RENDER_SCALE, 5, 2);
    Color white = rgb(236, 238, 240);
    p.rect(25, 60, 7, 46, cylX(25, 32, white, 0.45, 0.7, 24));
    p.rect(128, 60, 7, 46, cylX(128, 135, white, 0.45, 0.7, 24));
    p.roundRect(0, 0, 160, 66, 6, vgrad(0, 66, rgb(250, 250, 248), rgb(214, 216, 220)));
    p.roundRect(4, 4, 152, 58, 4, Paint([](double, double y) {
        Color c = lerpColor(rgb(40, 190, 190), rgb(20, 110, 140), y / 66);
        return std::fmod(y, 3.0) < 0.8 ? shade(c, 0.92) : c;                         // faint scan lines
    }));
    Color ol = rgb(0, 50, 60);
    p.imageCentered(ttf("HYPER", FONT_DISPLAY, 24, WHITE, rgb(210, 255, 250), ol, 1.2), 80, 22);
    p.imageCentered(ttf("DRIVE 2099", FONT_UI, 15, rgb(255, 216, 110), rgb(250, 180, 60), ol, 1.0), 80, 47);
    p.rect(4, 4, 152, 10, alpha(WHITE, 0.16));
    return p.finish(rgb(50, 60, 70), 1.0);
}

// -------------------------------------------------------------- Brazil -----
Bitmap makeTropical() {   // a bush of big leaves and bright hibiscus flowers
    Painter p(100, 60, SS, RENDER_SCALE, 2, 2);
    for (int i = 0; i < 14; ++i) {                                        // broad leaves
        double a = rnd(-2.9, -0.25), len = rnd(22, 34);
        double bx = 50 + rnd(-14, 14), by = 58;
        double tx = bx + std::cos(a) * len, ty = by + std::sin(a) * len * 0.9;
        double nx = -(ty - by) / len * 0.32, ny = (tx - bx) / len * 0.32;
        Color g = rgb(20 + int(rnd(0, 30)), 140 + int(rnd(0, 60)), 60);
        p.poly({bx, by, (bx + tx) / 2 + nx * len * 0.5, (by + ty) / 2 + ny * len * 0.5, tx, ty,
                (bx + tx) / 2 - nx * len * 0.5, (by + ty) / 2 - ny * len * 0.5},
               hgrad(std::min(bx, tx), std::max(bx, tx) + 1, shade(g, 1.2), shade(g, 0.8)));
        p.line(bx, by, tx, ty, 0.6, shade(g, 0.6));
    }
    Color petals[] = {rgb(255, 40, 90), rgb(255, 140, 0), rgb(255, 220, 30), rgb(240, 40, 220), rgb(255, 80, 40)};
    for (int i = 0; i < 9; ++i) {                                         // hibiscus flowers
        double x = rnd(16, 84), y = rnd(14, 44), r = rnd(5, 8);
        Color c = petals[i % 5];
        for (int k = 0; k < 5; ++k) {
            double a = k * 1.2566 + rnd(0, 0.4);
            double px_ = x + std::cos(a) * r * 0.6, py = y + std::sin(a) * r * 0.6;
            p.ellipse(px_, py, r * 0.62, r * 0.5, sphere(px_, py, r * 0.62, r * 0.5, c, 0.55, 0.3, 10));
        }
        p.ellipse(x, y, r * 0.25, r * 0.25, rgb(255, 250, 200));
    }
    return p.finish(rgb(20, 40, 20), 1.0);
}

Bitmap makeCarnival() {   // a carnival standard: a pole with a giant fan of feathers and ribbons
    Painter p(70, 180, SS, RENDER_SCALE, 3, 2);
    p.rect(33, 50, 4, 130, cylX(33, 37, rgb(240, 200, 60), 0.4, 0.8, 30));
    Color feathers[] = {rgb(255, 30, 90), rgb(255, 150, 0), rgb(255, 235, 0), rgb(40, 220, 80), rgb(0, 170, 255), rgb(170, 60, 255)};
    for (int i = 0; i < 13; ++i) {                                        // fan of feathers
        double a = -M_PI + 0.12 + i * (M_PI - 0.24) / 12;
        double len = 30 + 4 * std::sin(i * 1.3);
        double tx = 35 + std::cos(a) * len, ty = 52 + std::sin(a) * len;
        Color c = feathers[i % 6];
        double nx = -std::sin(a) * 4.5, ny = std::cos(a) * 4.5;
        p.poly({35, 52, (35 + tx) / 2 + nx, (52 + ty) / 2 + ny, tx, ty, (35 + tx) / 2 - nx, (52 + ty) / 2 - ny},
               Paint([=](double x, double y) {
                   double d = std::hypot(x - 35, y - 52) / len;
                   return lerpColor(shade(c, 0.75), shade(c, 1.25), d);
               }));
        p.line(35, 52, tx, ty, 0.5, alpha(WHITE, 0.6));
    }
    p.ellipse(35, 52, 7, 7, sphere(35, 52, 7, 7, rgb(255, 215, 40), 0.5, 0.9, 30));   // golden boss
    for (int i = 0; i < 4; ++i) {                                          // ribbons
        Color c = feathers[(i * 2 + 1) % 6];
        double x0 = 35 + (i - 1.5) * 3;
        for (double y = 60; y < 120; y += 2) {
            double xx = x0 + std::sin(y * 0.15 + i) * (3 + (y - 60) * 0.12);
            double xn = x0 + std::sin((y + 2) * 0.15 + i) * (3 + (y + 2 - 60) * 0.12);
            p.line(xx, y, xn, y + 2, 2.0, c);
        }
    }
    return p.finish(rgb(30, 10, 30), 1.0);
}

Bitmap makeFavela() {   // stacked, brightly painted little houses on the hillside
    Painter p(140, 170, SS, RENDER_SCALE, 2, 8);
    Color walls[] = {rgb(255, 90, 80), rgb(255, 200, 40), rgb(60, 200, 120), rgb(70, 150, 255), rgb(255, 140, 200),
                     rgb(255, 150, 40), rgb(150, 230, 90), rgb(120, 220, 230), rgb(200, 110, 255)};
    int k = 0;
    double y = 170;
    for (int row = 0; row < 4; ++row) {
        double hgt = rnd(32, 44);
        double x = row * rnd(4, 9);
        double right = 140 - row * rnd(6, 14);
        while (x < right - 12) {
            double w = std::min(right - x, rnd(24, 40));
            Color c = walls[(k++ * 4 + row) % 9];
            p.rect(x, y - hgt, w, hgt, vgrad(y - hgt, y, shade(c, 1.08), shade(c, 0.86)));
            p.rect(x, y - hgt, w, 2.5, shade(c, 0.7));                                  // flat roof edge
            for (double wx = x + 4; wx < x + w - 7; wx += 10) {
                p.rect(wx, y - hgt + 9, 5, 7, rgb(40, 34, 50));
                p.rect(wx, y - hgt + 9, 5, 1.5, rgb(240, 240, 240));
            }
            if (hashNoise(x, row) < 0.4) {                                              // blue water tank
                p.rect(x + w * 0.55, y - hgt - 7, 8, 7, cylX(x + w * 0.55, x + w * 0.55 + 8, rgb(40, 120, 220)));
                p.ellipse(x + w * 0.55 + 4, y - hgt - 7, 4, 1.4, rgb(90, 170, 255));
            }
            p.rect(x + w - 1, y - hgt, 1, hgt, alpha(BLACK, 0.35));
            x += w;
        }
        y -= hgt;
    }
    return p.finish(rgb(30, 20, 30), 1.0);
}

// --------------------------------------------------------------- Paris -----
Bitmap makeParisLamp() {   // cast-iron street lamp with a lantern on a curled arm
    Painter p(64, 200, SS, RENDER_SCALE, 6, 2);
    Color iron = rgb(34, 64, 52);
    p.roundRect(3, 180, 15, 20, 3, cylX(3, 18, iron, 0.38, 0.6, 24));         // fluted base
    p.rect(6, 172, 9, 10, cylX(6, 15, iron, 0.38, 0.6, 24));
    p.rect(8.5, 30, 4, 146, cylX(8.5, 12.5, iron, 0.38, 0.7, 26));
    for (double y : {60.0, 120.0}) p.ellipse(10.5, y, 4, 2, cylX(6.5, 14.5, iron, 0.38, 0.6, 24));
    for (double t = 0; t < 1; t += 0.04) {                                     // curled arm
        double a0 = M_PI * (1 - t * 0.55), a1 = M_PI * (1 - (t + 0.04) * 0.55);
        p.line(30 + 20 * std::cos(a0), 34 - 16 * std::sin(a0), 30 + 20 * std::cos(a1), 34 - 16 * std::sin(a1), 2.6, iron);
    }
    p.line(30, 18, 48, 18, 2.6, iron);
    p.line(48, 18, 48, 22, 1.6, iron);
    p.poly({40, 22, 56, 22, 53, 42, 43, 42}, vgrad(22, 42, rgb(255, 236, 170), rgb(255, 196, 90)));   // lantern glass
    p.poly({38, 22, 58, 22, 52, 16, 44, 16}, iron);                                                   // lantern cap
    p.rect(43, 42, 10, 2.5, iron);
    for (double x : {45.0, 48.0, 51.0}) p.line(x, 22, x - (x - 48) * 0.15, 42, 0.6, alpha(iron, 0.8));
    glow(p, 48, 32, 18, 16, rgb(255, 220, 140), 1.0);
    return p.finish(rgb(10, 20, 16), 1.0);
}

Bitmap makeHaussmann() {   // cream limestone apartment block with a zinc mansard roof
    const int w = 130, h = 230;
    Painter p(w, h, SS, RENDER_SCALE, 2, 10);
    const double side = w * 0.16, roofY = 52;
    Color stone = rgb(232, 218, 190), zinc = rgb(110, 124, 146);
    p.rect(0, roofY, w - side, h - roofY, Paint([=](double, double y) {       // front face
        Color c = lerpColor(stone, shade(stone, 0.86), (y - roofY) / (h - roofY));
        if (std::fmod(y - roofY, 6.0) < 0.5) c = shade(c, 0.92);                // stone courses
        return c;
    }));
    p.poly({w - side, roofY, double(w), roofY - 6, double(w), double(h), w - side, double(h)},
           vgrad(roofY, h, shade(stone, 0.7), shade(stone, 0.6)));              // side face
    // mansard roof with dormers and chimneys
    p.poly({-1, roofY, 8, 20, w - side - 8, 20, w - side + 1, roofY}, vgrad(20, roofY, shade(zinc, 1.15), shade(zinc, 0.8)));
    p.poly({w - side + 1, roofY, w - side - 8, 20, double(w) - 6, 14, double(w), roofY - 6}, shade(zinc, 0.62));
    for (double x = 14; x < w - side - 16; x += 22) {
        p.rect(x, 30, 11, 16, rgb(232, 226, 210));
        p.poly({x - 1, 31, x + 5.5, 25, x + 12, 31}, shade(zinc, 0.9));
        p.rect(x + 2.5, 34, 6, 10, rgb(50, 60, 80));
    }
    for (double x : {20.0, 64.0, 98.0}) {
        p.rect(x, 6, 9, 16, cylX(x, x + 9, rgb(190, 120, 90), 0.45, 0.2, 8));
        for (double cx : {x + 2.0, x + 6.0}) p.rect(cx, 3, 2, 4, rgb(150, 90, 70));
    }
    p.rect(-1, roofY - 2, w - side + 2, 4, rgb(236, 228, 210));                 // cornice
    // windows: tall French windows, balconies on the 2nd and 5th floors
    for (int f = 0; f < 5; ++f) {
        double y = roofY + 10 + f * 33;
        bool balcony = f == 0 || f == 3;
        for (double x = 8; x < w - side - 12; x += 19) {
            p.rect(x - 1.5, y - 2, 13, 2, shade(stone, 0.84));
            p.rect(x, y, 10, 22, vgrad(y, y + 22, rgb(70, 86, 110), rgb(40, 46, 62)));
            p.rect(x + 4.5, y, 1, 22, rgb(220, 214, 200));
            p.rect(x, y + 8, 10, 1, rgb(220, 214, 200));
            if (!balcony) {
                p.rect(x - 1, y + 16, 12, 6, alpha(rgb(20, 20, 24), 0.85));         // small railing
                for (double bx = x; bx < x + 10; bx += 2) p.rect(bx, y + 16, 0.6, 6, rgb(60, 60, 70));
            }
        }
        if (balcony) {                                                           // continuous iron balcony
            p.rect(2, y + 21, w - side - 6, 2, rgb(30, 30, 36));
            p.rect(2, y + 13, w - side - 6, 1.2, rgb(30, 30, 36));
            for (double bx = 3; bx < w - side - 4; bx += 2.2) p.rect(bx, y + 13, 0.6, 8, rgb(40, 40, 48));
        }
    }
    // shop front on the ground floor
    p.rect(0, h - 22, w - side, 22, rgb(40, 50, 70));
    for (double x = 6; x < w - side - 20; x += 26) p.rect(x, h - 18, 20, 18, vgrad(h - 18, h, rgb(255, 220, 150), rgb(200, 150, 90)));
    return p.finish(rgb(40, 36, 32), 1.0);
}

Bitmap makeCafe() {   // a pavement cafe: striped awning, little round tables
    Painter p(150, 90, SS, RENDER_SCALE, 3, 4);
    p.rect(0, 20, 150, 70, vgrad(20, 90, rgb(70, 40, 40), rgb(40, 22, 22)));              // shop front
    p.rect(8, 34, 134, 40, vgrad(34, 74, rgb(255, 214, 140), rgb(200, 140, 80)));        // lit window
    for (double x = 8; x < 142; x += 26.8) p.rect(x, 34, 2, 40, rgb(60, 34, 30));
    // striped awning
    p.poly({-3, 18, 153, 18, 160, 36, -10, 36}, Paint([](double x, double y) {
        double u = (x + 10 + (36 - y) * 0.4) / 10;
        return (int(std::floor(u)) & 1) ? rgb(240, 240, 236) : rgb(200, 30, 40);
    }));
    for (double x = -10; x < 160; x += 10) p.ellipse(x + 5, 36, 5, 2.5, (int((x + 10) / 10) & 1) ? rgb(240, 240, 236) : rgb(200, 30, 40));
    p.rect(30, 2, 90, 15, rgb(30, 40, 60));
    p.imageCentered(ttf("CAFE", FONT_DISPLAY, 13, rgb(255, 230, 160), rgb(255, 200, 110), 0, 0), 75, 9.5);
    // tables and chairs on the pavement
    for (double x : {22.0, 75.0, 128.0}) {
        p.rect(x - 1, 74, 2, 14, rgb(40, 40, 46));
        p.ellipse(x, 74, 9, 2.4, cylY(71.6, 76.4, rgb(220, 220, 214)));
        p.rect(x - 7, 87, 14, 2, rgb(40, 40, 46));
        for (double cx : {x - 15, x + 12}) {
            p.rect(cx, 70, 3, 18, rgb(150, 90, 50));
            p.rect(cx - 1, 78, 5, 2, rgb(170, 110, 60));
        }
    }
    return p.finish(rgb(26, 16, 14), 1.0);
}

Bitmap makeTreeRound() {   // a broad-leaved tree (plane tree / oak)
    Painter p(100, 150, SS, RENDER_SCALE, 2, 2);
    p.poly({44, 150, 46, 80, 54, 80, 56, 150}, Paint([](double x, double y) {
        double nx = std::clamp((x - 50) / 6, -0.98, 0.98);
        Color c = lit(rgb(120, 96, 70), nx, 0, std::sqrt(1 - nx * nx), 0.4, 0.1, 6);
        return hashNoise(x * 2, y * 0.7) > 0.8 ? shade(c, 0.8) : c;
    }));
    p.line(50, 100, 34, 76, 3, rgb(110, 88, 64));
    p.line(50, 96, 68, 74, 3, rgb(110, 88, 64));
    std::vector<std::array<double, 3>> balls;
    for (int i = 0; i < 22; ++i) {
        double a = rnd(0, 6.283), r = rnd(0, 1);
        balls.push_back({50 + std::cos(a) * r * 32, 50 + std::sin(a) * r * 34, rnd(13, 20)});
    }
    std::sort(balls.begin(), balls.end(), [](auto& a, auto& b) { return a[1] < b[1]; });
    for (auto& b : balls) {
        Color base = rgb(50 + int(rnd(0, 30)), 130 + int(rnd(0, 40)), 50 + int(rnd(0, 20)));
        p.ellipse(b[0], b[1], b[2], b[2] * 0.9, Paint([=](double x, double y) {
            Color c = sphere(b[0], b[1], b[2], b[2] * 0.9, base, 0.38, 0.12, 8)(x, y);
            double n = hashNoise(x * 2.2, y * 2.2);
            return n > 0.82 ? shade(c, 1.2) : n < 0.15 ? shade(c, 0.78) : c;
        }));
    }
    return p.finish(rgb(14, 40, 18), 1.0);
}

// ------------------------------------------------------------- Germany -----
Bitmap makeFachwerk() {   // half-timbered house with a steep tiled roof and flower boxes
    const int w = 140, h = 200;
    Painter p(w, h, SS, RENDER_SCALE, 3, 4);
    Color plaster = rgb(244, 236, 220), beam = rgb(86, 52, 32), tile = rgb(170, 60, 40);
    const double wallTop = 92, side = 22;
    p.rect(0, wallTop, w - side, h - wallTop, vgrad(wallTop, h, plaster, shade(plaster, 0.9)));
    p.poly({w - side, wallTop, double(w), wallTop - 8, double(w), double(h), w - side, double(h)}, shade(plaster, 0.72));
    // timber frame: posts, rails and diagonal braces
    for (double x = 0; x <= w - side + 0.1; x += (w - side) / 4) p.rect(std::min(x, w - side - 4.0), wallTop, 4, h - wallTop, beam);
    for (double y : {wallTop, wallTop + 52.0, double(h) - 4}) p.rect(0, y, w - side, 4, beam);
    for (int i = 0; i < 4; ++i) {
        double x0 = i * (w - side) / 4 + 4, x1 = (i + 1) * (w - side) / 4;
        if (i % 2 == 0) { p.line(x0, wallTop + 4, x1, wallTop + 52, 2.6, beam); p.line(x0, h - 4, x1, wallTop + 56, 2.6, beam); }
        else            { p.line(x0, wallTop + 52, x1, wallTop + 4, 2.6, beam); p.line(x0, wallTop + 56, x1, h - 4, 2.6, beam); }
    }
    // windows with shutters and flower boxes
    for (int i = 0; i < 4; ++i) {
        double cx = (i + 0.5) * (w - side) / 4;
        for (double y : {wallTop + 16, wallTop + 70}) {
            if (y > wallTop + 60 && i == 1) {                                         // the front door
                p.roundRect(cx - 8, wallTop + 64, 16, h - wallTop - 68, 7, vgrad(wallTop + 64, h, rgb(110, 60, 36), rgb(70, 36, 20)));
                continue;
            }
            p.rect(cx - 7, y, 14, 18, vgrad(y, y + 18, rgb(150, 190, 220), rgb(60, 80, 110)));
            p.rect(cx - 0.6, y, 1.2, 18, plaster);
            p.rect(cx - 7, y + 8, 14, 1.2, plaster);
            p.rect(cx - 12, y, 5, 18, rgb(40, 110, 60));
            p.rect(cx + 7, y, 5, 18, rgb(40, 110, 60));
            p.rect(cx - 9, y + 18, 18, 4, rgb(120, 70, 40));
            for (double fx = cx - 8; fx < cx + 8; fx += 3) p.ellipse(fx + 1, y + 17, 1.6, 1.6, hashNoise(fx, y) < 0.5 ? rgb(230, 30, 50) : rgb(255, 120, 160));
        }
    }
    // steep roof (gable end facing us) with a little dormer
    p.poly({-6, wallTop + 2, (w - side) / 2.0, 6, w - side + 6, wallTop + 2}, Paint([=](double x, double y) {
        Color c = lerpColor(shade(tile, 1.15), shade(tile, 0.8), (y - 6) / (wallTop - 4));
        if (std::fmod(y, 5.0) < 0.8) c = shade(c, 0.7);                               // rows of tiles
        if (std::fmod(x + std::floor(y / 5) * 3, 6.0) < 0.5) c = shade(c, 0.85);
        return c;
    }));
    p.poly({(w - side) / 2.0, 6, double(w), wallTop - 10, double(w), wallTop - 6, w - side + 6, wallTop + 2}, shade(tile, 0.62));
    p.poly({(w - side) / 2.0 - 30, wallTop - 30, (w - side) / 2.0 + 30, wallTop - 30, (w - side) / 2.0 + 18, wallTop - 52,
            (w - side) / 2.0 - 18, wallTop - 52}, Paint([=](double x, double) {             // half-timbered gable
        double rel = std::fmod(x - (w - side) / 2.0 + 60, 12.0);
        return rel < 2 ? beam : plaster;
    }));
    p.rect((w - side) / 2.0 - 30, wallTop - 32, 60, 3, beam);
    p.rect((w - side) / 2.0 - 5, wallTop - 48, 10, 12, vgrad(wallTop - 48, wallTop - 36, rgb(150, 190, 220), rgb(60, 80, 110)));
    p.rect(w - side - 30, 22, 9, 24, cylX(w - side - 30, w - side - 21, rgb(150, 70, 50), 0.4, 0.2, 8));   // chimney
    return p.finish(rgb(40, 24, 16), 1.0);
}

Bitmap makeAutobahnSign() {   // blue motorway direction sign
    Painter p(150, 110, SS, RENDER_SCALE, 3, 2);
    Color metal = rgb(150, 150, 162);
    p.rect(22, 60, 6, 50, cylX(22, 28, metal, 0.35, 0.6, 24));
    p.rect(122, 60, 6, 50, cylX(122, 128, metal, 0.35, 0.6, 24));
    p.roundRect(0, 0, 150, 66, 4, rgb(240, 240, 244));
    p.roundRect(3, 3, 144, 60, 3, vgrad(3, 63, rgb(30, 90, 200), rgb(16, 56, 150)));
    Color ink = WHITE;
    p.imageCentered(ttf("BERLIN", FONT_UI, 15, ink, ink, 0, 0), 56, 17);
    p.imageCentered(ttf("240", FONT_UI, 15, ink, ink, 0, 0), 122, 17);
    p.imageCentered(ttf("HAMBURG", FONT_UI, 15, ink, ink, 0, 0), 56, 40);
    p.imageCentered(ttf("95", FONT_UI, 15, ink, ink, 0, 0), 122, 40);
    p.rect(10, 52, 130, 1.4, ink);
    p.poly({120, 55, 134, 55, 134, 52, 140, 57.5, 134, 63, 134, 60, 120, 60}, ink);   // exit arrow
    p.rect(3, 3, 144, 8, alpha(WHITE, 0.15));
    return p.finish(rgb(10, 14, 30), 1.0);
}

Bitmap makeWindTurbine() {   // a tall, slender wind turbine
    Painter p(120, 300, SS, RENDER_SCALE, 3, 4);         // wide enough for the blades in every direction
    Color white = rgb(236, 240, 244);
    const double hx = 60, hy = 61;
    p.poly({hx - 4, 300, hx - 2, 64, hx + 2, 64, hx + 4, 300}, cylX(hx - 4, hx + 4, white, 0.5, 0.4, 16));   // tower
    p.roundRect(hx - 3, 56, 16, 10, 4, cylY(56, 66, white, 0.5, 0.5, 20));                                  // nacelle
    p.ellipse(hx, hy, 4, 4, sphere(hx, hy, 4, 4, white, 0.55, 0.6, 20));                                    // hub
    for (int i = 0; i < 3; ++i) {                                                                           // blades
        double a = -M_PI / 2 + 0.35 + i * 2 * M_PI / 3;
        double tx = hx + std::cos(a) * 54, ty = hy + std::sin(a) * 54;
        double nx = -std::sin(a), ny = std::cos(a);
        p.poly({hx + nx * 2.4, hy + ny * 2.4, tx + nx * 0.6, ty + ny * 0.6, tx - nx * 0.6, ty - ny * 0.6, hx - nx * 1.2, hy - ny * 1.2},
               hgrad(std::min(hx, tx) - 3, std::max(hx, tx) + 3, white, shade(white, 0.8)));
        if (i == 0) p.line(tx - std::cos(a) * 6, ty - std::sin(a) * 6, tx, ty, 1.0, rgb(220, 40, 40));   // red tip
    }
    return p.finish(rgb(60, 66, 80), 0.8);
}

// ------------------------------------------------------------ Tuscany -----
Bitmap makeCypress() {   // tall, narrow, flame-shaped Italian cypress
    Painter p(40, 180, SS, RENDER_SCALE, 2, 2);
    p.rect(18, 166, 4, 14, cylX(18, 22, rgb(100, 76, 54)));
    auto half = [](double y) {                                 // outline: pointed top, widest at 2/3
        double t = y / 170;
        return 15 * std::pow(std::sin(std::min(1.0, t * 1.25) * M_PI / 2), 1.6) * (t > 0.8 ? 1 - (t - 0.8) * 1.6 : 1.0);
    };
    std::vector<double> pts;
    for (int i = 0; i <= 40; ++i) { double y = i * 170.0 / 40; pts.push_back(20 + half(y) + std::sin(y * 0.9) * 0.8); pts.push_back(y); }
    for (int i = 40; i >= 0; --i) { double y = i * 170.0 / 40; pts.push_back(20 - half(y) + std::sin(y * 1.1 + 1) * 0.8); pts.push_back(y); }
    p.poly(pts, Paint([=](double x, double y) {
        double hw = std::max(1.0, half(y));
        double nx = std::clamp((x - 20) / hw, -0.98, 0.98);
        Color c = lit(rgb(34, 84, 44), nx, -0.2, std::sqrt(1 - nx * nx), 0.4, 0.1, 6);
        double n = hashNoise(x * 2.5, y * 1.3);
        return n > 0.82 ? shade(c, 1.22) : n < 0.18 ? shade(c, 0.75) : c;
    }));
    return p.finish(rgb(10, 30, 16), 1.0);
}

Bitmap makeVilla() {   // Tuscan farmhouse: ochre walls, terracotta roof, green shutters and a little tower
    const int w = 150, h = 140;
    Painter p(w, h, SS, RENDER_SCALE, 4, 4);
    Color wall = rgb(226, 178, 110), roof = rgb(196, 90, 52), shutter = rgb(70, 110, 70);
    const double side = 22, top = 66;
    // tower
    p.rect(14, 12, 34, top - 12, vgrad(12, top, shade(wall, 1.05), shade(wall, 0.9)));
    p.poly({10, 14, 31, 0, 52, 14}, vgrad(0, 14, shade(roof, 1.1), shade(roof, 0.85)));
    p.roundRect(25, 24, 12, 18, 6, rgb(50, 40, 34));
    // main house
    p.rect(0, top, w - side, h - top, Paint([=](double x, double y) {
        Color c = lerpColor(wall, shade(wall, 0.88), (y - top) / (h - top));
        double n = hashNoise(x * 0.7, y * 0.7);
        return shade(c, 0.94 + 0.12 * n);                                            // weathered plaster
    }));
    p.poly({w - side, top, double(w), top - 6, double(w), double(h), w - side, double(h)}, shade(wall, 0.7));
    p.poly({-6, top + 2, 12, top - 18, w - side - 4, top - 18, w - side + 6, top + 2}, Paint([=](double x, double y) {
        Color c = lerpColor(shade(roof, 1.1), shade(roof, 0.85), (y - top + 18) / 20);
        if (std::fmod(x, 4.0) < 0.8) c = shade(c, 0.8);                              // rows of curved tiles
        return c;
    }));
    p.poly({w - side - 4, top - 18, double(w) - 2, top - 22, double(w) + 2, top - 6, w - side + 6, top + 2}, shade(roof, 0.66));
    for (double x = 14; x < w - side - 14; x += 28) {
        for (double y : {top + 12, top + 44}) {
            if (y > top + 30 && std::fabs(x - 70) < 4) {                               // arched doorway
                p.roundRect(x - 2, top + 38, 18, h - top - 38, 9, rgb(80, 50, 34));
                continue;
            }
            p.rect(x, y, 12, 16, rgb(50, 44, 40));
            p.rect(x - 6, y, 6, 16, shutter);
            p.rect(x + 12, y, 6, 16, shutter);
            for (double sy = y + 2; sy < y + 16; sy += 3) { p.rect(x - 6, sy, 6, 0.6, shade(shutter, 0.7)); p.rect(x + 12, sy, 6, 0.6, shade(shutter, 0.7)); }
        }
    }
    p.rect(0, h - 3, w, 3, rgb(150, 130, 100));
    return p.finish(rgb(50, 30, 20), 1.0);
}

Bitmap makeOlive() {   // gnarled olive tree with a silvery crown
    Painter p(90, 70, SS, RENDER_SCALE, 2, 2);
    Color bark = rgb(110, 100, 86);
    p.poly({38, 70, 40, 46, 34, 34, 40, 34, 45, 44, 50, 32, 55, 34, 49, 48, 52, 70}, Paint([=](double x, double y) {
        Color c = shade(bark, 0.85 + 0.3 * hashNoise(x * 1.5, y * 3));
        return c;
    }));
    std::vector<std::array<double, 3>> balls;
    for (int i = 0; i < 18; ++i) balls.push_back({rnd(12, 78), rnd(12, 34), rnd(8, 13)});
    std::sort(balls.begin(), balls.end(), [](auto& a, auto& b) { return a[1] < b[1]; });
    for (auto& b : balls) {
        Color base = rgb(120 + int(rnd(0, 20)), 146 + int(rnd(0, 20)), 96);
        p.ellipse(b[0], b[1], b[2], b[2] * 0.8, Paint([=](double x, double y) {
            Color c = sphere(b[0], b[1], b[2], b[2] * 0.8, base, 0.45, 0.1, 6)(x, y);
            double n = hashNoise(x * 3, y * 3);
            if (n > 0.86) return shade(rgb(200, 210, 190), 1.0);                     // silver undersides
            return n < 0.2 ? shade(c, 0.8) : c;
        }));
    }
    return p.finish(rgb(30, 40, 24), 1.0);
}

// --------------------------------------------------- new backgrounds ----
BgLayer planetLayer() {   // a big ringed planet and a small moon, pale in the daytime sky (future)
    Painter p(1024, 170, 2);
    const double cx = 300, cy = 80, r = 52;
    auto ring = [&](bool front) {
        p.ellipse(cx, cy, r * 2.1, r * 0.42, Paint([=](double x, double y) {
            double dx = (x - cx) / (r * 2.1), dy = (y - cy) / (r * 0.42), d = std::sqrt(dx * dx + dy * dy);
            if (d < 0.62 || (front ? y < cy : y >= cy)) return TRANSPARENT;
            double band = std::sin(d * 40) * 0.5 + 0.5;
            return alpha(lerpColor(rgb(255, 250, 240), rgb(230, 236, 246), band), 0.55 * (1 - std::pow((d - 0.62) / 0.38, 3)));
        }));
    };
    ring(false);
    p.ellipse(cx, cy, r, r, Paint([=](double x, double y) {
        Color c = sphere(cx, cy, r, r, rgb(236, 206, 196), 0.55, 0.2, 10)(x, y);      // pale, washed out by daylight
        double band = std::sin((y - cy) * 0.22 + std::sin(x * 0.03) * 1.5);
        return alpha(shade(c, 0.95 + 0.06 * band), 0.85);
    }));
    ring(true);
    p.ellipse(760, 40, 13, 13, sphere(760, 40, 13, 13, rgb(240, 240, 246), 0.6, 0.2, 10));
    return BgLayer{p.finish(0, 0, false), 0.02, -40};
}

BgLayer futureSkylineLayer(int h, Color body, double parallax, int yOff, Color accent) {
    Painter p(1024, h, 2);
    double x = 0;
    while (x < 1024) {
        double bw = rnd(18, 46), bh = rnd(h * 0.3, h * 0.95);
        double kind = rnd(0, 1);
        Color c = shade(body, rnd(0.94, 1.06));
        if (kind < 0.35) {                                            // needle tower
            p.poly({x, double(h), x + bw * 0.4, h - bh, x + bw * 0.6, h - bh, x + bw, double(h)}, hgrad(x, x + bw, shade(c, 1.06), shade(c, 0.86)));
            p.rect(x + bw * 0.5 - 0.5, h - bh - 14, 1, 14, shade(c, 0.9));
        } else if (kind < 0.6) {                                      // dome
            p.rect(x, h - bh * 0.6, bw, bh * 0.6, hgrad(x, x + bw, shade(c, 1.05), shade(c, 0.86)));
            p.ellipse(x + bw / 2, h - bh * 0.6, bw / 2, bw * 0.35, sphere(x + bw / 2, h - bh * 0.6, bw / 2, bw * 0.35, accent, 0.55, 0.6, 20));
        } else {                                                      // block with a deck
            p.rect(x + bw * 0.15, h - bh, bw * 0.7, bh, hgrad(x + bw * 0.15, x + bw * 0.85, shade(c, 1.06), shade(c, 0.86)));
            p.rect(x, h - bh * 0.7, bw, 3, shade(c, 1.1));
            p.rect(x + bw * 0.45, h - bh + 4, bw * 0.1, bh - 8, accent);
        }
        x += bw + rnd(0, 8);
    }
    return BgLayer{p.finish(0, 0, false), parallax, yOff};
}

BgLayer rioMountainsLayer() {   // Sugarloaf by the bay and the statue on the peak
    Painter p(1024, 130, 2);
    Color rock = rgb(40, 110, 90), rock2 = rgb(30, 90, 80);
    auto dome = [&](double cx, double w, double hgt, Color c) {
        p.ellipse(cx, 130, w, hgt, Paint([=](double x, double y) {
            double nx = std::clamp((x - cx) / w, -0.98, 0.98);
            return lit(lerpColor(c, shade(c, 0.7), (y - (130 - hgt)) / hgt), nx, -0.3, std::sqrt(1 - nx * nx), 0.5, 0.15, 6);
        }));
    };
    dome(170, 60, 96, rock);                        // Sugarloaf
    dome(230, 40, 60, rock2);
    dome(420, 120, 70, rock2);
    // the peak with the statue
    p.poly({560, 130, 640, 34, 664, 30, 690, 40, 800, 130}, vgrad(30, 130, rgb(50, 120, 80), rgb(30, 90, 70)));
    const double sx = 664, sy = 30;
    Color st = rgb(236, 236, 240);
    p.rect(sx - 2.5, sy - 6, 5, 6, st);                                    // pedestal
    p.poly({sx - 2.2, sy - 6, sx - 1.5, sy - 22, sx + 1.5, sy - 22, sx + 2.2, sy - 6}, st);   // robe
    p.rect(sx - 13, sy - 21, 26, 2.4, st);                                 // outstretched arms
    p.ellipse(sx, sy - 24, 1.8, 2, st);                                    // head
    dome(900, 100, 50, rock);
    for (int i = 0; i < 6; ++i) glow(p, 664, 10, 60, 30, rgb(255, 250, 220), 0.05);
    return BgLayer{p.finish(0, 0, false), 0.08, 0};
}

BgLayer favelaHillLayer() {   // colourful little houses scattered over a green hill
    Painter p(1024, 70, 2);
    Ridge r(3);
    std::vector<double> pts = {0, 70};
    for (int x = 0; x <= 1024; x += 8) { pts.push_back(x); pts.push_back(70 - 20 - 40 * r(x, 1024)); }
    pts.push_back(1024); pts.push_back(70);
    p.poly(pts, vgrad(10, 70, rgb(40, 160, 70), rgb(20, 110, 50)));
    Color cols[] = {rgb(255, 90, 80), rgb(255, 210, 40), rgb(80, 200, 255), rgb(255, 140, 210), rgb(140, 240, 90), rgb(255, 150, 40)};
    for (int i = 0; i < 420; ++i) {
        double x = rnd(0, 1024);
        double top = 70 - 20 - 40 * r(x, 1024);
        double y = rnd(top + 3, 68);
        p.rect(x, y, rnd(2.5, 5), rnd(2, 3.5), cols[i % 6]);
    }
    return BgLayer{p.finish(0, 0, false), 0.18, 0};
}

BgLayer eiffelLayer() {   // the tower on the skyline, with a dome on a hill far away
    Painter p(1024, 190, 2);
    Color iron = rgb(110, 90, 100);
    // dome on a hill
    p.ellipse(860, 190, 110, 30, rgb(200, 170, 180));
    p.rect(842, 140, 36, 22, rgb(236, 226, 226));
    p.ellipse(860, 140, 18, 18, sphere(860, 140, 18, 18, rgb(246, 240, 238), 0.6, 0.2, 8));
    p.rect(858.5, 115, 3, 9, rgb(236, 226, 226));
    // the tower: two curved legs, platforms, a tapering shaft
    const double cx = 300, base = 190, top = 6;
    auto halfW = [=](double y) { double t = (base - y) / (base - top); return 4 + 64 * std::pow(1 - t, 3.2); };
    std::vector<double> pts;
    for (int i = 0; i <= 50; ++i) { double y = base - i * (base - top) / 50; pts.push_back(cx - halfW(y)); pts.push_back(y); }
    for (int i = 50; i >= 0; --i) { double y = base - i * (base - top) / 50; pts.push_back(cx + halfW(y)); pts.push_back(y); }
    p.poly(pts, Paint([=](double x, double y) {
        double hw = halfW(y);
        double u = (x - cx) / hw;
        // lattice: criss-cross lines; the arch between the legs is open
        if (y > base - 40 && std::fabs(u) < 0.62 * std::sqrt(std::max(0.0, 1 - std::pow((base - y) / 40, 2)))) return TRANSPARENT;
        double a = std::fmod(std::fabs(x - cx) + y, 7.0), b = std::fmod(std::fabs(x - cx) - y + 700, 7.0);
        bool lattice = a < 1.3 || b < 1.3 || std::fabs(u) > 0.8;
        return lattice ? iron : alpha(iron, 0.25);
    }));
    for (double y : {base - 52, base - 100}) p.rect(cx - halfW(y) - 3, y, 2 * halfW(y) + 6, 3, iron);   // platforms
    p.rect(cx - 1, top - 6, 2, 8, iron);
    return BgLayer{p.finish(0, 0, false), 0.06, 0};
}

BgLayer rooftopLayer(Color c, double parallax, int yOff) {   // mansard roofs and chimney pots
    Painter p(1024, 60, 2);
    double x = -10;
    while (x < 1034) {
        double w = rnd(30, 70), hgt = rnd(24, 46);
        Color wall = shade(c, rnd(0.9, 1.1));
        p.rect(x, 60 - hgt, w, hgt, wall);
        p.poly({x - 1, 60 - hgt, x + 4, 60 - hgt - 9, x + w - 4, 60 - hgt - 9, x + w + 1, 60 - hgt}, shade(wall, 0.75));
        for (double cxp = x + 6; cxp < x + w - 8; cxp += rnd(10, 18)) {
            p.rect(cxp, 60 - hgt - 15, 4, 7, shade(wall, 0.85));
            p.rect(cxp + 0.6, 60 - hgt - 17, 1.2, 2, shade(wall, 0.7));
            p.rect(cxp + 2.4, 60 - hgt - 17, 1.2, 2, shade(wall, 0.7));
        }
        for (double wx = x + 4; wx < x + w - 4; wx += 7)
            for (double wy = 60 - hgt + 5; wy < 58; wy += 8) p.rect(wx, wy, 3, 5, shade(wall, 0.8));
        x += w;
    }
    return BgLayer{p.finish(0, 0, false), parallax, yOff};
}

BgLayer castleLayer() {   // a fairy-tale castle on a wooded crag
    Painter p(1024, 150, 2);
    const double cx = 620;
    p.poly({500, 150, 560, 96, 600, 88, 660, 92, 700, 104, 760, 150}, vgrad(88, 150, rgb(70, 110, 70), rgb(50, 90, 56)));
    Color stone = rgb(236, 232, 226), roof = rgb(70, 90, 130);
    auto tower = [&](double x, double y, double w, double hgt) {
        p.rect(x - w / 2, y - hgt, w, hgt, cylX(x - w / 2, x + w / 2, stone, 0.55, 0.2, 8));
        p.poly({x - w / 2 - 1.5, y - hgt, x, y - hgt - w * 1.9, x + w / 2 + 1.5, y - hgt}, hgrad(x - w / 2, x + w / 2, shade(roof, 1.2), shade(roof, 0.75)));
        for (double wy = y - hgt + 6; wy < y - 6; wy += 9) p.rect(x - 1, wy, 2, 4, rgb(60, 60, 80));
    };
    p.rect(cx - 48, 60, 80, 34, cylX(cx - 48, cx + 32, stone, 0.6, 0.15, 6));                  // palace
    p.poly({cx - 50, 60, cx - 40, 46, cx + 24, 46, cx + 34, 60}, shade(roof, 0.9));
    for (double wx = cx - 42; wx < cx + 28; wx += 8) p.rect(wx, 68, 3, 6, rgb(60, 60, 80));
    tower(cx - 56, 94, 11, 46);
    tower(cx + 40, 94, 9, 60);
    tower(cx - 8, 60, 8, 34);
    tower(cx + 18, 60, 6, 20);
    tower(cx - 30, 60, 6, 16);
    for (int i = 0; i < 60; ++i) {                                                          // trees on the crag
        double x = rnd(510, 750), y = rnd(100, 148), hh = rnd(8, 16);
        p.poly({x, y, x + 3.5, y - hh, x + 7, y}, shade(rgb(40, 100, 50), rnd(0.8, 1.15)));
    }
    return BgLayer{p.finish(0, 0, false), 0.1, 0};
}

BgLayer florenceLayer() {   // the cathedral dome, the bell tower and the town hall tower over red roofs
    Painter p(1024, 150, 2);
    Color roofs = rgb(196, 106, 70), walls = rgb(226, 196, 150);
    // town of red roofs
    double x = 300;
    while (x < 760) {
        double w = rnd(10, 24), h = rnd(10, 22);
        p.rect(x, 150 - h, w, h, shade(walls, rnd(0.85, 1.05)));
        p.poly({x - 1, 150 - h, x + w / 2, 150 - h - 5, x + w + 1, 150 - h}, shade(roofs, rnd(0.85, 1.1)));
        x += w;
    }
    // the dome (octagonal drum, red brick shell with white ribs, lantern)
    const double dx = 520, dy = 104;
    p.rect(dx - 40, dy, 80, 26, cylX(dx - 40, dx + 40, rgb(236, 226, 206), 0.6, 0.15, 6));
    for (double wx = dx - 34; wx < dx + 34; wx += 12) p.ellipse(wx + 3, dy + 12, 2.6, 2.6, rgb(90, 90, 80));
    p.ellipse(dx, dy, 38, 54, Paint([=](double px_, double py) {
        if (py > dy) return TRANSPARENT;
        double nx = std::clamp((px_ - dx) / 38, -0.98, 0.98);
        Color c = lit(rgb(196, 92, 60), nx, -0.4, std::sqrt(1 - nx * nx), 0.5, 0.25, 10);
        for (double rib : {-0.62, 0.0, 0.62}) if (std::fabs(nx - rib) < 0.05) c = rgb(246, 240, 230);
        return c;
    }));
    p.rect(dx - 5, dy - 66, 10, 14, cylX(dx - 5, dx + 5, rgb(246, 240, 230), 0.55, 0.2, 8));
    p.poly({dx - 6, dy - 66, dx, dy - 78, dx + 6, dy - 66}, rgb(150, 150, 140));
    p.ellipse(dx, dy - 80, 2, 2, rgb(220, 190, 80));
    // the bell tower (tall, square, striped marble)
    const double tx = 470;
    p.rect(tx - 9, 30, 18, 120, Paint([=](double px_, double py) {
        Color c = std::fmod(py, 20.0) < 2 ? rgb(70, 120, 90) : rgb(240, 232, 220);
        if (std::fabs(px_ - tx) > 7) c = shade(c, 0.8);
        if (std::fmod(py, 20.0) > 8 && std::fmod(py, 20.0) < 15 && std::fabs(px_ - tx) < 3) c = rgb(80, 70, 70);
        return c;
    }));
    p.rect(tx - 10, 27, 20, 3, rgb(230, 220, 205));
    // the town hall tower (crenellated)
    const double hx = 610;
    p.rect(hx - 7, 50, 14, 100, cylX(hx - 7, hx + 7, rgb(200, 160, 110), 0.5, 0.1, 6));
    p.rect(hx - 10, 42, 20, 10, rgb(200, 160, 110));
    for (double cxp = hx - 10; cxp < hx + 10; cxp += 4) p.rect(cxp, 38, 2, 4, rgb(190, 150, 104));
    p.rect(hx - 4, 22, 8, 20, rgb(206, 166, 116));
    p.poly({hx - 5, 22, hx, 12, hx + 5, 22}, rgb(150, 90, 60));
    return BgLayer{p.finish(0, 0, false), 0.08, 0};
}

BgLayer tuscanHillsLayer() {   // soft rolling hills with rows of cypresses
    Painter p(1024, 60, 2);
    Ridge r(2);
    std::vector<double> pts = {0, 60};
    for (int x = 0; x <= 1024; x += 8) { pts.push_back(x); pts.push_back(60 - 12 - 34 * r(x, 1024)); }
    pts.push_back(1024); pts.push_back(60);
    p.poly(pts, Paint([](double x, double y) {
        Color c = lerpColor(rgb(170, 170, 80), rgb(130, 150, 60), y / 60);
        if (std::fmod(x * 0.5 + y * 2, 9.0) < 1.0) c = shade(c, 0.9);              // furrows
        return c;
    }));
    for (int i = 0; i < 70; ++i) {
        double x = rnd(0, 1024), top = 60 - 12 - 34 * r(x, 1024), hh = rnd(8, 14);
        p.ellipse(x, top - hh * 0.4, 1.6, hh * 0.6, rgb(36, 76, 44));
    }
    return BgLayer{p.finish(0, 0, false), 0.2, 0};
}

// =========================================================== flair (1.4) ====
// More roadside variety for every stage, and the bitmaps for the ambient effects. Each is generated in its own
// seeded block (see Graphics::generate), so none of them changes any other graphic.

// ---- desert
// A tumbleweed: a ball of dry, curling twigs. 'rot' turns the same tangle in the picture plane (it rolls
// sideways across the road), so a set of frames at different angles animates the roll.
Bitmap makeTumbleweed(double rot) {
    // a dense, airy ball of thin, curling, sun-dried stems: dark brown inside, greyish tan where the light
    // catches the outer twigs, with a few forked tips. Each stem is a short curved arc drawn as a polyline.
    std::mt19937 r(9177);                                  // the same tangle for every frame
    auto R = [&](double a, double b) { return std::uniform_real_distribution<double>(a, b)(r); };
    const double c = 24, rad = 21;
    Painter p(48, 48, SS, RENDER_SCALE, 2, 2);
    const double cr = std::cos(rot), sr = std::sin(rot);
    auto put = [&](double x, double y) { return std::array<double, 2>{c + (x * cr - y * sr) * rad, c + (x * sr + y * cr) * rad}; };
    struct Stem { std::vector<std::array<double, 2>> pts; double z, t; };
    std::vector<Stem> stems;
    for (int i = 0; i < 320; ++i) {
        // start on a jittered sphere and curl along it: the direction turns a little every step
        double th = R(0, 2 * M_PI), ph = std::acos(R(-1, 1)), rr = R(0.35, 1.0);
        double dth = R(-0.12, 0.12), dph = R(-0.1, 0.1), curl = R(-0.05, 0.05), drr = R(-0.03, 0.03);
        int n = int(R(4, 9));
        Stem st; st.t = R(0.45, 0.85) * (0.6 + 0.6 * rr); st.z = 0;
        for (int k = 0; k <= n; ++k) {
            double x = rr * std::sin(ph) * std::cos(th), y = rr * std::cos(ph), z = rr * std::sin(ph) * std::sin(th);
            st.pts.push_back(put(x, y));
            st.z += z / (n + 1);
            th += dth; ph += dph; dth += curl; rr = std::clamp(rr + drr, 0.25, 1.02);
        }
        stems.push_back(st);
    }
    std::sort(stems.begin(), stems.end(), [](const Stem& a, const Stem& b) { return a.z < b.z; });   // back first
    for (const Stem& st : stems) {
        double front = (st.z + 1) / 2;                      // 0 back .. 1 front
        Color col = lerpColor(rgb(72, 52, 32), rgb(172, 142, 102), std::pow(front, 1.2));
        col = shade(col, 0.85 + 0.25 * hashNoise(st.pts[0][0] * 3, st.pts[0][1] * 3));
        for (size_t k = 1; k < st.pts.size(); ++k)
            p.line(st.pts[k - 1][0], st.pts[k - 1][1], st.pts[k][0], st.pts[k][1], st.t, col);
        if (front > 0.55 && hashNoise(st.pts[0][0] * 7, st.pts[0][1] * 5) < 0.35) {   // a forked tip
            auto a = st.pts[st.pts.size() - 2], b = st.pts.back();
            double dx = b[0] - a[0], dy = b[1] - a[1];
            p.line(b[0], b[1], b[0] + dx * 0.8 - dy * 0.7, b[1] + dy * 0.8 + dx * 0.7, st.t * 0.7, shade(col, 1.1));
        }
    }
    return p.finish(rgb(52, 36, 22), 0.25);
}

Bitmap makeDeadTree() {   // a bleached, bare desert tree
    Painter p(100, 130, SS, RENDER_SCALE, 4, 2);
    std::function<void(double, double, double, double, double, int)> branch =
        [&](double x, double y, double ang, double len, double th, int depth) {
            double x1 = x + std::cos(ang) * len, y1 = y - std::sin(ang) * len;
            Color bark = lerpColor(rgb(120, 98, 80), rgb(196, 176, 150), depth / 6.0);
            p.line(x, y, x1, y1, th, bark);
            if (depth >= 6 || len < 5) return;
            int n = rnd(0, 1) < 0.35 ? 3 : 2;
            for (int i = 0; i < n; ++i)
                branch(x1, y1, ang + rnd(-0.75, 0.75), len * rnd(0.62, 0.8), th * 0.66, depth + 1);
        };
    p.poly({44, 130, 47, 96, 53, 96, 57, 130}, cylX(44, 57, rgb(150, 126, 100), 0.45, 0.1, 6));
    branch(50, 98, M_PI / 2 + 0.1, 30, 6, 1);
    return p.finish(rgb(50, 40, 32), 0.8);
}

Bitmap makeWindPump() {   // an old western wind pump: lattice tower, fan wheel, tail vane and a water tank
    Painter p(110, 210, SS, RENDER_SCALE, 4, 4);
    Color steel = rgb(150, 150, 150), rust = rgb(150, 86, 50);
    // tank
    p.rect(60, 168, 46, 42, cylX(60, 106, rgb(140, 150, 160), 0.4, 0.4, 14));
    for (double y : {176.0, 190.0, 204.0}) p.rect(60, y, 46, 1.5, shade(rgb(140, 150, 160), 0.7));
    p.ellipse(83, 168, 23, 3, rgb(90, 100, 110));
    // lattice tower
    const double bx0 = 18, bx1 = 58, tx0 = 34, tx1 = 42, top = 52;
    p.line(bx0, 210, tx0, top, 2.2, steel);
    p.line(bx1, 210, tx1, top, 2.2, steel);
    for (int i = 0; i < 7; ++i) {
        double t0 = i / 7.0, t1 = (i + 1) / 7.0;
        double ya = 210 - (210 - top) * t0, yb = 210 - (210 - top) * t1;
        double la = bx0 + (tx0 - bx0) * t0, ra = bx1 + (tx1 - bx1) * t0, lb = bx0 + (tx0 - bx0) * t1, rb = bx1 + (tx1 - bx1) * t1;
        p.line(la, ya, rb, yb, 1.0, shade(steel, 0.8));
        p.line(ra, ya, lb, yb, 1.0, shade(steel, 0.8));
        p.line(la, ya, ra, ya, 1.2, steel);
    }
    p.rect(30, top - 6, 16, 6, rust);                       // gearbox
    p.poly({46, top - 4, 92, top - 10, 92, top + 4, 46, top - 1}, rust);    // tail boom
    p.poly({80, top - 22, 100, top - 26, 100, top + 14, 80, top + 8}, Paint([=](double, double y) {   // tail vane
        return shade(rgb(196, 70, 50), 0.9 + 0.2 * std::sin(y * 0.4));
    }));
    // fan wheel (seen a little from the side: an ellipse)
    const double wx = 30, wy = top - 3;
    for (int i = 0; i < 18; ++i) {
        double a = i * 2 * M_PI / 18;
        double ex = wx + std::cos(a) * 9, ey = wy + std::sin(a) * 26;
        double ex2 = wx + std::cos(a + 0.22) * 9, ey2 = wy + std::sin(a + 0.22) * 26;
        double ix = wx + std::cos(a) * 3, iy = wy + std::sin(a) * 8;
        p.poly({ix, iy, ex, ey, ex2, ey2}, i % 2 ? rgb(196, 196, 190) : rgb(164, 164, 160));
    }
    p.ellipse(wx, wy, 9, 26, Paint([=](double x, double y) {
        double dx = (x - wx) / 9, dy = (y - wy) / 26, d = std::sqrt(dx * dx + dy * dy);
        return d > 0.93 ? rgb(110, 110, 110) : TRANSPARENT;
    }));
    p.ellipse(wx, wy, 3, 5, sphere(wx, wy, 3, 5, rust));
    return p.finish(rgb(40, 36, 32), 0.8);
}

Bitmap makeDiner() {   // a chrome roadside diner with a big sign and two old petrol pumps
    const int w = 200, h = 120;
    Painter p(w, h, SS, RENDER_SCALE, 4, 6);
    // sign on a pole
    p.rect(176, 20, 5, 100, cylX(176, 181, rgb(170, 170, 176), 0.4, 0.6, 20));
    p.roundRect(150, 0, 56, 30, 8, vgrad(0, 30, rgb(240, 60, 60), rgb(170, 20, 30)));
    p.roundRect(153, 3, 50, 24, 6, alpha(WHITE, 0.08));
    p.imageCentered(ttf("DINER", FONT_DISPLAY, 13, rgb(255, 250, 220), rgb(255, 220, 120), rgb(90, 0, 10), 0.8), 178, 15);
    for (double x = 154; x < 204; x += 6) p.ellipse(x, 29, 1.4, 1.4, rgb(255, 230, 140));
    // the diner: a long rounded chrome-and-red body
    const double top = 62;
    p.roundRect(4, top, 150, h - top, 10, Paint([=](double x, double y) {
        double t = (y - top) / (h - top);
        Color c = t < 0.18 ? lerpColor(rgb(250, 250, 255), rgb(150, 160, 176), t / 0.18)
                : t < 0.62 ? rgb(46, 60, 84)
                : t < 0.7  ? rgb(240, 240, 246)
                           : lerpColor(rgb(214, 40, 44), rgb(150, 20, 30), (t - 0.7) / 0.3);
        if (t > 0.7 && std::fmod(x, 6.0) < 0.6) c = shade(c, 0.85);
        return c;
    }));
    for (double x = 16; x < 146; x += 18) {                     // lit windows
        p.rect(x, top + 14, 14, 18, vgrad(top + 14, top + 32, rgb(255, 236, 170), rgb(240, 176, 90)));
        p.rect(x + 6.5, top + 14, 1, 18, rgb(200, 200, 210));
    }
    p.roundRect(68, top + 10, 18, h - top - 10, 3, vgrad(top + 10, h, rgb(220, 200, 160), rgb(160, 120, 80)));   // door
    p.rect(0, top - 4, 158, 5, cylY(top - 4, top + 1, rgb(220, 224, 232), 0.4, 0.8, 30));   // chrome cornice
    // two petrol pumps
    for (double x : {116.0, 136.0}) {
        p.roundRect(x, 92, 12, 28, 3, vgrad(92, 120, rgb(240, 200, 60), rgb(200, 150, 30)));
        p.ellipse(x + 6, 90, 6, 5, sphere(x + 6, 90, 6, 5, rgb(255, 250, 240), 0.6, 0.4, 20));
        p.rect(x + 2, 98, 8, 6, rgb(40, 40, 46));
    }
    return p.finish(rgb(30, 20, 20), 1.0);
}

// ---- coast
Bitmap makeBeachHut(int style) {   // a little striped wooden beach hut
    const int w = 74, h = 92;
    Painter p(w, h, SS, RENDER_SCALE, 3, 3);
    Color stripes[3][2] = {{rgb(70, 150, 230), rgb(250, 250, 246)}, {rgb(250, 120, 140), rgb(250, 248, 236)},
                           {rgb(80, 200, 160), rgb(255, 230, 120)}};
    Color a = stripes[style % 3][0], b = stripes[style % 3][1];
    const double top = 34;
    p.rect(4, top, 64, h - top - 6, Paint([=](double x, double y) {
        Color c = (int(std::floor(x / 8)) & 1) ? a : b;
        c = shade(c, 0.96 + 0.06 * hashNoise(x * 2, y * 0.3));
        return x > 56 ? shade(c, 0.78) : c;
    }));
    p.poly({0, top + 2, 36, 6, 72, top + 2}, vgrad(6, top, rgb(226, 220, 204), rgb(180, 170, 150)));
    p.rect(-2, top, 76, 3, rgb(250, 250, 250));
    p.roundRect(22, top + 16, 24, h - top - 22, 2, vgrad(top + 16, h, shade(a, 0.7), shade(a, 0.5)));   // door
    p.ellipse(41, top + 34, 1.4, 1.4, rgb(230, 200, 90));
    for (double x : {8.0, 64.0}) p.rect(x - 2, h - 6, 4, 6, rgb(130, 100, 70));          // stilts
    p.rect(-2, h - 8, 76, 3, rgb(170, 130, 90));                                          // deck
    return p.finish(rgb(30, 36, 50), 0.9);
}

Bitmap makeLifeguard() {   // a lifeguard tower on stilts, red and white, with a flag
    Painter p(80, 150, SS, RENDER_SCALE, 4, 6);
    Color wood = rgb(250, 248, 240), red = rgb(220, 40, 40);
    for (double x : {14.0, 62.0}) p.line(x, 150, x + (x < 40 ? 6 : -6), 70, 3.2, cylX(x - 2, x + 2, rgb(220, 210, 190)));
    p.line(17, 120, 59, 100, 1.6, rgb(200, 190, 170));
    p.line(17, 100, 59, 120, 1.6, rgb(200, 190, 170));
    p.line(46, 150, 64, 70, 2.0, rgb(200, 190, 170));                                     // ladder rails
    for (double t = 0.1; t < 1; t += 0.12) p.line(46 + 18 * t - 4, 150 - 80 * t, 46 + 18 * t + 4, 150 - 80 * t, 1.2, rgb(200, 190, 170));
    p.rect(8, 44, 64, 28, Paint([=](double x, double) { return std::fmod(x - 8, 16.0) < 8 ? red : wood; }));   // cabin
    p.rect(14, 50, 52, 12, vgrad(50, 62, rgb(110, 170, 210), rgb(40, 80, 120)));                            // window
    p.poly({2, 46, 40, 26, 78, 46}, vgrad(26, 46, rgb(240, 60, 50), rgb(170, 30, 30)));                     // roof
    p.rect(39, 6, 2, 22, rgb(200, 200, 200));                                                               // flag
    p.poly({41, 6, 60, 10, 41, 15}, Paint([](double, double y) { return y < 10.5 ? rgb(250, 220, 40) : rgb(220, 40, 40); }));
    return p.finish(rgb(30, 20, 20), 0.9);
}

// ---- night city: a tall tower with a vertical neon sign, setbacks and a lit crown
Bitmap makeBuilding3() {
    const int w = 112, h = 250;
    Painter p(w, h, SS, RENDER_SCALE, 4, 18);
    Color wall0 = rgb(54, 70, 110), wall1 = rgb(22, 26, 52);
    const double side = 18;
    auto block = [&](double x0, double x1, double y0) {
        p.rect(x0, y0, x1 - x0 - side * 0.6, h - y0, vgrad(y0, h, wall0, wall1));
        p.poly({x1 - side * 0.6, y0, x1, y0 - 5, x1, double(h), x1 - side * 0.6, double(h)}, vgrad(y0, h, shade(wall0, 0.55), shade(wall1, 0.55)));
        p.poly({x0, y0, x0 + side * 0.6, y0 - 5, x1, y0 - 5, x1 - side * 0.6, y0}, shade(wall0, 1.35));
        // windows in bands; whole floors lit or dark, a few odd ones out
        for (double y = y0 + 8; y < h - 18; y += 9) {
            bool floorLit = hashNoise(y * 0.37, x0) < 0.62;
            for (double x = x0 + 5; x < x1 - side * 0.6 - 5; x += 7) {
                double r = hashNoise(x * 1.7, y * 2.3);
                bool on = floorLit ? r < 0.8 : r < 0.12;
                Color c = r < 0.5 ? rgb(255, 222, 140) : r < 0.75 ? rgb(170, 230, 255) : rgb(255, 170, 120);
                p.rect(x, y, 4, 5, on ? vgrad(y, y + 5, c, shade(c, 0.7)) : Paint(rgb(18, 18, 36)));
            }
        }
    };
    block(14, 98, 70);       // the main shaft
    block(26, 86, 36);       // first setback
    block(38, 74, 14);       // crown
    // crown lights and a spire with a blinking aircraft light
    p.rect(38, 12, 30, 2, rgb(120, 230, 255));
    glow(p, 53, 13, 22, 6, rgb(120, 230, 255), 0.8);
    p.poly({51, 12, 53, -14, 55, 12}, cylX(51, 55, rgb(170, 176, 190), 0.4, 0.6, 20));
    p.ellipse(53, -14, 2, 2, rgb(255, 50, 40));
    glow(p, 53, -14, 7, 7, rgb(255, 60, 40), 1.0);
    // vertical neon sign down the side of the shaft
    p.roundRect(2, 90, 16, 96, 4, vgrad(90, 186, rgb(30, 10, 40), rgb(20, 6, 30)));
    const char* letters[] = {"C", "L", "U", "B"};
    for (int i = 0; i < 4; ++i)
        p.imageCentered(ttf(letters[i], FONT_DISPLAY, 15, rgb(255, 250, 255), rgb(255, 120, 230), rgb(150, 0, 110), 1.0), 10, 103 + i * 23);
    glow(p, 10, 138, 16, 56, rgb(255, 80, 220), 0.55);
    // street level: lit entrance and awning
    p.rect(14, h - 16, 66, 16, rgb(20, 18, 30));
    p.rect(34, h - 14, 24, 14, vgrad(h - 14, h, rgb(255, 214, 140), rgb(200, 130, 70)));
    p.rect(28, h - 18, 36, 4, rgb(220, 40, 120));
    return p.finish(rgb(6, 6, 18), 1.0);
}

// ---- snow: an alpine chalet with a wide, snow-laden roof
Bitmap makeChalet() {
    const int w = 160, h = 130;
    Painter p(w, h, SS, RENDER_SCALE, 6, 4);
    Color plaster = rgb(240, 238, 230), wood = rgb(120, 70, 40);
    const double wallTop = 54, side = 24, mid = 92;
    p.rect(0, mid, w - side, h - mid, vgrad(mid, h, plaster, shade(plaster, 0.88)));               // stone/plaster ground floor
    p.rect(0, wallTop, w - side, mid - wallTop, Paint([=](double x, double y) {                     // wooden upper floor
        Color c = shade(wood, 0.9 + 0.2 * hashNoise(x * 0.4, std::floor(y / 4)));
        return std::fmod(y, 4.0) < 0.6 ? shade(c, 0.7) : c;
    }));
    p.poly({w - side, wallTop, double(w), wallTop - 8, double(w), double(h), w - side, double(h)}, shade(wood, 0.55));
    // balcony with carved rails
    p.rect(-4, mid - 4, w - side + 8, 4, shade(wood, 0.8));
    for (double x = -2; x < w - side + 4; x += 5) p.roundRect(x, mid - 16, 3, 12, 1.4, shade(wood, 1.15));
    p.rect(-4, mid - 18, w - side + 8, 3, shade(wood, 0.9));
    // lit windows with shutters
    for (double cx : {20.0, 54.0, 88.0, 116.0})
        for (double y : {wallTop + 8, mid + 10}) {
            if (y > mid && std::fabs(cx - 54) < 1) { p.roundRect(cx - 9, mid + 8, 18, h - mid - 8, 3, vgrad(mid + 8, h, rgb(120, 70, 40), rgb(80, 44, 24))); continue; }
            p.rect(cx - 7, y, 14, 16, vgrad(y, y + 16, rgb(255, 222, 150), rgb(230, 160, 80)));
            p.rect(cx - 0.6, y, 1.2, 16, wood);
            p.rect(cx - 13, y, 6, 16, rgb(40, 100, 60));
            p.rect(cx + 7, y, 6, 16, rgb(40, 100, 60));
        }
    // a low, wide roof with deep eaves and a thick blanket of snow
    p.poly({-12, wallTop + 4, (w - side) / 2.0, 14, w - side + 12, wallTop + 4}, vgrad(14, wallTop + 4, rgb(90, 60, 40), rgb(60, 40, 26)));
    p.poly({-14, wallTop + 2, (w - side) / 2.0, 10, w - side + 14, wallTop + 2, w - side + 10, wallTop - 4, (w - side) / 2.0, 6, -10, wallTop - 4},
           Paint([=](double x, double y) { return shade(rgb(250, 252, 255), 0.9 + 0.1 * hashNoise(x * 0.5, y)); }));
    p.poly({(w - side) / 2.0, 6, double(w) + 4, wallTop - 14, double(w) + 4, wallTop - 6, w - side + 14, wallTop + 2},
           rgb(214, 224, 240));
    p.rect(26, 2, 10, 20, cylX(26, 36, rgb(170, 160, 150), 0.4, 0.2, 8));
    p.roundRect(24, -2, 14, 6, 3, rgb(250, 252, 255));
    return p.finish(rgb(40, 30, 24), 1.0);
}

// ---- forest: a log cabin with a mossy roof
Bitmap makeCabin() {
    const int w = 140, h = 100;
    Painter p(w, h, SS, RENDER_SCALE, 6, 6);
    Color log = rgb(140, 92, 56);
    const double top = 44, side = 22;
    p.rect(0, top, w - side, h - top, Paint([=](double x, double y) {
        double v = std::fmod(y - top, 8.0) / 8.0;                    // round logs
        Color c = shade(log, 0.7 + 0.45 * std::sin(v * M_PI));
        return shade(c, 0.92 + 0.12 * hashNoise(x * 0.2, std::floor((y - top) / 8)));
    }));
    for (double y = top; y < h; y += 8) {                            // log ends at the corners
        p.ellipse(2, y + 4, 4, 4, sphere(2, y + 4, 4, 4, rgb(200, 160, 110), 0.5, 0.1, 4));
        p.ellipse(w - side - 2, y + 4, 4, 4, sphere(w - side - 2, y + 4, 4, 4, rgb(200, 160, 110), 0.5, 0.1, 4));
    }
    p.poly({w - side, top, double(w), top - 6, double(w), double(h), w - side, double(h)}, shade(log, 0.55));
    for (double cx : {24.0, 94.0}) {
        p.rect(cx - 9, top + 14, 18, 16, vgrad(top + 14, top + 30, rgb(255, 214, 140), rgb(220, 150, 70)));
        p.rect(cx - 0.6, top + 14, 1.2, 16, shade(log, 0.6));
        p.rect(cx - 10, top + 30, 20, 3, shade(log, 0.8));
    }
    p.rect(52, top + 12, 18, h - top - 12, vgrad(top + 12, h, rgb(110, 70, 40), rgb(70, 44, 24)));   // door
    p.poly({-8, top + 2, (w - side) / 2.0, 6, w - side + 8, top + 2}, Paint([=](double x, double y) {   // mossy shingles
        Color c = lerpColor(rgb(90, 110, 60), rgb(60, 70, 44), (y - 6) / (top - 4));
        if (std::fmod(y, 5.0) < 0.8) c = shade(c, 0.75);
        return shade(c, 0.85 + 0.3 * hashNoise(x * 0.6, y * 0.6));
    }));
    p.poly({(w - side) / 2.0, 6, double(w) + 2, top - 10, double(w) + 2, top - 4, w - side + 8, top + 2}, rgb(56, 66, 40));
    p.rect(92, 4, 12, 24, Paint([](double x, double y) {             // stone chimney
        return shade(rgb(150, 146, 140), 0.75 + 0.4 * hashNoise(std::floor(x / 4), std::floor(y / 3)));
    }));
    return p.finish(rgb(30, 22, 14), 1.0);
}

// ---- sunset: a googie motel sign with a big arrow and a star
Bitmap makeMotelSign() {
    Painter p(110, 210, SS, RENDER_SCALE, 6, 10);
    p.rect(48, 70, 8, 140, cylX(48, 56, rgb(200, 190, 180), 0.45, 0.5, 18));
    // star on top
    std::vector<double> star;
    for (int i = 0; i < 10; ++i) {
        double a = -M_PI / 2 + i * M_PI / 5, r = i % 2 ? 7 : 17;
        star.push_back(52 + std::cos(a) * r); star.push_back(16 + std::sin(a) * r);
    }
    p.poly(star, vgrad(0, 32, rgb(255, 250, 200), rgb(255, 200, 60)));
    glow(p, 52, 16, 30, 26, rgb(255, 220, 120), 0.6);
    // the board
    p.roundRect(8, 36, 96, 34, 6, vgrad(36, 70, rgb(40, 200, 200), rgb(20, 130, 150)));
    p.imageCentered(ttf("MOTEL", FONT_DISPLAY, 19, rgb(255, 250, 240), rgb(255, 230, 200), rgb(120, 20, 60), 1.0), 56, 53);
    // the arrow, with chaser lights
    p.poly({0, 80, 82, 80, 82, 72, 108, 92, 82, 112, 82, 104, 0, 104}, vgrad(72, 112, rgb(255, 80, 120), rgb(200, 30, 80)));
    p.imageCentered(ttf("VACANCY", FONT_UI, 10, rgb(255, 255, 220), rgb(255, 240, 160), 0, 0), 42, 92);
    for (double x = 4; x < 80; x += 6) { p.ellipse(x, 84, 1.3, 1.3, rgb(255, 240, 160)); p.ellipse(x, 100, 1.3, 1.3, rgb(255, 240, 160)); }
    glow(p, 50, 92, 60, 22, rgb(255, 90, 140), 0.5);
    return p.finish(rgb(30, 10, 20), 1.0);
}

// ---- future: a glass habitat dome with a garden inside
Bitmap makeDome() {
    const int w = 170, h = 100;
    Painter p(w, h, SS, RENDER_SCALE, 4, 4);
    const double cx = 85, cy = 86, rx = 76, ry = 74;
    p.ellipse(cx, cy, rx, ry, Paint([=](double x, double y) {           // the garden inside
        if (y > cy) return TRANSPARENT;
        double n = hashNoise(x * 0.4, y * 0.4);
        Color c = lerpColor(rgb(60, 150, 100), rgb(140, 210, 150), (cy - y) / ry);
        return shade(c, 0.75 + 0.4 * n);
    }));
    for (int i = 0; i < 9; ++i) {                                         // a few round trees
        double tx = 30 + i * 14 + hashNoise(i, 3) * 6, ty = cy - 18 - hashNoise(i, 7) * 22, tr = 9 + hashNoise(i, 9) * 6;
        p.ellipse(tx, ty, tr, tr, sphere(tx, ty, tr, tr, rgb(40, 140, 90), 0.4, 0.15, 8));
    }
    p.ellipse(cx, cy, rx, ry, Paint([=](double x, double y) {           // the glass, with a gridded frame
        if (y > cy) return TRANSPARENT;
        double dx = (x - cx) / rx, dy = (y - cy) / ry, d = std::sqrt(dx * dx + dy * dy);
        double ang = std::atan2(-dy, dx);
        bool rib = std::fmod(ang * 8 / M_PI + 16, 1.0) < 0.06 || std::fmod(d * 6, 1.0) < 0.05;
        if (rib) return alpha(rgb(240, 250, 250), 0.9);
        double hl = std::max(0.0, 1 - std::hypot(dx + 0.4, dy + 0.55) / 0.35);
        return alpha(lerpColor(rgb(180, 230, 240), WHITE, hl), 0.25 + 0.5 * hl + 0.25 * std::pow(d, 6));
    }));
    p.rect(2, cy - 4, w - 4, h - cy + 4, vgrad(cy - 4, h, rgb(250, 250, 246), rgb(200, 200, 196)));   // white base ring
    for (double x = 10; x < w - 10; x += 16) p.rect(x, cy + 2, 8, 2, rgb(40, 190, 200));
    glow(p, cx, cy + 3, rx, 6, rgb(60, 220, 220), 0.4);
    return p.finish(rgb(30, 60, 70), 0.9);
}

// ---- Brazil: a carnival confetti cannon on a decorated pole, aimed at the road
Bitmap makeConfettiCannon() {
    Painter p(80, 160, SS, RENDER_SCALE, 6, 6);
    p.rect(36, 50, 8, 110, Paint([](double, double y) { return (int(std::floor(y / 8)) & 1) ? rgb(255, 230, 0) : rgb(0, 160, 80); }));
    for (int i = 0; i < 6; ++i) {                                         // streamers
        Color c[] = {rgb(255, 60, 140), rgb(0, 180, 255), rgb(255, 220, 0), rgb(140, 70, 255), rgb(0, 210, 120), rgb(255, 120, 30)};
        double x0 = 40, y0 = 60;
        for (double t = 0; t < 1; t += 0.05)
            p.line(x0 + (i - 2.5) * 10 * t + std::sin(t * 9 + i) * 2, y0 + t * 60, x0 + (i - 2.5) * 10 * (t + 0.05) + std::sin((t + 0.05) * 9 + i) * 2,
                   y0 + (t + 0.05) * 60, 1.4, c[i]);
    }
    // the cannon: a fat drum tilted towards the road
    p.poly({12, 30, 56, 14, 66, 40, 22, 56}, Paint([](double x, double y) {
        double u = (x * 0.35 + y * 0.94);
        return (int(std::floor(u / 7)) & 1) ? rgb(255, 60, 140) : rgb(255, 230, 40);
    }));
    p.ellipse(61, 27, 6, 13, sphere(61, 27, 6, 13, rgb(60, 30, 60), 0.5, 0.2, 8));
    for (int i = 0; i < 18; ++i) {                                        // confetti spilling out
        Color c[] = {rgb(255, 60, 140), rgb(0, 180, 255), rgb(255, 220, 0), rgb(140, 70, 255), rgb(0, 210, 120)};
        double x = 64 + hashNoise(i, 1) * 14, y = 10 + hashNoise(i, 2) * 34;
        p.rect(x, y, 2.4, 1.6, c[i % 5]);
    }
    p.ellipse(40, 50, 9, 6, sphere(40, 50, 9, 6, rgb(240, 200, 40), 0.5, 0.5, 20));
    return p.finish(rgb(40, 10, 40), 0.9);
}

// ---- Paris: a second apartment block style - pale grey stone, slate roof, rounded corner and a shop
Bitmap makeHaussmann2() {
    const int w = 140, h = 240;
    Painter p(w, h, SS, RENDER_SCALE, 4, 14);
    const double side = 20, roofY = 46;
    Color stone = rgb(222, 218, 210), slate = rgb(74, 82, 100);
    p.rect(0, roofY, w - side, h - roofY, Paint([=](double x, double y) {
        Color c = lerpColor(stone, shade(stone, 0.85), (y - roofY) / (h - roofY));
        if (std::fmod(y - roofY, 8.0) < 0.6) c = shade(c, 0.9);
        if (std::fmod(x + (int(std::floor((y - roofY) / 8)) & 1) * 9, 18.0) < 0.5) c = shade(c, 0.94);   // ashlar joints
        return c;
    }));
    p.poly({w - side, roofY, double(w), roofY - 8, double(w), double(h), w - side, double(h)}, vgrad(roofY, h, shade(stone, 0.68), shade(stone, 0.58)));
    // curved slate roof with oeil-de-boeuf dormers and a corner dome
    p.poly({-2, roofY, 6, 18, w - side - 6, 18, w - side + 2, roofY}, Paint([=](double x, double y) {
        Color c = lerpColor(shade(slate, 1.2), shade(slate, 0.8), (y - 18) / (roofY - 18));
        return std::fmod(x + (int(y / 3) & 1) * 2, 4.0) < 0.5 ? shade(c, 0.8) : c;
    }));
    p.poly({w - side + 2, roofY, w - side - 6, 18, double(w) - 6, 10, double(w), roofY - 8}, shade(slate, 0.6));
    p.ellipse(18, 18, 14, 16, Paint([=](double x, double y) {          // corner dome
        if (y > 18) return TRANSPARENT;
        return sphere(18, 18, 14, 16, slate, 0.45, 0.4, 14)(x, y);
    }));
    p.rect(17, -2, 2, 6, rgb(200, 170, 80));
    for (double x = 40; x < w - side - 14; x += 22) {
        p.ellipse(x, 32, 6, 7, rgb(236, 230, 220));
        p.ellipse(x, 32, 3.5, 4.5, rgb(46, 56, 76));
    }
    p.rect(-2, roofY - 3, w - side + 4, 5, rgb(240, 236, 226));       // cornice
    // windows: six floors, the 2nd with a long balcony and the top with little ones
    for (int f = 0; f < 6; ++f) {
        double y = roofY + 8 + f * 28;
        for (double x = 8; x < w - side - 12; x += 18) {
            p.rect(x - 1, y - 2, 12, 2, shade(stone, 0.82));
            p.rect(x, y, 10, 19, vgrad(y, y + 19, rgb(80, 96, 120), rgb(42, 48, 64)));
            p.rect(x + 4.5, y, 1, 19, rgb(226, 222, 212));
            if (f == 1) {
                p.rect(x - 2, y + 2, 14, 3, Paint([](double, double yy) { return yy < 0 ? BLACK : rgb(232, 228, 218); }));
            }
        }
        if (f == 1 || f == 4) {
            p.rect(2, y + 17, w - side - 6, 2, rgb(28, 28, 34));
            p.rect(2, y + 11, w - side - 6, 1.2, rgb(28, 28, 34));
            for (double bx = 3; bx < w - side - 4; bx += 2.4) p.rect(bx, y + 11, 0.6, 7, rgb(40, 40, 48));
        }
    }
    // ground floor: a bakery with a red awning
    p.rect(0, h - 26, w - side, 26, rgb(30, 70, 60));
    p.rect(6, h - 20, w - side - 12, 20, vgrad(h - 20, h, rgb(255, 226, 160), rgb(210, 160, 90)));
    p.poly({-4, h - 30, w - side + 4, h - 30, w - side + 8, h - 20, -8, h - 20}, Paint([](double x, double) {
        return (int(std::floor((x + 8) / 8)) & 1) ? rgb(250, 248, 240) : rgb(180, 30, 40);
    }));
    p.rect(20, h - 42, 80, 10, rgb(30, 70, 60));
    p.imageCentered(ttf("BOULANGERIE", FONT_UI, 7.5, rgb(250, 220, 140), rgb(240, 200, 110), 0, 0), 60, h - 37);
    return p.finish(rgb(36, 34, 32), 1.0);
}

Bitmap makeMorrisColumn() {   // the green Paris advertising column with posters and a little dome
    Painter p(48, 130, SS, RENDER_SCALE, 4, 6);
    Color green = rgb(40, 90, 70);
    p.rect(4, 120, 40, 10, cylX(4, 44, green, 0.35, 0.5, 20));
    p.rect(8, 30, 32, 92, Paint([=](double x, double y) {
        Color posters[] = {rgb(240, 200, 60), rgb(220, 70, 60), rgb(70, 140, 220), rgb(240, 240, 230), rgb(200, 120, 200)};
        int band = int(std::floor((y - 30) / 23)), col = int(std::floor((x - 8) / 11));
        Color c = posters[(band * 2 + col) % 5];
        if (hashNoise(std::floor(x), std::floor(y / 2)) > 0.86) c = shade(c, 0.7);   // printing
        return cylX(8, 40, c, 0.45, 0.25, 10)(x, y);
    }));
    p.rect(5, 26, 38, 5, cylX(5, 43, green, 0.35, 0.6, 20));
    p.ellipse(24, 26, 19, 17, Paint([=](double x, double y) {
        if (y > 26) return TRANSPARENT;
        return sphere(24, 26, 19, 17, green, 0.4, 0.5, 18)(x, y);
    }));
    p.ellipse(24, 8, 3, 3, sphere(24, 8, 3, 3, green, 0.4, 0.5, 18));
    return p.finish(rgb(14, 30, 24), 0.9);
}

// ---- Germany: more half-timbered houses and a village church
// style 0: ochre plaster, red beams, eaves to the front (a long, low farmhouse with a jettied upper floor)
// style 1: white plaster, black beams, a tall narrow town house with St Andrew's crosses and a stepped gable
Bitmap makeTimberHouse(int style) {
    const bool town = style == 1;
    const int w = town ? 110 : 170, h = town ? 230 : 160;
    Painter p(w, h, SS, RENDER_SCALE, 6, 6);
    Color plaster = town ? rgb(246, 244, 238) : rgb(236, 200, 120);
    Color beam = town ? rgb(40, 32, 30) : rgb(140, 50, 36);
    Color tile = town ? rgb(150, 70, 50) : rgb(110, 110, 120);          // town: red tiles; farm: grey slate
    Color shutter = town ? rgb(150, 40, 40) : rgb(40, 80, 140);
    const double side = town ? 18 : 24;
    const double fw = w - side;
    const double wallTop = town ? 98 : 74;
    const int floors = town ? 3 : 2;
    const double fh = (h - wallTop) / floors;
    p.rect(0, wallTop, fw, h - wallTop, vgrad(wallTop, h, plaster, shade(plaster, 0.88)));
    p.poly({fw, wallTop, double(w), wallTop - 8, double(w), double(h), fw, double(h)}, shade(plaster, 0.68));
    const int bays = town ? 3 : 5;
    const double bw = fw / bays;
    for (int f = 0; f < floors; ++f) {
        double y0 = wallTop + f * fh, y1 = y0 + fh;
        p.rect(0, y0, fw, 4, beam);
        for (int b = 0; b <= bays; ++b) p.rect(std::min(b * bw, fw - 4), y0, 4, fh, beam);
        for (int b = 0; b < bays; ++b) {
            double x0 = b * bw + 4, x1 = (b + 1) * bw;
            double cx = (x0 + x1) / 2;
            bool door = f == floors - 1 && b == bays / 2;
            if (door) {
                p.roundRect(cx - 9, y0 + 10, 18, y1 - y0 - 10, 8, vgrad(y0 + 10, y1, rgb(100, 60, 34), rgb(60, 32, 18)));
                continue;
            }
            // braces: St Andrew's cross (town) or a K-brace (farm) under the window
            if (town) { p.line(x0, y1, cx - 1, y1 - fh * 0.32, 2.4, beam); p.line(x1, y1, cx + 1, y1 - fh * 0.32, 2.4, beam);
                        p.line(x0, y1 - fh * 0.32, x1, y1, 2.0, beam); p.line(x1, y1 - fh * 0.32, x0, y1, 2.0, beam); }
            else      { p.line(x0, y0 + 4, cx - 8, y0 + fh * 0.45, 2.4, beam); p.line(x1, y0 + 4, cx + 8, y0 + fh * 0.45, 2.4, beam); }
            double wy = y0 + fh * 0.2, wh = fh * 0.42;
            p.rect(cx - 7, wy, 14, wh, vgrad(wy, wy + wh, rgb(170, 200, 226), rgb(60, 80, 110)));
            p.rect(cx - 0.6, wy, 1.2, wh, plaster);
            p.rect(cx - 7, wy + wh / 2, 14, 1.2, plaster);
            p.rect(cx - 12, wy, 5, wh, shutter);
            p.rect(cx + 7, wy, 5, wh, shutter);
            if (!town || f == 1) {                                    // flower boxes
                p.rect(cx - 9, wy + wh, 18, 3.5, rgb(110, 66, 40));
                for (double fx = cx - 8; fx < cx + 8; fx += 3)
                    p.ellipse(fx + 1, wy + wh - 0.5, 1.6, 1.6, hashNoise(fx, wy) < 0.5 ? rgb(240, 40, 60) : rgb(255, 240, 120));
            }
        }
    }
    p.rect(0, h - 4, fw, 4, beam);
    if (town) {   // stepped gable facing the street, with a little round window
        std::vector<double> g = {-2, wallTop + 2};
        const int steps = 5;
        for (int i = 0; i < steps; ++i) {
            double x = i * fw / (2 * steps), y = wallTop - (i + 1) * (wallTop - 14) / steps;
            g.insert(g.end(), {x, wallTop - i * (wallTop - 14) / steps, x, y});
        }
        for (int i = steps - 1; i >= 0; --i) {
            double x = fw - i * fw / (2 * steps), y = wallTop - (i + 1) * (wallTop - 14) / steps;
            g.insert(g.end(), {x, y, x, wallTop - i * (wallTop - 14) / steps});
        }
        g.insert(g.end(), {fw + 2, wallTop + 2});
        p.poly(g, vgrad(14, wallTop, shade(plaster, 1.0), shade(plaster, 0.9)));
        p.ellipse(fw / 2, wallTop - 46, 9, 9, beam);
        p.ellipse(fw / 2, wallTop - 46, 6.5, 6.5, vgrad(wallTop - 52, wallTop - 40, rgb(170, 200, 226), rgb(60, 80, 110)));
        for (double y : {wallTop - 22.0, wallTop - 66.0}) p.rect(fw * 0.2, y, fw * 0.6, 3, beam);
    } else {      // the eaves face the street: a long roof with two little dormers
        p.poly({-10, wallTop + 4, 18, 14, fw - 18, 14, fw + 10, wallTop + 4}, Paint([=](double x, double y) {
            Color c = lerpColor(shade(tile, 1.2), shade(tile, 0.8), (y - 14) / (wallTop - 10));
            if (std::fmod(y, 4.0) < 0.7) c = shade(c, 0.75);
            if (std::fmod(x + std::floor(y / 4) * 3, 7.0) < 0.5) c = shade(c, 0.88);
            return c;
        }));
        p.poly({fw + 10, wallTop + 4, fw - 18, 14, double(w) - 4, 8, double(w) + 4, wallTop - 6}, shade(tile, 0.6));
        for (double cx : {fw * 0.3, fw * 0.7}) {
            p.rect(cx - 10, 30, 20, 22, plaster);
            p.poly({cx - 13, 32, cx, 20, cx + 13, 32}, shade(tile, 0.9));
            p.rect(cx - 6, 36, 12, 13, vgrad(36, 49, rgb(170, 200, 226), rgb(60, 80, 110)));
        }
        p.rect(fw * 0.82, 2, 10, 20, cylX(fw * 0.82, fw * 0.82 + 10, rgb(160, 80, 60), 0.4, 0.2, 8));
    }
    return p.finish(rgb(36, 24, 16), 1.0);
}

Bitmap makeChurch() {   // a white village church with an onion-domed tower and a clock
    const int w = 130, h = 250;
    Painter p(w, h, SS, RENDER_SCALE, 4, 6);
    Color wall = rgb(246, 242, 232), roof = rgb(170, 70, 50), copper = rgb(60, 140, 110);
    // nave (to the right, partly hidden behind the tower)
    p.rect(50, 150, 74, 100, vgrad(150, h, wall, shade(wall, 0.86)));
    p.poly({44, 152, 87, 112, 130, 152}, vgrad(112, 152, shade(roof, 1.1), shade(roof, 0.8)));
    for (double x : {66.0, 88.0, 110.0}) p.roundRect(x - 5, 170, 10, 30, 5, vgrad(170, 200, rgb(120, 150, 190), rgb(50, 60, 90)));
    // tower
    p.rect(14, 84, 44, h - 84, Paint([=](double x, double y) {
        Color c = lerpColor(wall, shade(wall, 0.85), (y - 84) / (h - 84));
        return x > 48 ? shade(c, 0.85) : c;
    }));
    for (double y : {84.0, 140.0, 196.0}) p.rect(12, y, 48, 4, rgb(220, 214, 200));
    p.ellipse(36, 110, 13, 13, rgb(30, 40, 60));                                        // clock
    p.ellipse(36, 110, 11, 11, rgb(240, 236, 220));
    p.line(36, 110, 36, 102, 1.3, rgb(30, 30, 30));
    p.line(36, 110, 42, 113, 1.3, rgb(30, 30, 30));
    for (int i = 0; i < 12; ++i) p.ellipse(36 + std::cos(i * M_PI / 6) * 9, 110 + std::sin(i * M_PI / 6) * 9, 0.7, 0.7, rgb(40, 40, 40));
    p.roundRect(28, 154, 16, 26, 8, vgrad(154, 180, rgb(60, 50, 50), rgb(30, 24, 24)));   // belfry opening
    p.roundRect(26, 214, 20, h - 214, 10, vgrad(214, h, rgb(110, 70, 40), rgb(70, 40, 20)));   // door
    // onion dome with a lantern and a cross
    p.rect(18, 76, 36, 8, shade(copper, 0.9));
    p.ellipse(36, 62, 20, 18, sphere(36, 62, 20, 18, copper, 0.4, 0.5, 18));
    p.poly({24, 52, 36, 26, 48, 52}, cylX(24, 48, copper, 0.4, 0.5, 18));
    p.rect(31, 36, 10, 12, rgb(240, 236, 226));
    p.ellipse(36, 32, 7, 6, sphere(36, 32, 7, 6, copper, 0.4, 0.5, 18));
    p.rect(35.2, 8, 1.6, 20, rgb(220, 190, 80));
    p.rect(31, 14, 10, 1.6, rgb(220, 190, 80));
    return p.finish(rgb(40, 34, 30), 1.0);
}

// ---- Tuscany: more houses and a vineyard
// style 0: a long stone farmhouse (casale) with a loggia of arches and an outside staircase
// style 1: a three-storey rose-pink town house with green shutters, a balcony and deep eaves
Bitmap makeTuscanHouse(int style) {
    const bool town = style == 1;
    const int w = town ? 120 : 180, h = town ? 170 : 120;
    Painter p(w, h, SS, RENDER_SCALE, 8, 6);
    const double side = town ? 18 : 26, fw = w - side, top = town ? 30 : 46;
    Color roof = rgb(190, 86, 50), shutter = rgb(60, 110, 70);
    Paint wallPaint = town
        ? Paint([=](double x, double y) {
              Color c = lerpColor(rgb(232, 170, 150), rgb(214, 150, 130), (y - top) / (h - top));
              return shade(c, 0.94 + 0.1 * hashNoise(x * 0.6, y * 0.6));
          })
        : Paint([=](double x, double y) {                                 // rough stone blocks
              int row = int(std::floor((y - top) / 7));
              double off = (row & 1) * 6;
              double u = std::fmod(x + off, 12.0), v = std::fmod(y - top, 7.0);
              Color c = shade(rgb(200, 182, 150), 0.82 + 0.26 * hashNoise(std::floor((x + off) / 12), row));
              return (u < 0.8 || v < 0.8) ? shade(c, 0.72) : c;
          });
    p.rect(0, top, fw, h - top, wallPaint);
    p.poly({fw, top, double(w), top - 6, double(w), double(h), fw, double(h)}, town ? Paint(rgb(170, 116, 100)) : Paint(rgb(150, 136, 110)));
    // roof with deep eaves
    p.poly({-8, top + 2, 10, top - 16, fw - 8, top - 16, fw + 8, top + 2}, Paint([=](double x, double y) {
        Color c = lerpColor(shade(roof, 1.12), shade(roof, 0.85), (y - top + 16) / 18);
        return std::fmod(x, 4.0) < 0.8 ? shade(c, 0.8) : c;
    }));
    p.poly({fw + 8, top + 2, fw - 8, top - 16, double(w), top - 20, double(w) + 4, top - 4}, shade(roof, 0.65));
    p.rect(-8, top + 2, fw + 16, 2.5, rgb(120, 80, 60));
    if (town) {
        for (int f = 0; f < 3; ++f) {
            double y = top + 12 + f * 44;
            for (double x = 14; x < fw - 14; x += 30) {
                bool door = f == 2 && std::fabs(x - 44) < 1;
                if (door) { p.roundRect(x - 2, y - 2, 20, h - y + 2, 10, vgrad(y, h, rgb(90, 60, 40), rgb(50, 30, 20))); continue; }
                p.rect(x, y, 14, 22, rgb(50, 40, 36));
                p.rect(x - 7, y, 7, 22, shutter);
                p.rect(x + 14, y, 7, 22, shutter);
                for (double sy = y + 2; sy < y + 22; sy += 3) { p.rect(x - 7, sy, 7, 0.6, shade(shutter, 0.7)); p.rect(x + 14, sy, 7, 0.6, shade(shutter, 0.7)); }
                p.rect(x - 2, y - 3, 18, 3, rgb(240, 220, 200));
            }
            if (f == 1) {                                               // a wrought-iron balcony with geraniums
                p.rect(8, y + 22, fw - 16, 3, rgb(40, 36, 34));
                for (double bx = 9; bx < fw - 8; bx += 3) p.rect(bx, y + 14, 0.8, 9, rgb(40, 36, 34));
                for (double bx = 12; bx < fw - 10; bx += 5) p.ellipse(bx, y + 13, 2.4, 2, hashNoise(bx, y) < 0.6 ? rgb(230, 40, 50) : rgb(80, 150, 60));
            }
        }
    } else {
        // the loggia: three arches on the ground floor
        for (int i = 0; i < 3; ++i) {
            double x = 12 + i * 26;
            p.roundRect(x, top + 38, 20, h - top - 38, 10, vgrad(top + 38, h, rgb(70, 50, 40), rgb(40, 28, 22)));
        }
        // outside staircase up to a door, with a low wall
        p.poly({96, double(h), 140, top + 36, 148, top + 36, 148, top + 42, 104, double(h)}, shade(rgb(200, 182, 150), 0.86));
        for (double t = 0; t < 1; t += 0.12) p.rect(96 + 44 * t, h - (h - top - 36) * t - 2, 8, 1, rgb(140, 120, 100));
        p.roundRect(128, top + 10, 16, 26, 8, vgrad(top + 10, top + 36, rgb(100, 66, 40), rgb(60, 36, 20)));
        for (double x : {18.0, 50.0, 82.0}) {                          // upper windows
            p.rect(x, top + 10, 12, 16, rgb(50, 44, 40));
            p.rect(x - 6, top + 10, 6, 16, shutter);
            p.rect(x + 12, top + 10, 6, 16, shutter);
        }
        // the dovecote tower in the middle of the roof
        p.rect(54, top - 40, 28, 26, wallPaint);
        p.poly({50, top - 38, 68, top - 52, 86, top - 38}, vgrad(top - 52, top - 38, shade(roof, 1.1), shade(roof, 0.85)));
        for (double x : {58.0, 66.0, 74.0}) p.roundRect(x - 2, top - 32, 4, 8, 2, rgb(40, 30, 24));
        // potted lemon trees by the wall
        for (double x : {4.0, 160.0}) {
            p.rect(x, h - 10, 12, 10, cylX(x, x + 12, rgb(190, 100, 60), 0.45, 0.2, 8));
            p.ellipse(x + 6, h - 18, 9, 9, sphere(x + 6, h - 18, 9, 9, rgb(60, 130, 60), 0.4, 0.1, 6));
            for (int k = 0; k < 4; ++k) p.ellipse(x + 2 + k * 3, h - 20 + (k & 1) * 4, 1.5, 1.5, rgb(250, 220, 40));
        }
    }
    return p.finish(rgb(50, 32, 24), 1.0);
}

Bitmap makeVines() {   // a block of vineyard rows: posts, green leafy rows and purple grapes
    const int w = 180, h = 54;
    Painter p(w, h, SS, RENDER_SCALE, 2, 4);
    for (int row = 0; row < 4; ++row) {                      // back rows first, a little higher and greyer
        double base = 30 + row * 8, k = 0.8 + row * 0.07;
        for (double x = 4 + row * 3; x < w - 4; x += 30) p.rect(x, base - 20, 2.4, 20, rgb(120, 90, 60));
        p.rect(0, base - 16, w, 0.8, rgb(150, 150, 150));
        for (double x = 2; x < w - 2; x += 3.5) {
            double r = 4 + 2.5 * hashNoise(x, row), y = base - 12 + 3 * hashNoise(x * 3, row * 7);
            Color c = shade(lerpColor(rgb(60, 120, 40), rgb(130, 170, 60), hashNoise(x * 0.7, row)), k);
            p.ellipse(x, y, r, r * 0.85, sphere(x, y, r, r * 0.85, c, 0.45, 0.1, 6));
        }
        for (double x = 8; x < w - 8; x += 9 + 6 * hashNoise(x, row + 1))
            p.ellipse(x, base - 7, 2, 3, sphere(x, base - 7, 2, 3, shade(rgb(110, 40, 110), k), 0.4, 0.4, 12));
    }
    p.rect(0, h - 4, w, 4, rgb(150, 120, 80));
    return p.finish(rgb(30, 40, 20), 0.8);
}

// ===================================================== stages 12-13 (1.4) ====
// Road bridges, the autumn forest and the rainy night city. Each is generated in its own seeded block.

// A concrete road bridge spanning the road (in the spirit of the bridges in Jaguar XJ220): a deck with a railing.
// It tiles seamlessly sideways - the renderer repeats it out to both screen edges. Its pillars are separate
// sprites (they collide).
// Layout 640 x 150; the deck's underside is at y = 50, i.e. about 3600 world units above the road.
Bitmap makeBridge(bool night) {
    const int w = 640, h = 150;
    Painter p(w, h, SS, RENDER_SCALE, 2, 2);
    Color conc = night ? rgb(110, 110, 114) : rgb(196, 192, 182);
    // the deck repeats seamlessly sideways (the renderer tiles it out to the screen edges)
    // the deck: fascia with a darker band and drip stains, the underside in shadow
    p.rect(0, 14, w, 36, Paint([=](double x, double y) {
        Color c = vgrad(14, 50, shade(conc, 1.05), shade(conc, 0.82))(x, y);
        if (y > 22 && y < 26) c = shade(c, 0.86);
        if (y > 30 && hashNoise(std::fmod(std::floor(x / 3), 213.0), 1) > 0.7) c = shade(c, 0.9 - 0.1 * (y - 30) / 20);   // stains
        if (std::fmod(x, 80.0) < 1.0) c = shade(c, 0.8);                                                 // joints
        return c;
    }));
    p.rect(0, 48, w, 3, shade(conc, 0.45));
    // railing: posts and two rails, plus a lamp post every so often
    Color rail = night ? rgb(130, 134, 140) : rgb(110, 116, 124);
    p.rect(0, 2, w, 2, rail);
    p.rect(0, 8, w, 2, rail);
    for (double x = 4; x < w; x += 10) p.rect(x, 2, 1.6, 12, rail);
    for (double x = 80; x < w; x += 160) {
        p.rect(x, -24, 2.2, 26, rail);
        p.rect(x - 4, -26, 10, 3, rail);
        if (night) glow(p, x + 1, -22, 10, 6, rgb(255, 190, 110), 1.0);
    }
    return p.finish(0, 0);
}

Bitmap makeBridgePillar(bool night) {
    Painter p(40, 160, SS, RENDER_SCALE, 2, 2);
    Color conc = night ? rgb(104, 104, 108) : rgb(188, 184, 174);
    p.rect(4, 0, 32, 160, cylX(4, 36, conc, 0.4, 0.15, 6));
    for (double y = 20; y < 160; y += 26) p.rect(4, y, 32, 0.8, shade(conc, 0.85));
    p.rect(0, 150, 40, 10, shade(conc, 0.8));                                   // footing
    p.rect(4, 0, 32, 14, shade(conc, 0.55));                                    // in the deck's shadow
    return p.finish(rgb(30, 30, 32), 0.8);
}

// ---- autumn forest
// A broad-leaved tree in autumn colours: style 0 orange-red maple, 1 a slender golden birch
Bitmap makeAutumnTree(int style) {
    const bool birch = style == 1;
    Painter p(100, birch ? 170 : 150, SS, RENDER_SCALE, 2, 2);
    const double H = birch ? 170 : 150;
    Color bark = birch ? rgb(232, 228, 216) : rgb(110, 84, 60);
    p.poly({birch ? 47.0 : 44.0, H, 47, H * 0.5, 53, H * 0.5, birch ? 53.0 : 56.0, H}, Paint([=](double x, double y) {
        double nx = std::clamp((x - 50) / 6, -0.98, 0.98);
        Color c = lit(bark, nx, 0, std::sqrt(1 - nx * nx), 0.4, 0.1, 6);
        if (birch && hashNoise(std::floor(x), std::floor(y / 3)) > 0.78) c = rgb(50, 46, 44);   // birch marks
        return c;
    }));
    p.line(50, H * 0.62, 32, H * 0.48, 2.5, shade(bark, 0.9));
    p.line(50, H * 0.58, 68, H * 0.45, 2.5, shade(bark, 0.9));
    std::vector<std::array<double, 4>> balls;
    const int nb = birch ? 26 : 24;
    for (int i = 0; i < nb; ++i) {
        double a = rnd(0, 6.283), r = rnd(0, 1);
        double cx = 50 + std::cos(a) * r * (birch ? 24 : 34), cy = (birch ? 62 : 52) + std::sin(a) * r * (birch ? 50 : 34);
        balls.push_back({cx, cy, rnd(birch ? 9 : 12, birch ? 14 : 19), rnd(0, 1)});
    }
    std::sort(balls.begin(), balls.end(), [](auto& a, auto& b) { return a[1] < b[1]; });
    for (auto& b : balls) {
        Color base = birch ? lerpColor(rgb(230, 190, 40), rgb(250, 220, 90), b[3])
                           : (b[3] < 0.45 ? lerpColor(rgb(210, 70, 30), rgb(230, 110, 30), b[3] / 0.45)
                                          : lerpColor(rgb(240, 140, 30), rgb(240, 190, 50), (b[3] - 0.45) / 0.55));
        p.ellipse(b[0], b[1], b[2], b[2] * 0.9, Paint([=](double x, double y) {
            Color c = sphere(b[0], b[1], b[2], b[2] * 0.9, base, 0.38, 0.12, 8)(x, y);
            double n = hashNoise(x * 2.2, y * 2.2);
            return n > 0.82 ? shade(c, 1.18) : n < 0.15 ? shade(c, 0.74) : c;
        }));
    }
    return p.finish(rgb(50, 24, 10), 1.0);
}

Bitmap makeOak() {   // a big old oak in deep red and bronze: it stands where the gusts of leaves blow
    Painter p(150, 170, SS, RENDER_SCALE, 2, 2);
    Color bark = rgb(96, 74, 56);
    p.poly({58, 170, 64, 96, 86, 96, 92, 170}, Paint([=](double x, double y) {
        double nx = std::clamp((x - 75) / 13, -0.98, 0.98);
        Color c = lit(bark, nx, 0, std::sqrt(1 - nx * nx), 0.4, 0.1, 6);
        return hashNoise(x * 0.8, y * 0.3) > 0.7 ? shade(c, 0.78) : c;
    }));
    for (auto b : std::vector<std::array<double, 5>>{{75, 110, 36, 72, 5}, {75, 106, 116, 76, 5}, {75, 100, 70, 52, 4}, {44, 82, 26, 60, 3}})
        p.line(b[0], b[1], b[2], b[3], b[4], shade(bark, 0.95));
    std::vector<std::array<double, 4>> balls;
    for (int i = 0; i < 40; ++i) {
        double a = rnd(0, 6.283), r = std::sqrt(rnd(0, 1));
        balls.push_back({75 + std::cos(a) * r * 60, 62 + std::sin(a) * r * 40, rnd(13, 21), rnd(0, 1)});
    }
    std::sort(balls.begin(), balls.end(), [](auto& a, auto& b) { return a[1] < b[1]; });
    for (auto& b : balls) {
        Color base = b[3] < 0.5 ? lerpColor(rgb(170, 40, 30), rgb(200, 70, 30), b[3] * 2)
                                : lerpColor(rgb(190, 110, 40), rgb(160, 90, 40), (b[3] - 0.5) * 2);
        p.ellipse(b[0], b[1], b[2], b[2] * 0.85, Paint([=](double x, double y) {
            Color c = sphere(b[0], b[1], b[2], b[2] * 0.85, base, 0.38, 0.12, 8)(x, y);
            double n = hashNoise(x * 2.4, y * 2.4);
            return n > 0.84 ? shade(c, 1.2) : n < 0.14 ? shade(c, 0.72) : c;
        }));
    }
    return p.finish(rgb(40, 14, 8), 1.0);
}

Bitmap makeLeaf(int kind) {   // a small falling leaf: red-orange maple or golden
    Painter p(20, 20, SS, RENDER_SCALE, 1, 1);
    Color base = kind == 0 ? rgb(214, 80, 30) : rgb(236, 176, 40);
    p.poly({10, 1, 13, 5, 18, 4, 16, 9, 19, 12, 13, 13, 11, 19, 9, 19, 7, 13, 1, 12, 4, 9, 2, 4, 7, 5}, Paint([=](double x, double y) {
        Color c = lerpColor(shade(base, 1.15), shade(base, 0.8), y / 20);
        if (std::fabs(x - 10) < 0.5 || std::fabs((y - 11) + std::fabs(x - 10) * 0.7) < 0.45) c = shade(c, 0.72);   // veins
        return c;
    }));
    p.rect(9.6, 17, 0.8, 3, shade(base, 0.5));
    return p.finish(shade(base, 0.45), 0.4);
}

// ---- rainy night city
// A street lamp with a warm sodium light: its cone of light falls through the rain onto a pool of light on the
// road. The pole is on the left, the head reaches out to the right (over the road; mirrored on the right side).
Bitmap makeRainLamp() {
    const int w = 120, h = 200;
    Painter p(w, h, SS, RENDER_SCALE, 2, 2);
    Color metal = rgb(70, 72, 80);
    // the pool of light on the wet road under the head (flat, wide, brightest in the middle)
    p.ellipse(78, h - 3, 42, 5, Paint([=](double x, double y) {
        double dx = (x - 78) / 42, dy = (y - (h - 3)) / 5, d = std::sqrt(dx * dx + dy * dy);
        return alpha(rgb(255, 214, 150), 0.95 * std::pow(std::max(0.0, 1 - d), 1.2));
    }));
    // the cone: from the lamp head down to the road, fading and widening, with faint rain streaks in it
    p.poly({70, 20, 86, 20, 120, double(h - 2), 36, double(h - 2)}, Paint([=](double x, double y) {
        double t = (y - 20) / (h - 22);
        double half = 8 + 34 * t, u = std::fabs(x - 78) / half;
        double a = 0.62 * (1 - 0.45 * t) * std::pow(std::max(0.0, 1 - u), 1.0);
        if (std::fmod(x * 3 + y * 0.6, 7.0) < 0.35 && hashNoise(std::floor(x * 3 + y * 0.6), 3) > 0.5) a *= 1.5;   // rain glints
        return alpha(rgb(255, 222, 160), std::min(0.9, a));
    }));
    // pole, arm and the lamp head
    p.rect(8, 16, 5, h - 16, cylX(8, 13, metal, 0.35, 0.6, 24));
    p.roundRect(5, h - 14, 11, 14, 2, cylX(5, 16, shade(metal, 0.8), 0.35, 0.5, 20));
    p.line(10, 18, 70, 14, 3.0, metal);
    p.roundRect(66, 10, 24, 9, 3, cylY(10, 19, rgb(58, 60, 66), 0.35, 0.6, 24));
    p.roundRect(68, 18, 20, 3, 1.5, rgb(255, 214, 150));
    glow(p, 78, 21, 26, 16, rgb(255, 200, 120), 1.6);
    return p.finish(rgb(10, 10, 14), 0.8);
}

// Dark brick apartment blocks in the rain: few, warm-lit windows, fire escapes, a lit shop at street level
Bitmap makeRainBuilding(int kind, bool lightsOnly = false) {   // lightsOnly: just the lit windows, at the same size
    const int w = kind == 0 ? 110 : 140, h = kind == 0 ? 240 : 180;
    Painter p(w, h, SS, RENDER_SCALE, 2, 10);
    Color brick = kind == 0 ? rgb(70, 48, 42) : rgb(58, 58, 64);
    const double side = w * 0.16, fw = w - side;
    if (lightsOnly) {   // the lit windows, nothing else
        for (double y = 22; y < h - 30; y += 16)
            for (double x = 7; x < fw - 10; x += 15) {
                double r = hashNoise(x * 3 + kind * 11, y * 1.7);
                if (r >= 0.32) continue;
                Color c = r < 0.2 ? rgb(255, 196, 110) : rgb(250, 220, 160);
                p.rect(x, y, 9, 11, vgrad(y, y + 11, c, shade(c, 0.75)));
                glow(p, x + 4.5, y + 5.5, 8, 8, c, 0.35);
            }
        return p.finish(0, 0);
    }
    p.rect(0, 10, fw, h - 10, Paint([=](double x, double y) {
        int row = int(std::floor(y / 3));
        double off = (row & 1) * 3;
        Color c = shade(brick, 0.85 + 0.25 * hashNoise(std::floor((x + off) / 6), row));
        if (std::fmod(y, 3.0) < 0.5 || std::fmod(x + off, 6.0) < 0.5) c = shade(brick, 0.6);
        // wet: a faint sheen running down
        return lerpColor(c, rgb(120, 130, 150), 0.08 * hashNoise(std::floor(x), 7));
    }));
    p.poly({fw, 10, double(w), 4, double(w), double(h), fw, double(h)}, shade(brick, 0.5));
    p.rect(-1, 6, fw + 2, 5, shade(brick, 1.2));                               // cornice
    // windows: mostly dark, some warm
    for (double y = 22; y < h - 30; y += 16)
        for (double x = 7; x < fw - 10; x += 15) {
            double r = hashNoise(x * 3 + kind * 11, y * 1.7);
            bool on = r < 0.32;
            Color c = r < 0.2 ? rgb(255, 196, 110) : rgb(250, 220, 160);
            p.rect(x, y, 9, 11, on ? vgrad(y, y + 11, c, shade(c, 0.6)) : vgrad(y, y + 11, rgb(26, 28, 36), rgb(16, 16, 22)));
            if (on) glow(p, x + 4.5, y + 5.5, 8, 8, c, 0.35);
            p.rect(x - 1, y + 11, 11, 1.4, shade(brick, 1.3));
        }
    // a zig-zag fire escape
    if (kind == 0) {
        Color iron = rgb(40, 40, 44);
        for (double y = 36; y < h - 40; y += 32) {
            p.rect(fw * 0.55, y, fw * 0.38, 1.6, iron);
            for (double x = fw * 0.55; x < fw * 0.93; x += 3) p.rect(x, y - 6, 0.8, 6, iron);
            p.line(fw * 0.57, y, fw * 0.9, y + 32, 1.4, iron);
        }
    }
    // street level: a lit shop window under an awning
    p.rect(0, h - 26, fw, 26, rgb(20, 20, 24));
    p.rect(6, h - 22, fw - 12, 18, vgrad(h - 22, h - 4, rgb(255, 220, 160), rgb(200, 130, 70)));
    p.rect(2, h - 28, fw - 4, 5, kind == 0 ? rgb(40, 80, 60) : rgb(110, 30, 34));
    glow(p, fw / 2, h - 12, fw * 0.5, 12, rgb(255, 200, 130), 0.5);
    return p.finish(rgb(6, 6, 10), 1.0);
}

// A rain puddle lying on the road: blue water that mirrors the night sky (deep blue in the near part, paler
// towards the far edge, as reflections do), long shimmering streaks of the street lamps and a bright wet rim.
Bitmap makePuddle() {
    const int w = 200, h = 26;
    Painter p(w, h, SS, RENDER_SCALE, 1, 1);
    p.ellipse(100, 13, 98, 12, Paint([=](double x, double y) {
        double dx = (x - 100) / 98, dy = (y - 13) / 12, d = std::sqrt(dx * dx + dy * dy);
        if (d > 1) return TRANSPARENT;
        // the sky in the water: paler at the far edge (top), deep blue near
        Color c = lerpColor(rgb(90, 140, 210), rgb(24, 60, 130), 0.5 + 0.5 * dy);
        // street-lamp reflections: long vertical streaks, broken up by ripples
        for (double sx : {34.0, 92.0, 150.0, 176.0}) {
            double wob = std::sin(y * 1.9 + sx) * 1.6;
            double s = std::max(0.0, 1 - std::fabs(x - sx - wob) / (sx > 160 ? 3.0 : 5.5));
            c = lerpColor(c, sx > 160 ? rgb(200, 230, 255) : rgb(255, 214, 140), 0.85 * s * (0.6 + 0.4 * std::fabs(std::sin(y * 0.9 + sx))));
        }
        // a sheen near the far edge
        c = lerpColor(c, rgb(190, 220, 255), 0.35 * std::max(0.0, -dy - 0.3));
        // ripple rings from the rain
        for (int k = 0; k < 7; ++k) {
            double rx = 16 + hashNoise(k, 3) * 168, ry = 6 + hashNoise(k, 5) * 14, rr = 3 + hashNoise(k, 7) * 6;
            double dd = std::hypot((x - rx) / rr, (y - ry) / (rr * 0.35));
            if (std::fabs(dd - 1) < 0.18) c = lerpColor(c, rgb(210, 230, 255), 0.55);
        }
        // the wet rim: a thin bright edge
        if (d > 0.9) c = lerpColor(c, rgb(150, 180, 220), (d - 0.9) / 0.1 * 0.7);
        return alpha(c, d > 0.94 ? 0.95 * (1 - d) / 0.06 + 0.05 : 0.95);
    }));
    return p.finish(0, 0);
}
} // namespace

void Graphics::generate() {
    sprite[SPR_PALM] = makePalm();
    sprite[SPR_PINE] = makePine(false);
    sprite[SPR_PINE_SNOW] = makePine(true);
    sprite[SPR_BUSH] = makeBush();
    sprite[SPR_CACTUS] = makeCactus();
    sprite[SPR_ROCK] = makeRock();
    sprite[SPR_LAMP] = makeLamp();
    sprite[SPR_BILLBOARD1] = makeBillboard(0);
    sprite[SPR_BILLBOARD2] = makeBillboard(1);
    sprite[SPR_BILLBOARD3] = makeBillboard(2);
    sprite[SPR_BUILDING1] = makeBuilding(0);
    sprite[SPR_BUILDING2] = makeBuilding(1);
    sprite[SPR_SNOWMAN] = makeSnowman();
    sprite[SPR_TURN_RIGHT] = makeTurnSign();
    sprite[SPR_TURN_LEFT] = sprite[SPR_TURN_RIGHT].flipped();
    sprite[SPR_GANTRY_START] = makeGantry(true);
    sprite[SPR_GANTRY_CHECK] = makeGantry(false);
    sprite[SPR_SIGN_JUMP] = makeWarningSign(0);
    sprite[SPR_SIGN_NARROW] = makeWarningSign(1);
    sprite[SPR_SIGN_TUNNEL] = makeWarningSign(2);
    sprite[SPR_SIGN_DIRT] = makeWarningSign(3);
    {   // brighter core for tunnel lamps
        Bitmap g(32, 32);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                double dx = (x + 0.5 - 16) / 16, dy = (y + 0.5 - 16) / 16, d = std::sqrt(dx * dx + dy * dy);
                if (d >= 1) continue;
                double core = d < 0.28 ? 1.0 : std::pow(1 - (d - 0.28) / 0.72, 2.2);
                g.px[size_t(y) * 32 + x] = withAlpha(lerpColor(rgb(255, 230, 160), rgb(255, 255, 250), d < 0.28 ? 1 : 0.3), int(core * 255));
            }
        g.buildMips();
        lampGlow = g;
    }

    dust = makePuff(rgb(190, 165, 125), 1.0);
    smoke = makePuff(rgb(225, 225, 232), 0.7);
    spark = makePuff(rgb(255, 230, 140), 1.0);
    lightRed = makeLight(rgb(255, 40, 30), true);
    lightGreen = makeLight(rgb(60, 255, 80), true);
    lightOff = makeLight(rgb(255, 40, 30), false);
    {   // nitrous flame: white-hot core, cyan and blue, a violet fringe; streaks radiate from the centre
        const int N = 32;
        Bitmap f(N, N);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                double dx = (x + 0.5 - N / 2.0) / (N / 2.0), dy = (y + 0.5 - N / 2.0) / (N / 2.0);
                double d = std::sqrt(dx * dx + dy * dy), ang = std::atan2(dy, dx);
                double ray = 0.82 + 0.18 * std::sin(ang * 7 + 1.3) * std::sin(ang * 3);   // ragged edge
                double r = d / ray;
                if (r >= 1) continue;
                Color c = r < 0.22 ? rgb(255, 255, 255)
                        : r < 0.45 ? lerpColor(rgb(255, 255, 255), rgb(120, 230, 255), (r - 0.22) / 0.23)
                        : r < 0.75 ? lerpColor(rgb(120, 230, 255), rgb(40, 110, 255), (r - 0.45) / 0.30)
                                   : lerpColor(rgb(40, 110, 255), rgb(150, 60, 255), (r - 0.75) / 0.25);
                double a = r < 0.45 ? 1.0 : std::pow(1 - (r - 0.45) / 0.55, 1.4);
                f.px[size_t(y) * N + x] = withAlpha(c, int(a * 255));
            }
        f.buildMips();
        flame = f;
    }

    // 0 coast
    bg[0].push_back(cirrusLayer(rgb(255, 255, 255), 0.02, -120));
    bg[0].push_back(cloudLayer(9, rgb(255, 255, 255), 0.05, -70));
    bg[0].push_back(ridgeLayer(80, 20, 70, 2, rgb(120, 150, 210), rgb(160, 190, 228), 0.1, 0));
    bg[0].push_back(ridgeLayer(50, 8, 40, 3, rgb(40, 160, 64), rgb(14, 110, 44), 0.25, 0, 0, false, 2));
    // 1 desert
    bg[1].push_back(sunLayer(rgb(255, 252, 230), rgb(255, 225, 150), 20, false, 0.03, -100));
    bg[1].push_back(cirrusLayer(rgb(255, 246, 236), 0.02, -110));
    bg[1].push_back(ridgeLayer(90, 20, 80, 2, rgb(160, 116, 170), rgb(200, 146, 156), 0.08, 0, 0, true));
    bg[1].push_back(mesaLayer());
    // 2 night city
    bg[2].push_back(upscaled(starLayer(0.02)));
    bg[2].push_back(moonLayer(700, 60, 16, 0.02, -50));
    bg[2].push_back(upscaled(skylineLayer(120, rgb(58, 32, 88), false, 0.04, 0)));
    bg[2].push_back(upscaled(skylineLayer(100, rgb(44, 26, 72), false, 0.08, 0)));
    bg[2].push_back(upscaled(skylineLayer(80, rgb(28, 22, 50), true, 0.15, 0)));
    // 3 snow
    bg[3].push_back(cloudLayer(7, rgb(240, 244, 252), 0.04, -80));
    bg[3].push_back(ridgeLayer(130, 40, 125, 2, rgb(116, 132, 170), rgb(156, 172, 202), 0.08, 0, rgb(252, 253, 255), true));
    bg[3].push_back(ridgeLayer(60, 10, 50, 3, rgb(240, 244, 252), rgb(212, 222, 242), 0.2, 0));
    // 4 forest
    bg[4].push_back(ridgeLayer(80, 20, 70, 2, rgb(132, 152, 144), rgb(142, 160, 152), 0.08, 0, 0, false, 2));
    bg[4].push_back(treeLineLayer(rgb(98, 124, 108), 0.15, 0));
    bg[4].push_back(treeLineLayer(rgb(58, 88, 68), 0.25, 4));
    // 5 sunset
    bg[5].push_back(sunLayer(rgb(255, 240, 120), rgb(255, 50, 120), 70, true, 0.03, 75));
    bg[5].push_back(ridgeLayer(70, 10, 60, 2, rgb(100, 44, 120), rgb(126, 52, 116), 0.08, 0));
    bg[5].push_back(ridgeLayer(36, 4, 24, 4, rgb(64, 26, 74), rgb(74, 30, 74), 0.2, 0, 0, false, 2));
    bg[5].insert(bg[5].begin() + 1, cirrusLayer(rgb(255, 170, 200), 0.02, -100));

    // ---- stages 7-11: generated after the original graphics; each stage has its own random numbers, so changing
    // one stage's graphics never changes another's
    rng.seed(7001);
    sprite[SPR_HOLO_PYLON] = makeHoloPylon();
    sprite[SPR_SCIFI_TOWER] = makeScifiTower();
    sprite[SPR_UFO] = makeUfo();
    sprite[SPR_HOLO_SIGN] = makeHoloSign();
    bg[6].push_back(cloudLayer(5, rgb(255, 255, 255), 0.04, -80));
    bg[6].push_back(planetLayer());
    bg[6].push_back(futureSkylineLayer(120, rgb(196, 214, 222), 0.06, 0, rgb(120, 200, 200)));
    bg[6].push_back(futureSkylineLayer(80, rgb(240, 240, 236), 0.15, 0, rgb(40, 170, 180)));
    rng.seed(7002);
    sprite[SPR_TROPICAL] = makeTropical();
    sprite[SPR_CARNIVAL] = makeCarnival();
    sprite[SPR_FAVELA] = makeFavela();
    bg[7].push_back(sunLayer(rgb(255, 255, 200), rgb(255, 150, 40), 34, true, 0.03, -40));
    bg[7].push_back(rioMountainsLayer());
    bg[7].push_back(favelaHillLayer());
    rng.seed(7003);
    sprite[SPR_PARIS_LAMP] = makeParisLamp();
    sprite[SPR_HAUSSMANN] = makeHaussmann();
    sprite[SPR_CAFE] = makeCafe();
    sprite[SPR_TREE_ROUND] = makeTreeRound();
    bg[8].push_back(cloudLayer(6, rgb(255, 226, 226), 0.04, -80));
    bg[8].push_back(eiffelLayer());
    bg[8].push_back(rooftopLayer(rgb(206, 196, 186), 0.16, 0));
    rng.seed(7004);
    sprite[SPR_FACHWERK] = makeFachwerk();
    sprite[SPR_AUTOBAHN_SIGN] = makeAutobahnSign();
    sprite[SPR_WIND_TURBINE] = makeWindTurbine();
    bg[9].push_back(cloudLayer(8, rgb(255, 255, 255), 0.05, -70));
    bg[9].push_back(ridgeLayer(110, 50, 105, 3, rgb(150, 170, 200), rgb(186, 200, 222), 0.04, 0, rgb(250, 252, 255), true));   // the Alps, far away
    bg[9].push_back(ridgeLayer(70, 20, 60, 2, rgb(110, 150, 120), rgb(130, 168, 130), 0.07, 0, 0, false, 2));
    bg[9].push_back(castleLayer());
    bg[9].push_back(treeLineLayer(rgb(40, 96, 56), 0.25, 4));
    rng.seed(7005);
    sprite[SPR_CYPRESS] = makeCypress();
    sprite[SPR_VILLA] = makeVilla();
    sprite[SPR_OLIVE] = makeOlive();
    // 10 Florence
    bg[10].push_back(cirrusLayer(rgb(255, 250, 240), 0.02, -110));
    bg[10].push_back(ridgeLayer(70, 14, 50, 2, rgb(150, 160, 150), rgb(176, 176, 150), 0.05, 0, 0, false, 2));
    bg[10].push_back(florenceLayer());
    bg[10].push_back(tuscanHillsLayer());

    // ---- 1.4: more roadside variety and the ambient effects, each in its own seeded block
    rng.seed(7101);
    sprite[SPR_DEAD_TREE] = makeDeadTree();
    sprite[SPR_WINDPUMP] = makeWindPump();
    sprite[SPR_DINER] = makeDiner();
    for (int i = 0; i < TUMBLE_FRAMES; ++i) tumbleweed[i] = makeTumbleweed(i * M_PI / 4);
    rng.seed(7102);
    sprite[SPR_BEACH_HUT] = makeBeachHut(0);
    sprite[SPR_BEACH_HUT2] = makeBeachHut(1);
    sprite[SPR_LIFEGUARD] = makeLifeguard();
    sprite[SPR_BUILDING3] = makeBuilding3();
    sprite[SPR_CHALET] = makeChalet();
    sprite[SPR_CABIN] = makeCabin();
    sprite[SPR_MOTEL_SIGN] = makeMotelSign();
    sprite[SPR_DOME] = makeDome();
    rng.seed(7103);
    sprite[SPR_FAVELA2] = makeFavela();          // the same builder, other random numbers: a different hillside
    sprite[SPR_CONFETTI_CANNON] = makeConfettiCannon();
    rng.seed(7104);
    sprite[SPR_HAUSSMANN2] = makeHaussmann2();
    sprite[SPR_MORRIS] = makeMorrisColumn();
    rng.seed(7105);
    sprite[SPR_FACHWERK2] = makeTimberHouse(0);
    sprite[SPR_FACHWERK3] = makeTimberHouse(1);
    sprite[SPR_CHURCH] = makeChurch();
    rng.seed(7106);
    sprite[SPR_VILLA2] = makeTuscanHouse(0);
    sprite[SPR_VILLA3] = makeTuscanHouse(1);
    sprite[SPR_VINES] = makeVines();

    // ---- 1.4: stages 12-13 (autumn forest, rainy night city) and the bridges, each in its own seeded block
    rng.seed(7201);
    sprite[SPR_BRIDGE] = makeBridge(false);
    sprite[SPR_BRIDGE_PILLAR] = makeBridgePillar(false);
    bridgeNight = makeBridge(true);
    bridgePillarNight = makeBridgePillar(true);
    rng.seed(7202);
    sprite[SPR_AUTUMN_TREE1] = makeAutumnTree(0);
    sprite[SPR_AUTUMN_TREE2] = makeAutumnTree(1);
    sprite[SPR_OAK] = makeOak();
    leaf[0] = makeLeaf(0);
    leaf[1] = makeLeaf(1);
    rng.seed(7203);
    sprite[SPR_PUDDLE] = makePuddle();
    sprite[SPR_RAIN_LAMP] = makeRainLamp();
    sprite[SPR_RAIN_BUILDING1] = makeRainBuilding(0);
    sprite[SPR_RAIN_BUILDING2] = makeRainBuilding(1);
    rainLights[0] = makeRainBuilding(0, true);
    rainLights[1] = makeRainBuilding(1, true);
    rng.seed(7204);
    bg[11].push_back(sunLayer(rgb(255, 248, 220), rgb(255, 200, 120), 18, false, 0.03, -90));
    bg[11].push_back(cirrusLayer(rgb(255, 240, 220), 0.02, -110));
    bg[11].push_back(cloudLayer(7, rgb(255, 246, 232), 0.05, -70));
    bg[11].push_back(ridgeLayer(90, 30, 85, 2, rgb(150, 140, 170), rgb(186, 170, 180), 0.04, 0, 0, false, 2));
    bg[11].push_back(ridgeLayer(70, 20, 60, 3, rgb(196, 110, 50), rgb(170, 96, 46), 0.08, 0, 0, false, 2));
    bg[11].push_back(treeLineLayer(rgb(170, 70, 34), 0.15, 0));
    bg[11].push_back(treeLineLayer(rgb(120, 50, 26), 0.25, 4));
    rng.seed(7205);
    bg[12].push_back(cloudLayer(10, rgb(54, 52, 60), 0.03, -80));
    bg[12].push_back(upscaled(skylineLayer(125, rgb(30, 31, 40), false, 0.04, 0)));
    bg[12].push_back(upscaled(skylineLayer(95, rgb(20, 21, 28), false, 0.08, 0)));
}
