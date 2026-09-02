// Derived views: that a reduction refuses by name where it cannot be
// taken, that asking twice brings back the plot already made rather
// than a second copy of it, and that a density really is a count of
// where the points are — asked of the picture, because an orientation
// bug in a 2-D histogram passes every arithmetic assertion.
#include "harness/Bmp.h"
#include "harness/Harness.h"
#include "harness/Input.h"

#include "probe/Probe.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>
#include <vector>

int main() {
    harness::begin();
    using namespace sv;

    App app({.headless = true});
    if (!app)
        return check::skip("derive", LastError());

    // Every point in the upper-right quarter of its range, so the
    // density has exactly one place it can legitimately be bright.
    constexpr std::size_t N = 2000;
    std::vector<float> x(N), y(N);
    for (std::size_t i = 0; i < N; ++i) {
        const float u = float(i) / float(N - 1);
        x[i] = 0.60f + 0.35f * u;
        y[i] = 0.60f + 0.35f * (1.0f - u);
    }
    // Two corner points stretch the range to the full unit square, so
    // the bright cells have somewhere else they could have landed.
    x.push_back(0.0f);
    y.push_back(0.0f);
    x.push_back(1.0f);
    y.push_back(1.0f);

    auto cloud = app.Plot({.title = "cloud"});
    cloud.Scatter("pts", x, y);

    // A reduction that has nothing to reduce says so, and so does one
    // asked of a view that is already a reduction.
    auto bare = app.Plot({.title = "bare"});
    CHECK(!bare.Derive(Derived::Density));
    CHECK(std::string(LastError()).find("needs points") != std::string::npos);

    auto dens = cloud.Derive(Derived::Density);
    CHECK(dens);
    CHECK(!dens.Derive(Derived::Histogram));
    CHECK(std::string(LastError()).find("cannot itself be derived") !=
          std::string::npos);

    // The same question gives back the same window.
    auto again = cloud.Derive(Derived::Density);
    CHECK(again.Raw().p == dens.Raw().p);
    auto other = cloud.Derive(Derived::Histogram);
    CHECK(other);
    CHECK(other.Raw().p != dens.Raw().p);

    // The picture. The density window is placed on its own so the
    // quadrant question is about the heatmap and not about what else
    // happened to be under it.
    const char *ini = "[Window][density of pts (cloud)]\n"
                      "Pos=40,40\nSize=560,420\nCollapsed=0\n\n"
                      "[Window][cloud]\nPos=1400,700\nSize=200,120\n"
                      "Collapsed=1\n\n"
                      "[Window][bare]\nPos=1400,840\nSize=200,120\n"
                      "Collapsed=1\n\n"
                      "[Window][histogram of pts (cloud)]\n"
                      "Pos=1400,60\nSize=200,120\nCollapsed=1\n\n";
    ImGui::LoadIniSettingsFromMemory(ini);
    for (int f = 0; f < 6; ++f)
        app.Step();

    Bmp img;
    REQUIRE(harness::shot(app, "derive", img));
    const ImGuiWindow *w = ImGui::FindWindowByName("density of pts (cloud)");
    REQUIRE(w != nullptr);

    // Inside the canvas, clear of the axis furniture and the colour bar.
    const auto x0 = unsigned(w->Pos.x + 90.0f);
    const auto y0 = unsigned(w->Pos.y + 110.0f);
    const auto x1 = unsigned(w->Pos.x + w->Size.x - 110.0f);
    const auto y1 = unsigned(w->Pos.y + w->Size.y - 60.0f);
    const auto q = quadrants(img, x0, y0, x1, y1, 150);
    std::printf("(density quadrants: tl %zu tr %zu bl %zu br %zu)\n", q.tl,
                q.tr, q.bl, q.br);
    // Bright cells belong TOP-RIGHT, and a y-flip would put them
    // bottom-right instead, which is the bug this catches.
    CHECK_GT(q.tr, q.tl + q.bl + q.br);
    CHECK_GT(q.tr, std::size_t(40));

    // The window the engine offered, the reader may dismiss. Clicking
    // the title bar's close box is what a reader does, so that is what
    // this clicks — a probe that closed it directly would prove the
    // flag and not the affordance.
    probe::PlotTools t{};
    REQUIRE(probe::plot_tools(app.Raw(), "density of pts (cloud)", &t));
    CHECK(t.open);
    input::move(app, w->Pos.x + w->Size.x - 14.0f, w->Pos.y + 12.0f);
    input::press(app);
    input::release(app);
    REQUIRE(probe::plot_tools(app.Raw(), "density of pts (cloud)", &t));
    CHECK(!t.open);

    // Asking again brings THAT one back rather than a second copy.
    auto reopened = cloud.Derive(Derived::Density);
    CHECK(reopened.Raw().p == dens.Raw().p);
    app.Step();
    REQUIRE(probe::plot_tools(app.Raw(), "density of pts (cloud)", &t));
    CHECK(t.open);

    // A fit control where a refit would do something, and none where
    // it would not. A plot whose axes fit once and are then the
    // reader's offers one — the source and the density both do. A
    // plot pinned to fixed limits ignores a refit, so offering it
    // would be a button that does nothing.
    CHECK(t.fit_offered);
    probe::PlotTools src{};
    REQUIRE(probe::plot_tools(app.Raw(), "cloud", &src));
    CHECK(src.fit_offered);

    app.Plot({.title = "pinned",
              .x = {.min = 0.0, .max = 1.0, .fit = Fit::Fixed},
              .y = {.min = 0.0, .max = 1.0, .fit = Fit::Fixed}})
        .Line("l", x, y);
    app.Step();
    probe::PlotTools pinned{};
    REQUIRE(probe::plot_tools(app.Raw(), "pinned", &pinned));
    CHECK(!pinned.fit_offered);

    // The joint view is pictures that have to AGREE. A marginal
    // computed off the wrong axis, or a sideways bar handed its
    // arguments the way an upright one takes them, leaves the field
    // right and one margin wrong.
    // Its own source, and deliberately NOT symmetric in x and y: the
    // scatter above spreads both over the same range, so a joint view
    // that counted each margin off the other axis would draw exactly
    // the same picture. Here the mass is LEFT in x and HIGH in y, so
    // the two margins lean opposite ways and swapping them flips both.
    std::vector<float> sx(400), sy(400);
    for (std::size_t i = 0; i < 400; ++i) {
        const float u = float(i) / 399.0f;
        sx[i] = 0.05f + 0.40f * u;
        sy[i] = 0.80f + 0.15f * u;
    }
    sx.push_back(0.0f);
    sy.push_back(0.0f);
    sx.push_back(1.0f);
    sy.push_back(1.0f);
    auto spread = app.Plot({.title = "spread"});
    spread.Scatter("pts", sx, sy);
    auto jt = spread.Derive(Derived::Joint);
    REQUIRE(bool(jt));
    ImGui::LoadIniSettingsFromMemory(
        "[Window][spread]\nPos=1400,300\nSize=200,120\nCollapsed=1\n\n"
        "[Window][joint view of pts (spread)]\nPos=40,40\nSize=640,520\n\n");
    for (int f = 0; f < 6; ++f)
        app.Step();

    Bmp jimg;
    REQUIRE(harness::shot(app, "derive_joint", jimg));
    const ImGuiWindow *jw =
        ImGui::FindWindowByName("joint view of pts (spread)");
    REQUIRE(jw != nullptr);

    // Boxes from the subplot's OWN ratios and not from the window's
    // middle: the field column is 76% of the width, so its midpoint is
    // at 0.38 and a split down the centre puts part of the right
    // margin in the left half.
    const float jx = jw->Pos.x + 14.0f, jy = jw->Pos.y + 96.0f;
    const float jw_w = jw->Pos.x + jw->Size.x - 14.0f - jx;
    const float jw_h = jw->Pos.y + jw->Size.y - 14.0f - jy;
    const auto at_x = [&](float f) { return unsigned(jx + jw_w * f); };
    const auto at_y = [&](float f) { return unsigned(jy + jw_h * f); };

    const std::size_t top_left =
        lit_count(jimg, at_x(0.02f), at_y(0.02f), at_x(0.38f), at_y(0.24f), 60);
    const std::size_t top_right =
        lit_count(jimg, at_x(0.38f), at_y(0.02f), at_x(0.74f), at_y(0.24f), 60);
    const std::size_t side_up =
        lit_count(jimg, at_x(0.78f), at_y(0.28f), at_x(0.99f), at_y(0.62f), 60);
    const std::size_t side_down =
        lit_count(jimg, at_x(0.78f), at_y(0.62f), at_x(0.99f), at_y(0.98f), 60);
    std::printf("(joint margins: top %zu left vs %zu right, side %zu up vs "
                "%zu down)\n",
                top_left, top_right, side_up, side_down);
    CHECK_GT(top_left, top_right * 2);
    CHECK_GT(side_up, side_down * 2);

    return check::summary("derive");
}
