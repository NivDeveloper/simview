// Drawing a plot and a panel. Both are ImGui windows, so both dock,
// tab and tear out; the plot's canvas is ImPlot's.
//
// Two asymmetries here are easy to get wrong and are written once:
// ImGui::Begin returning false STILL requires End(), while BeginPlot
// returning false must NOT be followed by EndPlot(). And ImPlot's
// setup is a PHASE — every Setup* must precede the first item, so the
// axes are configured, SetupFinish draws the line, and only then are
// the sources asked.

#include "PlotState.h"

#include "../core/App.h"
#include "Ui.h"
#include "View.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <climits>
#include <cstdio>

namespace sv {
namespace {

void setup_axis(ImAxis ax, const impl::AxisState &a) {
    ImPlotAxisFlags flags = ImPlotAxisFlags_None;
    if (a.desc.fit == Fit::Stream)
        flags |= ImPlotAxisFlags_AutoFit;
    if (a.desc.invert)
        flags |= ImPlotAxisFlags_Invert;
    ImPlot::SetupAxis(ax, a.label.empty() ? nullptr : a.label.c_str(), flags);

    // Any limit at all suppresses ImPlot's initial fit — which is what
    // makes "start here, then it is yours" different from "stay here".
    if (a.desc.fit == Fit::Start)
        ImPlot::SetupAxisLimits(ax, a.desc.min, a.desc.max, ImPlotCond_Once);
    else if (a.desc.fit == Fit::Fixed)
        ImPlot::SetupAxisLimits(ax, a.desc.min, a.desc.max, ImPlotCond_Always);

    if (a.desc.scale == AxisScale::Log10)
        ImPlot::SetupAxisScale(ax, ImPlotScale_Log10);
    else if (a.desc.scale == AxisScale::SymLog)
        ImPlot::SetupAxisScale(ax, ImPlotScale_SymLog);
}

// Never brace-initialised: ImPlotSpec's variadic constructor reads
// braces as (property, value) pairs and fails the arity assert.
ImPlotSpec spec_of(const SeriesStyle &st) {
    ImPlotSpec spec;
    if (st.color[3] >= 0.0f)
        spec.LineColor =
            ImVec4(st.color[0], st.color[1], st.color[2], st.color[3]);
    if (st.fill[3] >= 0.0f)
        spec.FillColor = ImVec4(st.fill[0], st.fill[1], st.fill[2], st.fill[3]);
    spec.FillAlpha = st.fill_alpha;
    spec.LineWeight = st.weight;
    spec.Marker = st.marker;
    spec.MarkerSize = st.marker_size;
    spec.Size = st.size;
    return spec;
}

// The scalar is erased ONCE, here — so a new series kind is one enum
// value, one case, and one method on the builder, whatever the element
// type. A kind casts only the slots it reads, at the point it reads
// them; the slots are positional BY KIND and the case says which.
template <class T>
double emit_series(const impl::SeriesState &s, const impl::SeriesData &d,
                   const ImPlotSpec &spec) {
    const char *name = s.name.c_str();
    const int n = int(std::min<std::size_t>(d.count, INT_MAX));
    const T *a = static_cast<const T *>(d.a);
    const T *b = static_cast<const T *>(d.b);
    switch (s.kind) {
    case impl::SeriesKind::Line: // a = x (optional), b = y
        if (a)
            ImPlot::PlotLine(name, a, b, n, spec);
        else
            ImPlot::PlotLine(name, b, n, 1, 0, spec);
        return 0.0;
    case impl::SeriesKind::Scatter: // a = x (optional), b = y
        if (a)
            ImPlot::PlotScatter(name, a, b, n, spec);
        else
            ImPlot::PlotScatter(name, b, n, 1, 0, spec);
        return 0.0;
    case impl::SeriesKind::Histogram: // b = values; param[0] = bins
        return ImPlot::PlotHistogram(name, b, n, int(s.param[0]), 1.0,
                                     ImPlotRange(), spec);
    case impl::SeriesKind::Stairs: // a = x (optional), b = y
        if (a)
            ImPlot::PlotStairs(name, a, b, n, spec);
        else
            ImPlot::PlotStairs(name, b, n, 1, 0, spec);
        return 0.0;
    case impl::SeriesKind::Shaded: // a = x, b = y (+ c = hi: a band);
                                   // param[0] = yref
        if (d.c)
            ImPlot::PlotShaded(name, a, b, static_cast<const T *>(d.c), n,
                               spec);
        else if (a)
            ImPlot::PlotShaded(name, a, b, n, s.param[0], spec);
        else
            ImPlot::PlotShaded(name, b, n, s.param[0], 1, 0, spec);
        return 0.0;
    case impl::SeriesKind::Bars: // a = x (optional), b = y; param[0] = width
        if (a)
            ImPlot::PlotBars(name, a, b, n, s.param[0], spec);
        else
            ImPlot::PlotBars(name, b, n, s.param[0], 0, spec);
        return 0.0;
    case impl::SeriesKind::Stems: // a = x (optional), b = y; param[0] = ref
        if (a)
            ImPlot::PlotStems(name, a, b, n, s.param[0], spec);
        else
            ImPlot::PlotStems(name, b, n, s.param[0], 1, 0, spec);
        return 0.0;
    case impl::SeriesKind::InfLines: // b = positions
        ImPlot::PlotInfLines(name, b, n, spec);
        return 0.0;
    case impl::SeriesKind::Digital: // a = x, b = y
        ImPlot::PlotDigital(name, a, b, n, spec);
        return 0.0;
    case impl::SeriesKind::ErrorBars: // a = x, b = y, c = err | c = neg, d =
                                      // pos
        if (d.d)
            ImPlot::PlotErrorBars(name, a, b, static_cast<const T *>(d.c),
                                  static_cast<const T *>(d.d), n, spec);
        else
            ImPlot::PlotErrorBars(name, a, b, static_cast<const T *>(d.c), n,
                                  spec);
        return 0.0;
    case impl::SeriesKind::Heatmap: // b = values, count = rows, count2 = cols;
                                    // param[0..1] = scale, bounds = rect
        ImPlot::PlotHeatmap(name, b, int(d.count), int(d.count2), s.param[0],
                            s.param[1], nullptr,
                            ImPlotPoint(s.bounds.x0, s.bounds.y0),
                            ImPlotPoint(s.bounds.x1, s.bounds.y1), spec);
        return 0.0;
    }
    return 0.0;
}

// A kind's flag lives on the spec, set from the style: horizontal is
// the one every bar-shaped kind shares.
void apply_flags(ImPlotSpec &spec, impl::SeriesKind k, const SeriesStyle &st) {
    if (!st.horizontal)
        return;
    switch (k) {
    case impl::SeriesKind::Bars:
        spec.Flags |= ImPlotBarsFlags_Horizontal;
        break;
    case impl::SeriesKind::Stems:
        spec.Flags |= ImPlotStemsFlags_Horizontal;
        break;
    case impl::SeriesKind::InfLines:
        spec.Flags |= ImPlotInfLinesFlags_Horizontal;
        break;
    case impl::SeriesKind::ErrorBars:
        spec.Flags |= ImPlotErrorBarsFlags_Horizontal;
        break;
    case impl::SeriesKind::Histogram:
        spec.Flags |= ImPlotHistogramFlags_Horizontal;
        break;
    default:
        break;
    }
}

// simview's Palette enum is its own, so implot.h stays off the public
// surface. The values line up with ImPlotColormap_ by construction —
// asserted, because that is the kind of fact that drifts silently.
static_assert(int(Palette::Deep) == ImPlotColormap_Deep);
static_assert(int(Palette::Greys) == ImPlotColormap_Greys);

// Does this plot show a colourbar? A Heatmap with an explicit scale
// does; ImPlot's ColormapScale is an ImGui widget that must sit
// OUTSIDE the plot bracket, so plot_draw asks before it begins.
const impl::SeriesState *colourbar_of(const impl::PlotState &p) {
    for (const impl::SeriesState &s : p.series)
        if (s.kind == impl::SeriesKind::Heatmap && s.param[1] > s.param[0])
            return &s;
    return nullptr;
}

} // namespace

void plot_draw(impl::PlotState &p) {
    // A collapsed panel asks its sources nothing.
    if (!ImGui::Begin(p.title.c_str())) {
        ImGui::End();
        return;
    }
    // PushColormap wraps the whole bracket — and the colourbar beside
    // it, which is why a heatmap's bar reserves its width up front.
    const bool mapped = p.palette != Palette::Auto;
    if (mapped)
        ImPlot::PushColormap(int(p.palette));
    const impl::SeriesState *bar = colourbar_of(p);
    const float bar_w = bar ? 60.0f + ImGui::GetStyle().ItemSpacing.x : 0.0f;

    if (ImPlot::BeginPlot("##canvas", ImVec2(-1 - bar_w, -1))) {
        setup_axis(ImAxis_X1, p.x);
        setup_axis(ImAxis_Y1, p.y);
        ImPlot::SetupFinish();

        for (impl::SeriesState &s : p.series) {
            const impl::SeriesData d = s.src ? s.src(s.user) : s.data;
            if (!d.b || !d.count)
                continue;
            ImPlotSpec spec = spec_of(s.style);
            apply_flags(spec, s.kind, s.style);
            if (s.dtype == DType::f32)
                emit_series<float>(s, d, spec);
            else
                emit_series<double>(s, d, spec);
        }
        ImPlot::EndPlot();
    }
    if (bar) {
        ImGui::SameLine();
        ImPlot::ColormapScale("##scale", bar->param[0], bar->param[1],
                              ImVec2(60, -1));
    }
    if (mapped)
        ImPlot::PopColormap();
    ImGui::End();
}

void panel_draw(impl::PanelState &p) {
    if (!ImGui::Begin(p.title.c_str())) {
        ImGui::End();
        return;
    }
    for (impl::WidgetState &w : p.widgets) {
        switch (w.kind) {
        case impl::WidgetKind::Text:
            ImGui::TextUnformatted(w.label.c_str());
            break;
        case impl::WidgetKind::Separator:
            ImGui::Separator();
            break;
        case impl::WidgetKind::Button:
            // The click runs while the UI frame is being BUILT, which
            // is CPU-only — no command buffer is open, so a callback
            // that touches the GPU cannot stomp one.
            if (ImGui::Button(w.label.c_str()) && w.on_click)
                w.on_click(w.user);
            break;
        case impl::WidgetKind::Slider:
            ImGui::SliderFloat(w.label.c_str(), static_cast<float *>(w.target),
                               w.min, w.max);
            break;
        case impl::WidgetKind::Checkbox:
            ImGui::Checkbox(w.label.c_str(), static_cast<bool *>(w.target));
            break;
        case impl::WidgetKind::Value: {
            // Bound or pulled, the same duality a series has.
            const double v = w.value ? w.value(w.user)
                                     : double(*static_cast<float *>(w.target));
            char buf[64];
            std::snprintf(buf, sizeof buf, w.fmt.c_str(), v);
            ImGui::LabelText(w.label.c_str(), "%s", buf);
            break;
        }
        }
    }
    ImGui::End();
}

} // namespace sv
