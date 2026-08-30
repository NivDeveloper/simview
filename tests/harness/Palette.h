#pragma once

// Arrangement invariance, as a reusable assertion.
//
// The question every visual check here really asks is: does the same
// scene look the same however it is arranged? A golden image answers
// it badly — it has to be maintained, it breaks when a driver rounds
// differently, and it says nothing about a colour nobody predicted.
//
// A PALETTE answers it well. Capture the colours the arrangement that
// works produces, then assert that no later arrangement introduces a
// colour outside them. That catches a flash of any colour, a stale
// render target, and undefined memory, without naming a symptom in
// advance — which is how the torn-out-panel bug was found by a test
// that had never heard of magenta.

#include "harness/Bmp.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <set>

namespace palette {

// Colours are quantised before comparison, as Bmp::distinct does:
// antialiased edges otherwise make every frame a new palette.
inline constexpr int kStep = 16;

// How far from every known colour a pixel must be before it counts as
// new rather than as a blend along an existing edge.
inline constexpr int kFar = 64;

// How many pixels one unknown colour must cover to be a finding. A
// flash is a region; a stray pixel is quantisation.
inline constexpr std::size_t kRegion = 64;

using Set = std::set<int>;

inline int key_of(const std::array<int, 3> &p) {
    return (p[0] / kStep) << 16 | (p[1] / kStep) << 8 | (p[2] / kStep);
}

// Add everything this image shows to what is considered normal.
inline void absorb(const Bmp &img, Set &known) {
    for (const auto &p : img.px)
        known.insert(key_of(p));
}

inline int distance_to(const std::array<int, 3> &p, const Set &known) {
    int best = 765;
    for (int k : known) {
        const int r = ((k >> 16) & 0xff) * kStep + kStep / 2;
        const int g = ((k >> 8) & 0xff) * kStep + kStep / 2;
        const int b = (k & 0xff) * kStep + kStep / 2;
        const int d =
            std::abs(p[0] - r) + std::abs(p[1] - g) + std::abs(p[2] - b);
        best = d < best ? d : best;
    }
    return best;
}

// The largest run of one colour the palette does not contain, and what
// that colour was. Zero means every pixel is something this scene can
// produce. Compare against kRegion.
inline std::size_t intruder(const Bmp &img, const Set &known,
                            std::array<int, 3> &colour) {
    Set unseen;
    for (const auto &p : img.px)
        if (!known.count(key_of(p)) && distance_to(p, known) > kFar)
            unseen.insert(key_of(p));

    std::size_t worst = 0;
    for (int k : unseen) {
        std::size_t n = 0;
        std::array<int, 3> sample{0, 0, 0};
        for (const auto &p : img.px)
            if (key_of(p) == k) {
                ++n;
                sample = p;
            }
        if (n > worst) {
            worst = n;
            colour = sample;
        }
    }
    return worst;
}

} // namespace palette
