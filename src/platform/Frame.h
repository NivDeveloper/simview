#pragma once

#include "../core/App.h"

#include <nvrhi/nvrhi.h>

namespace sv {

struct Target {
    nvrhi::IFramebuffer *fb = nullptr;
    nvrhi::ITexture *tex = nullptr;
    std::uint32_t w = 0, h = 0;
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
};

// `acquire` may fail benignly: a minimized window has no image.
// `composites` is a property of the TARGET — the renderer's pipeline
// is pinned to one format at ui_init.
struct Presenter {
    bool (*acquire)(void *self, nvrhi::ICommandList *, Target *out) = nullptr;
    void (*finish)(void *self, nvrhi::ICommandList *, bool acquired) = nullptr;
    bool composites = false;
    void *self = nullptr;
};

// Resize the view targets, then build the UI frame. Deliberately NOT
// the frame callbacks: app_run checks `quit` between them and app_step
// does not, and that difference is theirs to keep.
void frame_build(impl::App *);

// Draw, finish, then the torn-out windows — in that order, once.
void frame_render(impl::App *, const Presenter &);

// The window. `self` is the App; the swapchain image's wait rides the
// graphics queue, so target and frame die together.
Presenter swapchain_presenter(impl::App *);

// A shot. The texture is created BEFORE the frame and outlives the
// submit, so the caller owns it and reads it back after.
struct ShotTarget {
    impl::App *app = nullptr;
    nvrhi::TextureHandle tex;
    // A shot of a world needs somewhere to test depth, exactly as its
    // window does. Created with the target and thrown away with it.
    nvrhi::TextureHandle depth;
    nvrhi::FramebufferHandle fb;
    nvrhi::StagingTextureHandle staging;
    std::uint32_t w = 0, h = 0;
    nvrhi::Format format = nvrhi::Format::RGBA8_UNORM;
};

Presenter shot_presenter(ShotTarget *);

} // namespace sv
