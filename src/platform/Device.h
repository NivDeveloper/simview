#pragma once

// Internal to src/ — the platform's state: the device, the window,
// and the lifecycle flags. What exists before a frame and outlives
// the last one.

#include "../core/Callbacks.h"

#include <simview/Types.h>

#include <SDL3/SDL.h>
#include <gpud/Sdl.h>

#include <forward_list>
#include <memory>

namespace sv {
namespace impl {

struct Platform {
    // ORDER IS LOAD-BEARING. dev is gpud's native handle, borrowed
    // from gdev and valid only until gdev.reset() — so gdev is
    // declared first and destroyed last.
    std::unique_ptr<gpud::Device> gdev;
    SDL_GPUDevice *dev = nullptr;
    // Null when headless — the ONE spelling of that fact.
    SDL_Window *win = nullptr;
    // What a headless UI frame is laid out for. A composited shot must
    // be this size or ImGui's projection puts most of the frame off
    // the target.
    Extent2 ui_size{};
    bool quit = false;
    std::forward_list<Cb> frame_cbs; // registration order at run()
};

} // namespace impl
} // namespace sv
