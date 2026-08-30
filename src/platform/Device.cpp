// Device bring-up, the window, and the App's lifecycle — what exists
// before a frame and outlives the last one.

#include "../core/Engine.h"
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
    a->scene.app = a;
    a->gdev = std::move(g);
    a->dev = gpud::sdl::native_device(*a->gdev);
    a->headless = c.headless;
    a->ui_size = c.size;
    if (!c.headless) {
        a->win = SDL_CreateWindow(c.title, int(c.size.w), int(c.size.h),
                                  SDL_WINDOW_RESIZABLE);
        if (!a->win || !SDL_ClaimWindowForGPUDevice(a->dev, a->win)) {
            set_error(SDL_GetError());
            if (a->win)
                SDL_DestroyWindow(a->win);
            a->gdev.reset();
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
    SDL_WaitForGPUIdle(a->dev);
    // Before the device dies: the renderer backend holds pipelines and
    // buffers on it, and the platform backend holds the torn-out
    // windows.
    ui_quit(a);
    for (const auto &e : a->pipelines)
        SDL_ReleaseGPUGraphicsPipeline(a->dev, e.pipeline);
    scene_release(a, a->scene);
    for (App::ViewState &v : a->views) {
        scene_release(a, v.scene);
        if (v.tex)
            SDL_ReleaseGPUTexture(a->dev, v.tex);
    }
    if (a->win) {
        SDL_ReleaseWindowFromGPUDevice(a->dev, a->win);
        SDL_DestroyWindow(a->win);
    }
    // gpud's ~Device waits idle, destroys the SDL device and quits its
    // own subsystem ref; ours pairs with app_init's InitSubSystem.
    a->gdev.reset();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    delete a;
}

void app_on_frame(App *a, void (*fn)(void *), void *user) {
    if (a && fn)
        a->frame_cbs.push_front({fn, user});
}

void app_on_event(App *a, void (*fn)(const Event &, void *), void *user) {
    if (a && fn)
        a->event_cbs.push_front({fn, user});
}

void app_on_ui(App *a, void (*fn)(void *), void *user) {
    if (a && fn)
        a->ui_cbs.push_front({fn, user});
}

void app_request_quit(App *a) {
    if (a)
        a->quit = true;
}

void app_post_event(App *a, const Event &e) {
    if (a)
        a->posted.push_back(e);
}

Stats app_stats(App *a) { return a ? a->stats : Stats{}; }

} // namespace impl
} // namespace sv
