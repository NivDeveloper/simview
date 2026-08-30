// A panel dragged off the main window becomes its own viewport: its
// own command buffer, drawn and presented separately, sampling what
// the main frame drew. This checks the arrangements a panel passes
// through — laid out inside the window, torn out, and resized while
// torn out — and asserts the one thing a user cares about across all
// of them: the same scene looks the same however the panels are
// arranged.
//
// The expected palette is not written down here. It is MEASURED from
// the arrangement that works, and every later arrangement must stay
// inside it. A frame that shows a colour the scene cannot produce
// fails, whatever that colour turns out to be.
#include "Harness.h"
#include "Viewports.h"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <set>
#include <vector>

namespace {

constexpr int kStep = 16;
constexpr int kFar = 64;
constexpr std::size_t kRegion = 64;

int key_of(const std::array<int, 3> &p) {
    return (p[0] / kStep) << 16 | (p[1] / kStep) << 8 | (p[2] / kStep);
}

void absorb(const Bmp &img, std::set<int> &palette) {
    for (const auto &p : img.px)
        palette.insert(key_of(p));
}

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

// The largest run of one colour the palette does not contain. Zero
// means every pixel is something this scene can produce.
std::size_t intruder(const Bmp &img, const std::set<int> &palette,
                     std::array<int, 3> &colour) {
    std::set<int> unseen;
    for (const auto &p : img.px)
        if (!palette.count(key_of(p)) && far_from(p, palette) > kFar)
            unseen.insert(key_of(p));

    std::size_t worst = 0;
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
            colour = sample;
        }
    }
    return worst;
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.title = "viewport", .size = {640, 480}, .headless = true});
    if (!app)
        return check::skip("viewport", LastError());

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

    auto view_points =
        app.View({.title = "phase"})
            .Range({0.0, -50.0, 100.0, 50.0})
            .Particles({.color = {1.0f, 0.72f, 0.3f, 0.85f}, .radius = 2.0f});
    REQUIRE(bool(view_points));
    CHECK(view_points.Update(pts));

    float want_w = 300.0f, want_h = 220.0f;
    app.OnUi([&] { ImGui::SetWindowSize("phase", ImVec2(want_w, want_h)); });

    // Arrangement one: laid out inside the window. This is the
    // reference — what the scene looks like when everything works.
    std::set<int> palette;
    for (int i = 0; i < 5; ++i)
        app.Step();
    Bmp settling;
    REQUIRE(harness::shot(app, "vp_settle", settling));
    app.Step();
    Bmp inside;
    REQUIRE(harness::shot(app, "vp_inside", inside));
    absorb(inside, palette);
    CHECK_GT(palette.size(), std::size_t(3));
    std::printf("(inside the window: %zu colours)\n", palette.size());

    // Arrangement two: torn out. The panel becomes a viewport of its
    // own, drawn by its own command buffer.
    viewports::enable(app);
    for (int i = 0; i < 3; ++i)
        app.Step();
    Bmp after_tear;
    REQUIRE(harness::shot(app, "vp_tear", after_tear));
    CHECK_GT(viewports::count(), std::size_t(0));
    std::printf("(torn out: %zu viewport(s))\n", viewports::count());

    Bmp torn;
    if (viewports::count() && viewports::read(0, "vp_torn", torn)) {
        CHECK_GT(torn.distinct(), std::size_t(2)); // it drew something

        std::array<int, 3> colour{0, 0, 0};
        const std::size_t n = intruder(torn, palette, colour);
        if (n >= kRegion)
            std::printf("  torn out: %zu pixels of rgb(%d,%d,%d), which the "
                        "same panel inside the window never showed\n",
                        n, colour[0], colour[1], colour[2]);
        CHECK(n < kRegion);
    }

    // Arrangement three: resized while torn out — the case a user
    // reaches by dragging the corner of a panel they pulled off.
    int flashes = 0;
    for (int w = 300; w <= 420; w += 10) {
        want_w = float(w);
        want_h = float(w) * 0.7f;
        app.Step();
        Bmp frame;
        if (!harness::shot(app, "vp_sweep", frame))
            continue;

        Bmp out;
        if (!viewports::count() || !viewports::read(0, "vp_sweep_out", out))
            continue;
        std::array<int, 3> colour{0, 0, 0};
        const std::size_t n = intruder(out, palette, colour);
        if (n >= kRegion) {
            ++flashes;
            std::printf("  at %dx%d torn out: %zu pixels of rgb(%d,%d,%d)\n", w,
                        int(float(w) * 0.7f), n, colour[0], colour[1],
                        colour[2]);
        }
    }
    CHECK_EQ(flashes, 0);

    return check::summary("viewport");
}
