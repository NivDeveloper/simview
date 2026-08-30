#pragma once

// Internal to src/ — the App, composed. Each member's type is declared
// in its own layer's header, so a layer includes only what is below
// it; this file is the ONE place that sees all of them, and the probe
// and the frame are the only things that include it.
//
// The order of members is load-bearing once: Platform holds the
// device everything else borrows, and destruction is reverse order.

#include "../platform/Device.h"
#include "../platform/Input.h"
#include "../scene/Scene.h"
#include "../ui/PlotState.h"
#include "../ui/Ui.h"
#include "../ui/View.h"
#include "Error.h"

#include <simview/App.h>

#include <list>
#include <vector>

namespace sv {
namespace impl {

struct App {
    Platform platform;
    Input input;
    Stats stats;
    std::vector<PipelineEntry> pipelines;
    SceneState scene;
    UiState ui;
    std::list<View> views;
    std::list<PlotState> plots;
    std::list<PanelState> panels;
};

} // namespace impl
} // namespace sv
