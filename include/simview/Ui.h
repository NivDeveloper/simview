#pragma once

#include "App.h"

#include <imgui.h>
#include <implot.h>

namespace sv {

namespace impl {
ImGuiContext *app_ui_context(App *);
ImPlotContext *app_plot_context(App *);
}

inline ImGuiContext &UiContext(const App &a) {
    return *impl::app_ui_context(a.Raw());
}

inline ImPlotContext &PlotContext(const App &a) {
    return *impl::app_plot_context(a.Raw());
}

}
