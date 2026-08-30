#include "Frame.h"

#include "Ui.h"

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

} // namespace sv
