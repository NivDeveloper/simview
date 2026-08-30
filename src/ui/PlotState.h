#pragma once

// Internal to src/ — the plot and panel state. Confined to ui/: the
// two files that touch these are Plot.cpp (registration) and
// PlotDraw.cpp (drawing), and nothing below this layer knows they
// exist.

#include <simview/Panel.h>
#include <simview/Plots.h>

#include <list>
#include <string>

namespace sv {
namespace impl {

// A series' source is owned here: the sugar handle is a bare pointer
// like Field's, so nothing on the user's side could keep a closure
// alive. std::list, because these are non-copyable and their
// addresses are handed to the ui callbacks.
struct SeriesState {
    std::string name;
    SeriesKind kind = SeriesKind::Line;
    DType dtype = DType::f32;
    double param[4] = {-2.0, 0.0, 0.0, 0.0};
    Range2 bounds{0.0, 0.0, 1.0, 1.0};
    SeriesData data{};
    SeriesData (*src)(void *) = nullptr;
    void *user = nullptr;
    void (*free)(void *) = nullptr;
    SeriesStyle style{};

    SeriesState() = default;
    SeriesState(const SeriesState &) = delete;
    SeriesState &operator=(const SeriesState &) = delete;
    ~SeriesState() {
        if (free)
            free(user);
    }
};

// Labels are COPIED at registration: a const char* from a temporary
// std::string would be a dangling read at draw time.
struct AxisState {
    std::string label;
    AxisDesc desc{};
};

struct PlotState {
    std::string title;
    Palette palette = Palette::Auto;
    AxisState x, y;
    std::list<SeriesState> series;
};

struct WidgetState {
    std::string label;
    std::string fmt;
    WidgetKind kind = WidgetKind::Text;
    void *target = nullptr;
    float min = 0.0f, max = 1.0f;
    double (*value)(void *) = nullptr;
    void (*on_click)(void *) = nullptr;
    void *user = nullptr;
    void (*free)(void *) = nullptr;

    WidgetState() = default;
    WidgetState(const WidgetState &) = delete;
    WidgetState &operator=(const WidgetState &) = delete;
    ~WidgetState() {
        if (free)
            free(user);
    }
};

struct PanelState {
    std::string title;
    std::list<WidgetState> widgets;
};

} // namespace impl

// One panel each, drawn from the ui callbacks they register.
void plot_draw(impl::PlotState &);
void panel_draw(impl::PanelState &);

} // namespace sv
