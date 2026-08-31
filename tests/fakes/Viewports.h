#pragma once

// A multi-viewport backend that needs no display.
//
// A torn-out panel is ImGui's "secondary viewport": its own OS window,
// its own swapchain, its own command buffer. None of that exists
// headless, so the whole tear-out path — the one arrangement a user
// reaches by dragging a panel off the window — is unreachable by every
// test the suite has.
//
// This supplies the missing halves against offscreen NVRHI textures.
// It is written to MIRROR imgui_impl_vulkan's own viewport functions
// rather than to be convenient: RenderWindow acquires its own command
// list, records one pass, and SUBMITS it right there, exactly as
// upstream does. Anything a secondary viewport samples must therefore
// already have been written by a submitted frame — which is a property
// of the app, not of this file.
//
// Tests may speak ImGui, Vulkan, NVRHI and gpud. They are not
// consumers.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include "harness/Bmp.h"
#include "harness/Harness.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <nvrhi/nvrhi.h>
#include <nvrhi/vulkan.h>

#include <cstring>
#include <string>
#include <vector>

namespace viewports {

inline constexpr nvrhi::Format kFormat = nvrhi::Format::RGBA8_UNORM;

// One secondary viewport's stand-in for an OS window and its
// swapchain. Sized on demand, exactly as a real swapchain would be.
struct Window {
    nvrhi::IDevice *dev = nullptr;
    nvrhi::TextureHandle tex;
    nvrhi::FramebufferHandle fb;
    std::uint32_t w = 0, h = 0;
    ImVec2 pos{0, 0}, size{0, 0};
    bool focused = false;
    std::string title;
};

inline std::vector<Window *> &live() {
    static std::vector<Window *> v;
    return v;
}

inline nvrhi::IDevice *&device() {
    static nvrhi::IDevice *d = nullptr;
    return d;
}

inline sv::impl::App *&app_raw() {
    static sv::impl::App *a = nullptr;
    return a;
}

// Submit the way the frame does — bracketed on a one-queue driver —
// and wait for the graphics queue alone, never the whole device.
inline void execute_and_wait(nvrhi::IDevice *dev, nvrhi::ICommandList *cl) {
    sv::probe::queue_lock(app_raw());
    dev->executeCommandList(cl);
    sv::probe::queue_unlock(app_raw());
    auto q = dev->createEventQuery();
    dev->setEventQuery(q, nvrhi::CommandQueue::Graphics);
    dev->waitEventQuery(q);
}

inline Window *of(ImGuiViewport *vp) {
    return static_cast<Window *>(vp->PlatformUserData);
}

inline void resize(Window *w, std::uint32_t nw, std::uint32_t nh) {
    if (w->tex && w->w == nw && w->h == nh)
        return;
    w->fb = nullptr;
    w->tex = w->dev->createTexture(
        nvrhi::TextureDesc()
            .setWidth(nw ? nw : 1)
            .setHeight(nh ? nh : 1)
            .setFormat(kFormat)
            .setIsRenderTarget(true)
            .setInitialState(nvrhi::ResourceStates::RenderTarget)
            .setKeepInitialState(true)
            .setDebugName("fake viewport"));
    if (w->tex)
        w->fb = w->dev->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(w->tex));
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
    delete w;
    vp->PlatformUserData = vp->PlatformHandle = nullptr;
}

// --- the renderer half: upstream's RenderWindow, offscreen ----------

