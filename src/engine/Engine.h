#pragma once

// Internal to src/ — inside the include firewall, never installed.
// The SDL side of the App lives here; public headers never see it.

#include <simview/App.h>
#include <simview/Panel.h>
#include <simview/Plots.h>

#include <SDL3/SDL.h>
#include <gpud/Sdl.h>

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
struct App {
    // The device is gpud's; dev is its borrowed native handle, valid
    // until gdev.reset().
    std::unique_ptr<gpud::Device> gdev;
    SDL_GPUDevice *dev = nullptr;
    SDL_Window *win = nullptr; // null when headless
    bool headless = false;
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
    struct PipelineEntry {
        SDL_GPUTextureFormat format;
        SDL_GPUGraphicsPipeline *pipeline;
    };
    std::vector<PipelineEntry> pipelines; // one per target format

    // Move 2: at most one field; the window is the field.
    struct FieldState {
        Uint32 w = 0, h = 0;
        Sint32 cmap = 0;
        float lo = 0, hi = 1;
        SDL_GPUBuffer *buf = nullptr;
        SDL_GPUTransferBuffer *staging = nullptr;
        bool dirty = false;    // staging holds a newer grid than buf
        bool external = false; // buf resolves from src: no staging, no release
        // The pull source an external field re-asks at every draw.
        gpud::BufferSource src{};
    };
    FieldState field; // w == 0 means "no field yet"

    // The UI layer. Appended last on purpose: FieldState's aggregate
    // initialisers are positional, so a member inserted above them
    // mis-assigns in silence.
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

// Plots.cpp: one panel each, drawn from the ui callbacks they register.
void plot_draw(impl::App::PlotState &);
void panel_draw(impl::App::PanelState &);

// Draw.cpp: upload-if-dirty then render the field into target — the
// ONE pass both the window and shot() record.
void render_field(impl::App *, SDL_GPUCommandBuffer *, SDL_GPUTexture *target,
                  Uint32 tw, Uint32 th, SDL_GPUTextureFormat);

// The per-thread sentence behind sv::LastError().
void set_error(std::string msg);

} // namespace sv
