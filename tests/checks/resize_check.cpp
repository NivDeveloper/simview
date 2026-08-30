// Resizing a panel must not flash. A view's target is released and
// recreated whenever its panel changes size, and a frame that sampled
// it too early — or sampled nothing — would show a colour the scene
// cannot produce, for one frame, and be gone before anyone could
// screenshot it.
//
// The property is arrangement invariance, from harness/Palette.h:
// a resize introduces no colour that steady state does not contain.
// A flash of any colour fails it, and a legitimate change of
// proportions does not, because the letterbox and the antialiased
// disc edges are already in the steady palette.
//
// Tests may speak ImGui — they are not consumers.
#include "harness/Harness.h"
#include "harness/Palette.h"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <vector>

int main() {
    harness::begin();
    using namespace sv;

    App app({.title = "resize", .size = {640, 480}, .headless = true});
    if (!app)
        return check::skip("resize", LastError());

    // gas's arrangement in miniature: a scene in the window, a second
    // scene in a view, and something on top of both.
    std::vector<float> pts;
    for (int i = 0; i < 64; ++i) {
        pts.push_back(4.0f + 92.0f * float(i % 8) / 7.0f);
        pts.push_back(4.0f + 92.0f * float(i / 8) / 7.0f);
    }
    app.SceneRange({0.0, 0.0, 100.0, 100.0});
    auto window_points =
        app.Particles({.color = {0.55f, 0.8f, 1.0f, 0.9f}, .radius = 3.0f});
    REQUIRE(bool(window_points));
    CHECK(window_points.Update(pts));

    auto view = app.View({.title = "phase"});
    REQUIRE(bool(view));
    auto view_points =
        view.Range({0.0, -50.0, 100.0, 50.0})
            .Particles({.color = {1.0f, 0.72f, 0.3f, 0.85f}, .radius = 2.0f});
    REQUIRE(bool(view_points));
    CHECK(view_points.Update(pts));

    // The lever: a test drives the resize by naming the window, which
    // is the same path a drag takes — view_draw reads the content
    // region, want changes, and the target is recreated next frame.
    float want_w = 300.0f, want_h = 220.0f;
    app.OnUi([&] { ImGui::SetWindowSize("phase", ImVec2(want_w, want_h)); });

    // Steady state, sampled at two sizes so the palette holds every
    // proportion the sweep will produce. The first shot after a size
    // change is DISCARDED: a view's target is only ever drawn during a
    // shot, so that shot is the one carrying the resize, and taking
    // the palette from it would define the anomaly as normal.
    palette::Set known;
    for (float w : {300.0f, 420.0f}) {
        want_w = w;
        want_h = w * 0.7f;
        for (int i = 0; i < 4; ++i)
            app.Step();
        Bmp settling;
        REQUIRE(harness::shot(app, "resize_settle", settling));

        app.Step();
        Bmp img;
        REQUIRE(harness::shot(app, "resize_steady", img));
        palette::absorb(img, known);
    }
    std::printf("(steady palette: %zu colours over %ux%u)\n", known.size(),
                640u, 480u);
    CHECK_GT(known.size(), std::size_t(3));

    // The sweep: every size between, one frame each — no settling, so
    // a frame that samples a target mid-recreation is not skipped over.
    int flashes = 0;
    for (int w = 300; w <= 420; w += 6) {
        want_w = float(w);
        want_h = float(w) * 0.7f;
        app.Step();

        Bmp img;
        if (!harness::shot(app, "resize_sweep", img))
            continue;

        std::array<int, 3> colour{0, 0, 0};
        const std::size_t n = palette::intruder(img, known, colour);
        if (n >= palette::kRegion) {
            ++flashes;
            std::printf("  at %dx%d: %zu pixels of rgb(%d,%d,%d), which "
                        "steady state never produced\n",
                        w, int(float(w) * 0.7f), n, colour[0], colour[1],
                        colour[2]);
        }
    }

    CHECK_EQ(flashes, 0);
    return check::summary("resize");
}
