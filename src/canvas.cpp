#include "canvas.h"
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- Bitmap ----
namespace {
// src over an opaque dst without divisions (the renderer's hot loops): alpha 0..255 -> weight 0..256, two
// channels per multiply. Within 1/255 of blendOver; blendOver itself stays as it is for asset generation.
inline Color blendFast(Color d, Color s) {
    unsigned a = s >> 24;
    if (a == 255) return s;
    if (!a) return d;
    unsigned f = a + (a >> 7), g = 256 - f;
    Color rb = (((d & 0x00FF00FFu) * g + (s & 0x00FF00FFu) * f) >> 8) & 0x00FF00FFu;
    Color gg = ((((d >> 8) & 0xFFu) * g + ((s >> 8) & 0xFFu) * f)) & 0x0000FF00u;
    return 0xFF000000u | rb | gg;
}
} // namespace

void Bitmap::fill(Color c) { std::fill(px.begin(), px.end(), c); }

void Bitmap::fillRect(int x, int y, int rw, int rh, Color c) {
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(w, x + rw), y1 = std::min(h, y + rh);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) px[size_t(yy) * w + xx] = c;
}

Bitmap Bitmap::flipped() const {
    Bitmap b(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) b.px[size_t(y) * w + x] = px[size_t(y) * w + (w - 1 - x)];
    if (!mips.empty()) b.buildMips();
    else b.computeBounds();
    return b;
}

Bitmap Bitmap::downsampled(int f) const {
    int nw = std::max(1, w / f), nh = std::max(1, h / f);
    Bitmap b(nw, nh);
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x) {
            unsigned long r = 0, g = 0, bl = 0, a = 0, n = 0;
            for (int sy = y * f; sy < std::min(h, y * f + f); ++sy)
                for (int sx = x * f; sx < std::min(w, x * f + f); ++sx) {
                    Color c = px[size_t(sy) * w + sx];
                    unsigned ca = c >> 24;
                    r += ((c >> 16) & 255) * ca; g += ((c >> 8) & 255) * ca; bl += (c & 255) * ca; a += ca; n++;
                }
            if (a == 0) continue;
            unsigned oa = unsigned(a / n);
            if (oa == 0) continue;
            b.px[size_t(y) * nw + x] = (Color(oa) << 24) | (Color(r / a) << 16) | (Color(g / a) << 8) | Color(bl / a);
        }
    return b;
}

void Bitmap::computeBounds() {
    int x0 = w, y0 = h, x1 = 0, y1 = 0;
    rowL.assign(size_t(h), w); rowR.assign(size_t(h), 0);
    for (int y = 0; y < h; ++y) {
        const Color* row = &px[size_t(y) * w];
        int l = 0;
        while (l < w && !(row[l] >> 24)) ++l;
        if (l == w) continue;
        int r = w;
        while (r > l && !(row[r - 1] >> 24)) --r;
        rowL[size_t(y)] = l; rowR[size_t(y)] = r;
        x0 = std::min(x0, l); x1 = std::max(x1, r);
        y0 = std::min(y0, y); y1 = y + 1;
    }
    if (x0 >= x1) { x0 = y0 = x1 = y1 = 0; }   // fully transparent
    bx0 = x0; by0 = y0; bx1 = x1; by1 = y1;
}

void ByteMap::buildMips() {
    mips.clear();
    const ByteMap* cur = this;
    while (cur->w >= 8 && cur->h >= 8) {          // same rule as Bitmap::buildMips
        ByteMap m;
        m.w = std::max(1, cur->w / 2); m.h = std::max(1, cur->h / 2);
        m.px.assign(size_t(m.w) * m.h, 0);
        for (int y = 0; y < m.h; ++y)
            for (int x = 0; x < m.w; ++x) {
                const unsigned char* a = &cur->px[size_t(y * 2) * cur->w + x * 2];
                const unsigned char* b = a + cur->w;
                m.px[size_t(y) * m.w + x] = (unsigned char)((a[0] + a[1] + b[0] + b[1] + 2) / 4);
            }
        mips.push_back(std::move(m));
        cur = &mips.back();
    }
}

