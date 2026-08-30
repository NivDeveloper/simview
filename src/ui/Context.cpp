// The ImGui layer's implementation. Ordering here is upstream's own
// (examples/example_sdl3_sdlgpu3): NewFrame renderer-then-platform,
// PrepareDrawData outside every render pass, viewports before submit,
// and shutdown platform-then-renderer so both halves of a torn-out
// window are released while both hook sets are still registered.

#include "Ui.h"

#include "../core/App.h"
#include "View.h"

#include <imgui.h>

#include <algorithm>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <implot.h>
#include <implot3d.h>

namespace {

// A layout is EARNED: you arrange your panels once and expect them
// back. The predecessor put its ini in the working directory, so two
// apps clobbered each other and running from elsewhere lost the lot.
// SDL's pref path is per-app and per-user, and it creates the
// directory for us. A title is arbitrary text, and punctuation makes
// SDL_GetPrefPath fail outright on Windows — hence the sieve.
std::string ini_path(const char *title) {
    std::string app;
    for (const char *p = title ? title : ""; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == ' ' || c == '-' ||
                        c == '_';
        app += ok ? char(c) : '_';
    }
    if (app.empty())
        app = "simview";

    char *dir = SDL_GetPrefPath("simview", app.c_str());
    if (!dir) {
        // Not a refusal: the app runs, it just will not remember.
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "simview: no layout directory (%s) — the panel layout "
                    "will not persist",
                    SDL_GetError());
        return {};
    }
    std::string p = std::string(dir) + "layout.ini";
    SDL_free(dir);
    return p;
}

} // namespace

namespace sv {

bool ui_on(impl::App *a) { return a && a->ui.ctx && !a->ui.cbs.empty(); }

void ui_init(impl::App *a, const Config &c) {
    IMGUI_CHECKVERSION();
    a->ui.ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(a->ui.ctx);
    // ImPlot's CreateContext sets current only when no context exists
    // at all — unlike ImGui's — so a second App would otherwise draw
    // its plots into the first App's context.
    a->ui.plot = ImPlot::CreateContext();
    ImPlot::SetCurrentContext(a->ui.plot);
    // ImPlot3D has the identical trap, read from its source: it sets
    // current only when GImPlot3D is null.
    a->ui.plot3d = ImPlot3D::CreateContext();
    ImPlot3D::SetCurrentContext(a->ui.plot3d);

    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr; // the layout file is ours to place
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    if (!a->platform.win) {
        // Headless keeps the RENDERER backend and drops only the
        // platform one: ImGui_ImplSDLGPU3_Init needs a device and a
        // target format, never a window. So a headless frame is built
        // by the same code, with the same backend flags and the same
        // font atlas, and can be composited into a shot — which is
        // the only way the UI is pixel-testable at all. What a
        // platform backend would have supplied is supplied here: the
        // display metrics and a frame time, since the library owns no
        // clock and a test's frames are its own.
        io.DisplaySize = ImVec2(float(c.size.w), float(c.size.h));
        io.DeltaTime = 1.0f / 60.0f;

        ImGui_ImplSDLGPU3_InitInfo ii{};
        ii.Device = a->platform.dev;
        ii.ColorTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ii.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        ImGui_ImplSDLGPU3_Init(&ii);
        return;
    }

    a->ui.ini = ini_path(c.title);
    if (!a->ui.ini.empty())
        ImGui::LoadIniSettingsFromDisk(a->ui.ini.c_str());

    ImGui_ImplSDL3_InitForSDLGPU(a->platform.win);
    // The backend polls gamepads on every frame; simview initialises
    // only the video subsystem, and letting it look sets SDL's error
    // string for a device that was never meant to exist.
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr,
                                  0);

    ImGui_ImplSDLGPU3_InitInfo ii{};
    ii.Device = a->platform.dev;
    ii.ColorTargetFormat =
        SDL_GetGPUSwapchainTextureFormat(a->platform.dev, a->platform.win);
    ii.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&ii);

    // Viewports only when the BACKENDS claim them. Advertising the
    // capability ourselves is what turned the predecessor's missing
    // platform backend into a null-pointer crash instead of a refusal.
    if ((io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) &&
        (io.BackendFlags & ImGuiBackendFlags_RendererHasViewports))
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
}

void ui_quit(impl::App *a) {
    if (!a || !a->ui.ctx)
        return;
    ImGui::SetCurrentContext(a->ui.ctx);
    ImPlot::SetCurrentContext(a->ui.plot);
    ImPlot3D::SetCurrentContext(a->ui.plot3d);
    if (a->platform.win)
        ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    // The frame-count guard is the difference between saving a layout
    // and truncating a good one: an App that opens and closes without
    // ever building a UI frame has nothing to write, and that is the
    // default path for an app with no panels.
    if (!a->ui.ini.empty() && ImGui::GetFrameCount() > 0)
        ImGui::SaveIniSettingsToDisk(a->ui.ini.c_str());
    // The plot contexts hold ImGui-derived state: they go first.
    ImPlot3D::DestroyContext(a->ui.plot3d);
    a->ui.plot3d = nullptr;
    ImPlot::DestroyContext(a->ui.plot);
    a->ui.plot = nullptr;
    ImGui::DestroyContext(a->ui.ctx);
    ImGui::SetCurrentContext(nullptr);
    a->ui.ctx = nullptr;
}

