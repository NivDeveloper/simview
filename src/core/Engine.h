#pragma once

// Internal to src/ — inside the include firewall, never installed.
// The SDL side of the App lives here; public headers never see it.

#include <simview/App.h>
#include <simview/Panel.h>
#include <simview/Plots.h>
#include <simview/Scene.h>

#include "../scene/Kinds.h"

#include <SDL3/SDL.h>
#include <gpud/Sdl.h>

#include <algorithm>
#include <forward_list>
#include <list>
#include <memory>
#include <string>
#include <vector>

// The UI layer's contexts, opaque here so Draw.cpp never sees them.
struct ImGuiContext;
struct ImPlotContext;

namespace sv {

// The one App state, in impl:: because the impl's opaque `App *`
// points here.
namespace impl {

// An item knows its App because an upload needs the device, its ops
// because that is what it IS, and its state only as a void * — the
// kind's own file is the one place that knows the type.
struct SceneItem {
    App *app = nullptr;
    const KindOps *ops = nullptr;
    void *state = nullptr;
};

// What is drawn to a target, in registration order. std::list because
// an item's ADDRESS is the public handle.
// The range every item maps into. Unset means: the first item with a
// natural grid, in cells, so a lattice and the points over it share
// coordinates; with no such item, the unit square.
struct SceneState {
    App *app = nullptr;
    std::list<SceneItem> items;
    Range2 range{};
};

struct App {
    // The device is gpud's; dev is its borrowed native handle, valid
    // until gdev.reset().
    std::unique_ptr<gpud::Device> gdev;
    SDL_GPUDevice *dev = nullptr;
    SDL_Window *win = nullptr; // null when headless
    bool headless = false;
    // What a headless UI frame is laid out for. A composited shot must
    // be this size or ImGui's projection puts most of the frame off
    // the target.
    Extent2 ui_size{};
    bool quit = false;
    struct Cb {
        void (*fn)(void *);
        void *user;
    };
    struct Ecb {
        void (*fn)(const Event &, void *);
        void *user;
    };
    std::forward_list<Cb> frame_cbs; // registration order at run()
    std::forward_list<Ecb> event_cbs;
    // Events posted through the automation seam, delivered by the
    // next Step or loop iteration exactly like SDL's own.
    std::vector<Event> posted;
    sv::Stats stats;
    // Keyed on BOTH: a request that matched on format alone would
    // silently bind another kind's pipeline.
    struct PipelineEntry {
        const KindOps *kind;
        SDL_GPUTextureFormat format;
        SDL_GPUGraphicsPipeline *pipeline;
    };
    std::vector<PipelineEntry> pipelines;

    SceneState scene;

    // A view is a scene whose target is a texture rather than the
    // swapchain, shown by a panel that can dock and tear out like any
    // other. The texture is sized from what the panel had room for
    // LAST frame: it is recreated before the UI is built, so no draw
    // list can be left pointing at a texture that has been released.
    struct ViewState {
        App *app = nullptr;
        std::string title;
        SceneState scene;
        SDL_GPUTexture *tex = nullptr;
        Uint32 w = 0, h = 0;
        Uint32 want_w = 256, want_h = 256;
    };
    std::list<ViewState> views;

    // The UI layer.
    struct UiState {
        ::ImGuiContext *ctx = nullptr;
        ::ImPlotContext *plot = nullptr;
        std::string ini;
    };
    UiState ui;
    std::forward_list<Cb> ui_cbs; // panel callbacks, registration order

    // A series' source is owned here: the sugar handle is a bare
    // pointer like Field's, so nothing on the user's side could keep a
    // closure alive. std::list, because these are non-copyable and
    // their addresses are handed to the ui callbacks.
    struct SeriesState {
        std::string name;
        SeriesKind kind = SeriesKind::Line;
        DType dtype = DType::f32;
        int bins = -2;
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

    // Labels are COPIED at registration: a const char* from a
    // temporary std::string would be a dangling read at draw time.
    struct AxisState {
        std::string label;
        AxisDesc desc{};
    };

    struct PlotState {
        std::string title;
        AxisState x, y;
        std::list<SeriesState> series;
    };
    std::list<PlotState> plots;

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
    std::list<PanelState> panels;
};
} // namespace impl

// scene/Scene.cpp: prepare every item, then ONE pass — the clear belongs to
// the scene, not to an item, or only the first item could composite.
void scene_draw(impl::SceneState &, SDL_GPUCommandBuffer *,
                SDL_GPUTexture *target, Uint32 tw, Uint32 th,
                SDL_GPUTextureFormat);

// Match every view's texture to the size its panel asked for. Must
// run BEFORE the UI frame is built, because that frame's draw list
// records the texture handle.
void views_resize(impl::App *);

// Every view's scene into its own texture, recorded ahead of the
// scene that will sample them.
void views_draw(impl::App *, SDL_GPUCommandBuffer *);

// Release a scene's items and, for a view, its texture.
void scene_release(impl::App *, impl::SceneState &);

// The first item with a natural grid, if any — what a scene's default
// range and a shot's dimensions are both derived from.
bool scene_grid(const impl::SceneState &, Extent2 *);

// forward_list push_front reversed registration; fire in registration
// order by walking a copied reverse. The COPY matters: a callback that
// registers another does not see its own registration this frame.
// Lists are tiny; per frame is fine.
template <typename L, typename F> void in_order(const L &l, F f) {
    std::vector<typename L::value_type> v(l.begin(), l.end());
    std::for_each(v.rbegin(), v.rend(), f);
}

// Plots.cpp: is this window title spoken for? Plots, panels and views
// open ImGui windows and share one title namespace.
namespace impl {
bool title_taken(App *, const char *title);
}

// Plots.cpp: one panel each, drawn from the ui callbacks they register.
void plot_draw(impl::App::PlotState &);
void panel_draw(impl::App::PanelState &);

// Ui.cpp: the panel a view lives in — it shows the texture and reports
// how much room it had, which is what sizes the next frame's texture.
void view_draw(impl::App::ViewState &);

// The per-thread sentence behind sv::LastError().
void set_error(std::string msg);

} // namespace sv