void Bitmap::bleed() {
    std::vector<Color> out = px;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (px[size_t(y) * w + x] >> 24) continue;
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int yy = std::max(0, y - 1); yy <= std::min(h - 1, y + 1); ++yy)
                for (int xx = std::max(0, x - 1); xx <= std::min(w - 1, x + 1); ++xx) {
                    Color c = px[size_t(yy) * w + xx];
                    if (!(c >> 24)) continue;
                    r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; ++n;
                }
            if (n) out[size_t(y) * w + x] = (Color(r / n) << 16) | (Color(g / n) << 8) | Color(b / n);   // alpha 0
        }
    px.swap(out);
}

void Bitmap::buildMips() {
    computeBounds();
    bleed();
    mips.clear();
    const Bitmap* cur = this;
    while (cur->w >= 8 && cur->h >= 8) {
        mips.push_back(cur->downsampled(2));
        mips.back().computeBounds();
        mips.back().bleed();
        cur = &mips.back();
    }
}

void Bitmap::draw(const Bitmap& src, int dx, int dy) {
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x) {
            Color c = src.px[size_t(y) * src.w + x];
            if (c >> 24) blend(dx + x, dy + y, c);
        }
}

// ---------------------------------------------------------------- Canvas ----
void Canvas::span(int x0, int x1, int y, Color c) {
    if (y < clip.y || y >= clip.y + clip.h) return;
    x0 = std::max(x0, clip.x); x1 = std::min(x1, clip.x + clip.w);
    Color* row = &px[size_t(y) * w];
    for (int x = x0; x < x1; ++x) row[x] = c;
}

void Canvas::spanAA(double x0, double x1, int y, Color c) {
    if (y < clip.y || y >= clip.y + clip.h || x1 <= x0) return;
    double cl = clip.x, cr = clip.x + clip.w;
    x0 = std::max(x0, cl); x1 = std::min(x1, cr);
    if (x1 <= x0) return;
    Color* row = &px[size_t(y) * w];
    int i0 = int(std::floor(x0)), i1 = int(std::floor(x1));
    if (i0 == i1) {                                   // inside one pixel
        if (i0 >= clip.x && i0 < clip.x + clip.w) row[i0] = blendFast(row[i0], withAlpha(c, int((x1 - x0) * 255)));
        return;
    }
    double cov0 = (i0 + 1) - x0;                      // left edge coverage
    if (i0 >= clip.x) row[i0] = blendFast(row[i0], withAlpha(c, int(cov0 * 255)));
    for (int x = i0 + 1; x < i1; ++x) row[x] = c;
    double cov1 = x1 - i1;
    if (i1 < clip.x + clip.w && cov1 > 0) row[i1] = blendFast(row[i1], withAlpha(c, int(cov1 * 255)));
}

void Canvas::vspanAA(int x, double y0, double y1, Color c, double clipBottom) {
    if (x < clip.x || x >= clip.x + clip.w) return;
    y0 = std::max(y0, double(clip.y));
    y1 = std::min({y1, double(clip.y + clip.h), clipBottom});
    if (y1 <= y0) return;
    int i0 = int(std::floor(y0)), i1 = int(std::floor(y1));
    if (i0 == i1) { Color& d = px[size_t(i0) * w + x]; d = blendFast(d, withAlpha(c, int((y1 - y0) * 255))); return; }
    { Color& d = px[size_t(i0) * w + x]; d = blendFast(d, withAlpha(c, int(((i0 + 1) - y0) * 255))); }
    for (int y = i0 + 1; y < i1; ++y) px[size_t(y) * w + x] = c;
    double cov1 = y1 - i1;
    if (cov1 > 0 && i1 < clip.y + clip.h) { Color& d = px[size_t(i1) * w + x]; d = blendFast(d, withAlpha(c, int(cov1 * 255))); }
}

void Canvas::rect(int x, int y, int rw, int rh, Color c) {
    for (int yy = y; yy < y + rh; ++yy) span(x, x + rw, yy, c);
}

