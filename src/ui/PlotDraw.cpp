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
    }
    return 0.0;
}

} // namespace

void plot_draw(impl::PlotState &p) {
    // A collapsed panel asks its sources nothing.
    if (!ImGui::Begin(p.title.c_str())) {
        ImGui::End();
        return;
    }
    if (ImPlot::BeginPlot("##canvas", ImVec2(-1, -1))) {
        setup_axis(ImAxis_X1, p.x);
        setup_axis(ImAxis_Y1, p.y);
        ImPlot::SetupFinish();

        for (impl::SeriesState &s : p.series) {
            const impl::SeriesData d = s.src ? s.src(s.user) : s.data;
            if (!d.b || !d.count)
                continue;
            const ImPlotSpec spec = spec_of(s.style);
            if (s.dtype == DType::f32)
                emit_series<float>(s, d, spec);
            else
                emit_series<double>(s, d, spec);
        }
        ImPlot::EndPlot();
    }
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
