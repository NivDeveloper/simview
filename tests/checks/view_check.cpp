// A view: a scene drawn into its own texture and shown by a panel.
// What is checked here is the wiring a display would otherwise be
// needed to see — that the view's scene really draws, that its target
// is sized from the panel rather than from a constant, and that the
// two never share a window title.
#include "harness/Harness.h"

#include "probe/Probe.h"

#include <imgui.h>

#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    harness::begin();
    using namespace sv;

    App app({.headless = true});
    if (!app)
        return check::skip("view", LastError());

    auto scene = app.View({.title = "lattice"});
    REQUIRE(bool(scene));

    // A view is a scene: the same items go in it, through the same
    // spelling as the App's own.
    auto f = scene.Field({.extent = {32, 32}, .map = Colormap::Viridis});
    REQUIRE(bool(f));
    std::vector<float> v(32 * 32);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = float(i % 32) / 31.0f;
    CHECK(f.Update(v));

    // One window title namespace across plots, panels and views.
    CHECK(!app.View({.title = "lattice"}));
    CHECK(!app.Panel("lattice"));
    CHECK(!app.View({.title = ""}));

    // Before any frame the target is unsized: nothing has said how
    // much room the panel has.
    CHECK_EQ(sv::probe::view_extent(app.Raw(), "lattice").w, 0u);

    app.Step();
    app.Step();
    const Extent2 e = sv::probe::view_extent(app.Raw(), "lattice");
    // Sized from the panel, not from a constant — and not collapsed,
    // which is what a window auto-fitting an image sized from itself
    // would do.
    CHECK_GT(e.w, 64u);
    CHECK_GT(e.h, 64u);
    CHECK(e.w != 256u || e.h != 256u);
    std::printf("(view target: %u x %u)\n", e.w, e.h);

    // The panel emits the image quad, so the UI frame is not empty.
    CHECK_GT(ImGui::GetDrawData()->TotalVtxCount, 0);

    // A shot draws every view's scene into its own texture before the
    // main scene: one field in the view, one in the App, two draws.
    auto g = app.Field({.extent = {16, 16}});
    REQUIRE(bool(g));
    CHECK(g.Update(std::vector<float>(16 * 16, 0.5f)));

    const std::uint64_t before = app.Stats().draws;
    Bmp img;
    CHECK(harness::shot(app, "view", img));
    CHECK_EQ(app.Stats().draws - before, std::uint64_t(2));

    return check::summary("view");
}
