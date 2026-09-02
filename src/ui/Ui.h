#pragma once

// Internal to src/ — the ImGui layer. The scene never leaves the
// swapchain: ImGui composites over it in a second pass, and the
// dockspace's central node is a hole (PassthruCentralNode), so panels
// float over the scene or frame it depending only on where the user
// drags them.

#include "../core/Callbacks.h"

#include <simview/Theme.h>
#include <simview/Types.h>

#include <SDL3/SDL.h>
#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <forward_list>
#include <string>

struct ImFont;
struct ImGuiContext;
struct ImPlotContext;
struct ImPlot3DContext;

namespace sv {

namespace impl {

struct App;
struct WorldState;

struct UiState {
    ::ImGuiContext *ctx = nullptr;
    ::ImPlotContext *plot = nullptr;
    ::ImPlot3DContext *plot3d = nullptr;
    std::string ini;
    ::ImFont *mono = nullptr;  // the numeric face; see ui_fonts
    std::forward_list<Cb> cbs; // panel callbacks, registration order
    // A view's texture is a lattice, not a photograph: the nearest
    // sampler its ImGui descriptor is baked with (VkSampler).
    std::uint64_t nearest_sampler = 0;
    // The frame ui_viewports last ran for: a composited shot and a
    // stepped frame may both ask, and ImGui asserts on a second
    // UpdatePlatformWindows in one frame.
    int viewports_pumped = -1;
    int viewports_rendered = -1;
};

// Is this window title spoken for? Plots, panels and views open ImGui
// windows and share one title namespace.
bool title_taken(App *, const char *title);

} // namespace impl

// One look for every widget: the palette, the metrics, and the two
// embedded typefaces. Applied once, at bring-up, before any frame.
void ui_fonts(impl::UiState &);
void ui_style(const Theme &);

// Run the panel callbacks — the middle of a UI frame, so platform
// never touches ui's list itself.
void ui_run_panels(impl::App *);

// Resize every view's target to what its panel asked for. Here and not
// in scene/, because only ui knows what a view is.
void ui_views_resize(impl::App *);

// Draw every view's scene into its target, ahead of the scene that
// will sample them.
void ui_views_draw(impl::App *, nvrhi::ICommandList *);

// The camera gesture. `hovered` and `active` are the caller's to
// establish: a world in a panel takes them from the image item ImGui
// already latches, a world in the window from whether any panel
// claimed the pointer.
void world_camera_gesture(impl::WorldState &, bool hovered, bool active);

// The window's world reads what the panels did not claim. Called once
// a frame, after they are built, because that is when the answer is
// true.
void ui_world_input(impl::App *);

// Is there a UI frame to build? A context exists AND somebody asked
// for a panel. With nobody asking, no ImGui frame is built at all and
// the window is exactly what it was before ImGui existed — which is
// also what keeps a headless sim's Step() free.
bool ui_on(impl::App *);

void ui_init(impl::App *, const Config &);
void ui_quit(impl::App *);

// Build one UI frame: NewFrame, the dockspace, then the caller runs
// the panel callbacks between these two.
void ui_begin(impl::App *);
void ui_end(impl::App *);

// Composite the UI over the scene: the ONE raw-Vulkan seam — NVRHI
// transitions the attachments, then a dynamic-rendering pass records
// ImGui's draw data into the command list's native VkCommandBuffer,
// then clearState() so NVRHI's cache is not stale.
void ui_draw(impl::App *, nvrhi::ICommandList *, nvrhi::IFramebuffer *);

// The torn-out panels. UpdatePlatformWindows is ImGui's per-frame
// bookkeeping and runs once per frame wherever it is first asked;
// RenderPlatformWindowsDefault draws them and must come AFTER the
// frame drew the view textures they sample — so a headless Step asks
// with render = false, and only frame_render asks with true.
void ui_viewports(impl::App *, bool render);

// Feed one OS event to ImGui. Returns true when a panel wants the
// keyboard, so the sim's hotkeys stay out of the way of typing.
bool ui_event(impl::App *, const SDL_Event &);

} // namespace sv
