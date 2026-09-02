// Drawing a plot and a panel. Both are ImGui windows, so both dock,
// tab and tear out; the plot's canvas is ImPlot's.
//
// Two asymmetries here are easy to get wrong and are written once:
// ImGui::Begin returning false STILL requires End(), while BeginPlot
// returning false must NOT be followed by EndPlot(). And ImPlot's
// setup is a PHASE — every Setup* must precede the first item, so the
// axes are configured, SetupFinish draws the line, and only then are
// the sources asked.

#include "Icons.h"
#include "PlotState.h"

#include "../core/App.h"
#include "Ui.h"
#include "View.h"

#include <imgui.h>
#include <implot.h>
#include <implot3d.h>

#include <simview/sync/Sync.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <vector>

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
    case impl::SeriesKind::Line3:
    case impl::SeriesKind::Scatter3:
    case impl::SeriesKind::Surface:
    case impl::SeriesKind::Mesh:
        // A 3D kind cannot reach the 2D emitter: plot_series refuses
        // the family mismatch first. Listed so -Wswitch stays a real
        // signal for the next kind that IS forgotten.
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

// The 3D twins. Same shape as their 2D counterparts, against the
// other library: the axis setup lifts Fit unchanged (the Cond and the
// AutoFit/Invert flags mirror ImPlot's), and AxisScale is a no-op —
// ImPlot3D has no log axis, so a 3D axis asked for one gets linear,
// which is said here rather than left to be discovered.
void setup_axis3(ImAxis3D ax, const impl::AxisState &a) {
    ImPlot3DAxisFlags flags = ImPlot3DAxisFlags_None;
    if (a.desc.fit == Fit::Stream)
        flags |= ImPlot3DAxisFlags_AutoFit;
    if (a.desc.invert)
        flags |= ImPlot3DAxisFlags_Invert;
    ImPlot3D::SetupAxis(ax, a.label.empty() ? nullptr : a.label.c_str(), flags);
    if (a.desc.fit == Fit::Start)
        ImPlot3D::SetupAxisLimits(ax, a.desc.min, a.desc.max,
                                  ImPlot3DCond_Once);
    else if (a.desc.fit == Fit::Fixed)
        ImPlot3D::SetupAxisLimits(ax, a.desc.min, a.desc.max,
                                  ImPlot3DCond_Always);
}

ImPlot3DSpec spec3_of(const SeriesStyle &st) {
    ImPlot3DSpec spec;
    if (st.color[3] >= 0.0f)
        spec.LineColor =
            ImVec4(st.color[0], st.color[1], st.color[2], st.color[3]);
    if (st.fill[3] >= 0.0f)
        spec.FillColor = ImVec4(st.fill[0], st.fill[1], st.fill[2], st.fill[3]);
    spec.FillAlpha = st.fill_alpha;
    spec.LineWeight = st.weight;
    spec.Marker = st.marker;
    spec.MarkerSize = st.marker_size;
    return spec;
}

