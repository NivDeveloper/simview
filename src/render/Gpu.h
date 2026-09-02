#pragma once

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
