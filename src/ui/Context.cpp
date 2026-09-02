#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include "Ui.h"

#include "../core/App.h"
#include "View.h"

#include <nvrhi/vulkan.h>

#include <imgui.h>

#include <algorithm>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <implot.h>
#include <implot3d.h>

namespace {

// Per-app, next to the executable: two apps sharing one ini clobber
// each other, and a working-directory ini loses the layout.
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

// The color formats the backend's pipelines are built for. Static on
// purpose: the InitInfo keeps POINTERS to these past ui_init — the
// viewport pipelines are created when a panel is first torn out.
VkFormat g_main_format = VK_FORMAT_R8G8B8A8_UNORM;
VkFormat g_viewport_format = VK_FORMAT_B8G8R8A8_UNORM;

} // namespace

namespace sv {

// A world in the window needs a UI frame even with no panels: the
// pointer it steers by is ImGui's to report.
bool ui_on(impl::App *a) {
    return a && a->ui.ctx && (!a->ui.cbs.empty() || a->world);
}

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
    ui_fonts(a->ui);
    ui_style(a->ui, a->theme);

    // No Vulkan loader is linked anywhere in this build: the backend
    // resolves every entry point through the app's own
    // vkGetInstanceProcAddr.
    impl::VkContext &vk = a->platform.vk;
    const bool loaded = ImGui_ImplVulkan_LoadFunctions(
        VK_API_VERSION_1_3,
        +[](const char *name, void *user) {
            auto *ctx = static_cast<impl::VkContext *>(user);
            auto *fn = reinterpret_cast<PFN_vkVoidFunction>(
                ctx->gipa(ctx->instance, name));
            if (!fn)
                SDL_Log("simview: vulkan loader has no %s", name);
            return fn;
        },
        &vk);
    if (!loaded)
        SDL_Log("simview: ImGui_ImplVulkan_LoadFunctions failed");

    // The nearest sampler view panels bake into their descriptors: a
    // view's texture is a lattice, not a photograph.
    {
        vk::SamplerCreateInfo sci;
        sci.magFilter = vk::Filter::eNearest;
        sci.minFilter = vk::Filter::eNearest;
        sci.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        sci.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        a->ui.nearest_sampler = reinterpret_cast<std::uint64_t>(
            VkSampler(vk::Device(vk.device).createSampler(sci)));
    }

    const bool windowed = a->platform.win != nullptr;
    if (windowed) {
        a->ui.ini = ini_path(c.title);
        if (!a->ui.ini.empty())
            ImGui::LoadIniSettingsFromDisk(a->ui.ini.c_str());
        ImGui_ImplSDL3_InitForVulkan(a->platform.win);
        // The backend polls gamepads on every frame; simview initialises
        // only the video subsystem, and letting it look sets SDL's error
        // string for a device that was never meant to exist.
        ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual,
                                      nullptr, 0);
        g_main_format = VkFormat(a->platform.sc.vk_format);
    } else {
        // Headless drops only the PLATFORM backend, so what it would
        // have supplied — display metrics, a frame time — is supplied
        // here; the library owns no clock.
        io.DisplaySize = ImVec2(float(c.size.w), float(c.size.h));
        io.DeltaTime = 1.0f / 60.0f;
        g_main_format = VK_FORMAT_R8G8B8A8_UNORM;
    }

    ImGui_ImplVulkan_InitInfo ii{};
    ii.ApiVersion = VK_API_VERSION_1_3;
    ii.Instance = vk.instance;
    ii.PhysicalDevice = vk.physical;
    ii.Device = vk.device;
    ii.QueueFamily = vk.gfx_family;
    ii.Queue = vk.gfx_q;
    ii.DescriptorPoolSize = 128; // internal pool; grows per AddTexture
    ii.MinImageCount = 2;
    ii.ImageCount =
        std::max<std::uint32_t>(2, std::uint32_t(a->platform.sc.images.size()));
    ii.UseDynamicRendering = true;
    ii.PipelineInfoMain.PipelineRenderingCreateInfo = {
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        nullptr,
        0,
        1,
        &g_main_format,
        VK_FORMAT_UNDEFINED,
        VK_FORMAT_UNDEFINED};
    ii.PipelineInfoForViewports.PipelineRenderingCreateInfo = {
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        nullptr,
        0,
        1,
        &g_viewport_format,
        VK_FORMAT_UNDEFINED,
        VK_FORMAT_UNDEFINED};
    ImGui_ImplVulkan_Init(&ii);

    // Viewports only when the BACKENDS claim them. Advertising the
    // capability ourselves is what turned the predecessor's missing
    // platform backend into a null-pointer crash instead of a refusal.
    if (windowed &&
        (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) &&
        (io.BackendFlags & ImGuiBackendFlags_RendererHasViewports))
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
}

