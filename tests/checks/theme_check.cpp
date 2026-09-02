// The look, asserted rather than admired: that the text is a real
// rasterized typeface and not the built-in bitmap, that the numeric
// face is genuinely monospaced where the reading face is not, that the
// style the panels draw with is the one the theme set — and that every
// theme on offer is one a person can actually read.
//
// Tests may speak ImGui — they are not consumers.
#include "harness/Bmp.h"
#include "harness/Harness.h"
#include "harness/Input.h"

#include "probe/Probe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <implot3d.h>

// WCAG relative luminance and the contrast ratio built from it. The
// sRGB decode is the part that matters: a naive average of the three
// channels calls white-on-yellow readable.
double luminance(const float (&c)[3]) {
    double v[3];
    for (int k = 0; k < 3; ++k) {
        const double u = double(c[k]);
        v[k] = u <= 0.04045 ? u / 12.92 : std::pow((u + 0.055) / 1.055, 2.4);
    }
    return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
}

// How many pixels in a box are within `tol` of one colour. A series is
// drawn in a colour the theme names, so this is how a check asks
// whether the DATA changed and not merely the chrome around it.
std::size_t near(const Bmp &img, unsigned x0, unsigned y0, unsigned x1,
                 unsigned y1, const float (&c)[3], int tol) {
    const int r = int(c[0] * 255.0f), g = int(c[1] * 255.0f),
              b = int(c[2] * 255.0f);
    std::size_t n = 0;
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = x0; x < x1; ++x) {
            const auto &q = img.at(x, y);
            if (std::abs(q[0] - r) + std::abs(q[1] - g) + std::abs(q[2] - b) <=
                tol)
                ++n;
        }
    return n;
}