template <class T>
void emit_series3(const impl::SeriesState &s, const impl::SeriesData &d,
                  const ImPlot3DSpec &spec) {
    const char *name = s.name.c_str();
    const int n = int(std::min<std::size_t>(d.count, INT_MAX));
    const T *a = static_cast<const T *>(d.a);
    const T *b = static_cast<const T *>(d.b);
    const T *c = static_cast<const T *>(d.c);
    switch (s.kind) {
    case impl::SeriesKind::Line3: // a, b, c = xs, ys, zs
        ImPlot3D::PlotLine(name, a, b, c, n, spec);
        return;
    case impl::SeriesKind::Scatter3: // a, b, c = xs, ys, zs
        ImPlot3D::PlotScatter(name, a, b, c, n, spec);
        return;
    case impl::SeriesKind::Surface: // a, b, c = xs, ys, zs over a grid;
                                    // count, count2 = x_count, y_count;
                                    // param[0..1] = scale
        ImPlot3D::PlotSurface(name, a, b, c, int(d.count), int(d.count2),
                              s.param[0], s.param[1], spec);
        return;
    case impl::SeriesKind::Mesh: // a, b, c = vertices; idx = triangles;
                                 // count, count2 = vtx_count, idx_count
        ImPlot3D::PlotMesh(name, a, b, c, d.idx, int(d.count),
                           int(std::min<std::size_t>(d.count2, INT_MAX)), spec);
        return;
    default:
        return; // a 2D kind cannot reach here: plot_series refused it
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

// Which colormap a plot draws with. `Auto` is the caller delegating the
// decision, not asking for the default: a CONTINUOUS field read through
// a qualitative set is a picture with no order in it, so a plot holding
// a heatmap or a surface takes a perceptually uniform map instead. An
// explicit palette always wins.
ImPlotColormap colormap_of(const impl::PlotState &p) {
    if (p.palette != Palette::Auto)
        return ImPlotColormap(int(p.palette));
    for (const impl::SeriesState &s : p.series)
        if (s.kind == impl::SeriesKind::Heatmap ||
            s.kind == impl::SeriesKind::Surface)
            return ImPlotColormap_Viridis;
    return -1;
}

// Where a window lands before the layout file has an opinion. ImGui
// puts every new window in the same place, so an app that opens nine
// plots opens one plot with eight underneath it. A cascade that wraps
// every eighth window leaves every title bar reachable, which is all a
// default has to do — the layout the user drags into place is saved and
// wins from then on.
void place_window(int slot, float width, float height) {
    const int col = slot / 6, row = slot % 6;
    // The column step follows the window's own width, or the second
    // column lands on the first and the cascade has bought nothing.
    ImGui::SetNextWindowPos(
        ImVec2(26.0f + float(col) * (width + 60.0f) + float(row) * 22.0f,
               26.0f + float(row) * 30.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
}

// The strip above the canvas: what this plot can BECOME, then whatever
// controls the caller put on it. A reduction offered where the data is
// costs one click; the same reduction written out costs an afternoon,
// which is the difference between a question you ask and one you do
// not bother to.
// Refitting is worth a control only where it would change something:
// a fixed axis ignores it, and a streaming one has already refitted
// this frame. That leaves the two that fit ONCE and are then the
// reader's — which is exactly when a reader who has panned away wants
// the data back.
bool fittable(const impl::AxisState &a) {
    return a.desc.fit == Fit::Data || a.desc.fit == Fit::Start;
}

void plot_toolbar(impl::PlotState &p) {
    const std::vector<DeriveOption> options = derive_options(p);
    const bool has_controls = p.controls && !p.controls->widgets.empty();
    p.fit_offered =
        p.family == impl::Family::Plot2D && (fittable(p.x) || fittable(p.y));
    if (options.empty() && !has_controls && !p.fit_offered)
        return;

    if (p.fit_offered) {
        if (impl::icon_button(Icon::Fit, "fit", "fit the axes to the data"))
            p.fit_pending = true;
        if (!options.empty())
            ImGui::SameLine();
    }
    if (!options.empty()) {
        if (ImGui::SmallButton("views"))
            ImGui::OpenPopup("##views");
        if (ImGui::BeginPopup("##views")) {
            for (const DeriveOption &o : options)
                if (ImGui::Selectable(o.label.c_str()))
                    impl::plot_derive(impl::Plot{&p}, o.kind,
                                      o.series->name.c_str());
            ImGui::EndPopup();
        }
    }
    if (has_controls)
        panel_body(*p.controls);
    ImGui::Separator();
}

} // namespace

void plot_draw(impl::PlotState &p) {
    // A view the engine offered is a view the reader may dismiss. One
    // the CALLER declared is not: it would close with nothing left to
    // reopen it from.
    const bool closable = p.derivation != nullptr;
    if (closable && !p.open)
        return;

    place_window(p.slot, 520.0f, 360.0f);
    // A collapsed panel asks its sources nothing.
    bool keep = true;
    if (!ImGui::Begin(p.title.c_str(), closable ? &keep : nullptr)) {
        ImGui::End();
        p.open = keep;
        return;
    }
    if (p.derivation)
        derive_update(p);
    plot_toolbar(p);

    // PushColormap wraps the whole bracket — and the colourbar beside
    // it, which is why a heatmap's bar reserves its width up front.
    const ImPlotColormap map = colormap_of(p);
    const bool mapped = map != -1;
    if (mapped)
        ImPlot::PushColormap(map);
    const impl::SeriesState *bar = colourbar_of(p);
    const float bar_w = bar ? 60.0f + ImGui::GetStyle().ItemSpacing.x : 0.0f;

    // ONE function, ONE place the family is switched: the bracket.
    // The window, the title namespace, the source pull, the dtype
    // erasure and the list addressing are shared; only the library
    // that opens the plot and draws the items differs.
    if (p.family == impl::Family::Plot3D) {
        if (mapped)
            ImPlot3D::PushColormap(map);
        if (ImPlot3D::BeginPlot("##canvas", ImVec2(-1, -1))) {
            setup_axis3(ImAxis3D_X, p.x);
            setup_axis3(ImAxis3D_Y, p.y);
            setup_axis3(ImAxis3D_Z, p.z);
            // ImPlot3D has no Outside flag, so the key sits inside at
            // the top — where a 3-D box has its most empty corner.
            ImPlot3D::SetupLegend(ImPlot3DLocation_NorthEast,
                                  ImPlot3DLegendFlags_Horizontal);
            for (impl::SeriesState &s : p.series) {
                const impl::SeriesData d = s.src ? s.src(s.user) : s.data;
                if (!d.c || !d.count)
                    continue;
                const ImPlot3DSpec spec = spec3_of(s.style);
                if (s.dtype == DType::f32)
                    emit_series3<float>(s, d, spec);
                else
                    emit_series3<double>(s, d, spec);
            }
            ImPlot3D::EndPlot();
        }
        if (p.palette != Palette::Auto)
            ImPlot3D::PopColormap();
        if (mapped)
            ImPlot::PopColormap();
        ImGui::End();
        p.open = keep;
        return;
    }

    if (p.fit_pending) {
        ImPlot::SetNextAxesToFit();
        p.fit_pending = false;
    }
    if (ImPlot::BeginPlot("##canvas", ImVec2(-1 - bar_w, -1))) {
        setup_axis(ImAxis_X1, p.x);
        setup_axis(ImAxis_Y1, p.y);
        // A key ABOVE the axes, not floating on the data: an inside
        // legend covers whatever it lands on, and the one place it can
        // never be in the way is outside the frame.
        ImPlot::SetupLegend(ImPlotLocation_North,
                            ImPlotLegendFlags_Outside |
                                ImPlotLegendFlags_Horizontal);
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
    p.open = keep;
}

namespace {

// Numbers wear the monospaced face. Digits that do not share a width
// make a column of readouts jump as the last figure changes, which is
// the one thing a reader is watching.
struct Numerals {
    explicit Numerals(::ImFont *f) {
        ImGui::PushFont(f, ImGui::GetStyle().FontSizeBase * 0.94f);
    }
    ~Numerals() { ImGui::PopFont(); }
};

// The transport is the one widget with state of its own to show. Every
// control READS the Executor and WRITES it back through the any-thread
// impl functions, so the panel, the keys and the code cannot disagree
// about what the sim is doing.
void transport_draw(void *target, ::ImFont *mono) {
    const impl::Executor ex{target};
    ImGui::PushID(target);
    const bool playing = impl::executor_playing(ex);
    if (impl::icon_button(playing ? Icon::Pause : Icon::Play, "tp_play",
                          playing ? "pause" : "play"))
        playing ? impl::executor_pause(ex) : impl::executor_play(ex);
    ImGui::SameLine();
    if (impl::icon_button(Icon::Step, "tp_step", "one step"))
        impl::executor_advance(ex, 1);
    ImGui::SameLine();

    // "Run N": the jump vklib could not have, because only an Executor
    // that counts can promise EXACTLY N.
    static int run_n = 100;
    ImGui::SetNextItemWidth(72.0f);
    ImGui::InputInt("##n", &run_n, 0, 0);
    ImGui::SameLine();
    if (impl::icon_button(Icon::Forward, "tp_run", "run that many steps") &&
        run_n > 0)
        impl::executor_advance(ex, std::uint64_t(run_n));
    ImGui::SameLine();
    if (impl::icon_button(Icon::Restart, "tp_restart", "restart"))
        impl::executor_restart(ex);

    {
        const Tick t = impl::executor_tick(ex);
        const Numerals face(mono);
        ImGui::Text("n = %llu   t = %.4g   %.1f /s",
                    static_cast<unsigned long long>(t.n), t.time,
                    impl::executor_rate(ex));
    }

    // Speed: vklib's four presets, PLUS the slider it never exposed —
    // a preset list was the thing users could not get past.
    static constexpr struct {
        const char *label;
        std::uint64_t ns;
    } kPresets[] = {{"uncapped", 0},
                    {"60 /s", 16'666'667},
                    {"10 /s", 100'000'000},
                    {"1 /s", 1'000'000'000}};
    const std::uint64_t cur = impl::executor_delay_ns(ex);
    int idx = -1;
    for (int i = 0; i < 4; ++i)
        if (kPresets[i].ns == cur)
            idx = i;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::BeginCombo("##speed",
                          idx >= 0 ? kPresets[idx].label : "custom")) {
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable(kPresets[i].label, i == idx))
                impl::executor_set_delay_ns(ex, kPresets[i].ns);
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    // Log slider over 0..1 s: a delay is a ratio, not a sum.
    float ms = float(cur) / 1e6f;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##delay", &ms, 0.0f, 1000.0f, "%.1f ms",
                           ImGuiSliderFlags_Logarithmic))
        impl::executor_set_delay_ns(ex, std::uint64_t(ms * 1e6f));
    ImGui::PopID();
}

void choice_draw(impl::WidgetState &w) {
    int *sel = static_cast<int *>(w.target);
    const int n = int(w.options.size());
    *sel = std::clamp(*sel, 0, n - 1);
    if (!ImGui::BeginCombo(w.label.c_str(),
                           w.options[std::size_t(*sel)].c_str()))
        return;
    for (int i = 0; i < n; ++i)
        if (ImGui::Selectable(w.options[std::size_t(i)].c_str(), i == *sel))
            *sel = i;
    ImGui::EndCombo();
}

void value_draw(impl::WidgetState &w, ::ImFont *mono) {
    // Bound or pulled, the same duality a series has.
    const double v =
        w.value ? w.value(w.user) : double(*static_cast<float *>(w.target));
    char buf[64];
    std::snprintf(buf, sizeof buf, w.fmt.c_str(), v);
    const Numerals face(mono);
    ImGui::LabelText(w.label.c_str(), "%s", buf);
}

void progress_draw(impl::WidgetState &w, ::ImFont *mono) {
    const double v = w.value(w.user);
    const float span = w.max - w.min;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.3g", v);
    const Numerals face(mono);
    ImGui::ProgressBar(span != 0.0f ? float((v - w.min) / span) : 0.0f,
                       ImVec2(0.0f, 0.0f), buf);
    ImGui::SameLine();
    ImGui::TextUnformatted(w.label.c_str());
}

void widget_draw(impl::WidgetState &w, ::ImFont *mono) {
    switch (w.kind) {
    case impl::WidgetKind::Text:
        ImGui::TextUnformatted(w.label.c_str());
        break;
    case impl::WidgetKind::Separator:
        w.label.empty() ? ImGui::Separator()
                        : ImGui::SeparatorText(w.label.c_str());
        break;
    case impl::WidgetKind::Help:
        // Pinned to the widget before it, and hover-only: a panel that
        // spells every parameter out in full is a panel nobody reads.
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", w.label.c_str());
        break;
    case impl::WidgetKind::Button:
        // The click runs while the UI frame is being BUILT, which is
        // CPU-only — no command buffer is open, so a callback that
        // touches the GPU cannot stomp one.
        if (ImGui::Button(w.label.c_str()) && w.on_click)
            w.on_click(w.user);
        break;
    case impl::WidgetKind::Slider:
        ImGui::SliderFloat(w.label.c_str(), static_cast<float *>(w.target),
                           w.min, w.max, "%.4g",
                           w.scale == Scale::Log ? ImGuiSliderFlags_Logarithmic
                                                 : ImGuiSliderFlags_None);
        break;
    case impl::WidgetKind::SliderInt:
        ImGui::SliderInt(w.label.c_str(), static_cast<int *>(w.target),
                         int(w.min), int(w.max));
        break;
    case impl::WidgetKind::SliderVec3:
        ImGui::SliderFloat3(w.label.c_str(), static_cast<float *>(w.target),
                            w.min, w.max);
        break;
    case impl::WidgetKind::Drag:
        ImGui::DragFloat(w.label.c_str(), static_cast<float *>(w.target),
                         w.speed, w.min, w.max);
        break;
    case impl::WidgetKind::InputFloat:
        ImGui::InputFloat(w.label.c_str(), static_cast<float *>(w.target));
        break;
    case impl::WidgetKind::InputInt:
        ImGui::InputInt(w.label.c_str(), static_cast<int *>(w.target));
        break;
    case impl::WidgetKind::Choice:
        choice_draw(w);
        break;
    case impl::WidgetKind::Color:
        ImGui::ColorEdit3(w.label.c_str(), static_cast<float *>(w.target));
        break;
    case impl::WidgetKind::Checkbox:
        ImGui::Checkbox(w.label.c_str(), static_cast<bool *>(w.target));
        break;
    case impl::WidgetKind::Value:
        value_draw(w, mono);
        break;
    case impl::WidgetKind::Progress:
        progress_draw(w, mono);
        break;
    case impl::WidgetKind::IconButton:
        if (impl::icon_button(w.icon, w.id.c_str(), w.label.c_str()) &&
            w.on_click)
            w.on_click(w.user);
        break;
    case impl::WidgetKind::IconToggle: {
        bool &on = *static_cast<bool *>(w.target);
        if (impl::icon_button(w.icon, w.id.c_str(), w.label.c_str(), on))
            on = !on;
        break;
    }
    case impl::WidgetKind::Transport:
        transport_draw(w.target, mono);
        break;
    case impl::WidgetKind::GroupBegin:
    case impl::WidgetKind::GroupEnd:
        break;
    }
}

bool group_on(const impl::WidgetState &w) {
    return w.value ? w.value(w.user) != 0.0 : *static_cast<bool *>(w.target);
}

bool group_begin(impl::WidgetState &w) {
    switch (w.group) {
    case impl::Group::Section:
        return ImGui::CollapsingHeader(w.label.c_str(),
                                       ImGuiTreeNodeFlags_DefaultOpen);
    case impl::Group::Tabs:
        return ImGui::BeginTabBar(w.id.c_str());
    case impl::Group::Tab:
        return ImGui::BeginTabItem(w.label.c_str());
    case impl::Group::Enabled:
        // Disabled, not hidden: a greyed control still says what it
        // would do, which is how a reader learns why it cannot.
        ImGui::BeginDisabled(!group_on(w));
        return true;
    case impl::Group::Row:
        break;
    }
    return true;
}

void group_end(const impl::WidgetState &w) {
    switch (w.group) {
    case impl::Group::Tabs:
        ImGui::EndTabBar();
        break;
    case impl::Group::Tab:
        ImGui::EndTabItem();
        break;
    case impl::Group::Enabled:
        ImGui::EndDisabled();
        break;
    default:
        break;
    }
}

} // namespace

// The widgets are a FLAT list with Begin/End markers, walked with a
// stack: the builder's lambdas make an unbalanced nesting unspellable,
// and a flat list means one loop draws every depth.
//
// A group whose body is not showing — a collapsed section, the tab that
// is not in front — must not draw its children AND must not emit its
// own End, which is why `on` is remembered per frame rather than asked
// again.
void panel_body(impl::PanelState &p) {
    struct Frame {
        impl::Group group;
        bool on;
        bool first;
    };
    std::vector<Frame> stack;
    int hidden = 0;

    for (impl::WidgetState &w : p.widgets) {
        if (w.kind == impl::WidgetKind::GroupBegin) {
            const bool on = hidden == 0 && group_begin(w);
            stack.push_back({w.group, on, true});
            if (!on)
                ++hidden;
            continue;
        }
        if (w.kind == impl::WidgetKind::GroupEnd) {
            if (stack.empty())
                continue;
            const Frame f = stack.back();
            stack.pop_back();
            f.on ? group_end(w) : void(--hidden);
            continue;
        }
        if (hidden)
            continue;

        // A row lays its widgets side by side. Help is exempt: it pins
        // itself to whatever came before, row or not.
        if (!stack.empty() && stack.back().group == impl::Group::Row) {
            if (!stack.back().first && w.kind != impl::WidgetKind::Help)
                ImGui::SameLine();
            stack.back().first = false;
        }
        widget_draw(w, p.mono);
    }
}

void panel_draw(impl::PanelState &p) {
    // Width only: a label sits to the RIGHT of its control, so an
    // auto-fitted panel sizes itself to the widest CONTROL and clips
    // every name. The height stays auto.
    place_window(p.slot, 400.0f, 0.0f);
    if (!ImGui::Begin(p.title.c_str())) {
        ImGui::End();
        return;
    }
    panel_body(p);
    ImGui::End();
}

} // namespace sv
