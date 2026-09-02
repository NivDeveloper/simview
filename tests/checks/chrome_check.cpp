// The plot chrome: one shell every plot draws through, arranged the
// way the theme says.
//
// The claim worth checking is not that three arrangements LOOK
// different — that is obvious the moment one opens — but that each
// makes the trade it is named for. A bar spends height, a rail spends
// width, a menu spends neither. Read off what the chrome actually
// left for the data, so an arrangement that quietly became another one
// fails here rather than in somebody's screenshot.
#include "harness/Bmp.h"
#include "harness/Harness.h"
#include "harness/Input.h"

#include "probe/Probe.h"

#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>

namespace {

// The topmost row of the window holding a pixel of one colour. The
// legend draws a solid swatch in the series colour ABOVE the axes, so
// this says where the highest thing of that colour is without having
// to model where ImPlot put the frame.
unsigned top_row(const Bmp &img, unsigned x0, unsigned y0, unsigned x1,
                 unsigned y1, const float (&c)[3], int tol) {
    const int r = int(c[0] * 255.0f), g = int(c[1] * 255.0f),
              b = int(c[2] * 255.0f);
    for (unsigned y = y0; y < y1 && y < img.h; ++y)
        for (unsigned x = x0; x < x1 && x < img.w; ++x) {
            const auto &q = img.at(x, y);
            if (std::abs(q[0] - r) + std::abs(q[1] - g) + std::abs(q[2] - b) <=
                tol)
                return y;
        }
    return y1;
}

sv::Theme with(sv::PlotChrome how) {
    sv::Theme t = sv::themes::Midnight;
    t.plot_chrome = how;
    return t;
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {900, 600}, .headless = true});
    if (!app)
        return check::skip("chrome", LastError());

    bool mine = false;
    std::vector<float> x(64), y(64);
    for (std::size_t i = 0; i < 64; ++i) {
        x[i] = float(i) * 0.1f;
        y[i] = float((i * 7) % 13) * 0.02f;
    }
    float knob = 0.5f;
    // y is PINNED with the data in its bottom quarter, so the top of
    // the canvas is empty and the band above it has exactly one thing
    // that can be the series colour. An auto-fitted axis would spread
    // the data over the whole canvas however small the values are.
    app.Plot({.title = "signal",
              .x = {.label = "t"},
              .y = {.label = "y", .min = 0.0, .max = 1.0, .fit = Fit::Fixed}})
        .Scatter("pts", x, y, {.color = {1.0f, 0.0f, 1.0f, 1.0f}})
        .Tools([&](Panel &t) { t.IconToggle(Icon::Eye, "mine", mine); })
        .Controls([&](Panel &p) { p.Slider("knob", knob, 0.0f, 1.0f); });

    ImGui::LoadIniSettingsFromMemory(
        "[Window][signal]\nPos=30,30\nSize=600,460\n\n");

    const auto measure = [&](PlotChrome how) -> probe::PlotTools {
        app.Theme(with(how));
        for (int f = 0; f < 4; ++f)
            app.Step();
        probe::PlotTools t{};
        if (!probe::plot_tools(app.Raw(), "signal", &t))
            std::printf("no plot named signal\n");
        return t;
    };

    const auto bar = measure(PlotChrome::Bar);
    const auto rail = measure(PlotChrome::Rail);
    const auto menu = measure(PlotChrome::Menu);
    std::printf("(canvas: bar %.0fx%.0f  rail %.0fx%.0f  menu %.0fx%.0f)\n",
                double(bar.canvas_w), double(bar.canvas_h),
                double(rail.canvas_w), double(rail.canvas_h),
                double(menu.canvas_w), double(menu.canvas_h));

    // Every arrangement leaves a canvas at all.
    CHECK_GT(bar.canvas_w, 100.0f);
    CHECK_GT(bar.canvas_h, 100.0f);

    // The bar spends HEIGHT on a row of controls, so a menu holding the
    // same controls behind one button keeps more of it.
    CHECK_GT(menu.canvas_h, bar.canvas_h);
    // The rail spends WIDTH instead, which is the whole of what it is.
    CHECK_GT(bar.canvas_w, rail.canvas_w);
    // And having spent width it keeps the height the bar gave away.
    CHECK_GT(rail.canvas_h, bar.canvas_h);

    // The same things are on offer whichever arrangement is in force —
    // that is what makes this one shell and not three.
    CHECK(bar.fit_offered);
    CHECK(rail.fit_offered);
    CHECK(menu.fit_offered);

    // A rail is ONE BUTTON wide. It was a word wide first, because the
    // views control was a text button, and a column holding a word is
    // a panel that has not admitted it yet.
    const float button =
        ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.x * 2.0f;
    const float spent = bar.canvas_w - rail.canvas_w;
    std::printf("(rail costs %.0f px, one button is %.0f)\n", double(spent),
                double(button));
    CHECK_GT(button + 24.0f, spent);

    // The caller's own tools ride the strip with the engine's. Which
    // slot they land in is the engine's business, so this walks the
    // rail clicking each in turn and asks only that one of them is the
    // caller's — an ordering the check does not have to know.
    app.Theme(with(PlotChrome::Rail));
    for (int f = 0; f < 4; ++f)
        app.Step();
    const ImGuiWindow *sw = ImGui::FindWindowByName("signal");
    REQUIRE(sw != nullptr);
    const float pitch =
        ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    const float rx = sw->Pos.x + ImGui::GetStyle().WindowPadding.x +
                     ImGui::GetFrameHeight() * 0.5f + 4.0f;
    const float ry = sw->Pos.y + 40.0f + ImGui::GetFrameHeight() * 0.5f;
    int hit = -1;
    for (int k = 0; k < 8 && hit < 0; ++k) {
        input::move(app, rx, ry + pitch * float(k));
        input::press(app);
        input::release(app);
        if (mine)
            hit = k;
    }
    std::printf("(the caller's tool answered at rail slot %d)\n", hit);
    CHECK_GT(hit, -1);

    // A setting the chrome owns reaches the plot. The legend sits
    // ABOVE the axes, so turning it off gives its band back to the
    // data and nothing else in the window moves.
    app.Theme(with(PlotChrome::Bar));
    for (int f = 0; f < 4; ++f)
        app.Step();
    probe::PlotTools shown{}, hidden{};
    REQUIRE(probe::plot_tools(app.Raw(), "signal", &shown));
    Bmp with_key, without_key;
    REQUIRE(harness::shot(app, "chrome_key_on", with_key));

    probe::plot_show_legend(app.Raw(), "signal", false);
    for (int f = 0; f < 4; ++f)
        app.Step();
    REQUIRE(probe::plot_tools(app.Raw(), "signal", &hidden));
    REQUIRE(harness::shot(app, "chrome_key_off", without_key));
    std::printf("(legend on %.0f, off %.0f)\n", double(shown.canvas_h),
                double(hidden.canvas_h));
    CHECK_GT(hidden.canvas_h, shown.canvas_h);

    // Reclaiming the band is not the same as removing the key, and the
    // size alone cannot tell them apart: skipping SetupLegend gives the
    // band back AND leaves a legend drawn inside the plot, which
    // measures as success and is not. So the PICTURE is asked where
    // the highest thing in the series colour is. With the key drawn it
    // is the swatch, above the axes; without it, the first data point
    // — and the data sits in the bottom quarter of a pinned axis, so
    // the two answers are far apart.
    const ImGuiWindow *w = ImGui::FindWindowByName("signal");
    REQUIRE(w != nullptr);
    const auto wx0 = unsigned(w->Pos.x), wy0 = unsigned(w->Pos.y);
    const auto wx1 = unsigned(w->Pos.x + w->Size.x);
    const auto wy1 = unsigned(w->Pos.y + w->Size.y);
    // A colour no theme uses, so the highest pixel of it is the key or
    // the data and never a slider grab — series[0] IS the accent by
    // design, which is what this measured the first time.
    static constexpr float kKey[3] = {1.0f, 0.0f, 1.0f};
    const unsigned on = top_row(with_key, wx0, wy0, wx1, wy1, kKey, 80);
    const unsigned off = top_row(without_key, wx0, wy0, wx1, wy1, kKey, 80);
    std::printf("(highest key colour: on y=%u, off y=%u)\n", on, off);
    CHECK_GT(off, on + 100);

    return check::summary("chrome");
}
