#pragma once

struct ImFont;

#include <simview/Panel.h>
#include <simview/Plots.h>

#include <list>
#include <memory>
#include <string>
#include <vector>

namespace sv {
namespace impl {

// A list, not a vector: these are non-copyable and their addresses
// are handed to the ui callbacks.
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

struct PanelState;
struct PlotState;
struct App;

// Recomputed every frame from whatever the source series holds. The
// parent is a pointer: plots live in a list and are never removed.
struct Derivation {
    const PlotState *from = nullptr;
    const SeriesState *series = nullptr;
    Derived kind = Derived::Histogram;
    int bins = 32;
    // The reduction's own storage; the derived series borrows it.
    std::vector<double> a, b, c;
    // The joint view needs four things at once: the field, a bin
    // centre per axis for the two marginals, and the contour segments.
    std::vector<double> mx, my, cx, cy, seg_x, seg_y;
};

struct PlotState {
    Family family = Family::Plot2D;
    std::string title;
    int slot = 0;
    Palette palette = Palette::Auto;
    bool derive = true;
    AxisState x, y, z;
    std::list<SeriesState> series;
    // The plot's own strip of controls, above the canvas. Empty for a
    // plot that asked for none.
    std::unique_ptr<PanelState> controls;
    // Icon controls, which ride the engine's own strip. Separate from
    // `controls` because the two want different rooms.
    std::unique_ptr<PanelState> tools;
    std::unique_ptr<Derivation> derivation;
    // What the LAST toolbar decided, so a probe reads what was drawn.
    bool open = true;
    bool fit_offered = false;
    bool fit_pending = false;
    // Offered by the chrome on every plot, so a reader never has to
    // find out whether THIS one happens to expose them.
    bool legend = true;
    bool grid = true;
    // What the last draw left for the data, which is the number every
    // arrangement is really trading against.
    float canvas_w = 0.0f;
    float canvas_h = 0.0f;
    App *app = nullptr;
};

struct WidgetState {
    std::string label;
    std::string fmt;
    std::string id;
    std::vector<std::string> options;
    WidgetKind kind = WidgetKind::Text;
    Group group = Group::Section;
    Icon icon = Icon::Gear;
    void *target = nullptr;
    float min = 0.0f, max = 1.0f;
    float speed = 0.0f;
    Scale scale = Scale::Linear;
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

// `open` is registration-time only: it is what lets a malformed
// nesting be refused at the call that made it, rather than becoming an
// ImGui assertion inside a draw the user cannot see.
struct PanelState {
    std::string title;
    int slot = 0;
    // The numeric face, taken at creation because the draw callback is
    // handed a panel and not the App that owns the font.
    ::ImFont *mono = nullptr;
    std::list<WidgetState> widgets;
    std::vector<Group> open;
    int ids = 0;
    App *app = nullptr;
};

} // namespace impl

// One panel each, drawn from the ui callbacks they register.
// Where a window lands before the layout file has an opinion —
// asked by both a plot and a panel, so it belongs to neither.
void place_window(impl::App *, int slot, float width, float height);

void plot_draw(impl::PlotState &);
void panel_draw(impl::PanelState &);

// The widget walk, without a window around it: a plot's control strip
// is the same list drawn in the plot's own window.
void panel_body(impl::PanelState &, bool inline_row = false);

// Recompute a derived plot from its source. Called at the top of the
// derived plot's draw, before its series are read.
void derive_update(impl::PlotState &);

// Which reductions this plot's series admit, and what they are called.
// Empty when the plot offers none.
struct DeriveOption {
    const impl::SeriesState *series;
    Derived kind;
    std::string label;
};
std::vector<DeriveOption> derive_options(const impl::PlotState &);

// Whether a reduction can be taken of a series at all — the
// capability, where derive_options is the offer.
bool reducible(impl::SeriesKind);

} // namespace sv
