// The seam definitions for Types.h, App.h and native.h — thin walls
// over the engine.

#include "../engine/Engine.h"

#include <simview/native.h>
#include <simview/simview.h>

#include <algorithm>
#include <filesystem>

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
    return a;
}

void app_quit(App *a) {
    if (!a) return;
    SDL_WaitForGPUIdle(a->dev);
    SDL_DestroyGPUDevice(a->dev);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    delete a;
}

void app_on_frame(App *a, void (*fn)(void *), void *user) {
    if (a && fn) a->frame_cbs.push_front({fn, user});
}

void app_run(App *a) {
    if (!a) return;
    // Move 1: no window, no frames — run() is the shape, not yet the
    // loop. Move 2 gives it the swapchain-paced body.
    SDL_Log("simview: run() has no window to drive yet (Move 2)");
}

SDL_GPUDevice *native_device(App *a) { return a ? a->dev : nullptr; }

} // namespace simview
