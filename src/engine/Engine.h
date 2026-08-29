#pragma once

// Internal to src/ — inside the include firewall, never installed.
// The SDL side of the App lives here; public headers never see it.

#include <SDL3/SDL.h>

#include <forward_list>
#include <string>

namespace simview {

// The one App state. Named `App` because the public opaque handle IS
// this type — the seam's `App *` points here.
struct App {
    SDL_GPUDevice *dev = nullptr;
    bool headless = false;
    struct Cb {
        void (*fn)(void *);
        void *user;
    };
    std::forward_list<Cb> frame_cbs; // registration order at run()
};

// The per-thread sentence behind simview::last_error().
void set_error(std::string msg);

} // namespace simview
