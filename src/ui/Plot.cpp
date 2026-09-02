// The exported plot and panel functions. They share a file because
// they share a machinery: each registers a ui callback that draws one
// ImGui window, so both dock, tab and tear out for free.
//
// Every label crossing this wall is COPIED. A const char* from a
// temporary std::string would otherwise be a dangling read at draw
// time, days later, in a different thread of the frame.

#include "PlotState.h"

#include "../core/App.h"
#include "Ui.h"
#include "View.h"

#include <simview/simview.h>

namespace sv {
namespace impl {
// ImGui appends into a window of the same title, so two same-titled
// windows would draw into each other. Every kind that opens one is
// named here — plots, panels and views share one namespace.
bool title_taken(App *a, const char *title) {
    for (const PlotState &p : a->plots)
        if (p.title == title)
            return true;
    for (const PanelState &p : a->panels)
        if (p.title == title)
            return true;
    for (const View &v : a->views)
        if (v.title == title)
            return true;
    return false;
}

namespace {

bool axis_ok(const AxisDesc &d, const char *which) {
    if ((d.fit == Fit::Start || d.fit == Fit::Fixed) && !(d.min < d.max)) {
        set_error(std::string("the ") + which +
                  " axis names a range but min is not below max — a fixed "
                  "or starting range needs both");
        return false;
    }
    return true;
}

void copy_axis(AxisState &to, const AxisDesc &from) {
    to.label = from.label ? from.label : "";
    to.desc = from;
    to.desc.label = nullptr; // the copy above is the one that lives
}

} // namespace

Plot plot_create(App *a, const PlotDesc &d) {
    if (!a)
        return {};
    if (!d.title || !*d.title)
        return set_error("a plot needs a title — it names the panel and "
                         "scopes its series"),
               Plot{};
    if (title_taken(a, d.title))
        return set_error(std::string("\"") + d.title +
                         "\" is already the title of a plot or panel, and "
                         "two windows of one name draw into each other"),
               Plot{};
    if (!axis_ok(d.x, "x") || !axis_ok(d.y, "y"))
        return Plot{};

    PlotState &st = a->plots.emplace_back();
    st.slot = a->windows++;
    st.app = a;
    st.title = d.title;
    st.palette = d.palette;
    st.derive = d.derive;
    copy_axis(st.x, d.x);
    copy_axis(st.y, d.y);

    a->ui.cbs.push_front(
        {[](void *u) { plot_draw(*static_cast<PlotState *>(u)); }, &st});
    return Plot{&st};
}

Plot plot3d_create(App *a, const Plot3DDesc &d) {
    if (!a)
        return {};
    if (!d.title || !*d.title)
        return set_error("a plot needs a title — it names the panel and "
                         "scopes its series"),
               Plot{};
    if (title_taken(a, d.title))
        return set_error(std::string("\"") + d.title +
                         "\" is already the title of a plot or panel, and "
                         "two windows of one name draw into each other"),
               Plot{};
    if (!axis_ok(d.x, "x") || !axis_ok(d.y, "y") || !axis_ok(d.z, "z"))
        return Plot{};

    PlotState &st = a->plots.emplace_back();
    st.slot = a->windows++;
    st.app = a;
    st.family = Family::Plot3D;
    st.title = d.title;
    st.palette = d.palette;
    copy_axis(st.x, d.x);
    copy_axis(st.y, d.y);
    copy_axis(st.z, d.z);

    a->ui.cbs.push_front(
        {[](void *u) { plot_draw(*static_cast<PlotState *>(u)); }, &st});
    return Plot{&st};
}

// The family a kind belongs to. The family is on the PLOT, the kind
// on the SERIES, and the two must agree — a 3D kind never reaches the
// 2D emitter and vice versa, which is what lets each emitter cast its
// slots without a second check.
Family family_of(SeriesKind k) {
    switch (k) {
    case SeriesKind::Line3:
    case SeriesKind::Scatter3:
    case SeriesKind::Surface:
    case SeriesKind::Mesh:
        return Family::Plot3D;
    default:
        return Family::Plot2D;
    }
}

bool plot_series(Plot p, const SeriesDesc &d) {
    PlotState *st = static_cast<PlotState *>(p.p);
    // The source is consumed either way: a refusal must not leak the
    // closure the caller already handed over.
    if (!st || !d.name || !*d.name) {
        if (d.free)
            d.free(d.user);
        return set_error("a series needs a plot and a name"), false;
    }
    if (family_of(d.kind) != st->family) {
        if (d.free)
            d.free(d.user);
        return set_error(st->family == Family::Plot3D
                             ? "this is a 3D plot and that is a 2D series "
                               "kind — a Plot3D takes Line, Scatter, Surface "
                               "and Mesh over three coordinates"
                             : "this is a 2D plot and that is a 3D series "
                               "kind — use app.Plot3D for three coordinates"),
               false;
    }
    // Identity is the name — with ONE exception, and it is ImPlot's own
    // idiom: ErrorBars DECORATE a series, and are bound to it by
    // sharing its name, so the whiskers take the host's colour and
    // legend entry. The exception is narrow on purpose: only ErrorBars
    // may reuse a name, only a name that exists, and only on a kind
    // whiskers can sit on.
    for (const SeriesState &s : st->series)
        if (s.name == d.name) {
            const bool decorates =
                d.kind == SeriesKind::ErrorBars &&
                (s.kind == SeriesKind::Bars || s.kind == SeriesKind::Line ||
                 s.kind == SeriesKind::Scatter || s.kind == SeriesKind::Stairs);
            if (decorates)
                break;
            if (d.free)
                d.free(d.user);
            return set_error(std::string("this plot already has a series "
                                         "named \"") +
                             d.name +
                             "\", and same-named series merge into one "
                             "legend entry, colour and visibility"),
                   false;
        }

    SeriesState &s = st->series.emplace_back();
    s.name = d.name;
    s.kind = d.kind;
    s.dtype = d.dtype;
    for (int i = 0; i < 4; ++i)
        s.param[i] = d.param[i];
    s.bounds = d.bounds;
    s.data = d.data;
    s.src = d.src;
    s.user = d.user;
    s.free = d.free;
    s.style = d.style;
    return true;
}

Panel panel_create(App *a, const char *title) {
    if (!a)
        return {};
    if (!title || !*title)
        return set_error("a panel needs a title — it names the window"),
               Panel{};
    if (title_taken(a, title))
        return set_error(std::string("\"") + title +
                         "\" is already the title of a plot or panel, and "
                         "two windows of one name draw into each other"),
               Panel{};

    PanelState &st = a->panels.emplace_back();
    st.title = title;
    st.mono = a->ui.mono;
    st.slot = a->windows++;
    st.app = a;
    a->ui.cbs.push_front(
        {[](void *u) { panel_draw(*static_cast<PanelState *>(u)); }, &st});
    return Panel{&st};
}

namespace {

// What a widget must be given to mean anything. A refusal names the
// widget and what it wanted, because the alternative is a control that
// silently does nothing.
const char *widget_needs(const WidgetDesc &d) {
    switch (d.kind) {
    case WidgetKind::Slider:
    case WidgetKind::SliderInt:
    case WidgetKind::SliderVec3:
        return d.target ? nullptr : "a slider needs a value to move";
    case WidgetKind::Drag:
        return d.target ? nullptr : "a drag needs a value to move";
    case WidgetKind::InputFloat:
    case WidgetKind::InputInt:
        return d.target ? nullptr : "an input needs a value to write into";
    case WidgetKind::Choice:
        if (!d.target)
            return "a choice needs an index to select into";
        return d.option_count > 0
                   ? nullptr
                   : "a choice with no options can select nothing";
    case WidgetKind::Color:
        return d.target ? nullptr : "a colour needs three floats to edit";
    case WidgetKind::Checkbox:
        return d.target ? nullptr : "a checkbox needs a value to toggle";
    case WidgetKind::IconToggle:
        if (!d.target)
            return "an icon toggle needs a value to toggle";
        [[fallthrough]];
    case WidgetKind::IconButton:
        // The tooltip is the label: an icon that cannot say its own
        // name in words is a control the reader has to guess at, and
        // there is no second place for that name to appear.
        return (d.label && *d.label)
                   ? nullptr
                   : "an icon control needs a tooltip — an icon is not a name";
    case WidgetKind::Transport:
        return d.target ? nullptr : "a transport needs an Executor to drive";
    case WidgetKind::Progress:
        return d.value ? nullptr : "a progress bar needs a callable to read";
    case WidgetKind::Value:
        return (d.target || d.value)
                   ? nullptr
                   : "a value needs something to read — bind a float or "
                     "give a callable";
    default:
        return nullptr;
    }
}

// Where a widget is allowed to sit. A tab bar draws only tabs, and a
// tab only exists inside one; both are ImGui assertions in a draw
// nobody is watching, so they are refused at registration instead.
const char *widget_place(const PanelState &st, const WidgetDesc &d) {
    const bool in_bar = !st.open.empty() && st.open.back() == Group::Tabs;
    if (d.kind == WidgetKind::GroupBegin && d.group == Group::Tab)
        return in_bar ? nullptr
                      : "a tab only exists inside Tabs — wrap the tabs in "
                        "one bar";
    if (d.kind == WidgetKind::GroupEnd) {
        if (st.open.empty())
            return "this group was never opened";
        return st.open.back() == d.group
                   ? nullptr
                   : "groups closed out of order — an inner group is still "
                     "open";
    }
    if (in_bar && d.kind != WidgetKind::GroupBegin)
        return "a tab bar holds tabs and nothing else — put this inside a "
               "tab";
    return nullptr;
}

} // namespace

bool panel_widget(Panel p, const WidgetDesc &d) {
    PanelState *st = static_cast<PanelState *>(p.p);
    const auto refuse = [&](const char *why) {
        if (d.free)
            d.free(d.user);
        return set_error(why), false;
    };
    if (!st)
        return refuse("a widget needs a panel");
    if (const char *why = widget_needs(d))
        return refuse(why);
    if (const char *why = widget_place(*st, d))
        return refuse(why);

    if (d.kind == WidgetKind::GroupBegin)
        st->open.push_back(d.group);
    else if (d.kind == WidgetKind::GroupEnd)
        st->open.pop_back();

    WidgetState &w = st->widgets.emplace_back();
    w.label = d.label ? d.label : "";
    w.fmt = d.fmt ? d.fmt : "%.3g";
    // Copied, like every label: an option list is usually a braced
    // temporary, and the array it lives in dies with the call.
    for (int i = 0; i < d.option_count; ++i)
        w.options.emplace_back(d.options[i] ? d.options[i] : "");
    w.kind = d.kind;
    w.group = d.group;
    w.icon = d.icon;
    w.target = d.target;
    w.min = d.min;
    w.max = d.max;
    w.speed = d.speed;
    w.scale = d.scale;
    w.value = d.value;
    w.on_click = d.on_click;
    w.user = d.user;
    w.free = d.free;
    // Anything ImGui must tell apart gets an id of its own. An icon
    // control has no visible label to be identified by, so two of them
    // in one panel would be the SAME control without this.
    if (d.kind == WidgetKind::GroupBegin && d.group == Group::Tabs)
        w.id = "##tabs" + std::to_string(st->ids++);
    else if (d.kind == WidgetKind::IconButton ||
             d.kind == WidgetKind::IconToggle)
        w.id = "icon" + std::to_string(st->ids++);
    return true;
}

namespace {

const char *derived_word(Derived k) {
    switch (k) {
    case Derived::Histogram:
        return "histogram";
    case Derived::Density:
        return "density";
    case Derived::Profile:
        return "profile";
    case Derived::Joint:
        return "joint view";
    }
    return "view";
}

// One series, borrowed storage filled by derive_update every frame.
SeriesState &derived_series(PlotState &st, SeriesKind k, std::string name) {
    SeriesState &s = st.series.emplace_back();
    s.name = std::move(name);
    s.kind = k;
    s.dtype = DType::f64;
    return s;
}

// The same window for the same question: clicking a reduction twice
// should bring back the plot it already made, not a second copy of it.
PlotState *already_derived(App *a, const SeriesState *from, Derived k) {
    for (PlotState &p : a->plots)
        if (p.derivation && p.derivation->series == from &&
            p.derivation->kind == k)
            return &p;
    return nullptr;
}

} // namespace

Panel plot_controls(Plot p) {
    PlotState *st = static_cast<PlotState *>(p.p);
    if (!st)
        return set_error("controls need a plot to sit on"), Panel{};
    if (!st->controls)
        st->controls = std::make_unique<PanelState>();
    return Panel{st->controls.get()};
}

Panel plot_tools(Plot p) {
    PlotState *st = static_cast<PlotState *>(p.p);
    if (!st)
        return set_error("a plot's tools need a plot"), Panel{};
    if (!st->tools)
        st->tools = std::make_unique<PanelState>();
    return Panel{st->tools.get()};
}

Plot plot_derive(Plot p, Derived kind, const char *series) {
    PlotState *st = static_cast<PlotState *>(p.p);
    if (!st || !st->app)
        return set_error("a derived view needs a plot to derive from"), Plot{};
    if (st->derivation)
        return set_error("a derived view cannot itself be derived from — "
                         "derive from the plot that holds the data"),
               Plot{};

    const SeriesState *from = nullptr;
    for (const DeriveOption &o : derive_options(*st))
        if (o.kind == kind && (!series || o.series->name == series)) {
            from = o.series;
            break;
        }
    if (!from) {
        const std::string why =
            series ? std::string("this plot has no series \"") + series +
                         "\" that a " + derived_word(kind) + " can be taken of"
                   : std::string("this plot has no series a ") +
                         derived_word(kind) +
                         " can be taken of — a reduction needs points, and "
                         "a heatmap or a histogram is already one";
        return set_error(why), Plot{};
    }

    App *a = st->app;
    // Asking again re-OPENS it: a closed view is the same answer put
    // away, not a missing one, and a second copy of it helps nobody.
    if (PlotState *seen = already_derived(a, from, kind)) {
        seen->open = true;
        return Plot{seen};
    }

    PlotState &d = a->plots.emplace_back();
    d.slot = a->windows++;
    d.app = a;
    d.title = std::string(derived_word(kind)) + " of " + from->name + " (" +
              st->title + ")";
    d.derive = false;
    d.derivation = std::make_unique<Derivation>();
    d.derivation->from = st;
    d.derivation->series = from;
    d.derivation->kind = kind;

    switch (kind) {
    case Derived::Histogram:
        // The parent's own name for the quantity, when it has one: a
        // histogram is of a QUANTITY, and the series name is what the
        // legend already said.
        d.x.label = st->y.label.empty() ? from->name : st->y.label;
        d.y.label = "count";
        derived_series(d, SeriesKind::Histogram, from->name);
        break;
    case Derived::Density:
        d.x.label = st->x.label;
        d.y.label = st->y.label;
        derived_series(d, SeriesKind::Heatmap, from->name);
        break;
    case Derived::Profile:
        d.x.label = st->x.label;
        d.y.label = "mean " + (st->y.label.empty() ? from->name : st->y.label);
        derived_series(d, SeriesKind::Line, "mean");
        derived_series(d, SeriesKind::ErrorBars, "standard error");
        break;
    case Derived::Joint:
        // Four series in a fixed order, because the draw puts each in
        // its own cell of a 2x2 rather than all of them in one plot.
        d.x.label = st->x.label;
        d.y.label = st->y.label;
        derived_series(d, SeriesKind::Heatmap, from->name);
        derived_series(d, SeriesKind::Contour, "contours");
        derived_series(d, SeriesKind::Bars, "along x");
        derived_series(d, SeriesKind::Bars, "along y");
        break;
    }

    // Its own control: the bin count is the one number a reduction has,
    // and it belongs on the plot that shows the answer.
    d.controls = std::make_unique<PanelState>();
    panel_widget(Panel{d.controls.get()},
                 WidgetDesc{.label = "bins",
                            .kind = WidgetKind::SliderInt,
                            .target = &d.derivation->bins,
                            .min = 4.0f,
                            .max = 128.0f});

    a->ui.cbs.push_front(
        {[](void *u) { plot_draw(*static_cast<PlotState *>(u)); }, &d});
    return Plot{&d};
}

} // namespace impl
} // namespace sv
