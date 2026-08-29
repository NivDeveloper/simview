// The UI layer without a display: the context lives, panel callbacks
// run once per frame, and a panel that draws produces real geometry.
// Tests may speak ImGui — they are not consumers.
#include "Harness.h"

#include <simview/Ui.h>

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
        bare.OnUi([] {});
        bare.Step();
        // Measured, not assumed: PassthruCentralNode makes the host
        // window NoBackground, so an empty dockspace draws nothing.
        CHECK_EQ(ImGui::GetDrawData()->TotalVtxCount, 0);
        CHECK_EQ(ImGui::GetFrameCount(), 1);
    }

    // The first App's context survived the second's whole lifetime.
    ImGui::SetCurrentContext(&UiContext(app));
    app.Step();
    CHECK_EQ(calls, 3);

    return check::summary("ui");
}