void ui_quit(impl::App *a) {
    if (!a || !a->ui.ctx)
        return;
    ImGui::SetCurrentContext(a->ui.ctx);
    ImPlot::SetCurrentContext(a->ui.plot);
    ImPlot3D::SetCurrentContext(a->ui.plot3d);
    // View descriptors before the backend that owns their pool.
    for (impl::View &v : a->views) {
        if (v.imgui_tex)
            ImGui_ImplVulkan_RemoveTexture(
                reinterpret_cast<VkDescriptorSet>(v.imgui_tex));
        v.imgui_tex = nullptr;
        v.bound_gen = 0;
    }
    if (a->platform.win)
        ImGui_ImplSDL3_Shutdown();
    ImGui_ImplVulkan_Shutdown();
    if (a->ui.nearest_sampler) {
        vk::Device(a->platform.vk.device)
            .destroySampler(reinterpret_cast<VkSampler>(a->ui.nearest_sampler));
        a->ui.nearest_sampler = 0;
    }
    // An App that never built a UI frame has nothing to write, and
    // writing anyway truncates a good layout.
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
    // Before NewFrame, so no control is measured against one style and
    // drawn against another.
    if (a->theme_changed) {
        a->theme = a->pending;
        a->theme_changed = false;
        ui_style(a->ui, a->theme);
    }
    ImGui_ImplVulkan_NewFrame();
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

void ui_draw(impl::App *a, nvrhi::ICommandList *cl, nvrhi::IFramebuffer *fb) {
    ImGui::SetCurrentContext(a->ui.ctx);
    ImDrawData *dd = ImGui::GetDrawData();
    if (!dd || dd->TotalVtxCount == 0)
        return; // nothing drawn: the scene's frame is untouched

    // What the UI samples must be readable, and what it draws to must
    // be a render target — stated to NVRHI, which owns the barriers.
    for (impl::View &v : a->views)
        if (v.target.tex)
            cl->setTextureState(v.target.tex, nvrhi::AllSubresources,
                                nvrhi::ResourceStates::ShaderResource);
    nvrhi::ITexture *target = fb->getDesc().colorAttachments[0].texture;
    cl->setTextureState(target, nvrhi::AllSubresources,
                        nvrhi::ResourceStates::RenderTarget);
    cl->commitBarriers();
    // commitBarriers ends NVRHI's pass only when a barrier was
    // pending; clearState ends it either way.
    cl->clearState();

    const auto cmd = VkCommandBuffer(
        cl->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer).pointer);
    const auto view = VkImageView(
        target->getNativeView(nvrhi::ObjectTypes::VK_ImageView).pointer);
    const auto &tdesc = target->getDesc();

    vk::RenderingAttachmentInfo att;
    att.imageView = view;
    att.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
    att.loadOp = vk::AttachmentLoadOp::eLoad; // composite over the scene
    att.storeOp = vk::AttachmentStoreOp::eStore;
    vk::RenderingInfo ri;
    ri.renderArea = vk::Rect2D{{0, 0}, {tdesc.width, tdesc.height}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    vk::CommandBuffer(cmd).beginRendering(ri);
    ImGui_ImplVulkan_RenderDrawData(dd, cmd);
    vk::CommandBuffer(cmd).endRendering();
    cl->clearState();
}

void ui_viewports(impl::App *a, bool render) {
    ImGui::SetCurrentContext(a->ui.ctx);
    // UNGATED: its bookkeeping is what ImGui's cadence assert reads
    // at the next NewFrame.
    const int fc = ImGui::GetFrameCount();
    if (a->ui.viewports_pumped != fc) {
        a->ui.viewports_pumped = fc;
        ImGui::UpdatePlatformWindows();
    }
    // Only where the caller vouches the view textures are drawn.
    if (render && a->ui.viewports_rendered != fc &&
        (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
        a->ui.viewports_rendered = fc;
        ImGui::RenderPlatformWindowsDefault();
    }
}

void view_draw(impl::View &v) {
    // A first size, or the view collapses: the window sizes to its
    // content and the content is sized from the window.
    ImGui::SetNextWindowSize(ImVec2(480, 360), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::Begin(v.title.c_str());
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::End();
        return;
    }

    // In PIXELS: the content region is in ImGui's points, and a
    // texture sized in points is drawn at half resolution on retina.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 scale = ImGui::GetWindowViewport()->FramebufferScale;
    v.target.want_w =
        std::uint32_t(std::max(1.0f, avail.x * std::max(1.0f, scale.x)));
    v.target.want_h =
        std::uint32_t(std::max(1.0f, avail.y * std::max(1.0f, scale.y)));

    // Last frame's handle, drawn into before the UI composites. The
    // descriptor is baked with the NEAREST sampler.
    if (v.target.tex) {
        // Keyed on the GENERATION, not the address: a resized target
        // may land exactly where the old one was, and a pointer
        // comparison then keeps a dead image view.
        if (v.bound_gen != v.target.gen) {
            if (v.imgui_tex)
                ImGui_ImplVulkan_RemoveTexture(
                    reinterpret_cast<VkDescriptorSet>(v.imgui_tex));
            const auto iv = VkImageView(
                v.target.tex->getNativeView(nvrhi::ObjectTypes::VK_ImageView)
                    .pointer);
            v.imgui_tex = ImGui_ImplVulkan_AddTexture(
                reinterpret_cast<VkSampler>(v.app->ui.nearest_sampler), iv,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            v.bound_gen = v.target.gen;
        }
        if (v.imgui_tex) {
            const ImTextureRef tex(
                ImTextureID(reinterpret_cast<intptr_t>(v.imgui_tex)));
            if (v.world) {
                // A world is DRAGGED and an image cannot be, so the
                // rect is an invisible button.
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                // Without this the drag rect keeps every click in its
                // area and the button on top is hoverable, never pressed.
                ImGui::SetNextItemAllowOverlap();
                ImGui::InvisibleButton("##world", avail,
                                       ImGuiButtonFlags_MouseButtonLeft |
                                           ImGuiButtonFlags_MouseButtonRight);
                const bool hovered = ImGui::IsItemHovered();
                const bool active = ImGui::IsItemActive();
                ImGui::GetWindowDrawList()->AddImage(
                    tex, p0, ImVec2(p0.x + avail.x, p0.y + avail.y));
                world_camera_gesture(*v.world, hovered, active);
                world_controls(*v.world, p0);
            } else {
                ImGui::Image(tex, avail);
            }
        }
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
    // The corner button is a panel too, as far as the pointer is
    // concerned: drawn before the test below, so clicking it steers
    // nothing.
    ui_world_overlay(a);
    // After them, never before: whether a panel claimed the pointer is
    // only true once every panel has had its say.
    ui_world_input(a);
}

void ui_views_resize(impl::App *a) {
    for (impl::View &v : a->views)
        target_resize(a->scene.gpu, v.target);
}

void ui_views_draw(impl::App *a, nvrhi::ICommandList *cl) {
    for (impl::View &v : a->views) {
        if (v.world)
            world_draw(*v.world, a->platform, cl, v.target);
        else
            target_draw(v.scene, cl, v.target);
    }
}

} // namespace sv
