// The UI layer without a display: the context lives, panel callbacks
// run once per frame, and a panel that draws produces real geometry.
// Tests may speak ImGui — they are not consumers.
#include "harness/Harness.h"

#include "probe/Probe.h"

#include <imgui.h>
#include <implot.h>
#include <implot3d.h>

int main() {
    harness::begin();
    using namespace sv;

    App app({.headless = true});
    if (!app)
        return check::skip("ui", LastError());

    // An App with no panel registered builds no frame at all — that is
    // what keeps a headless sim's Step() free.
    app.Step();
    CHECK_EQ(ImGui::GetFrameCount(), 0);

    // The layer's own context is current, and headless claims the one
    // backend flag that keeps its geometry equal to the window's:
    // without it ImDrawList never splits at 65535 vertices.
    CHECK(ImGui::GetCurrentContext() == sv::probe::ui_context(app.Raw()));
    CHECK(ImPlot::GetCurrentContext() == sv::probe::plot_context(app.Raw()));
    // ImPlot3D shares ImPlot's trap — CreateContext sets current only
    // when none exists — so this is the same assertion, for the same
    // reason, against the second library.
    CHECK(ImPlot3D::GetCurrentContext() ==
          sv::probe::plot3d_context(app.Raw()));
    CHECK((ImGui::GetIO().BackendFlags &
           ImGuiBackendFlags_RendererHasVtxOffset) != 0);

    int calls = 0, seen = 0;
    app.OnUi([&] {
        ++calls;
        CHECK_GT(ImGui::GetFrameCount(), seen);
        seen = ImGui::GetFrameCount();
        ImGui::Begin("panel");
        ImGui::Text("a panel with real text in it");
        ImGui::End();
    });

    app.Step();
    app.Step();
    CHECK_EQ(calls, 2);
    CHECK_GT(ImGui::GetDrawData()->TotalVtxCount, 0);
    std::printf("(panel frame: %d vertices)\n",
                ImGui::GetDrawData()->TotalVtxCount);

    // A second App is a second ImGui context against one global
    // current-context — the hazard that bit the predecessor's every
    // input callback. A panel that draws NOTHING leaves the dockspace
    // alone, whose central node is a hole.
    {
        App bare({.headless = true});
        // The trap, caught at the only moment it can bite: BEFORE the
        // second App's first frame re-asserts its contexts. ImPlot's
        // and ImPlot3D's CreateContext set current only when none
        // exists — the first App's does — so without an explicit
        // SetCurrentContext in ui_init, `bare` would plot into `app`.
        CHECK(ImPlot::GetCurrentContext() ==
              sv::probe::plot_context(bare.Raw()));
        CHECK(ImPlot3D::GetCurrentContext() ==
              sv::probe::plot3d_context(bare.Raw()));
        bare.OnUi([] {});
        bare.Step();
        // Measured, not assumed: PassthruCentralNode makes the host
        // window NoBackground, so an empty dockspace draws nothing.
        CHECK_EQ(ImGui::GetDrawData()->TotalVtxCount, 0);
        CHECK_EQ(ImGui::GetFrameCount(), 1);
    }

    // The first App's contexts survived the second's whole lifetime —
    // ImPlot's included, which its CreateContext does NOT guarantee on
    // its own (it sets current only when none exists).
    app.Step();
    CHECK(ImGui::GetCurrentContext() == sv::probe::ui_context(app.Raw()));
    CHECK(ImPlot::GetCurrentContext() == sv::probe::plot_context(app.Raw()));
    CHECK(ImPlot3D::GetCurrentContext() ==
          sv::probe::plot3d_context(app.Raw()));
    CHECK_EQ(calls, 3);

    return check::summary("ui");
}