void Canvas::rectBlend(int x, int y, int rw, int rh, Color c, double a) {
    int x0 = std::max(x, clip.x), x1 = std::min(x + rw, clip.x + clip.w);
    int y0 = std::max(y, clip.y), y1 = std::min(y + rh, clip.y + clip.h);
    if (x0 >= x1 || y0 >= y1) return;
    int ai = std::clamp(int(a * 255), 0, 255);
    if (ai == 0) return;
    // the source premultiplied once; per pixel only the destination is scaled (two channels per multiply)
    const unsigned f = unsigned(ai + (ai >> 7)), g = 256 - f;
    const Color srb = (c & 0x00FF00FFu) * f, sg = ((c >> 8) & 0xFFu) * f;
    for (int yy = y0; yy < y1; ++yy) {
        Color* row = &px[size_t(yy) * w];
        for (int xx = x0; xx < x1; ++xx) {
            Color d = row[xx];
            Color rb = (((d & 0x00FF00FFu) * g + srb) >> 8) & 0x00FF00FFu;
            Color gg = (((d >> 8) & 0xFFu) * g + sg) & 0x0000FF00u;
            row[xx] = 0xFF000000u | rb | gg;
        }
    }
}

void Canvas::vgradRect(int x, int y, int rw, int rh, Color top, Color bottom, double a) {
    for (int yy = 0; yy < rh; ++yy) {
        Color c = lerpColor(top, bottom, rh > 1 ? double(yy) / (rh - 1) : 0);
        rectBlend(x, y + yy, rw, 1, c, a);
    }
}

namespace {
// Filter taps for one axis of a scaled blit: destination pixel -> two source texels + weights (0..256).
// Magnifying (texels larger than pixels): exact area coverage, so every texel becomes an equally sized
// block with anti-aliased borders - no texel is drawn 1 pixel wide and its neighbour 2 pixels wide.
// Minifying (a mip level at most 2x larger is used): bilinear. Texels outside the bitmap get weight 0,
// which makes the sprite's outer border partially transparent (anti-aliased) as well.
struct Tap { int i0, i1, f; };            // texels i0, i1 (clamped into the bitmap) and the weight of i1, 0..256
inline Tap makeTap(double d, double pos, double scale, int n) {
    // d = destination pixel index, pos = destination position of the bitmap, scale = texels per pixel
    double t0, f;
    if (scale < 1) {                      // magnify: pixel [d, d+1) covers texels [u0, u1), shorter than 1 texel
        double u0 = (d - pos) * scale, u1 = u0 + scale;
        t0 = std::floor(u0);
        f = u1 > t0 + 1 ? (u1 - (t0 + 1)) / scale : 0.0;   // share of the next texel
    } else {                              // minify: bilinear around the pixel centre
        double u = (d + 0.5 - pos) * scale - 0.5;
        t0 = std::floor(u);
        f = u - t0;
    }
    int i0 = int(t0);
    int fi = int(f * 256 + 0.5);
    if (i0 < 0) { i0 = 0; fi = 0; }                     // left of the bitmap: only its first texel
    if (i0 >= n - 1) { i0 = n - 1; fi = 0; }            // at / beyond the last texel
    return {i0, std::min(i0 + 1, n - 1), fi};
}
// packed lerp of two ARGB colours, f = 0..256 (weight of b)
inline Color lerpPacked(Color a, Color b, unsigned f) {
    if (!f) return a;
    unsigned g = 256 - f;
    Color rb = (((a & 0x00FF00FFu) * g + (b & 0x00FF00FFu) * f) >> 8) & 0x00FF00FFu;
    Color ag = (((a >> 8) & 0x00FF00FFu) * g + ((b >> 8) & 0x00FF00FFu) * f) & 0xFF00FF00u;
    return rb | ag;
}
} // namespace

