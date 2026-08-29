#pragma once

// What a TEST may reach that a consumer may not. The exported
// accessors exist in the library; declaring them here — with the
// context types left opaque — lets a check assert which context is
// current without any of it appearing on the public surface.
// Tests are not consumers.

struct ImGuiContext;
struct ImPlotContext;

namespace sv {
namespace impl {

struct App;

ImGuiContext *app_ui_context(App *);
ImPlotContext *app_plot_context(App *);

} // namespace impl
} // namespace sv
