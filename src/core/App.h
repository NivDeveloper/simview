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
#include <memory>
#include <vector>

namespace sv {
namespace impl {

struct App {
    Platform platform;
    Input input;
    Stats stats;
    std::vector<PipelineEntry> pipelines;
    std::vector<WorldPipelineEntry> world_pipelines;
    // Every Sync a scene draws, flipped once at the top of each frame.
    // Owned here for the same reason pipelines are: a view's scene and
    // the main scene borrow ONE list, so a Sync drawn by both flips once.
    std::vector<SyncGate> gates;
    SceneState scene;
    // The world drawn into the WINDOW, when there is one. A world in
    // a panel lives on its View; this is the other arrangement, and
    // the one a 3D program wants by default.
    std::unique_ptr<WorldState> world;
    UiState ui;
    std::list<View> views;
    std::list<PlotState> plots;
    std::list<PanelState> panels;
    // Creation order across BOTH: it is what staggers the windows of an
    // app that opens several, so none is hidden under another.
    int windows = 0;
};

} // namespace impl
} // namespace sv
