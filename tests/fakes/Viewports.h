#pragma once

// A multi-viewport backend that needs no display.
//
// A torn-out panel is ImGui's "secondary viewport": its own OS window,
// its own swapchain, its own command buffer. None of that exists
// headless, so the whole tear-out path — the one arrangement a user
// reaches by dragging a panel off the window — is unreachable by every
// test the suite has.
//
// This supplies the missing halves against offscreen textures. It is
// written to MIRROR imgui_impl_sdlgpu3's own viewport functions rather
// than to be convenient: RenderWindow acquires its own command buffer,
// records one pass, and SUBMITS it right there, exactly as upstream
// does. Anything a secondary viewport samples must therefore already
// have been written by a buffer that is submitted — which is a
// property of the app, not of this file.
//
// Tests may speak ImGui, SDL and gpud. They are not consumers.

#include "harness/Bmp.h"
#include "harness/Harness.h"

#include <simview/gpud.h>
#include <simview/simview.h>

#include <SDL3/SDL.h>
#include <gpud/Sdl.h>
#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>

#include <string>
#include <vector>

namespace viewports {

inline constexpr SDL_GPUTextureFormat kFormat =
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

// One secondary viewport's stand-in for an OS window and its
// swapchain. Sized on demand, exactly as a real swapchain would be.
struct Window {
    SDL_GPUDevice *dev = nullptr;
    SDL_GPUTexture *tex = nullptr;
    Uint32 w = 0, h = 0;
    ImVec2 pos{0, 0}, size{0, 0};
    bool focused = false;
    std::string title;
};

inline std::vector<Window *> &live() {
    static std::vector<Window *> v;
    return v;
}

inline SDL_GPUDevice *&device() {
    static SDL_GPUDevice *d = nullptr;
    return d;
}

inline Window *of(ImGuiViewport *vp) {
    return static_cast<Window *>(vp->PlatformUserData);
}

inline void resize(Window *w, Uint32 nw, Uint32 nh) {
    if (w->tex && w->w == nw && w->h == nh)
        return;
    if (w->tex)
        SDL_ReleaseGPUTexture(w->dev, w->tex);
    const SDL_GPUTextureCreateInfo ti{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = kFormat,
        .usage =
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = nw ? nw : 1,
        .height = nh ? nh : 1,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    w->tex = SDL_CreateGPUTexture(w->dev, &ti);
    w->w = nw;
    w->h = nh;
}

// --- the platform half: geometry, no windows ------------------------

inline void create_window(ImGuiViewport *vp) {
    Window *w = new Window;
    w->dev = device();
    w->pos = vp->Pos;
    w->size = vp->Size;
    vp->PlatformUserData = w;
    vp->PlatformHandle = w;
    vp->PlatformWindowCreated = true;
    live().push_back(w);
}

inline void destroy_window(ImGuiViewport *vp) {
    Window *w = of(vp);
    if (!w)
        return;
    for (std::size_t i = 0; i < live().size(); ++i)
        if (live()[i] == w) {
            live().erase(live().begin() + long(i));
            break;
        }
    if (w->tex)
        SDL_ReleaseGPUTexture(w->dev, w->tex);
    delete w;
    vp->PlatformUserData = vp->PlatformHandle = nullptr;
}

// --- the renderer half: upstream's RenderWindow, offscreen ----------

inline void render_window(ImGuiViewport *vp, void *) {
    Window *w = of(vp);
    if (!w)
        return;
    resize(w, Uint32(vp->Size.x), Uint32(vp->Size.y));
    if (!w->tex)
        return;

    ImDrawData *dd = vp->DrawData;
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(w->dev);
    ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
    SDL_GPUColorTargetInfo ct{};
    ct.texture = w->tex;
    ct.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, pass);
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
}

// Install both halves and turn viewports on. NoAutoMerge makes every
// floating panel its own viewport, which is the tear-out arrangement
// without a mouse to drag with.
inline void enable(sv::App &app) {
    device() = gpud::sdl::native_device(sv::Device(app));

    ImGuiIO &io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoAutoMerge = true;

    ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
    pio.Platform_CreateWindow = create_window;
    pio.Platform_DestroyWindow = destroy_window;
    pio.Platform_ShowWindow = [](ImGuiViewport *) {};
    pio.Platform_SetWindowPos = [](ImGuiViewport *vp, ImVec2 p) {
        if (Window *w = of(vp))
            w->pos = p;
    };
    pio.Platform_GetWindowPos = [](ImGuiViewport *vp) {
        Window *w = of(vp);
        return w ? w->pos : ImVec2(0, 0);
    };
    pio.Platform_SetWindowSize = [](ImGuiViewport *vp, ImVec2 s) {
        if (Window *w = of(vp))
            w->size = s;
    };
    pio.Platform_GetWindowSize = [](ImGuiViewport *vp) {
        Window *w = of(vp);
        return w ? w->size : ImVec2(0, 0);
    };
    pio.Platform_SetWindowFocus = [](ImGuiViewport *vp) {
        if (Window *w = of(vp))
            w->focused = true;
    };
    pio.Platform_GetWindowFocus = [](ImGuiViewport *vp) {
        Window *w = of(vp);
        return w ? w->focused : false;
    };
    pio.Platform_GetWindowMinimized = [](ImGuiViewport *) { return false; };
    pio.Platform_SetWindowTitle = [](ImGuiViewport *vp, const char *t) {
        if (Window *w = of(vp))
            w->title = t ? t : "";
    };
    pio.Platform_RenderWindow = [](ImGuiViewport *, void *) {};
    pio.Platform_SwapBuffers = [](ImGuiViewport *, void *) {};

    // The renderer half the SDL_GPU backend installed points at a real
    // SDL_Window through PlatformHandle. Ours does not have one.
    pio.Renderer_CreateWindow = [](ImGuiViewport *) {};
    pio.Renderer_DestroyWindow = [](ImGuiViewport *) {};
    pio.Renderer_SetWindowSize = [](ImGuiViewport *, ImVec2) {};
    pio.Renderer_RenderWindow = render_window;
    pio.Renderer_SwapBuffers = [](ImGuiViewport *, void *) {};

    // ImGui refuses to place a window with nowhere to place it.
    pio.Monitors.resize(1);
    ImGuiPlatformMonitor &m = pio.Monitors[0];
    m.MainPos = m.WorkPos = ImVec2(0, 0);
    m.MainSize = m.WorkSize = io.DisplaySize;
    m.DpiScale = 1.0f;
}

inline std::size_t count() { return live().size(); }

// Read one torn-out viewport back as pixels.
inline bool read(std::size_t index, const std::string &name, Bmp &out) {
    if (index >= live().size())
        return false;
    Window *w = live()[index];
    if (!w->tex || !w->w || !w->h)
        return false;

    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = w->w * w->h * 4;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(w->dev, &tci);
    if (!tb)
        return false;

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(w->dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion reg{};
    reg.texture = w->tex;
    reg.w = w->w;
    reg.h = w->h;
    reg.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(cp, &reg, &dst);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *fe = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(w->dev, true, &fe, 1);
    SDL_ReleaseGPUFence(w->dev, fe);

    void *px = SDL_MapGPUTransferBuffer(w->dev, tb, false);
    SDL_Surface *s = SDL_CreateSurfaceFrom(
        int(w->w), int(w->h), SDL_PIXELFORMAT_RGBA32, px, int(w->w * 4));
    const std::string path = harness::tmp_path(name + ".bmp");
    const bool ok = SDL_SaveBMP(s, path.c_str());
    SDL_DestroySurface(s);
    SDL_UnmapGPUTransferBuffer(w->dev, tb);
    SDL_ReleaseGPUTransferBuffer(w->dev, tb);
    return ok && load_bmp(path, out);
}

} // namespace viewports
