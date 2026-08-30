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

} // namespace probe
} // namespace sv
