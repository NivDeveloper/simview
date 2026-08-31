#pragma once

// What a TEST may reach that a consumer may not.
//
// These live in sv::probe, never sv::impl, and they are compiled into
// simview_probe — an archive built only under SIMVIEW_BUILD_TESTS and
// never installed. A release build does not contain them, which
// tests/installed_surface proves rather than assumes.
//
// The context types stay opaque here so a check can assert which
// context is current without any of it reaching the public surface.
// Tests are not consumers.

#include <simview/Types.h>

#include <cstddef>

struct ImGuiContext;
struct ImPlotContext;
struct ImPlot3DContext;

namespace sv {
namespace probe {

ImGuiContext *ui_context(impl::App *);
ImPlotContext *plot_context(impl::App *);
ImPlot3DContext *plot3d_context(impl::App *);

// The size a view's texture currently holds — how a check asks
// whether the panel's room reached the render target.
Extent2 view_extent(impl::App *, const char *title);

// How many Syncs the frame flips — one per Sync however many scenes
// draw it.
std::size_t gate_count(impl::App *);

// The renderer device (an nvrhi::IDevice*), for fakes that draw
// offscreen the way a real backend would. Opaque here so the probe
// header stays SDK-free.
void *render_device(impl::App *);

// A test that submits on the graphics queue itself (the viewport fake)
// brackets exactly as the frame does: no-ops unless the driver gave
// the app and gpud one queue.
void queue_lock(impl::App *);
void queue_unlock(impl::App *);

// The validation tally — errors from the Khronos layer and NVRHI's
// wrapper, process-wide — and whether SIMVIEW_VVL turned them on.
bool validation_on(impl::App *);
std::size_t validation_errors(impl::App *);

} // namespace probe
} // namespace sv
