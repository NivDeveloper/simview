#pragma once

// Internal to src/ — the passes a world draws, as DATA.
//
// Four rows, fixed at compile time, in EXECUTION order. A render graph
// would buy automatic barriers (the renderer already inserts those),
// transient aliasing (there are two targets), pass culling (that is the
// `enabled` bool) and decoupled authoring (there are no user passes) —
// so it would cost a scheduler and buy nothing. What it would buy back
// is declared here anyway: each row says what it loads, what it clears,
// how it treats depth and how its draws are ordered, so the day a
// scheduler is wanted the declarations already exist.
//
// The shadow row is present and disabled rather than absent, because a
// row appended later is a row that renumbers everything after it.

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
    // How a draw within this pass is ordered against its neighbours.
    //   StateThenNear — pipeline first, then nearest-first, so the
    //     depth test discards the far ones cheaply.
    //   FarThenState  — farthest first, because a blend is only
    //     correct in that order.
    //   Submission    — registration order, the painter's rule.
    enum class Sort : std::uint8_t { StateThenNear, FarThenState, Submission };

    const char *name;
    bool enabled;
    bool clear_color, clear_depth;
    bool depth_test, depth_write;
    nvrhi::ComparisonFunc depth_func;
    Sort sort;
};

// Reverse-Z: the near plane is 1 and infinity is 0, so the depth clear
// is 0 and the test passes on GREATER. Transparent tests strictly
// greater — a translucent surface coplanar with the opaque one that
// wrote the depth must not blend itself twice.
//
// GROUND sits between the opaque geometry and the translucent: it is
// the floor of the scene, so it is occluded by solid things and
// translucent things wash over it. Drawing it in the overlay instead
// puts grid lines ON TOP of a cloud that is in front of them, which
// is what it did until this row existed.
// clang-format off: the table reads as a table
inline constexpr PassDesc kPasses[] = {
    {.name = "shadow",      .enabled = false, .clear_color = false, .clear_depth = true,
     .depth_test = true, .depth_write = true,
     .depth_func = nvrhi::ComparisonFunc::GreaterOrEqual,
     .sort = PassDesc::Sort::StateThenNear},
    {.name = "opaque",      .enabled = true,  .clear_color = true,  .clear_depth = true,
     .depth_test = true, .depth_write = true,
     .depth_func = nvrhi::ComparisonFunc::GreaterOrEqual,
     .sort = PassDesc::Sort::StateThenNear},
    {.name = "ground",      .enabled = true,  .clear_color = false, .clear_depth = false,
     .depth_test = true, .depth_write = false,
     .depth_func = nvrhi::ComparisonFunc::GreaterOrEqual,
     .sort = PassDesc::Sort::Submission},
    {.name = "transparent", .enabled = true,  .clear_color = false, .clear_depth = false,
     .depth_test = true, .depth_write = false,
     .depth_func = nvrhi::ComparisonFunc::Greater,
     .sort = PassDesc::Sort::FarThenState},
    {.name = "overlay",     .enabled = true,  .clear_color = false, .clear_depth = false,
     .depth_test = true, .depth_write = false,
     .depth_func = nvrhi::ComparisonFunc::GreaterOrEqual,
     .sort = PassDesc::Sort::Submission},
};
// clang-format on

inline constexpr const PassDesc &pass_of(PassId p) {
    return kPasses[static_cast<std::uint8_t>(p)];
}

} // namespace sv
