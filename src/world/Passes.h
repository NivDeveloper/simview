#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>

namespace sv {

enum class PassId : std::uint8_t {
    Shadow = 0,
    Opaque = 1,
    Ground = 2,
    Transparent = 3,
    Overlay = 4,
};

struct PassDesc {
    // StateThenNear: pipeline first, then nearest-first, so the depth
    // test discards the most. NearestLast is back-to-front blending.
    enum class Sort : std::uint8_t { StateThenNear, FarThenState, Submission };

    const char *name;
    bool enabled;
    bool clear_color, clear_depth;
    bool depth_test, depth_write;
    nvrhi::ComparisonFunc depth_func;
    Sort sort;
};

// Reverse-Z: clear is 0 and the test passes on GREATER. Transparent
// tests strictly greater and does not write.
inline constexpr PassDesc kPasses[] = {
    {.name = "shadow",
     .enabled = false,
     .clear_color = false,
     .clear_depth = true,
     .depth_test = true,
     .depth_write = true,
     .depth_func = nvrhi::ComparisonFunc::GreaterOrEqual,
     .sort = PassDesc::Sort::StateThenNear},
    {.name = "opaque",
     .enabled = true,
     .clear_color = true,
     .clear_depth = true,
     .depth_test = true,
     .depth_write = true,
     .depth_func = nvrhi::ComparisonFunc::GreaterOrEqual,
     .sort = PassDesc::Sort::StateThenNear},
    {.name = "ground",
     .enabled = true,
     .clear_color = false,
     .clear_depth = false,
     .depth_test = true,
     .depth_write = false,
     .depth_func = nvrhi::ComparisonFunc::GreaterOrEqual,
     .sort = PassDesc::Sort::Submission},
    {.name = "transparent",
     .enabled = true,
     .clear_color = false,
     .clear_depth = false,
     .depth_test = true,
     .depth_write = false,
     .depth_func = nvrhi::ComparisonFunc::Greater,
     .sort = PassDesc::Sort::FarThenState},
    {.name = "overlay",
     .enabled = true,
     .clear_color = false,
     .clear_depth = false,
     .depth_test = true,
     .depth_write = false,
     .depth_func = nvrhi::ComparisonFunc::GreaterOrEqual,
     .sort = PassDesc::Sort::Submission},
};
// clang-format on

inline constexpr const PassDesc &pass_of(PassId p) {
    return kPasses[static_cast<std::uint8_t>(p)];
}

} // namespace sv
