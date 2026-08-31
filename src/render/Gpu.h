#pragma once

// Internal to src/ — the two devices anything that draws may speak to,
// and nothing else.
//
// It sits at the bottom because both strata need it and neither owns
// it: a 2D kind and a 3D world item each carry one of these and reach
// no higher. NVRHI is for a drawer's own resources; gpud is for
// resolving a pull source's native buffer, which is the one thing the
// renderer cannot do for itself.

#include <gpud/Device.h>
#include <nvrhi/nvrhi.h>

namespace sv {
namespace impl {

struct Gpu {
    nvrhi::IDevice *dev = nullptr;
    gpud::Device *gdev = nullptr;
};

} // namespace impl
} // namespace sv
