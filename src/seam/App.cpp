// The seam definitions for Types.h, App.h and native.h — thin walls
// over the engine.

#include "../engine/Engine.h"

#include <simview/native.h>
#include <simview/simview.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace simview {

namespace {
thread_local std::string g_error;
} // namespace

void set_error(std::string msg) { g_error = std::move(msg); }

const char *version() { return SIMVIEW_VERSION; }
const char *last_error() { return g_error.c_str(); }

App *app_init(const Config &c) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return set_error(SDL_GetError()), nullptr;

    // The device creator owns the Vulkan-loader hint: SDL dlopens the
    // loader by bare name and misses /usr/local/lib on macOS.
    if (!SDL_GetHint(SDL_HINT_VULKAN_LIBRARY))
        for (const char *p : {"/usr/local/lib/libvulkan.1.dylib",
                              "/opt/homebrew/lib/libvulkan.1.dylib"})
            if (std::filesystem::exists(p)) {
                SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, p);
                break;
            }

    // Every format simview ships bytecode for; SDL picks the driver.
    SDL_GPUDevice *dev = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL |
            SDL_GPU_SHADERFORMAT_DXIL,
        false, nullptr);
    if (!dev) {
        set_error(SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }
    App *a = new App;
    a->dev = dev;
    a->headless = c.headless;
    if (!c.headless) {
        a->win = SDL_CreateWindow(c.title, int(c.size.w), int(c.size.h),
                                  SDL_WINDOW_RESIZABLE);
        if (!a->win || !SDL_ClaimWindowForGPUDevice(dev, a->win)) {
            set_error(SDL_GetError());
            if (a->win) SDL_DestroyWindow(a->win);
            SDL_DestroyGPUDevice(dev);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            delete a;
            return nullptr;
        }
    }
    return a;
}

void app_quit(App *a) {
    if (!a) return;
    SDL_WaitForGPUIdle(a->dev);
    for (const auto &e : a->pipelines)
        SDL_ReleaseGPUGraphicsPipeline(a->dev, e.pipeline);
    if (a->win) {
        SDL_ReleaseWindowFromGPUDevice(a->dev, a->win);
        SDL_DestroyWindow(a->win);
    }
    SDL_DestroyGPUDevice(a->dev);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    delete a;
}

void app_on_frame(App *a, void (*fn)(void *), void *user) {
    if (a && fn) a->frame_cbs.push_front({fn, user});
}

void app_on_event(App *a, void (*fn)(const Event &, void *), void *user) {
    if (a && fn) a->event_cbs.push_front({fn, user});
}

void app_request_quit(App *a) {
    if (a) a->quit = true;
}

namespace {

// forward_list push_front reversed registration; fire in registration
// order by walking a copied reverse. Lists are tiny; per frame is fine.
template <typename L, typename F> void in_order(const L &l, F f) {
    std::vector<typename L::value_type> v(l.begin(), l.end());
    std::for_each(v.rbegin(), v.rend(), f);
}

void poll(App *a) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT) a->quit = true;
        if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
            const Event e{ev.type == SDL_EVENT_KEY_DOWN
                              ? Event::Type::KeyDown
                              : Event::Type::KeyUp,
                          std::int32_t(ev.key.scancode), ev.key.repeat};
            in_order(a->event_cbs, [&](const App::Ecb &c) { c.fn(e, c.user); });
        }
    }
}

} // namespace

void app_run(App *a) {
    if (!a) return;
    if (a->headless) {
        SDL_Log("simview: headless app — drive it with step()/shot()");
        return;
    }
    a->quit = false;
    while (!a->quit) {
        poll(a);
        in_order(a->frame_cbs, [](const App::Cb &c) { c.fn(c.user); });
        if (a->quit) break;

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(a->dev);
        SDL_GPUTexture *swap = nullptr;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, a->win, &swap,
                                                   nullptr, nullptr) ||
            !swap) {
            SDL_CancelGPUCommandBuffer(cmd); // minimized: not an error
            continue;
        }
        // Move 2 step 1: clear only; the field render pass lands next.
        SDL_GPUColorTargetInfo ct{};
        ct.texture = swap;
        ct.clear_color = {0.09f, 0.09f, 0.10f, 1.0f};
        ct.load_op = SDL_GPU_LOADOP_CLEAR;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
        SDL_EndGPURenderPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
    }
}

SDL_GPUDevice *native_device(App *a) { return a ? a->dev : nullptr; }

} // namespace simview
