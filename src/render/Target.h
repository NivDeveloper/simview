#pragma once

#include "Gpu.h"

#include <cstdint>

namespace sv {
namespace impl {

struct RenderTarget {
    nvrhi::TextureHandle tex;
    // Only where something asked: a framebuffer carrying an
    // attachment nobody writes is a pipeline incompatibility.
    nvrhi::TextureHandle depth;
    bool want_depth = false;
    nvrhi::FramebufferHandle fb;
    std::uint32_t w = 0, h = 0;
    std::uint32_t want_w = 256, want_h = 256;
    // Bumped by every (re)creation. What a sampler of this target keys
    // its descriptor on — never the texture's address, which the
    // allocator hands straight back to the next texture.
    std::uint32_t gen = 0;
};

} // namespace impl

// The one format a colour target is asked for: sampleable as well as
// drawable, and universally supported as both.
inline constexpr nvrhi::Format kTargetFormat = nvrhi::Format::RGBA8_UNORM;

// Reverse-Z wants a FLOAT depth format: the precision it buys comes
// from the exponent, and a normalized one would throw it away. What
// writes this clears it to 0 and tests GreaterOrEqual.
inline constexpr nvrhi::Format kDepthFormat = nvrhi::Format::D32;

// Match the texture to the size asked for. A failure leaves w = h = 0
// and reports it.
void target_resize(const impl::Gpu &, impl::RenderTarget &);

void target_release(impl::RenderTarget &);

} // namespace sv
