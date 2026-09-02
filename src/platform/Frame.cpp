#include "Frame.h"

#include "../core/Trace.h"
#include "../ui/Ui.h"
#include "Input.h"

#include <simview/simview.h>

#include <gpud/Vulkan.h>
#include <nvrhi/vulkan.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace sv {
namespace {

// FIRST, so the callbacks, the panels and the scene all read one
// generation. Once per entry path, never inside build or render.
void frame_sync(impl::App *a) {
    for (impl::SyncGate g : a->gates)
        impl::sync_gate_flip(g);
}

// Waited BEFORE the flips: a slot leaving Shown then has no frame
// still reading it, so the producer may overwrite or free it.
void frame_wait_previous(impl::App *a) {
    impl::Platform &pl = a->platform;
    if (!pl.frame_instance)
        return;
    platform_wait_graphics(pl, pl.frame_instance, "the previous frame");
    pl.frame_instance = 0;
    // The renderer learns a frame finished only from here, and
    // without it a versioned constant buffer runs out of versions.
    pl.ndev->runGarbageCollection();
    timing_collect(pl);
}

} // namespace

void frame_build(impl::App *a) {
    // Before the UI frame: a draw list must never record a texture
    // this frame is about to release.
    ui_views_resize(a);
    if (!ui_on(a))
        return;

    ui_begin(a);
    ui_run_panels(a);
    ui_end(a);
    // Headless renders nothing, but the fake's viewports still need
    // the pump to keep ImGui's cadence assert satisfied.
    if (!a->platform.win)
        ui_viewports(a, /*render=*/false);
}

void frame_render(impl::App *a, const Presenter &p) {
    // One read, used for both the composite and the viewports, so a
    // callback cannot desync them mid-frame.
    const bool ui = ui_on(a) && p.composites;

    // Waits GPU-SIDE on the compute timeline for exactly the work
    // that produced what it SHOWS. The host never blocks here.
    if (gpud::Device *gdev = a->platform.gdev.get()) {
        std::uint64_t wait = 0;
        for (impl::SyncGate g : a->gates)
            wait = std::max(wait, impl::sync_gate_shown_stamp(g));
        bool untracked = a->scene.untracked_pulls > 0 ||
                         (a->world && a->world->untracked_pulls > 0);
        for (impl::View &v : a->views)
            untracked = untracked || v.scene.untracked_pulls > 0 ||
                        (v.world && v.world->untracked_pulls > 0);
        if (untracked)
            wait = std::max(wait, gdev->submitted().value);
        if (wait) {
            // The pump: makes the awaited value reachable without any
            // host wait (a failed submit host-signals, so no hang) —
            // and a failed submit is a lost device, which ends here.
            try {
                gdev->submit();
            } catch (const std::exception &e) {
                vk_fatal(std::string("compute submit: ") + e.what());
            }
            a->platform.compute_waited = wait;
            auto *vknv = static_cast<nvrhi::vulkan::IDevice *>(
                a->platform.nraw
                    ->getNativeObject(nvrhi::ObjectTypes::Nvrhi_VK_Device)
                    .pointer);
            vknv->queueWaitForSemaphore(
                nvrhi::CommandQueue::Graphics,
                reinterpret_cast<VkSemaphore>(
                    gpud::vulkan::native_timeline(*gdev)),
                wait);
        }
    }

    impl::Platform &pl = a->platform;
    nvrhi::ICommandList *cl = pl.cl;
    cl->open();
    timing_frame_open(pl, cl);
    Target t{};
    bool have = false;
    {
        SV_ZONE("acquire");
        have = p.acquire(p.self, cl, &t);
    }
    if (have) {
        ++a->stats.frames;
        {
            SV_ZONE("views");
            timing_begin(pl, cl, "views");
            ui_views_draw(a, cl);
            timing_end(pl, cl);
        }
        {
            SV_ZONE("scene");
            // One or the other: a world owns the depth buffer and the
            // clear, and stamps its own passes.
            if (a->world) {
                world_draw_into(*a->world, pl, cl, t.fb, t.w, t.h);
            } else {
                timing_begin(pl, cl, "scene");
                scene_draw(a->scene, cl, t.fb, t.w, t.h, t.format);
                timing_end(pl, cl);
            }
        }
        if (ui) {
            SV_ZONE("ui");
            timing_begin(pl, cl, "ui");
            ui_draw(a, cl, t.fb);
            timing_end(pl, cl);
        }
    }

    // BEFORE the torn-out windows, which sample what it wrote.
    {
        SV_ZONE("finish");
        p.finish(p.self, cl, have);
    }
    if (ui) {
        SV_ZONE("viewports");
        ui_viewports(a, /*render=*/true);
    }
}

