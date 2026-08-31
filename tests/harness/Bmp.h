#pragma once

// A minimal BMP reader for the shot checks: enough of the format to
// ask what colour a pixel is. Uncompressed 24/32-bit bottom-up rows —
// what SDL_SaveBMP writes. Reading pixels is what turns "the file is
// bigger than 1KB" into an assertion about what was drawn.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

struct Bmp {
    unsigned w = 0, h = 0;
    std::vector<std::array<int, 3>> px; // row 0 = TOP, RGB

    const std::array<int, 3> &at(unsigned x, unsigned y) const {
        return px[y * w + x];
    }

    // Channel-wise distance, 0..765 — a plain "how different".
    int diff(unsigned x0, unsigned y0, unsigned x1, unsigned y1) const {
        const auto &a = at(x0, y0);
        const auto &b = at(x1, y1);
        return std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) +
               std::abs(a[2] - b[2]);
    }

    // The average colour: a cheap whole-image fingerprint that no
    // driver difference moves much.
    std::array<int, 3> mean() const {
        long acc[3] = {0, 0, 0};
        for (const auto &p : px)
            for (int c = 0; c < 3; ++c)
                acc[c] += p[c];
        const long n = long(px.size());
        return {int(acc[0] / n), int(acc[1] / n), int(acc[2] / n)};
    }

    // How many colours appear, quantised to `step` per channel — the
    // difference between "a picture" and "a flat fill".
    std::size_t distinct(int step = 16) const {
        std::vector<int> keys;
        keys.reserve(px.size());
        for (const auto &p : px)
            keys.push_back((p[0] / step) << 16 | (p[1] / step) << 8 |
                           (p[2] / step));
        std::sort(keys.begin(), keys.end());
        return std::size_t(std::unique(keys.begin(), keys.end()) -
                           keys.begin());
    }

    // The box holding everything that is not `bg` (within tol): where
    // the drawing IS, which is what letterboxing and layout move.
    struct Box {
        unsigned x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool empty = true;
        unsigned w() const { return empty ? 0 : x1 - x0 + 1; }
        unsigned h() const { return empty ? 0 : y1 - y0 + 1; }
    };

    Box content(const std::array<int, 3> &bg, int tol = 24) const {
        Box b;
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x) {
                const auto &p = at(x, y);
                if (std::abs(p[0] - bg[0]) + std::abs(p[1] - bg[1]) +
                        std::abs(p[2] - bg[2]) <=
                    tol)
                    continue;
                if (b.empty) {
                    b = {x, y, x, y, false};
                    continue;
                }
                b.x0 = std::min(b.x0, x), b.y0 = std::min(b.y0, y);
                b.x1 = std::max(b.x1, x), b.y1 = std::max(b.y1, y);
            }
        return b;
    }
};

// Two images agree when almost every pixel is within tolerance —
// never byte equality, because three drivers never round alike.
// ── structure ────────────────────────────────────────────────────────
// What a picture is made of, rather than what colour it is. A colour
// probe can say "there are grey pixels here"; none of these can be
// answered that way, and a grid with a whole quadrant missing passes
// every colour probe there is.

// A pixel that is not the background — brighter than it by `floor`,
// summed over the channels.
inline bool lit(const Bmp &img, unsigned x, unsigned y, int floor = 40) {
    if (x >= img.w || y >= img.h)
        return false;
    const auto &p = img.at(x, y);
    return p[0] + p[1] + p[2] > 23 + 23 + 26 + floor;
}

inline std::size_t lit_count(const Bmp &img, unsigned x0, unsigned y0,
                             unsigned x1, unsigned y1, int floor = 40) {
    std::size_t n = 0;
    for (unsigned y = y0; y < y1 && y < img.h; ++y)
        for (unsigned x = x0; x < x1 && x < img.w; ++x)
            if (lit(img, x, y, floor))
                ++n;
    return n;
}

// Runs of lit pixels along one scan — the number of LINES crossing it.
// Scanning a row counts the lines that run down the picture; scanning
// a column counts the ones that run across it. A grid missing one
// family answers zero to one of them and not to the other.
inline std::size_t runs_in_row(const Bmp &img, unsigned y, unsigned x0,
                               unsigned x1, int floor = 40) {
    std::size_t runs = 0;
    bool was = false;
    for (unsigned x = x0; x < x1 && x < img.w; ++x) {
        const bool now = lit(img, x, y, floor);
        if (now && !was)
            ++runs;
        was = now;
    }
    return runs;
}

