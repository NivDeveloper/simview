#pragma once

// Internal to src/ — one frame, in one place.
//
// A frame is: resize what the UI is about to record, build the UI,
// draw, submit, then present the torn-out windows. That order is a
// correctness property, not a style: a secondary viewport records and
// submits a command buffer of its OWN, and what it samples includes
// the view textures the main buffer wrote. Written twice, the order
// drifts and only one copy is reachable by a test — which is exactly
// how a magenta flash shipped.
//
// So the target is a PORT. A window is one adapter, a shot texture
// another, and a test may supply a third; the sequence is written
// once against the port and every caller obeys it.

#include "../core/App.h"

#include <nvrhi/nvrhi.h>

namespace sv {

struct Target {
    nvrhi::IFramebuffer *fb = nullptr;
    nvrhi::ITexture *tex = nullptr;
    std::uint32_t w = 0, h = 0;
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
};

// `acquire` may fail benignly — a minimized window has no image, and
// the frame then draws nothing but still finishes and still presents
// its torn-out windows. `composites` says whether ImGui may draw to
// this target: the renderer's pipeline is pinned to ONE format at
// ui_init, so it is a property of the target, not a preference.
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
    nvrhi::FramebufferHandle fb;
    nvrhi::StagingTextureHandle staging;
    std::uint32_t w = 0, h = 0;
    nvrhi::Format format = nvrhi::Format::RGBA8_UNORM;
};

Presenter shot_presenter(ShotTarget *);

} // namespace sv
