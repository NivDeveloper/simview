#pragma once

// The seam vocabulary: PODs, enums, and the two free functions every
// consumer touches first. Core public headers include each other and
// std — nothing else, ever (CLAUDE.md's include graph).

#include <cstddef>
#include <cstdint>

namespace simview {

struct Extent2 {
    std::uint32_t w = 0, h = 0;
};

enum class DType : std::uint8_t { f32, f64, i32, u32, u8 };

struct Config {
    const char *title = "simview";
    Extent2 size = {1200, 800};
    // Headless: no window, offscreen only — the mode CI runs in and
    // the pixel tests render through.
    bool headless = false;
};

// ── seam ────────────────────────────────────────────────────────────
// The library version, "major.minor.patch".
const char *version();
// The sentence for the most recent seam failure on this thread.
const char *last_error();

} // namespace simview
