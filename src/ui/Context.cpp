// The ImGui layer's implementation, on the upstream SDL3 + Vulkan
// backends. Ordering is upstream's own: platform init before renderer
// init, NewFrame renderer-then-platform, viewports before submit, and
// shutdown platform-then-renderer so both halves of a torn-out window
// are released while both hook sets are still registered.
//
// ui_draw is the app's ONE raw-Vulkan seam: NVRHI transitions the
// attachments, a dynamic-rendering pass records ImGui's draw data into
// the command list's native VkCommandBuffer (LOAD — the composite
// invariant), and clearState() keeps NVRHI's cache honest after it.

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

// The color formats the backend's pipelines are built for. Static on
// purpose: the InitInfo keeps POINTERS to these past ui_init — the
// viewport pipelines are created when a panel is first torn out.
VkFormat g_main_format = VK_FORMAT_R8G8B8A8_UNORM;
VkFormat g_viewport_format = VK_FORMAT_B8G8R8A8_UNORM;

} // namespace

namespace sv {

// A UI frame runs when something needs one — and a world in the
// window needs one even with no panels over it, because the pointer
// it steers by is ImGui's to report. Without this a 3D program with
// nothing but a scene never sees a mouse at all.
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
        // Headless keeps the RENDERER backend and drops only the
        // platform one — the same frame code, the same font atlas, so
        // the UI can composite into a shot. What a platform backend
        // would have supplied is supplied here: the display metrics
        // and a frame time, since the library owns no clock.
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
    // commitBarriers ends NVRHI's own rendering pass ONLY when a
    // barrier was pending; the scene's last draw may have left it
    // open with the target already a render target. clearState ends
    // it unconditionally — a nested vkCmdBeginRendering is a driver
    // crash, not a message.
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
    // UpdatePlatformWindows once per frame, UNGATED by the enable
    // flag: its bookkeeping is what ImGui's cadence assert reads at the
    // next NewFrame, and a mid-run ViewportsEnable (the tests' fake)
    // must find the previous frame pumped. ImGui asserts on a second
    // call within one frame — a Step and a composited shot both ask.
    const int fc = ImGui::GetFrameCount();
    if (a->ui.viewports_pumped != fc) {
        a->ui.viewports_pumped = fc;
        ImGui::UpdatePlatformWindows();
    }
    // The render, at most once per frame and only where the caller
    // vouches the view textures are drawn: a torn-out panel rendered
    // from a headless Step showed a view texture nothing had drawn yet
    // — magenta, whenever the allocator handed back fresh memory.
    if (render && a->ui.viewports_rendered != fc &&
        (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
        a->ui.viewports_rendered = fc;
        ImGui::RenderPlatformWindowsDefault();
    }
}

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
    v.target.want_w =
        std::uint32_t(std::max(1.0f, avail.x * std::max(1.0f, scale.x)));
    v.target.want_h =
        std::uint32_t(std::max(1.0f, avail.y * std::max(1.0f, scale.y)));

    // The texture is last frame's — this frame's is drawn into the
    // same handle before the UI is composited, so what shows is
    // current. The descriptor is baked with the NEAREST sampler and
    // remade only when the resize gave the view a new texture.
    if (v.target.tex) {
        // Keyed on the target's GENERATION, not its address: a resized
        // target is a new texture the allocator may place exactly where
        // the old one was, and a pointer comparison then keeps a
        // descriptor whose image view is already destroyed — sampled
        // garbage, and the validation layer's "imageView 0x0" to say so.
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
                // A world is DRAGGED, and an image cannot be: it is an
                // item that can be hovered and never becomes active,
                // because nothing about it responds. So the rect is an
                // invisible button — which owns the pointer properly,
                // press and all — and the texture is drawn under it.
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##world", avail,
                                       ImGuiButtonFlags_MouseButtonLeft |
                                           ImGuiButtonFlags_MouseButtonRight);
                const bool hovered = ImGui::IsItemHovered();
                const bool active = ImGui::IsItemActive();
                ImGui::GetWindowDrawList()->AddImage(
                    tex, p0, ImVec2(p0.x + avail.x, p0.y + avail.y));
                world_camera_gesture(*v.world, hovered, active);
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
