#pragma once

// Internal to src/ — a texture that is resized to a requested size and
// drawn into. It knows nothing about panels, scenes or worlds: the
// layer above asks for a size (want_w, want_h) and the next resize
// honours it. That one-way channel is what keeps the DAG exact — ui
// writes a field down here, this layer never reads a ui type.
//
// The texture is recreated BEFORE the UI frame is built, because that
// frame's draw list records the handle; a texture released after being
// recorded is a use-after-free with a picture on the other side.
//
// Two consumers make this its own layer rather than the scene's: a
// view's target, which carries a colour attachment and optionally a
// depth one, and a world's shadow map, which is depth ONLY. What they
// share is the resize-and-recreate discipline above, and that is the
// whole of what lives here.

#include "Gpu.h"

#include <cstdint>

namespace sv {
namespace impl {

struct RenderTarget {
    nvrhi::TextureHandle tex;
    // A depth attachment, only where something asked for one: a 2D
    // scene never tests depth, and a framebuffer that carries an
    // attachment nobody writes is a pipeline incompatibility waiting
    // to be discovered. Set before the first resize; never after.
    nvrhi::TextureHandle depth;
    bool want_depth = false;
    // No colour at all — a shadow map is a depth texture and a
    // framebuffer with an attachment nobody writes costs a format
    // the pipeline cache would then have to distinguish.
    bool depth_only = false;
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
