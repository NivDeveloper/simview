#pragma once

// Internal to src/ — inside the include firewall, never installed.
// The SDL side of the App lives here; public headers never see it.

#include <SDL3/SDL.h>

#include <forward_list>
#include <string>
#include <vector>

namespace simview {

// The one App state. Named `App` because the public opaque handle IS
// this type — the seam's `App *` points here.
struct Event; // simview/Event.h

struct App {
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
        bool dirty = false; // staging holds a newer grid than buf
    };
    FieldState field; // w == 0 means "no field yet"
};

// Draw.cpp: upload-if-dirty then render the field into target — the
// ONE pass both the window and shot() record.
void render_field(App *, SDL_GPUCommandBuffer *, SDL_GPUTexture *target,
                  Uint32 tw, Uint32 th, SDL_GPUTextureFormat);

// The per-thread sentence behind simview::last_error().
void set_error(std::string msg);

} // namespace simview
