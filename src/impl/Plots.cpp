// The exported plot and panel functions. They share a file because
// they share a machinery: each registers a ui callback that draws one
// ImGui window, so both dock, tab and tear out for free.
//
// Every label crossing this wall is COPIED. A const char* from a
// temporary std::string would otherwise be a dangling read at draw
// time, days later, in a different thread of the frame.

#include "../engine/Engine.h"

#include <simview/simview.h>

namespace sv {
namespace impl {
namespace {

// ImGui appends into a window of the same title, so two same-titled
// panels would draw into each other.
bool title_taken(App *a, const char *title) {
    for (const App::PlotState &p : a->plots)
        if (p.title == title)
            return true;
    for (const App::PanelState &p : a->panels)
        if (p.title == title)
            return true;
    return false;
}

bool axis_ok(const AxisDesc &d, const char *which) {
    if ((d.fit == Fit::Start || d.fit == Fit::Fixed) && !(d.min < d.max)) {
        set_error(std::string("the ") + which +
                  " axis names a range but min is not below max — a fixed "
                  "or starting range needs both");
        return false;
    }
    return true;
}

void copy_axis(App::AxisState &to, const AxisDesc &from) {
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

    App::PlotState &st = a->plots.emplace_back();
    st.title = d.title;
    copy_axis(st.x, d.x);
    copy_axis(st.y, d.y);

    a->ui_cbs.push_front(
        {[](void *u) { plot_draw(*static_cast<App::PlotState *>(u)); }, &st});
    return Plot{&st};
}

bool plot_series(Plot p, const SeriesDesc &d) {
    App::PlotState *st = static_cast<App::PlotState *>(p.p);
    // The source is consumed either way: a refusal must not leak the
    // closure the caller already handed over.
    if (!st || !d.name || !*d.name) {
        if (d.free)
            d.free(d.user);
        return set_error("a series needs a plot and a name"), false;
    }
    for (const App::SeriesState &s : st->series)
        if (s.name == d.name) {
            if (d.free)
                d.free(d.user);
            return set_error(std::string("this plot already has a series "
                                         "named \"") +
                             d.name +
                             "\", and same-named series merge into one "
                             "legend entry, colour and visibility"),
                   false;
        }

    App::SeriesState &s = st->series.emplace_back();
    s.name = d.name;
    s.kind = d.kind;
    s.dtype = d.dtype;
    s.bins = d.bins;
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

    App::PanelState &st = a->panels.emplace_back();
    st.title = title;
    a->ui_cbs.push_front(
        {[](void *u) { panel_draw(*static_cast<App::PanelState *>(u)); }, &st});
    return Panel{&st};
}

bool panel_widget(Panel p, const WidgetDesc &d) {
    App::PanelState *st = static_cast<App::PanelState *>(p.p);
    if (!st) {
        if (d.free)
            d.free(d.user);
        return set_error("a widget needs a panel"), false;
    }
    if (d.kind == WidgetKind::Slider && !d.target) {
        if (d.free)
            d.free(d.user);
        return set_error("a slider needs a value to move"), false;
    }
    if (d.kind == WidgetKind::Checkbox && !d.target) {
        if (d.free)
            d.free(d.user);
        return set_error("a checkbox needs a value to toggle"), false;
    }
    if (d.kind == WidgetKind::Value && !d.target && !d.value) {
        if (d.free)
            d.free(d.user);
        return set_error("a value needs something to read — bind a float or "
                         "give a callable"),
               false;
    }

    App::WidgetState &w = st->widgets.emplace_back();
    w.label = d.label ? d.label : "";
    w.fmt = d.fmt ? d.fmt : "%.3g";
    w.kind = d.kind;
    w.target = d.target;
    w.min = d.min;
    w.max = d.max;
    w.value = d.value;
    w.on_click = d.on_click;
    w.user = d.user;
    w.free = d.free;
    return true;
}

} // namespace impl
} // namespace sv
