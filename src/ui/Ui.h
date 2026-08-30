#pragma once

// Internal to src/ — the ImGui layer. The scene never leaves the
// swapchain: ImGui composites over it in a second pass, and the
// dockspace's central node is a hole (PassthruCentralNode), so panels
// float over the scene or frame it depending only on where the user
// drags them.

#include "../core/Engine.h"

namespace sv {

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

// Upload the geometry and composite it over the scene. Must be called
// with no render pass open: PrepareDrawData opens a copy pass.
void ui_draw(impl::App *, SDL_GPUCommandBuffer *, SDL_GPUTexture *target);

// Present the panels the user tore out into their own OS windows.
void ui_viewports(impl::App *);

// Feed one OS event to ImGui. Returns true when a panel wants the
// keyboard, so the sim's hotkeys stay out of the way of typing.
bool ui_event(impl::App *, const SDL_Event &);

} // namespace sv
