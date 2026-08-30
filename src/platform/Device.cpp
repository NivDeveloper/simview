// Device bring-up, the window, and the App's lifecycle — what exists
// before a frame and outlives the last one.

#include "../core/App.h"
#include "../ui/Ui.h"

#include <simview/simview.h>

namespace sv {
namespace impl {

App *app_init(const Config &c) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return set_error(SDL_GetError()), nullptr;

    // gpud owns device bring-up — the Vulkan-loader hint, the SPIR-V
    // format choice (the one format whose emitted binding layout
    // provably matches SDL's conventions; native MSL/DXIL are a
    // planned follow-up) — and the engine borrows the native handle.
    auto g = gpud::sdl::try_open();
    if (!g) {
        set_error("no GPU device (gpud sdl backend; GPUD_LOG=1 says why)");
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }
    App *a = new App;
    Platform &pl = a->platform;
    pl.gdev = std::move(g);
    pl.dev = gpud::sdl::native_device(*pl.gdev);
    pl.ui_size = c.size;
    // The scene borrows what it needs from the App it lives in.
    a->scene.dev = pl.dev;
    a->scene.stats = &a->stats;
    a->scene.pipelines = &a->pipelines;
    if (!c.headless) {
        pl.win = SDL_CreateWindow(c.title, int(c.size.w), int(c.size.h),
                                  SDL_WINDOW_RESIZABLE);
        if (!pl.win || !SDL_ClaimWindowForGPUDevice(pl.dev, pl.win)) {
            set_error(SDL_GetError());
            if (pl.win)
                SDL_DestroyWindow(pl.win);
            pl.gdev.reset();
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            delete a;
            return nullptr;
        }
    }
    ui_init(a, c);
    return a;
}

void app_quit(App *a) {
    if (!a)
        return;
    Platform &pl = a->platform;
    SDL_WaitForGPUIdle(pl.dev);
    // One release per layer, top down. Before the device dies: the
    // renderer backend holds pipelines and buffers on it, and the
    // platform backend holds the torn-out windows.
    ui_quit(a);
    pipelines_release(pl.dev, a->pipelines);
    scene_release(a->scene);
    for (View &v : a->views) {
        scene_release(v.scene);
        target_release(pl.dev, v.target);
    }
    if (pl.win) {
        SDL_ReleaseWindowFromGPUDevice(pl.dev, pl.win);
        SDL_DestroyWindow(pl.win);
    }
    // gpud's ~Device waits idle, destroys the SDL device and quits its
    // own subsystem ref; ours pairs with app_init's InitSubSystem.
    pl.gdev.reset();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    delete a;
}

void app_on_frame(App *a, void (*fn)(void *), void *user) {
    if (a && fn)
        a->platform.frame_cbs.push_front({fn, user});
}

void app_on_event(App *a, void (*fn)(const Event &, void *), void *user) {
    if (a && fn)
        a->input.event_cbs.push_front({fn, user});
}

void app_on_ui(App *a, void (*fn)(void *), void *user) {
    if (a && fn)
        a->ui.cbs.push_front({fn, user});
}

void app_request_quit(App *a) {
    if (a)
        a->platform.quit = true;
}

void app_post_event(App *a, const Event &e) {
    if (a)
        a->input.posted.push_back(e);
}

Stats app_stats(App *a) { return a ? a->stats : Stats{}; }

Scene app_scene(App *a) { return a ? Scene{&a->scene} : Scene{}; }

} // namespace impl
} // namespace sv