void ui_begin(impl::App *a) {
    ImGui::SetCurrentContext(a->ui.ctx);
    ImPlot::SetCurrentContext(a->ui.plot);
    ImPlot3D::SetCurrentContext(a->ui.plot3d);
    ImGui_ImplSDLGPU3_NewFrame();
    if (a->platform.win)
        ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    // A passthru central node is a hole: the scene shows through it,
    // and an empty dockspace emits no geometry at all.
    ImGui::DockSpaceOverViewport(0, nullptr,
                                 ImGuiDockNodeFlags_PassthruCentralNode);
}

void ui_end(impl::App *a) {
    ImGui::SetCurrentContext(a->ui.ctx);
    ImGui::Render();
    // IniFilename is null, so ImGui asks rather than writing: we own
    // the file, and clearing the flag is our half of that bargain.
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantSaveIniSettings) {
        if (!a->ui.ini.empty())
            ImGui::SaveIniSettingsToDisk(a->ui.ini.c_str());
        io.WantSaveIniSettings = false;
    }
}

void ui_draw(impl::App *a, SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target) {
    ImGui::SetCurrentContext(a->ui.ctx);
    ImDrawData *dd = ImGui::GetDrawData();
    if (!dd || dd->TotalVtxCount == 0)
        return; // nothing drawn: the scene's frame is untouched

    ImGui_ImplSDLGPU3_PrepareDrawData(dd, cmd);
    SDL_GPUColorTargetInfo ct{};
    ct.texture = target;
    ct.load_op = SDL_GPU_LOADOP_LOAD; // composite over the scene
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    ImGui_ImplSDLGPU3_RenderDrawData(dd, cmd, pass);
    SDL_EndGPURenderPass(pass);
}

void ui_viewports(impl::App *a) {
    ImGui::SetCurrentContext(a->ui.ctx);
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
        return;
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
}

namespace {

// A view's texture is a lattice, not a photograph: sampled with the
// backend's default linear filter it would smear cell edges the
// moment the panel is not an exact multiple of the grid. The backend
// carries a nearest sampler for exactly this, switched per draw
// command and switched back so text stays smooth.
void sampler_nearest(const ImDrawList *, const ImDrawCmd *) {
    auto *rs = static_cast<ImGui_ImplSDLGPU3_RenderState *>(
        ImGui::GetPlatformIO().Renderer_RenderState);
    if (rs)
        rs->SamplerCurrent = rs->SamplerNearest;
}

void sampler_linear(const ImDrawList *, const ImDrawCmd *) {
    auto *rs = static_cast<ImGui_ImplSDLGPU3_RenderState *>(
        ImGui::GetPlatformIO().Renderer_RenderState);
    if (rs)
        rs->SamplerCurrent = rs->SamplerLinear;
}

} // namespace

void view_draw(impl::View &v) {
    // A first size, or the view collapses: an ImGui window sizes
    // itself to its content on its first frame, the content is an
    // image sized from the window, and the two would settle at the
    // smallest window ImGui will draw. No padding, so the image meets
    // the frame and the letterbox is the scene's own.
    ImGui::SetNextWindowSize(ImVec2(480, 360), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::Begin(v.title.c_str());
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::End();
        return;
    }

    // What the panel has room for, in PIXELS: content region is in
    // ImGui's points, and on a retina display a texture sized in
    // points would be drawn at half resolution. Asking for the pixel
    // size makes the image 1:1 and the letterbox the scene's own.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 scale = ImGui::GetWindowViewport()->FramebufferScale;
    v.target.want_w = Uint32(std::max(1.0f, avail.x * std::max(1.0f, scale.x)));
    v.target.want_h = Uint32(std::max(1.0f, avail.y * std::max(1.0f, scale.y)));

    // The texture is last frame's — this frame's is drawn into the
    // same handle before the UI is composited, so what shows is
    // current. A view that has never been sized has no texture yet.
    if (v.target.tex) {
        ImGui::GetWindowDrawList()->AddCallback(sampler_nearest, nullptr);
        ImGui::Image(
            ImTextureRef(ImTextureID(reinterpret_cast<intptr_t>(v.target.tex))),
            avail);
        ImGui::GetWindowDrawList()->AddCallback(sampler_linear, nullptr);
    }
    ImGui::End();
}

bool ui_event(impl::App *a, const SDL_Event &ev) {
    ImGui::SetCurrentContext(a->ui.ctx);
    ImGui_ImplSDL3_ProcessEvent(&ev);
    return ImGui::GetIO().WantCaptureKeyboard;
}

void ui_run_panels(impl::App *a) {
    impl::in_order(a->ui.cbs, [](const impl::Cb &c) { c.fn(c.user); });
}

void ui_views_resize(impl::App *a) {
    for (impl::View &v : a->views)
        target_resize(a->platform.dev, v.target);
}

void ui_views_draw(impl::App *a, SDL_GPUCommandBuffer *cmd) {
    for (impl::View &v : a->views)
        target_draw(v.scene, cmd, v.target);
}

} // namespace sv
