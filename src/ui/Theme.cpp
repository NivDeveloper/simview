#include "../core/Math.h"
#include "Ui.h"

#include <simview/Theme.h>

#include "../core/App.h"

#include <Fonts.h>
#include <imgui.h>
#include <implot.h>
#include <implot3d.h>
#include <implot3d_internal.h>
#include <implot_internal.h>

#include <cstdint>
#include <cstdio>

namespace sv {
namespace {

constexpr ImVec4 fade(ImVec4 c, float a) { return {c.x, c.y, c.z, a}; }

constexpr ImVec4 rgb(const float (&v)[3], float a = 1.0f) {
    return {v[0], v[1], v[2], a};
}

constexpr ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
            a.w};
}

// The eight roles, plus the states derived from them. Hover and active
// are a mix TOWARD TEXT, which is why they brighten on a dark theme
// and darken on a light one without either being spelled out.
struct Roles {
    ImVec4 ink, panel, sunken, field, edge, text, dim, accent;
    ImVec4 field_hot, field_on, accent_hot, accent_deep;

    explicit constexpr Roles(const Theme &t)
        : ink(rgb(t.ink)), panel(rgb(t.panel, t.panel_alpha)),
          sunken(rgb(t.sunken)), field(rgb(t.field)), edge(rgb(t.edge)),
          text(rgb(t.text)), dim(rgb(t.text_dim)), accent(rgb(t.accent)),
          field_hot(mix(field, text, 0.10f)), field_on(mix(field, text, 0.20f)),
          accent_hot(mix(accent, text, 0.22f)),
          accent_deep(mix(accent, ink, 0.28f)) {}
};