inline void render_window(ImGuiViewport *vp, void *) {
    Window *w = of(vp);
    if (!w)
        return;
    resize(w, std::uint32_t(vp->Size.x), std::uint32_t(vp->Size.y));
    if (!w->fb)
        return;

    ImDrawData *dd = vp->DrawData;
    auto cl = w->dev->createCommandList();
    cl->open();
    cl->clearTextureFloat(w->tex, nvrhi::AllSubresources,
                          nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
    cl->setTextureState(w->tex, nvrhi::AllSubresources,
                        nvrhi::ResourceStates::RenderTarget);
    cl->commitBarriers();
    cl->clearState(); // ends NVRHI's pass unconditionally (see ui_draw)

    const auto cmd = VkCommandBuffer(
        cl->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer).pointer);
    const auto view = VkImageView(
        w->tex->getNativeView(nvrhi::ObjectTypes::VK_ImageView).pointer);
    vk::RenderingAttachmentInfo att;
    att.imageView = view;
    att.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    att.loadOp = vk::AttachmentLoadOp::eLoad;
    att.storeOp = vk::AttachmentStoreOp::eStore;
    vk::RenderingInfo ri;
    ri.renderArea = vk::Rect2D{{0, 0}, {w->w, w->h}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    // The backend keeps per-viewport buffers in RendererUserData,
    // created only by ITS window path — which this fake bypasses. The
    // main viewport's storage exists and its buffer ring rotates per
    // call, so a secondary's draw data borrows it for the call; the
    // wait below keeps a rotation that wraps from touching buffers a
    // submitted frame still reads.
    ImGuiViewport *owner = dd->OwnerViewport;
    dd->OwnerViewport = ImGui::GetMainViewport();
    vk::CommandBuffer(cmd).beginRendering(ri);
    ImGui_ImplVulkan_RenderDrawData(dd, cmd);
    vk::CommandBuffer(cmd).endRendering();
    dd->OwnerViewport = owner;
    cl->clearState();
    cl->close();
    execute_and_wait(w->dev, cl);
}

// Install both halves and turn viewports on. NoAutoMerge makes every
// floating panel its own viewport, which is the tear-out arrangement
// without a mouse to drag with.
inline void enable(sv::App &app) {
    app_raw() = app.Raw();
    device() =
        static_cast<nvrhi::IDevice *>(sv::probe::render_device(app.Raw()));

    ImGuiIO &io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoAutoMerge = true;

    // Headless has no platform backend, so nothing claimed the MAIN
    // viewport; ImGui's sanity check wants a non-null handle once a
    // platform half exists. Any stable non-null pointer will do.
    ImGui::GetMainViewport()->PlatformHandle = &device();

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

    // The renderer half the Vulkan backend installed creates real
    // surfaces and swapchains through PlatformHandle. Ours does not
    // have one.
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

    auto staging = w->dev->createStagingTexture(
        nvrhi::TextureDesc().setWidth(w->w).setHeight(w->h).setFormat(kFormat),
        nvrhi::CpuAccessMode::Read);
    if (!staging)
        return false;
    auto cl = w->dev->createCommandList();
    cl->open();
    cl->copyTexture(staging, nvrhi::TextureSlice(), w->tex,
                    nvrhi::TextureSlice());
    cl->close();
    execute_and_wait(w->dev, cl);

    std::size_t pitch = 0;
    const auto *px = static_cast<const std::uint8_t *>(
        w->dev->mapStagingTexture(staging, nvrhi::TextureSlice(),
                                  nvrhi::CpuAccessMode::Read, &pitch));
    if (!px)
        return false;
    std::vector<std::uint8_t> tight(std::size_t(w->w) * w->h * 4);
    for (std::uint32_t y = 0; y < w->h; ++y)
        std::memcpy(tight.data() + std::size_t(y) * w->w * 4, px + y * pitch,
                    std::size_t(w->w) * 4);
    w->dev->unmapStagingTexture(staging);

    SDL_Surface *s =
        SDL_CreateSurfaceFrom(int(w->w), int(w->h), SDL_PIXELFORMAT_RGBA32,
                              tight.data(), int(w->w * 4));
    const std::string path = harness::tmp_path(name + ".bmp");
    const bool ok = s && SDL_SaveBMP(s, path.c_str());
    SDL_DestroySurface(s);
    return ok && load_bmp(path, out);
}

} // namespace viewports