namespace {

bool swapchain_acquire_adapter(void *self, nvrhi::ICommandList *, Target *out) {
    impl::App *a = static_cast<impl::App *>(self);
    impl::Swapchain &sc = a->platform.sc;
    if (!swapchain_acquire(
            sc, a->platform.ndev,
            +[](void *u) {
                platform_gfx_idle(*static_cast<impl::Platform *>(u));
            },
            &a->platform))
        return false;
    out->fb = sc.fbs[sc.index];
    out->tex = sc.images[sc.index];
    out->w = sc.w;
    out->h = sc.h;
    out->format = sc.format;
    return true;
}

void swapchain_finish(void *self, nvrhi::ICommandList *cl, bool acquired) {
    impl::App *a = static_cast<impl::App *>(self);
    cl->close();
    if (acquired) {
        // The present semaphore signals on THIS submit; present waits it.
        swapchain_ready_present(a->platform.sc, a->platform.ndev);
        platform_execute(a->platform, cl);
        a->platform.frame_instance = a->platform.gfx_last;
        swapchain_present(a->platform.sc);
    } else {
        // Nothing recorded; executing keeps the list reusable without
        // leaning on re-open-without-execute.
        platform_execute(a->platform, cl);
    }
}

bool shot_acquire(void *self, nvrhi::ICommandList *, Target *out) {
    ShotTarget *s = static_cast<ShotTarget *>(self);
    if (!s->tex)
        return false;
    out->fb = s->fb;
    out->tex = s->tex;
    out->w = s->w;
    out->h = s->h;
    out->format = s->format;
    return true;
}

// The readback belongs to finishing: it has to be recorded after the
// drawing and before the submit, and the caller must not be able to
// put it anywhere else.
void shot_finish(void *self, nvrhi::ICommandList *cl, bool acquired) {
    ShotTarget *s = static_cast<ShotTarget *>(self);
    impl::Platform &pl = s->app->platform;
    if (acquired) {
        timing_begin(pl, cl, "readback");
        cl->copyTexture(s->staging, nvrhi::TextureSlice(), s->tex,
                        nvrhi::TextureSlice());
        timing_end(pl, cl);
    }
    cl->close();
    platform_execute(pl, cl);
    // Waited here, bounded, so the staging map that follows finds the
    // copy complete — NVRHI's own wait inside the map is not bounded.
    if (acquired) {
        platform_wait_graphics(pl, pl.gfx_last, "the shot's frame");
        timing_collect(pl);
    }
}

} // namespace

Presenter swapchain_presenter(impl::App *a) {
    return {swapchain_acquire_adapter, swapchain_finish, true, a};
}

Presenter shot_presenter(ShotTarget *s) {
    // A windowed app's ImGui pipeline is built for the swapchain's
    // format, and a shot target may not match it — so a windowed shot
    // is the scene alone.
    return {shot_acquire, shot_finish, s->app && !s->app->platform.win, s};
}

