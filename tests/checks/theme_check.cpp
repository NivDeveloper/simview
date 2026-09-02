// The look, asserted rather than admired: that the text is a real
// rasterized typeface and not the built-in bitmap, that the numeric
// face is genuinely monospaced where the reading face is not, that the
// style the panels draw with is the one the theme set — and that every
// theme on offer is one a person can actually read.
//
// Tests may speak ImGui — they are not consumers.
#include "harness/Bmp.h"
#include "harness/Harness.h"

#include "probe/Probe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
    CHECK_EQ(s.WindowRounding, 6.0f);
    CHECK_EQ(s.FrameRounding, 4.0f);
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
    // Against the BACKGROUND, not against the inset: a square-cornered
    // panel is still darker at its corner than eight pixels in, because
    // of the border and the title bar, so "darker" passes either way.
    // Unpainted means indistinguishable from what is behind the panel.
    const int outside = sum(px - 6, py - 6);
    const int corner = sum(px + 1, py + 1), inset = sum(px + 9, py + 9);
    CHECK_LT(corner - outside, 20);
    CHECK_GT(inset - outside, 60);
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
    for (const Theme &th :
         {themes::Midnight, themes::Paper, themes::Contrast}) {
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

    // A theme reaches the pixels, and lands at a frame boundary rather
    // than mid-frame. Two shots of the same window under two themes
    // must differ; the same theme twice must not.
    Bmp dark, light, again;
    REQUIRE(harness::shot(app, "theme_dark", dark));
    app.Theme(themes::Paper);
    app.Step();
    REQUIRE(harness::shot(app, "theme_light", light));
    CHECK(!similar(dark, light));
    CHECK_EQ(ImGui::GetStyle().WindowRounding, themes::Paper.rounding);

    app.Theme(themes::Midnight);
    app.Step();
    REQUIRE(harness::shot(app, "theme_back", again));
    CHECK(similar(dark, again));

    // Two themes, two registrations: switching back finds the palette
    // it registered the first time rather than the other theme's.
    app.Theme(themes::Contrast);
    app.Step();
    const ImPlotColormap other = ImPlot::GetStyle().Colormap;
    CHECK(other != map);
    app.Theme(themes::Midnight);
    app.Step();
    CHECK_EQ(ImPlot::GetStyle().Colormap, map);

    return check::summary("theme");
}
