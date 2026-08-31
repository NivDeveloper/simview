#include "Frame.h"

#include "../ui/Ui.h"
#include "Input.h"

#include <simview/simview.h>

#include <cstring>
#include <vector>

namespace sv {
namespace {

// The flip is the FIRST thing a frame does: every tracked Sync moves
// shown onto current once, so the callbacks, the panels and the scene
// all read one generation. One call per entry path (run, step, shot),
// never inside frame_build or frame_render — a frame that flipped
// twice would build its panels on one generation and draw another.
void frame_sync(impl::App *a) {
    for (impl::SyncGate g : a->gates)
        impl::sync_gate_flip(g);
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
    // Headless: nothing renders after a Step, but torn-out viewports
    // (the tests' fake) still expect the platform-windows pump after
    // EndFrame — it is what keeps ImGui's cadence contract satisfied
    // for a stepped frame. Windowed frames pump in frame_render.
    if (!a->platform.win)
        ui_viewports(a);
}

void frame_render(impl::App *a, const Presenter &p) {
    // One read, used for both the composite and the viewports, so a
    // callback cannot desync them mid-frame.
    const bool ui = ui_on(a) && p.composites;

    // The SDL-era serialization, spelled ONCE: every compute dispatch
    // completes before the frame reads a buffer. The decoupling commit
    // replaces this host wait with the timeline-semaphore wait.
    if (a->platform.gdev)
        a->platform.gdev->flush();

    nvrhi::ICommandList *cl = a->platform.cl;
    cl->open();
    Target t{};
    const bool have = p.acquire(p.self, cl, &t);
    if (have) {
        ++a->stats.frames;
        ui_views_draw(a, cl);
        scene_draw(a->scene, cl, t.fb, t.w, t.h, t.format);
        if (ui)
            ui_draw(a, cl, t.fb);
    }

    // Submitted BEFORE the torn-out windows, which sample what it
    // wrote from command buffers of their own. Still outside the
    // acquire: a torn-out panel keeps presenting while this window is
    // minimized.
    p.finish(p.self, cl, have);
    if (ui)
        ui_viewports(a);
}

namespace {

bool swapchain_acquire_adapter(void *self, nvrhi::ICommandList *, Target *out) {
    impl::App *a = static_cast<impl::App *>(self);
    impl::Swapchain &sc = a->platform.sc;
    if (!swapchain_acquire(sc, a->platform.ndev))
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
        a->platform.ndev->executeCommandList(cl);
        swapchain_present(a->platform.sc);
    } else {
        // Nothing recorded; executing keeps the list reusable without
        // leaning on re-open-without-execute.
        a->platform.ndev->executeCommandList(cl);
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
    if (acquired)
        cl->copyTexture(s->staging, nvrhi::TextureSlice(), s->tex,
                        nvrhi::TextureSlice());
    cl->close();
    s->app->platform.ndev->executeCommandList(cl);
    if (acquired)
        s->app->platform.ndev->waitForIdle();
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
    a->platform.quit = false;
    while (!a->platform.quit) {
        poll(a);
        frame_sync(a);
        in_order(a->platform.frame_cbs, [](const Cb &c) { c.fn(c.user); });
        // A frame callback may quit; nothing after this point should
        // run when it did.
        if (a->platform.quit)
            break;

        frame_build(a);
        frame_render(a, swapchain_presenter(a));
    }
}

void app_step(App *a) {
    if (!a)
        return;
    deliver_posted(a);
    frame_sync(a);
    in_order(a->platform.frame_cbs, [](const Cb &c) { c.fn(c.user); });
    // A headless frame builds the UI too: the panels run, their
    // geometry exists, and nothing is drawn — which is what makes the
    // callbacks testable without a display. It does NOT render, which
    // is why a Step counts no frame.
    frame_build(a);
}

bool app_shot(App *a, const char *path) {
    if (!a || !path)
        return set_error("shot: null"), false;
    // Sized from the first field so the shot carries no letterbox
    // bars; 512 square when the scene has none. A COMPOSITED shot is
    // the exception: it must match what the UI frame was laid out
    // for, because that is what ImGui's projection assumes.
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
    st.fb = dev->createFramebuffer(
        nvrhi::FramebufferDesc().addColorAttachment(st.tex));

    // The same frame app_run draws, into a texture instead of a
    // window. The order lives in frame_render, once — the readback
    // included, because it has to be recorded after the drawing and
    // before the submit. A composited shot reuses the draw data the
    // last Step built, so it shows what THAT frame flipped to; a plain
    // shot is a frame of its own and flips like one.
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