void colours(ImGuiStyle &s, const Roles &p) {
    ImVec4 *c = s.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.dim;
    c[ImGuiCol_TextSelectedBg] = fade(p.accent, 0.35f);
    c[ImGuiCol_TextLink] = p.accent_hot;

    c[ImGuiCol_WindowBg] = p.panel;
    c[ImGuiCol_ChildBg] = fade(p.sunken, 0.60f);
    c[ImGuiCol_PopupBg] = fade(p.ink, 0.98f);
    c[ImGuiCol_Border] = p.edge;
    c[ImGuiCol_BorderShadow] = fade(p.ink, 0.0f);

    c[ImGuiCol_FrameBg] = p.field;
    c[ImGuiCol_FrameBgHovered] = p.field_hot;
    c[ImGuiCol_FrameBgActive] = p.field_on;

    // The title bar reads as part of the panel, not as a separate bar:
    // a strong title is a second thing competing with the scene.
    c[ImGuiCol_TitleBg] = p.sunken;
    c[ImGuiCol_TitleBgActive] = p.field;
    c[ImGuiCol_TitleBgCollapsed] = fade(p.sunken, 0.75f);
    c[ImGuiCol_MenuBarBg] = p.sunken;

    c[ImGuiCol_ScrollbarBg] = fade(p.ink, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = p.field_hot;
    c[ImGuiCol_ScrollbarGrabHovered] = p.field_on;
    c[ImGuiCol_ScrollbarGrabActive] = p.accent_deep;

    c[ImGuiCol_CheckMark] = p.accent_hot;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = p.accent_hot;

    // Buttons are quieter than fields until touched: on a panel of
    // sliders the two or three ACTIONS should not be the loudest thing.
    c[ImGuiCol_Button] = fade(p.field, 0.85f);
    c[ImGuiCol_ButtonHovered] = p.field_on;
    c[ImGuiCol_ButtonActive] = p.accent_deep;

    c[ImGuiCol_Header] = fade(p.accent, 0.22f);
    c[ImGuiCol_HeaderHovered] = fade(p.accent, 0.34f);
    c[ImGuiCol_HeaderActive] = fade(p.accent, 0.46f);

    c[ImGuiCol_Separator] = p.edge;
    c[ImGuiCol_SeparatorHovered] = fade(p.accent, 0.60f);
    c[ImGuiCol_SeparatorActive] = p.accent;

    c[ImGuiCol_ResizeGrip] = fade(p.dim, 0.25f);
    c[ImGuiCol_ResizeGripHovered] = fade(p.accent, 0.55f);
    c[ImGuiCol_ResizeGripActive] = p.accent;

    c[ImGuiCol_Tab] = fade(p.sunken, 0.90f);
    c[ImGuiCol_TabHovered] = p.field_on;
    c[ImGuiCol_TabSelected] = p.field;
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TabDimmed] = fade(p.sunken, 0.70f);
    c[ImGuiCol_TabDimmedSelected] = fade(p.field, 0.80f);
    c[ImGuiCol_TabDimmedSelectedOverline] = fade(p.accent, 0.40f);

    c[ImGuiCol_DockingPreview] = fade(p.accent, 0.45f);
    c[ImGuiCol_DockingEmptyBg] = fade(p.ink, 0.0f);

    c[ImGuiCol_PlotLines] = p.accent;
    c[ImGuiCol_PlotLinesHovered] = p.accent_hot;
    c[ImGuiCol_PlotHistogram] = p.accent;
    c[ImGuiCol_PlotHistogramHovered] = p.accent_hot;

    c[ImGuiCol_TableHeaderBg] = p.sunken;
    c[ImGuiCol_TableBorderStrong] = p.edge;
    c[ImGuiCol_TableBorderLight] = fade(p.edge, 0.55f);
    c[ImGuiCol_TableRowBg] = fade(p.ink, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = fade(p.text, 0.03f);

    c[ImGuiCol_TreeLines] = fade(p.edge, 0.80f);
    c[ImGuiCol_DragDropTarget] = p.accent_hot;
    c[ImGuiCol_NavCursor] = p.accent;
    c[ImGuiCol_NavWindowingHighlight] = fade(p.text, 0.70f);
    c[ImGuiCol_NavWindowingDimBg] = fade(p.ink, 0.55f);
    c[ImGuiCol_ModalWindowDimBg] = fade(p.ink, 0.65f);
    c[ImGuiCol_InputTextCursor] = p.accent_hot;
}

// PANEL and CONTROL rounding are separate, and so are PADDING
// (inside a control) and GAP (between them): one number cannot spell
// a dense console with still-legible controls.
void metrics(ImGuiStyle &s, const Theme &t) {
    const float p = t.padding;
    const float g = t.gap;
    const float r = t.panel_rounding;
    const float c = t.control_rounding;

    s.WindowPadding = ImVec2(11.0f * p, 9.0f * p);
    s.FramePadding = ImVec2(8.0f * p, 4.0f * p);
    s.CellPadding = ImVec2(7.0f * p, 4.0f * p);
    s.ItemSpacing = ImVec2(9.0f * g, 7.0f * g);
    s.ItemInnerSpacing = ImVec2(7.0f * g, 5.0f * g);
    s.IndentSpacing = 18.0f * g;
    s.ScrollbarSize = 11.0f * p;
    s.GrabMinSize = 11.0f * p;

    s.WindowBorderSize = t.window_border;
    s.ChildBorderSize = t.window_border;
    s.PopupBorderSize = t.window_border;
    s.FrameBorderSize = t.control_border;
    s.TabBorderSize = 0.0f;
    s.TabBarBorderSize = t.window_border;
    s.SeparatorTextBorderSize = 1.0f;

    // Exact fractions off the two radii, not decimals near them: the
    // ratios ARE the design, and a check that pins a rounding to 4
    // should not fail on 4.02.
    s.WindowRounding = r;
    s.ChildRounding = r * 5.0f / 6.0f;
    s.PopupRounding = r * 5.0f / 6.0f;
    s.FrameRounding = c;
    s.GrabRounding = c * 5.0f / 4.0f;
    s.ScrollbarRounding = c * 3.0f / 2.0f;
    s.TabRounding = c;

    // Left reads as a label on the panel, centred as a heading — which
    // is a weight most of this content does not have, so a theme that
    // wants it has to say so.
    s.WindowTitleAlign = ImVec2(t.title_align, 0.5f);
    s.SeparatorTextAlign = ImVec2(t.title_align, 0.5f);
    s.SeparatorTextPadding = ImVec2(18.0f * g, 5.0f * g);
    s.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f);

    // Greyed, not ghostly: a disabled control still has to be readable
    // enough to say what it would do.
    s.DisabledAlpha = 0.45f;
    // The triangle says where. Without it the only way to collapse a
    // window is a double-click nobody discovers.
    s.WindowMenuButtonPosition =
        t.collapse_button ? ImGuiDir_Left : ImGuiDir_None;
    s.AntiAliasedLines = true;
    s.AntiAliasedLinesUseTex = true;
    s.AntiAliasedFill = true;
}

// ImPlot has no way to REPLACE a colormap, so the eight colours are
// hashed and the hash is the name.
struct Series {
    ImVec4 c[8];
    char name[16];
};

Series series_of(const Theme &t) {
    Series out{};
    std::uint32_t h = impl::kFnvSeed;
    for (int i = 0; i < 8; ++i) {
        out.c[i] = rgb(t.series[i]);
        for (int k = 0; k < 3; ++k)
            h = impl::fnv1a(h, std::uint32_t(t.series[i][k] * 255.0f + 0.5f));
    }
    std::snprintf(out.name, sizeof out.name, "sv%08x", h);
    return out;
}

ImPlotColormap series_colormap(const Series &s) {
    const ImPlotColormap found = ImPlot::GetColormapIndex(s.name);
    return found != -1 ? found : ImPlot::AddColormap(s.name, s.c, 8);
}

ImPlot3DColormap series_colormap3(const Series &s) {
    const ImPlot3DColormap found = ImPlot3D::GetColormapIndex(s.name);
    return found != -1 ? found : ImPlot3D::AddColormap(s.name, s.c, 8);
}

