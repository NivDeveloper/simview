// The window is not at the screen's origin, and once viewports are on
// ImGui reports the pointer in SCREEN coordinates. A pick that measured
// from the window's own corner landed a window's position away from
// every click — which is what happened, and what the headless suite
// could not see with its window at (0, 0).

#include "fakes/Viewports.h"
#include "harness/Harness.h"
#include "harness/Input.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <imgui.h>

#include <cstdio>
#include <vector>

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {800, 600}, .headless = true});
    if (!app)
        return check::skip("pick_screen", LastError());
    viewports::enable(app);
    viewports::main_pos() = ImVec2(100.0f, 50.0f);

    sv::World w = app.World({.axes = false, .controls = false});
    REQUIRE(bool(w));
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 5.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
    auto dot = w.Cloud({.radius = 0.3f});
    REQUIRE(bool(dot));
    CHECK(dot.Update(std::vector<float>{0.0f, 0.0f, 0.0f}));
    std::vector<int> hits;
    w.OnPick([&](const Pick &p) { hits.push_back(p.index); });
    app.Step();
    app.Step();
    const ImVec2 at = ImGui::GetMainViewport()->Pos;
    std::printf("  the window sits at (%.0f, %.0f) on the screen\n", at.x,
                at.y);
    REQUIRE(at.x == 100.0f && at.y == 50.0f);
    Bmp img;
    REQUIRE(harness::shot(app, "pick_screen", img));

    // The point at the focus is at the middle of the WINDOW, which is
    // not the middle of the screen's first 800 by 600.
    input::click(app, 100.0f + 400.0f, 50.0f + 300.0f);
    std::printf("  click at the window's middle: %zu pick(s)\n", hits.size());
    CHECK_EQ(hits.size(), std::size_t(1));
    input::click(app, 400.0f, 300.0f);
    std::printf("  click a window's position away: %zu pick(s)\n", hits.size());
    CHECK_EQ(hits.size(), std::size_t(1));

    return check::summary("pick_screen");
}
