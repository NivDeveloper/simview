// The look, asserted rather than admired: that the text is a real
// rasterized typeface and not the built-in bitmap, that the numeric
// face is genuinely monospaced where the reading face is not, and that
// the style the panels draw with is the one the theme set.
//
// Tests may speak ImGui — they are not consumers.
#include "harness/Bmp.h"
#include "harness/Harness.h"

#include "probe/Probe.h"

#include <imgui.h>
#include <imgui_internal.h>

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

    return check::summary("theme");
}
