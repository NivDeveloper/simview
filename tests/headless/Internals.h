#pragma once

// What a TEST may reach that a consumer may not. The exported
// accessors exist in the library; declaring them here — with the
// context types left opaque — lets a check assert which context is
// current without any of it appearing on the public surface.
// Tests are not consumers.

#include <simview/Types.h>

struct ImGuiContext;
struct ImPlotContext;

namespace sv {
namespace impl {

ImGuiContext *app_ui_context(App *);
ImPlotContext *app_plot_context(App *);

// The size a view's texture currently holds — how a check asks
// whether the panel's room reached the render target.
Extent2 app_view_extent(App *, const char *title);

} // namespace impl
} // namespace sv