double contrast(const float (&a)[3], const float (&b)[3]) {
    const double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

int main() {
    harness::begin();
    using namespace sv;

    App app({.headless = true});
    if (!app)
        return check::skip("theme", LastError());

    // Only text, on a flat panel, so what the picture is made of is
    // glyph edges and nothing else.
    app.Panel("legibility")
        .Text("Illegible? 0O1lI58B")
        .Text("mixing sans glyphs and shapes")
        .Text("gravity 9.81 gamma 0.577");
    // Three frames: the first bakes the atlas and auto-fits, and a
    // panel measured on it is a title bar with nothing under it.
    for (int f = 0; f < 3; ++f)
        app.Step();

    ImGuiIO &io = ImGui::GetIO();
    const ImGuiStyle &s = ImGui::GetStyle();

    // Two faces, neither the built-in: AddFontDefault would leave one.
    CHECK_EQ(io.Fonts->Fonts.Size, 2);
    CHECK_EQ(s.FontSizeBase, 16.0f);

    // The numeric face is monospaced and the reading face is not —
    // which is the whole reason there are two. Asked of the fonts
    // themselves, so a swap that lost the distinction fails here.
    ImFont *sans = io.Fonts->Fonts[0];
    ImFont *mono = io.Fonts->Fonts[1];
    CHECK(sans != mono);
    const float size = s.FontSizeBase;
    ImFontBaked *bs = sans->GetFontBaked(size);
    ImFontBaked *bm = mono->GetFontBaked(size);
    REQUIRE(bs && bm);
    CHECK_EQ(bm->GetCharAdvance('i'), bm->GetCharAdvance('W'));
    CHECK_EQ(bm->GetCharAdvance('0'), bm->GetCharAdvance('8'));
    CHECK(bs->GetCharAdvance('i') != bs->GetCharAdvance('W'));

    // The style the theme set, at three places a panel actually shows:
    // a rounded window, rounded frames, and room between rows.
    CHECK_EQ(s.WindowRounding, themes::Midnight.panel_rounding);
    CHECK_EQ(s.FrameRounding, themes::Midnight.control_rounding);
    CHECK_GT(s.ItemSpacing.y, 6.0f);
    CHECK(s.WindowTitleAlign.x == 0.0f);

    // And the picture, asked about the SHAPE the theme gives a panel.
    // A rounded window leaves its extreme corner unpainted, so the
    // background shows through there and not eight pixels in; with
    // square corners the two are the same colour. Shades were tried
    // first and are not a test: a bitmap face resampled to 16px is
    // filtered, so it holds a ramp too.
    Bmp img;
    REQUIRE(harness::shot(app, "theme", img));
    const ImGuiWindow *w = ImGui::FindWindowByName("legibility");
    REQUIRE(w != nullptr);
    const auto px = unsigned(w->Pos.x), py = unsigned(w->Pos.y);
    CHECK_GT(lit_count(img, px, py, px + unsigned(w->Size.x),
                       py + unsigned(w->Size.y), 20),
             std::size_t(2000));

    const auto sum = [&](unsigned x, unsigned y) {
        const auto &q = img.at(x, y);
        return int(q[0]) + int(q[1]) + int(q[2]);
    };
    // The corner against the BACKGROUND, and the title bar against it
    // too. Painted is +47 here and unpainted is 0, because the panel
    // BODY is only +3 over the app's own ground — which is why the
    // painted sample has to be the title bar and not the body.
    //
    // Taken from the RIGHT end of that bar: the left holds the collapse
    // control now, and the original sample eight pixels in was reading
    // the title TEXT, which a glyph moving over it silently changed.
    const int outside = sum(px - 6, py - 6);
    const int corner = sum(px + 1, py + 1);
    const int inset = sum(px + unsigned(w->Size.x) - 12, py + 9);
    CHECK_LT(corner - outside, 20);
    CHECK_GT(inset - outside, 30);
    std::printf("(background %d, corner %d, eight pixels in %d)\n", outside,
                corner, inset);

    // One palette for the data too, in BOTH registries — ImPlot and
    // ImPlot3D keep separate ones, and a 3D plot that was never
    // registered falls back to its own defaults while every value in
    // Theme.cpp still looks right.
    // Asked of the COLOURS and not of a name: the registration is
    // named after a hash of the palette, so that a theme switch can
    // register a new one where ImPlot offers no way to replace an old.
    const ImPlotColormap map = ImPlot::GetStyle().Colormap;
    CHECK(map != -1);
    CHECK_EQ(ImPlot::GetColormapSize(map), 8);
    CHECK_EQ(ImPlot3D::GetColormapSize(ImPlot3D::GetStyle().Colormap), 8);

    // The first series colour IS the UI accent, so a one-series plot
    // and the chrome around it are the same blue.
    const ImVec4 first = ImPlot::GetColormapColor(0, map);
    CHECK_LT(std::abs(first.x - themes::Midnight.accent[0]), 0.01f);
    CHECK_LT(std::abs(first.y - themes::Midnight.accent[1]), 0.01f);
    CHECK_LT(std::abs(first.z - themes::Midnight.accent[2]), 0.01f);

    // What every theme owes its reader, checked of the THEMES rather
    // than of a picture: text that can be read on the surface it sits
    // on, and a dim text that is quieter without disappearing. WCAG
    // wants 4.5 for body text; a UI at this size is held to 4.5 for
    // text and to 3 for the secondary face the axis numbers use.
    //
    // This is the one property a palette can lose silently. Every
    // other mistake is visible the moment the window opens; a
    // contrast of 3.9 looks fine to whoever picked it.
    const Theme all[] = {themes::Midnight, themes::Paper, themes::Contrast,
                         themes::Terminal};
    for (const Theme &th : all) {
        const double on_panel = contrast(th.text, th.panel);
        const double dim_panel = contrast(th.text_dim, th.panel);
        const double on_field = contrast(th.text, th.field);
        const double accent_panel = contrast(th.accent, th.panel);
        std::printf("(%s: text/panel %.2f  dim/panel %.2f  text/field %.2f  "
                    "accent/panel %.2f)\n",
                    th.name, on_panel, dim_panel, on_field, accent_panel);
        CHECK_GT(on_panel, 4.5);
        CHECK_GT(on_field, 4.5);
        CHECK_GT(dim_panel, 3.0);
        CHECK_GT(accent_panel, 3.0);
    }

    // A look is more than a palette, so the SHAPE tokens have to vary
    // across the set too — otherwise four themes are four colour
    // schemes wearing the same suit. Asked of the values, since that
    // is where a fifth theme would be added.
    const auto varies = [&](auto pick) {
        for (const Theme &th : all)
            if (pick(th) != pick(all[0]))
                return true;
        return false;
    };
    CHECK(varies([](const Theme &v) { return v.panel_rounding; }));
    CHECK(varies([](const Theme &v) { return v.control_rounding; }));
    CHECK(varies([](const Theme &v) { return v.padding; }));
    CHECK(varies([](const Theme &v) { return v.gap; }));
    CHECK(varies([](const Theme &v) { return v.control_border; }));
    CHECK(varies([](const Theme &v) { return v.font_size; }));
    CHECK(varies([](const Theme &v) { return float(v.mono_ui); }));
    CHECK(varies([](const Theme &v) { return v.plot_border; }));
    CHECK(varies([](const Theme &v) { return v.tick_len; }));
    CHECK(varies([](const Theme &v) { return v.line_weight; }));

    // And every one of them REACHES the styles it names — the way a
    // token added to the struct and never wired would not.
    for (const Theme &th : all) {
        app.Theme(th);
        app.Step();
        const ImGuiStyle &g = ImGui::GetStyle();
        CHECK_EQ(g.WindowRounding, th.panel_rounding);
        CHECK_EQ(g.FrameRounding, th.control_rounding);
        CHECK_EQ(g.FrameBorderSize, th.control_border);
        CHECK_EQ(g.WindowBorderSize, th.window_border);
        CHECK_EQ(g.FontSizeBase, th.font_size);
        CHECK_EQ(ImPlot::GetStyle().MinorAlpha, th.grid_alpha);
        CHECK_EQ(ImPlot::GetStyle().PlotBorderSize, th.plot_border);
        CHECK_EQ(ImPlot::GetStyle().MajorTickLen.x, th.tick_len);
        CHECK_EQ(ImPlot3D::GetStyle().LineWeight, th.line_weight);
        CHECK_EQ(io.FontDefault == mono, th.mono_ui);
    }

    // The corner test from above, run again under a SQUARE theme: the
    // same assertion has to flip. A rounded panel leaves its extreme
    // corner unpainted; a square one paints it. Nothing else in this
    // file would notice a rounding that stopped being applied.
    app.Theme(themes::Contrast);
    app.Step();
    Bmp sharp;
    REQUIRE(harness::shot(app, "theme_sharp", sharp));
    const auto sq = [&](unsigned x, unsigned y) {
        const auto &q = sharp.at(x, y);
        return int(q[0]) + int(q[1]) + int(q[2]);
    };
    std::printf("(square: background %d, corner %d)\n", sq(px - 6, py - 6),
                sq(px + 1, py + 1));
    CHECK_GT(sq(px + 1, py + 1) - sq(px - 6, py - 6), 10);

    // The DATA takes the theme too, and this is the half that fails
    // silently: a plot caches an item's colour when the item is first
    // seen, so a switch recolours the chrome, leaves every existing
    // series as it was, and looks applied. Asked of the canvas, in the
    // colour each theme names for its first series.
    std::vector<float> ly(64);
    for (std::size_t i = 0; i < 64; ++i)
        ly[i] = float(i % 17) * 0.06f;
    app.Plot({.title = "one"}).Line("l", ly);
    ImGui::LoadIniSettingsFromMemory(
        "[Window][one]\nPos=700,80\nSize=380,280\n\n");

    app.Theme(themes::Midnight);
    for (int f = 0; f < 3; ++f)
        app.Step();
    const ImGuiWindow *lw = ImGui::FindWindowByName("one");
    REQUIRE(lw != nullptr);
    const auto cx0 = unsigned(lw->Pos.x + 70.0f);
    const auto cy0 = unsigned(lw->Pos.y + 70.0f);
    const auto cx1 = unsigned(lw->Pos.x + lw->Size.x - 30.0f);
    const auto cy1 = unsigned(lw->Pos.y + lw->Size.y - 60.0f);

    Bmp blue;
    REQUIRE(harness::shot(app, "theme_series_a", blue));
    const std::size_t az =
        near(blue, cx0, cy0, cx1, cy1, themes::Midnight.series[0], 90);

    app.Theme(themes::Terminal);
    for (int f = 0; f < 3; ++f)
        app.Step();
    Bmp green;
    REQUIRE(harness::shot(app, "theme_series_b", green));
    const std::size_t still_az =
        near(green, cx0, cy0, cx1, cy1, themes::Midnight.series[0], 90);
    const std::size_t gr =
        near(green, cx0, cy0, cx1, cy1, themes::Terminal.series[0], 90);
    std::printf("(series: azure %zu -> %zu, terminal green %zu)\n", az,
                still_az, gr);
    CHECK_GT(az, std::size_t(120));
    CHECK_GT(gr, std::size_t(120));
    CHECK_GT(az, still_az * 4);

    // A theme reaches the pixels, and lands at a frame boundary rather
    // than mid-frame. Two shots of the same window under two themes
    // must differ; the same theme twice must not.
    app.Theme(themes::Midnight);
    app.Step();
    Bmp dark, light, again;
    REQUIRE(harness::shot(app, "theme_dark", dark));
    app.Theme(themes::Paper);
    app.Step();
    REQUIRE(harness::shot(app, "theme_light", light));
    CHECK(!similar(dark, light));
    CHECK_EQ(ImGui::GetStyle().WindowRounding, themes::Paper.panel_rounding);

    app.Theme(themes::Midnight);
    app.Step();
    REQUIRE(harness::shot(app, "theme_back", again));
    CHECK(similar(dark, again));

    // Two themes, two registrations: switching back finds the palette
    // it registered the first time rather than the other theme's.
    app.Theme(themes::Terminal);
    app.Step();
    const ImPlotColormap other = ImPlot::GetStyle().Colormap;
    CHECK(other != map);
    app.Theme(themes::Midnight);
    app.Step();
    CHECK_EQ(ImPlot::GetStyle().Colormap, map);

    // Every panel and every plot can be put away, and a collapsed plot
    // asks its sources nothing — but the only way to do it was a
    // double-click on the title bar, which nobody discovers. So the
    // control has to BE there, and that is what this asks: the style
    // says where it goes, and the picture says it arrived.
    //
    // Asked of the STYLE, like every other token. Two other ways were
    // tried and neither says what it looks like it says. Clicking it
    // is unreliable: ImGui defers a press and a release delivered
    // before one NewFrame to separate frames, so a sweep of synthetic
    // clicks accumulates lag and a window toggles several frames after
    // the check looked. And counting lit pixels in the corner cannot
    // tell the glyph from the title TEXT, which moves into the same
    // strip the moment the button is gone.
    app.Theme(themes::Midnight);
    for (int f = 0; f < 3; ++f)
        app.Step();
    CHECK_EQ(int(ImGui::GetStyle().WindowMenuButtonPosition),
             int(ImGuiDir_Left));
    Theme quiet = themes::Midnight;
    quiet.collapse_button = false;
    app.Theme(quiet);
    for (int f = 0; f < 3; ++f)
        app.Step();
    CHECK_EQ(int(ImGui::GetStyle().WindowMenuButtonPosition),
             int(ImGuiDir_None));
    app.Theme(themes::Midnight);
    app.Step();

    return check::summary("theme");
}