void plot_theme(const Theme &t, const Roles &p) {
    const Series series = series_of(t);
    const float pad = t.padding;
    const float tick = t.tick_len;
    ImPlotStyle &a = ImPlot::GetStyle();
    ImVec4 *c = a.Colors;
    c[ImPlotCol_FrameBg] = fade(p.ink, 0.0f);
    c[ImPlotCol_PlotBg] = fade(p.sunken, 0.65f);
    c[ImPlotCol_PlotBorder] = p.edge;
    // The legend sits ON the data by default, so it is translucent:
    // a reader should be able to see what it covers.
    c[ImPlotCol_LegendBg] = fade(p.ink, 0.72f);
    c[ImPlotCol_LegendBorder] = p.edge;
    c[ImPlotCol_LegendText] = p.text;
    c[ImPlotCol_TitleText] = p.text;
    c[ImPlotCol_InlayText] = p.dim;
    c[ImPlotCol_AxisText] = p.dim;
    c[ImPlotCol_AxisGrid] = fade(p.edge, 0.55f);
    c[ImPlotCol_AxisTick] = p.edge;
    c[ImPlotCol_AxisBg] = fade(p.ink, 0.0f);
    c[ImPlotCol_AxisBgHovered] = fade(p.accent, 0.20f);
    c[ImPlotCol_AxisBgActive] = fade(p.accent, 0.32f);
    c[ImPlotCol_Selection] = p.accent_hot;
    c[ImPlotCol_Crosshairs] = fade(p.text, 0.55f);

    // Short ticks with room around the numbers read as a scale; the
    // defaults read as a ruler pressed against the data. Tick DENSITY
    // is not ours — ImPlot's locator exposes no knob.
    a.PlotPadding = ImVec2(12.0f * pad, 10.0f * pad);
    a.LabelPadding = ImVec2(7.0f * pad, 6.0f * pad);
    a.MajorTickLen = ImVec2(tick, tick);
    a.MinorTickLen = ImVec2(tick * 0.5f, tick * 0.5f);
    a.MajorTickSize = ImVec2(1.0f, 1.0f);
    a.MinorTickSize = ImVec2(1.0f, 1.0f);
    // A curve that touches the frame reads as clipped. A fitted axis
    // leaves a little room, more above and below than left and right,
    // because that is where a peak lands.
    a.FitPadding = ImVec2(0.02f, 0.07f);
    a.LegendPadding = ImVec2(9.0f * pad, 9.0f * pad);
    a.LegendInnerPadding = ImVec2(6.0f * pad, 4.0f * pad);
    a.PlotBorderSize = t.plot_border;
    a.MinorAlpha = t.grid_alpha;
    // ImPlot keeps the item weights on the SPEC and not on the style,
    // so a 2-D series takes them in spec_of. ImPlot3D keeps them on
    // the style, which is why only one of these two blocks has them.
    a.Colormap = series_colormap(series);

    ImPlot3DStyle &q = ImPlot3D::GetStyle();
    q.LineWeight = t.line_weight;
    q.MarkerSize = t.marker_size;
    q.FillAlpha = t.fill_alpha;
    q.PlotPadding = ImVec2(11.0f * pad, 9.0f * pad);
    q.Colormap = series_colormap3(series);
    ImVec4 *d = q.Colors;
    d[ImPlot3DCol_FrameBg] = fade(p.ink, 0.0f);
    d[ImPlot3DCol_PlotBg] = fade(p.sunken, 0.65f);
    d[ImPlot3DCol_PlotBorder] = p.edge;
    d[ImPlot3DCol_LegendBg] = fade(p.ink, 0.72f);
    d[ImPlot3DCol_LegendBorder] = p.edge;
    d[ImPlot3DCol_LegendText] = p.text;
    d[ImPlot3DCol_TitleText] = p.text;
    d[ImPlot3DCol_InlayText] = p.dim;
    d[ImPlot3DCol_AxisText] = p.dim;
    d[ImPlot3DCol_AxisGrid] = fade(p.edge, 0.55f);
    d[ImPlot3DCol_AxisTick] = p.edge;
}

} // namespace

void ui_fonts(impl::UiState &ui) {
    ImGuiIO &io = ImGui::GetIO();

    // ImGui 1.92 rasterizes on demand, so the size is a STYLE number
    // and one TTF serves every size. The bytes are ours, not freed.
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    ui.sans = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char *>(sv_font_sans), int(sizeof sv_font_sans),
        0.0f, &cfg);
    // A second face for NUMBERS. Digits that do not share a width make
    // a column of readouts jump as the last figure changes, which is
    // the one thing a reader is watching for.
    ui.mono = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char *>(sv_font_mono), int(sizeof sv_font_mono),
        0.0f, &cfg);
}

void ui_style(impl::UiState &ui, const Theme &t) {
    const Roles p{t};
    ImGuiIO &io = ImGui::GetIO();
    // Both faces are loaded either way, so which one READS is a theme
    // decision and not a load-time one. A console look wants the whole
    // interface monospaced, not just its numbers.
    io.FontDefault = t.mono_ui ? ui.mono : ui.sans;

    ImGuiStyle &s = ImGui::GetStyle();
    s.FontSizeBase = t.font_size;
    colours(s, p);
    metrics(s, t);
    plot_theme(t, p);

    // A plot caches an item's colour when the item is FIRST seen, so
    // without this the switch reaches the chrome and not the data.
    // It also clears which series a reader had hidden.
    ImPlot::BustItemCache();
    ImPlot3D::BustItemCache();
}

} // namespace sv
