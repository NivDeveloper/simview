#pragma once

// A minimal BMP reader for the shot checks: enough of the format to
// ask what colour a pixel is. Uncompressed 24/32-bit bottom-up rows —
// what SDL_SaveBMP writes. Reading pixels is what turns "the file is
// bigger than 1KB" into an assertion about what was drawn.

#include <array>
#include <cstdint>
#include <cstdio>
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
};

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
