#pragma once

// Internal to src/ — a texture that is resized to a requested size
// and drawn into. It knows nothing about panels: the layer above asks
// for a size (want_w, want_h), and the next resize honours it. That
// one-way channel is what keeps the DAG exact — ui writes a scene
// struct's field, scene never reads a ui type.
//
// The texture is recreated BEFORE the UI frame is built, because that
// frame's draw list records the handle; a texture released after
// being recorded is a use-after-free with a picture on the other side.

#include "Scene.h"

namespace sv {
namespace impl {

struct RenderTarget {
    SDL_GPUTexture *tex = nullptr;
    Uint32 w = 0, h = 0;
    Uint32 want_w = 256, want_h = 256;
};

} // namespace impl

// Match the texture to the size asked for. A failure leaves w = h = 0
// and reports it.
void target_resize(SDL_GPUDevice *, impl::RenderTarget &);

// Draw a scene into the target, if it has one.
void target_draw(impl::SceneState &, SDL_GPUCommandBuffer *,
                 impl::RenderTarget &);

void target_release(SDL_GPUDevice *, impl::RenderTarget &);

} // namespace sv
