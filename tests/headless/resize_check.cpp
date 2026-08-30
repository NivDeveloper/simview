// Resizing a panel must not flash. A view's target is released and
// recreated whenever its panel changes size, and a frame that sampled
// it too early — or sampled nothing — would show a colour the scene
// cannot produce, for one frame, and be gone before anyone could
// screenshot it.
//
// The property asserted here is deliberately not "no magenta": it is
// that a resize introduces NO COLOUR THAT STEADY STATE DOES NOT
// CONTAIN. A flash of any colour fails it, and a legitimate change of
// proportions does not, because the letterbox and the antialiased
// disc edges are already in the steady palette.
//
// Tests may speak ImGui — they are not consumers.
#include "Harness.h"

#include <imgui.h>

#include <array>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <vector>

namespace {

constexpr int kStep = 16; // colour quantisation, as Bmp::distinct uses
constexpr int kFar = 64;  // channel-sum distance that counts as "new"
constexpr std::size_t kRegion = 64; // pixels before a colour is a flash

int key_of(const std::array<int, 3> &p) {
    return (p[0] / kStep) << 16 | (p[1] / kStep) << 8 | (p[2] / kStep);
}

void absorb(const Bmp &img, std::set<int> &palette) {
    for (const auto &p : img.px)
        palette.insert(key_of(p));
}

// A colour is a flash when steady state never showed anything like it.
// Distance is measured against the palette's own colours so that a new
// blend along an existing edge does not read as new.
int far_from(const std::array<int, 3> &p, const std::set<int> &palette) {
    int best = 765;
    for (int k : palette) {
        const int r = ((k >> 16) & 0xff) * kStep + kStep / 2;
        const int g = ((k >> 8) & 0xff) * kStep + kStep / 2;
        const int b = (k & 0xff) * kStep + kStep / 2;
        const int d =
            std::abs(p[0] - r) + std::abs(p[1] - g) + std::abs(p[2] - b);
        best = d < best ? d : best;
    }
    return best;
}

} // namespace

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
    std::set<int> palette;
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
        absorb(img, palette);
    }
    std::printf("(steady palette: %zu colours over %ux%u)\n", palette.size(),
                640u, 480u);
    CHECK_GT(palette.size(), std::size_t(3));

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

        std::set<int> unseen;
        std::vector<std::size_t> counts;
        for (const auto &p : img.px) {
            const int k = key_of(p);
            if (palette.count(k) || far_from(p, palette) <= kFar)
                continue;
            unseen.insert(k);
        }
        std::size_t worst = 0;
        std::array<int, 3> worst_colour{0, 0, 0};
        for (int k : unseen) {
            std::size_t n = 0;
            std::array<int, 3> sample{0, 0, 0};
            for (const auto &p : img.px)
                if (key_of(p) == k) {
                    ++n;
                    sample = p;
                }
            if (n > worst) {
                worst = n;
                worst_colour = sample;
            }
        }
        if (worst >= kRegion) {
            ++flashes;
            std::printf("  at %dx%d: %zu pixels of rgb(%d,%d,%d), which "
                        "steady state never produced\n",
                        w, int(float(w) * 0.7f), worst, worst_colour[0],
                        worst_colour[1], worst_colour[2]);
        }
    }

    CHECK_EQ(flashes, 0);
    return check::summary("resize");
}
