#include "Frame.h"

#include "../ui/Ui.h"
#include "Input.h"

#include <simview/simview.h>

namespace sv {

void frame_build(impl::App *a) {
    // Before the UI frame: a draw list must never record a texture
    // this frame is about to release.
    views_resize(a);
    if (!ui_on(a))
        return;

    ui_begin(a);
    in_order(a->ui_cbs, [](const impl::App::Cb &c) { c.fn(c.user); });
    ui_end(a);
}

void frame_render(impl::App *a, const Presenter &p) {
    // One read, used for both the composite and the viewports, so a
    // callback cannot desync them mid-frame.
    const bool ui = ui_on(a) && p.composites;

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(a->dev);
    Target t{};
    const bool have = p.acquire(p.self, cmd, &t);
    if (have) {
        ++a->stats.frames;
        views_draw(a, cmd);
        scene_draw(a->scene, cmd, t.tex, t.w, t.h, t.format);
        if (ui)
            ui_draw(a, cmd, t.tex);
    }

    // Submitted BEFORE the torn-out windows, which sample what it
    // wrote from command buffers of their own. Still outside the
    // acquire: a torn-out panel keeps presenting while this window is
    // minimized.
    p.finish(p.self, cmd, have);
    if (ui)
        ui_viewports(a);
}

namespace {

bool swapchain_acquire(void *self, SDL_GPUCommandBuffer *cmd, Target *out) {
    impl::App *a = static_cast<impl::App *>(self);
    SDL_GPUTexture *swap = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, a->win, &swap, nullptr,
                                               nullptr) ||
        !swap)
        return false;

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(a->win, &w, &h);
    out->tex = swap;
    out->w = Uint32(w);
    out->h = Uint32(h);
    out->format = SDL_GetGPUSwapchainTextureFormat(a->dev, a->win);
    return true;
}

void swapchain_finish(void *, SDL_GPUCommandBuffer *cmd, bool acquired) {
    if (acquired)
        SDL_SubmitGPUCommandBuffer(cmd);
    else
        SDL_CancelGPUCommandBuffer(cmd); // minimized: not an error
}

bool shot_acquire(void *self, SDL_GPUCommandBuffer *, Target *out) {
    ShotTarget *s = static_cast<ShotTarget *>(self);
    if (!s->tex)
        return false;
    out->tex = s->tex;
    out->w = s->w;
    out->h = s->h;
    out->format = s->format;
    return true;
}

// The readback belongs to finishing: it has to be recorded after the
// drawing and before the submit, and the caller must not be able to
// put it anywhere else.
void shot_finish(void *self, SDL_GPUCommandBuffer *cmd, bool acquired) {
    ShotTarget *s = static_cast<ShotTarget *>(self);
    if (!acquired) {
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion reg{};
    reg.texture = s->tex;
    reg.w = s->w;
    reg.h = s->h;
    reg.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = s->transfer;
    SDL_DownloadFromGPUTexture(cp, &reg, &dst);
    SDL_EndGPUCopyPass(cp);

    SDL_GPUFence *fe = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(s->app->dev, true, &fe, 1);
    SDL_ReleaseGPUFence(s->app->dev, fe);
}

} // namespace

Presenter swapchain_presenter(impl::App *a) {
    return {swapchain_acquire, swapchain_finish, true, a};
}

Presenter shot_presenter(ShotTarget *s) {
    // A windowed app's ImGui pipeline is built for the swapchain's
    // format, and a shot target is never that format — so a windowed
    // shot is the scene alone.
    return {shot_acquire, shot_finish, s->app && !s->app->win, s};
}

namespace impl {

void app_run(App *a) {
    if (!a)
        return;
    if (a->headless) {
        SDL_Log("simview: headless app — drive it with Step()/Shot()");
        return;
    }
    a->quit = false;
    while (!a->quit) {
        poll(a);
        in_order(a->frame_cbs, [](const App::Cb &c) { c.fn(c.user); });
        // A frame callback may quit; nothing after this point should
        // run when it did.
        if (a->quit)
            break;

        frame_build(a);
        frame_render(a, swapchain_presenter(a));
    }
}

void app_step(App *a) {
    if (!a)
        return;
    deliver_posted(a);
    in_order(a->frame_cbs, [](const App::Cb &c) { c.fn(c.user); });
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
    Uint32 w = 512, h = 512;
    const bool composited = !a->win && ui_on(a);
    if (composited) {
        w = a->ui_size.w;
        h = a->ui_size.h;
    } else if (Extent2 g{}; scene_grid(a->scene, &g)) {
        // At least two pixels a cell, so a lattice is legible.
        w = g.w * 2;
        h = g.h * 2;
    }

    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ti.width = w;
    ti.height = h;
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(a->dev, &ti);
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = w * h * 4;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(a->dev, &tci);
    if (!tex || !tb) {
        set_error(SDL_GetError());
        if (tex)
            SDL_ReleaseGPUTexture(a->dev, tex);
        if (tb)
            SDL_ReleaseGPUTransferBuffer(a->dev, tb);
        return false;
    }

    // The same frame app_run draws, into a texture instead of a
    // window. The order lives in frame_render, once — the readback
    // included, because it has to be recorded after the drawing and
    // before the submit.
    ShotTarget st{a, tex, tb, w, h, ti.format};
    frame_render(a, shot_presenter(&st));

    void *pixels = SDL_MapGPUTransferBuffer(a->dev, tb, false);
    bool ok = pixels != nullptr;
    if (!ok) {
        set_error(SDL_GetError());
    } else {
        SDL_Surface *s = SDL_CreateSurfaceFrom(
            int(w), int(h), SDL_PIXELFORMAT_RGBA32, pixels, int(w * 4));
        ok = s && SDL_SaveBMP(s, path);
        if (!ok)
            set_error(SDL_GetError());
        SDL_DestroySurface(s);
        SDL_UnmapGPUTransferBuffer(a->dev, tb);
    }
    SDL_ReleaseGPUTransferBuffer(a->dev, tb);
    SDL_ReleaseGPUTexture(a->dev, tex);
    return ok;
}

} // namespace impl
} // namespace sv
