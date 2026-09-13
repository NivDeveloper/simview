#pragma once

#include "../core/Callbacks.h"
#include "Chips.h"

#include <simview/Event.h>
#include <simview/Input.h>
#include <simview/Theme.h>
#include <simview/Types.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <nvrhi/nvrhi.h>

#include <cstddef>
#include <cstdint>
#include <forward_list>
#include <string>
#include <vector>

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
    ::ImFont *sans = nullptr;  // the reading face; see ui_fonts
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
    // The bindings file beside the layout, its text until the app has
    // registered its rows, and whether the page is showing.
    std::string bindings;
    std::string bindings_text;
    bool bindings_dirty = false;
    bool settings_open = false;
};

// Is this window title spoken for? Plots, panels and views open ImGui
// windows and share one title namespace.
bool title_taken(App *, const char *title);

} // namespace impl

// One look for every widget: the palette, the metrics, and the two
// embedded typefaces. Applied once, at bring-up, before any frame.
void ui_fonts(impl::UiState &);
void ui_style(impl::UiState &, const Theme &);

// Run the panel callbacks — the middle of a UI frame, so platform
// never touches ui's list itself.
void ui_run_panels(impl::App *);

// Resize every view's target to what its panel asked for. Here and not
// in scene/, because only ui knows what a view is.
void ui_views_resize(impl::App *);

// Draw every view's scene into its target, ahead of the scene that
// will sample them.
void ui_views_draw(impl::App *, nvrhi::ICommandList *);

void ui_world_overlay(impl::App *);
void world_controls(impl::App *, impl::WorldState &, ::ImVec2);

// The engine's contexts and their default rows, once at bring-up; and
// every handler an app registered, freed at the end.
void actions_init(impl::App *);
void actions_quit(impl::App *);

// The frame's actions: the device snapshot, the resolve, then every
// handler — modes, pointer, gestures, camera, the app's. After the
// panels, since whether one claimed the pointer is only true then.
void actions_frame(impl::App *, bool ui);

// The world a gesture, a key or the pad means: grabbed, else under
// the pointer, else the window's; the aimed one under a crosshair.
impl::WorldState *ui_target(impl::App *);

// One function in and out of the crosshair: relative motion, ImGui
// blind, the aimed world — so the style cannot be half-entered.
void ui_pointer_style(impl::App *, PointerStyle);

// The pad's cursor, fed to ImGui before NewFrame so this frame's
// panels hover and press under it.
void ui_pointer_feed(impl::App *);

// A posted pointer event into ImGui's queue, where SDL's own land.
void ui_inject(impl::App *, const Event &);

// The gestures on one world from this frame's values: hover, pick,
// double-click, stroke and carry.
void world_gestures(impl::App *, impl::WorldState &, float dt);

// The camera's handlers for the mode that is on.
void world_camera_act(impl::App *, impl::WorldState &, float dt);

// The key bar's entries for the device in hand, the current lit.
std::vector<Chip> world_legend(impl::App *, impl::WorldState &);

// The view presets, as the menu loops them. Shared so a test applies
// the same table rather than a copy of it.
struct Preset {
    const char *name;
    float az, el;
};
const Preset *world_presets(std::size_t *count);
void world_look(impl::WorldState &, float az_deg, float el_deg);

// A context exists AND somebody asked for a panel. With nobody
// asking, no ImGui frame is built at all.
bool ui_on(impl::App *);

void ui_init(impl::App *, const Config &);
void ui_quit(impl::App *);

// Build one UI frame: NewFrame, the dockspace, then the caller runs
// the panel callbacks between these two.
void ui_begin(impl::App *);
void ui_end(impl::App *);

// The one raw-Vulkan seam: a dynamic-rendering pass records ImGui
// into the native VkCommandBuffer, then clearState().
void ui_draw(impl::App *, nvrhi::ICommandList *, nvrhi::IFramebuffer *);

// Must come AFTER the frame drew the view textures they sample.
void ui_viewports(impl::App *, bool render);

// Feed one OS event to ImGui. Returns true when a panel wants the
// keyboard, so the sim's hotkeys stay out of the way of typing.
bool ui_event(impl::App *, const SDL_Event &);

} // namespace sv