void Canvas::blitScaled(const Bitmap& src0, double dx, double dy, double dw, double dh,
                        bool flipX, double clipBottom, double fog, Color fogColor, double alpha,
                        const ByteMap* mask0, const Color* pal) {
    if (dw < 0.5 || dh < 0.5 || alpha <= 0.01) return;
    const Bitmap* src = &src0;
    const ByteMap* mask = mask0;
    for (size_t level = 0; level < src0.mips.size(); ++level) {   // smallest mip that is still >= the target size
        const Bitmap& m = src0.mips[level];
        if (m.w >= dw * 0.999 && m.h >= dh * 0.999) {
            src = &m;
            if (mask0) mask = level < mask0->mips.size() ? &mask0->mips[level] : nullptr;
        } else break;
    }
    if (mask && (mask->w != src->w || mask->h != src->h)) mask = nullptr;
    if (!pal) mask = nullptr;
    if (src->bx1 <= src->bx0 || src->by1 <= src->by0) return;   // nothing opaque
    const double sx = src->w / dw, sy = src->h / dh;            // texels per destination pixel
    // destination pixels that can touch the opaque bounds (one texel of margin for the filter)
    int bxl = flipX ? src->w - src->bx1 : src->bx0, bxr = flipX ? src->w - src->bx0 : src->bx1;
    int cx0 = std::max(int(std::floor(dx + (bxl - 1) / sx)), clip.x);
    int cx1 = std::min(int(std::ceil(dx + (bxr + 1) / sx)) + 1, clip.x + clip.w);
    int cy0 = std::max(int(std::floor(dy + (src->by0 - 1) / sy)), clip.y);
    int cy1 = std::min({int(std::ceil(dy + (src->by1 + 1) / sy)) + 1, clip.y + clip.h, int(std::ceil(clipBottom))});
    if (cx0 >= cx1 || cy0 >= cy1) return;

    thread_local std::vector<Tap> cols;
    cols.resize(size_t(cx1 - cx0));
    for (int x = cx0; x < cx1; ++x) {
        Tap t = makeTap(x, dx, sx, src->w);
        if (flipX) { t.i0 = src->w - 1 - t.i0; t.i1 = src->w - 1 - t.i1; }   // mirrored: same weights
        cols[size_t(x - cx0)] = t;
    }
    // drop columns whose texels all lie outside the opaque bounds
    auto colDead = [&](const Tap& t) {
        auto out = [&](int i) { return i < src->bx0 || i >= src->bx1; };
        return out(t.i0) && (t.f == 0 || out(t.i1));
    };
    int ca = 0, cb = cx1 - cx0;
    while (ca < cb && colDead(cols[size_t(ca)])) ++ca;
    while (cb > ca && colDead(cols[size_t(cb - 1)])) --cb;
    if (ca >= cb) return;

    const bool doFog = fog > 0.01;
    const unsigned fogT = doFog ? unsigned(std::clamp(fog, 0.0, 1.0) * 256) : 0;
    const Color fogRGB = fogColor & 0x00FFFFFFu;
    const unsigned aMul = alpha < 0.99 ? unsigned(alpha * 256) : 256;
    const int sw = src->w;
    const Color* spx = src->px.data();
    const Tap* ct = cols.data();
    const bool rows = src->rowL.size() == size_t(src->h);

    // unscaled blit at whole-pixel positions (background layers, UI): straight copy with alpha
    if (sx == 1.0 && sy == 1.0 && dx == std::floor(dx) && dy == std::floor(dy) && !flipX && !mask && !doFog && aMul == 256) {
        for (int y = cy0; y < cy1; ++y) {
            int sy0 = y - int(dy);
            if (sy0 < src->by0 || sy0 >= src->by1) continue;
            int l = cx0 + ca, r = cx0 + cb;
            if (rows) { l = std::max(l, int(dx) + src->rowL[size_t(sy0)]); r = std::min(r, int(dx) + src->rowR[size_t(sy0)]); }
            const Color* srow = spx + size_t(sy0) * sw - int(dx);
            Color* drow = &px[size_t(y) * w];
            for (int x = l; x < r; ++x) {
                Color c = srow[x];
                unsigned a = c >> 24;
                if (a == 255) drow[x] = c;
                else if (a) drow[x] = lerpPacked(drow[x], c, a + (a >> 7)) | 0xFF000000u;
            }
        }
        return;
    }

    for (int y = cy0; y < cy1; ++y) {
        Tap ty = makeTap(y, dy, sy, src->h);
        if ((ty.i0 < src->by0 || ty.i0 >= src->by1) && (ty.f == 0 || ty.i1 < src->by0 || ty.i1 >= src->by1)) continue;
        // only the columns that can reach this row's visible texels
        int ka = ca, kb = cb;
        if (rows) {
            int l = src->rowL[size_t(ty.i0)], r = src->rowR[size_t(ty.i0)];
            if (ty.f) { l = std::min(l, src->rowL[size_t(ty.i1)]); r = std::max(r, src->rowR[size_t(ty.i1)]); }
            if (l >= r) continue;
            if (flipX) { int t = l; l = src->w - r; r = src->w - t; }
            ka = std::max(ka, int(std::floor(dx + (l - 1) / sx)) - cx0);
            kb = std::min(kb, int(std::ceil(dx + (r + 1) / sx)) + 1 - cx0);
            if (ka >= kb) continue;
        }
        // blend the two source rows once (over the texels this row reads), so that each pixel then needs only
        // a horizontal lerp of two texels: when magnifying, many pixels share the same texels
        const Color* row0 = spx + size_t(ty.i0) * sw;
        const unsigned char* mrow0 = mask ? &mask->px[size_t(ty.i0) * sw] : nullptr;
        const unsigned fy = unsigned(ty.f);
        if (fy) {
            int t0 = std::min({ct[ka].i0, ct[ka].i1, ct[kb - 1].i0, ct[kb - 1].i1});
            int t1 = std::max({ct[ka].i0, ct[ka].i1, ct[kb - 1].i0, ct[kb - 1].i1});
            thread_local std::vector<Color> vrow;
            thread_local std::vector<unsigned char> vmask;
            if (vrow.size() < size_t(sw)) vrow.resize(size_t(sw));
            const Color* row1 = spx + size_t(ty.i1) * sw;
            for (int t = t0; t <= t1; ++t) {
                vrow[size_t(t)] = lerpPacked(row0[t], row1[t], fy);   // transparent texels keep their bled colour
            }
            row0 = vrow.data();
            if (mrow0) {
                if (vmask.size() < size_t(sw)) vmask.resize(size_t(sw));
                const unsigned char* mrow1 = &mask->px[size_t(ty.i1) * sw];
                for (int t = t0; t <= t1; ++t) vmask[size_t(t)] = (unsigned char)((mrow0[t] * (256 - fy) + mrow1[t] * fy) >> 8);
                mrow0 = vmask.data();
            }
        }
        Color* drow = &px[size_t(y) * w + cx0];
        for (int k = ka; k < kb; ++k) {
            const Tap& tx = ct[k];
            const Color q0 = row0[tx.i0], q1 = row0[tx.i1];
            if (!((q0 | q1) >> 24)) continue;               // empty space around the sprite
            Color c = tx.f ? lerpPacked(q0, q1, unsigned(tx.f)) : q0;
            if (mrow0) {                                   // car paint: grey -> palette colour, by the mask
                unsigned fx = unsigned(tx.f);
                unsigned m = (mrow0[tx.i0] * (256 - fx) + mrow0[tx.i1] * fx) >> 8;
                if (m) {
                    unsigned grey = (((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255)) / 3;
                    c = lerpPacked(c, (pal[grey] & 0x00FFFFFFu) | (c & 0xFF000000u), m + (m >> 7));
                }
            }
            unsigned a = c >> 24;
            if (!a) continue;
            if (doFog) c = lerpPacked(c, fogRGB | (c & 0xFF000000u), fogT);
            if (aMul != 256) { a = (a * aMul) >> 8; if (!a) continue; }
            Color& d = drow[k];
            d = a >= 255 ? (c | 0xFF000000u) : (lerpPacked(d, c, a + (a >> 7)) | 0xFF000000u);
        }
    }
}