namespace impl {

void app_run(App *a) {
    if (!a)
        return;
    if (!a->platform.win) {
        SDL_Log("simview: headless app — drive it with Step()/Shot()");
        return;
    }
    // SIMVIEW_FRAMES=N: quit after N loop iterations — how a showcase
    // runs to an exit code under `make validate` without an argv mode.
    const char *budget_env = std::getenv("SIMVIEW_FRAMES");
    const long budget = budget_env ? std::atol(budget_env) : 0;
    long iterations = 0;
    a->platform.quit = false;
    while (!a->platform.quit) {
        if (budget > 0 && iterations++ >= budget)
            break;
        {
            SV_ZONE("poll");
            poll(a);
        }
        {
            SV_ZONE("wait previous frame");
            frame_wait_previous(a); // before the flips — the reverse edge
        }
        {
            SV_ZONE("flip");
            frame_sync(a);
        }
        {
            SV_ZONE("frame callbacks");
            in_order(a->platform.frame_cbs, [](const Cb &c) { c.fn(c.user); });
        }
        // A frame callback may quit; nothing after this point should
        // run when it did.
        if (a->platform.quit)
            break;

        {
            SV_ZONE("build");
            frame_build(a);
        }
        {
            SV_ZONE("render");
            frame_render(a, swapchain_presenter(a));
        }
        SV_FRAME_MARK();
    }
}

void app_step(App *a) {
    if (!a)
        return;
    deliver_posted(a);
    frame_sync(a);
    in_order(a->platform.frame_cbs, [](const Cb &c) { c.fn(c.user); });
    // A headless frame builds the UI too, which is what makes the
    // callbacks testable without a display.
    frame_build(a);
}

bool app_shot(App *a, const char *path) {
    if (!a || !path)
        return set_error("shot: null"), false;
    // Sized from the first field; 512 square when there is none. A
    // COMPOSITED shot must match what the UI frame was built at.
    std::uint32_t w = 512, h = 512;
    const bool composited = !a->platform.win && ui_on(a);
    if (composited) {
        w = a->platform.ui_size.w;
        h = a->platform.ui_size.h;
    } else if (Extent2 g{}; scene_grid(a->scene, &g)) {
        // At least two pixels a cell, so a lattice is legible.
        w = g.w * 2;
        h = g.h * 2;
    }

    nvrhi::IDevice *dev = a->platform.ndev;
    ShotTarget st;
    st.app = a;
    st.w = w;
    st.h = h;
    st.tex = dev->createTexture(
        nvrhi::TextureDesc()
            .setWidth(w)
            .setHeight(h)
            .setFormat(nvrhi::Format::RGBA8_UNORM)
            .setIsRenderTarget(true)
            .setInitialState(nvrhi::ResourceStates::RenderTarget)
            .setKeepInitialState(true)
            .setDebugName("shot"));
    st.staging = dev->createStagingTexture(
        nvrhi::TextureDesc().setWidth(w).setHeight(h).setFormat(
            nvrhi::Format::RGBA8_UNORM),
        nvrhi::CpuAccessMode::Read);
    if (!st.tex || !st.staging)
        return set_error("shot: target creation failed"), false;
    auto sfb = nvrhi::FramebufferDesc().addColorAttachment(st.tex);
    // A world in the window draws into this target instead, and it
    // tests depth: without an attachment here a headless shot of a 3D
    // program would come back empty.
    if (a->world) {
        st.depth = dev->createTexture(
            nvrhi::TextureDesc()
                .setWidth(w)
                .setHeight(h)
                .setFormat(nvrhi::Format::D32)
                .setIsRenderTarget(true)
                .setInitialState(nvrhi::ResourceStates::DepthWrite)
                .setKeepInitialState(true)
                .setDebugName("shot depth"));
        if (!st.depth)
            return set_error("shot: depth creation failed"), false;
        sfb.setDepthAttachment(st.depth);
    }
    st.fb = dev->createFramebuffer(sfb);

    // The readback is recorded in frame_render, after the UI and
    // before the submit.
    frame_wait_previous(a);
    if (!composited)
        frame_sync(a);
    frame_render(a, shot_presenter(&st));

    std::size_t pitch = 0;
    const auto *px = static_cast<const std::uint8_t *>(dev->mapStagingTexture(
        st.staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch));
    if (!px)
        return set_error("shot: staging map failed"), false;
    std::vector<std::uint8_t> tight(std::size_t(w) * h * 4);
    for (std::uint32_t y = 0; y < h; ++y)
        std::memcpy(tight.data() + std::size_t(y) * w * 4, px + y * pitch,
                    std::size_t(w) * 4);
    dev->unmapStagingTexture(st.staging);

    SDL_Surface *s = SDL_CreateSurfaceFrom(
        int(w), int(h), SDL_PIXELFORMAT_RGBA32, tight.data(), int(w * 4));
    const bool ok = s && SDL_SaveBMP(s, path);
    if (!ok)
        set_error(SDL_GetError());
    SDL_DestroySurface(s);
    return ok;
}

} // namespace impl
} // namespace sv