inline std::size_t runs_in_col(const Bmp &img, unsigned x, unsigned y0,
                               unsigned y1, int floor = 40) {
    std::size_t runs = 0;
    bool was = false;
    for (unsigned y = y0; y < y1 && y < img.h; ++y) {
        const bool now = lit(img, x, y, floor);
        if (now && !was)
            ++runs;
        was = now;
    }
    return runs;
}

// The four quadrants of a region, lit-pixel counts, in the order
// top-left, top-right, bottom-left, bottom-right.
struct Quadrants {
    std::size_t tl, tr, bl, br;
    std::size_t lowest() const {
        std::size_t m = tl;
        m = tr < m ? tr : m;
        m = bl < m ? bl : m;
        return br < m ? br : m;
    }
    std::size_t highest() const {
        std::size_t m = tl;
        m = tr > m ? tr : m;
        m = bl > m ? bl : m;
        return br > m ? br : m;
    }
};

inline Quadrants quadrants(const Bmp &img, unsigned x0, unsigned y0,
                           unsigned x1, unsigned y1, int floor = 40) {
    const unsigned mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
    return {lit_count(img, x0, y0, mx, my, floor),
            lit_count(img, mx, y0, x1, my, floor),
            lit_count(img, x0, my, mx, y1, floor),
            lit_count(img, mx, my, x1, y1, floor)};
}

// How many DISTINCT brightnesses a region holds, which is what says a
// line was anti-aliased rather than drawn as an on/off mask.
inline std::size_t shades(const Bmp &img, unsigned x0, unsigned y0, unsigned x1,
                          unsigned y1, int step = 8) {
    std::set<int> seen;
    for (unsigned y = y0; y < y1 && y < img.h; ++y)
        for (unsigned x = x0; x < x1 && x < img.w; ++x) {
            const auto &p = img.at(x, y);
            seen.insert((p[0] + p[1] + p[2]) / step);
        }
    return seen.size();
}

inline bool similar(const Bmp &a, const Bmp &b, int tol = 24,
                    double allowed_fraction = 0.01) {
    if (a.w != b.w || a.h != b.h)
        return false;
    std::size_t bad = 0;
    for (std::size_t i = 0; i < a.px.size(); ++i) {
        const auto &p = a.px[i];
        const auto &q = b.px[i];
        if (std::abs(p[0] - q[0]) + std::abs(p[1] - q[1]) +
                std::abs(p[2] - q[2]) >
            tol)
            ++bad;
    }
    return double(bad) <= allowed_fraction * double(a.px.size());
}

inline bool load_bmp(const std::string &path, Bmp &out) {
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::vector<unsigned char> raw;
    std::fseek(f, 0, SEEK_END);
    raw.resize(std::size_t(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    const bool read = std::fread(raw.data(), 1, raw.size(), f) == raw.size();
    std::fclose(f);
    if (!read || raw.size() < 54 || raw[0] != 'B' || raw[1] != 'M')
        return false;

    auto u32 = [&](std::size_t o) {
        return unsigned(raw[o]) | unsigned(raw[o + 1]) << 8 |
               unsigned(raw[o + 2]) << 16 | unsigned(raw[o + 3]) << 24;
    };
    const unsigned offset = u32(10);
    out.w = u32(18);
    out.h = u32(22);
    const unsigned bpp = unsigned(raw[28]) | unsigned(raw[29]) << 8;
    if ((bpp != 24 && bpp != 32) || !out.w || !out.h)
        return false;

    const unsigned bytes = bpp / 8;
    const unsigned stride = (out.w * bytes + 3) & ~3u;
    if (raw.size() < offset + stride * out.h)
        return false;

    out.px.assign(std::size_t(out.w) * out.h, {0, 0, 0});
    for (unsigned y = 0; y < out.h; ++y) {
        // BMP rows run bottom-up; flip so row 0 is the top one.
        const unsigned char *row =
            raw.data() + offset + stride * (out.h - 1 - y);
        for (unsigned x = 0; x < out.w; ++x)
            out.px[y * out.w + x] = {int(row[x * bytes + 2]),
                                     int(row[x * bytes + 1]),
                                     int(row[x * bytes + 0])};
    }
    return true;
}
