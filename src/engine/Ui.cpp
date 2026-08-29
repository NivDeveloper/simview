// The ImGui layer's implementation. Ordering here is upstream's own
// (examples/example_sdl3_sdlgpu3): NewFrame renderer-then-platform,
// PrepareDrawData outside every render pass, viewports before submit,
// and shutdown platform-then-renderer so both halves of a torn-out
// window are released while both hook sets are still registered.

#include "Ui.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

namespace sv {

bool ui_on(impl::App *a) { return a && a->ui.ctx && !a->ui_cbs.empty(); }

void ui_init(impl::App *a, const Config &c) {
    IMGUI_CHECKVERSION();
    a->ui.ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(a->ui.ctx);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr; // the layout file is ours to place
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    if (!a->win) {
        // Headless: the context alone, given what a backend would have
        // supplied — the display metrics, a frame time (the library
        // owns no clock, so a test's frames are its own), and a built
        // font atlas, which ImGui otherwise asserts on. The texture id
        // is a placeholder: nothing samples it because nothing draws.
        io.DisplaySize = ImVec2(float(c.size.w), float(c.size.h));
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char *pixels = nullptr;
        int tw = 0, th = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &tw, &th);
        io.Fonts->SetTexID(ImTextureID(1));
        return;
    }

    ImGui_ImplSDL3_InitForSDLGPU(a->win);
    // The backend polls gamepads on every frame; simview initialises
    // only the video subsystem, and letting it look sets SDL's error
    // string for a device that was never meant to exist.
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr,
                                  0);

    ImGui_ImplSDLGPU3_InitInfo ii{};
    ii.Device = a->dev;
    ii.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(a->dev, a->win);
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
    if (a->win) {
        ImGui_ImplSDL3_Shutdown();
        ImGui_ImplSDLGPU3_Shutdown();
    }
    ImGui::DestroyContext(a->ui.ctx);
    ImGui::SetCurrentContext(nullptr);
    a->ui.ctx = nullptr;
}

void ui_begin(impl::App *a) {
    ImGui::SetCurrentContext(a->ui.ctx);
    if (a->win) {
        ImGui_ImplSDLGPU3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
    }
    ImGui::NewFrame();
    // A passthru central node is a hole: the scene shows through it,
    // and an empty dockspace emits no geometry at all.
    ImGui::DockSpaceOverViewport(0, nullptr,
                                 ImGuiDockNodeFlags_PassthruCentralNode);
}

void ui_end(impl::App *a) {
    ImGui::SetCurrentContext(a->ui.ctx);
    ImGui::Render();
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

bool ui_event(impl::App *a, const SDL_Event &ev) {
    ImGui::SetCurrentContext(a->ui.ctx);
    ImGui_ImplSDL3_ProcessEvent(&ev);
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace sv
