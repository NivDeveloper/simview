// The impl definitions for Types.h, App.h and native.h — thin walls
// over the engine.

#include "../engine/Engine.h"
#include "../engine/Ui.h"

#include <simview/gpud.h>
#include <simview/simview.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

struct ImGuiContext;
struct ImPlotContext;

namespace sv {

namespace {
thread_local std::string g_error;
} // namespace

void set_error(std::string msg) {
    g_error = std::move(msg);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "simview: %s", g_error.c_str());
}

namespace impl {

const char *version() { return SIMVIEW_VERSION; }
const char *last_error() { return g_error.c_str(); }

App *app_init(const Config &c) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return set_error(SDL_GetError()), nullptr;

    // gpud owns device bring-up — the Vulkan-loader hint, the SPIR-V
    // format choice (the one format whose emitted binding layout
    // provably matches SDL's conventions; native MSL/DXIL are a
    // planned follow-up) — and the engine borrows the native handle.
    auto g = gpud::sdl::try_open();
    if (!g) {
        set_error("no GPU device (gpud sdl backend; GPUD_LOG=1 says why)");
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }
    App *a = new App;
    a->gdev = std::move(g);
    a->dev = gpud::sdl::native_device(*a->gdev);
    a->headless = c.headless;
    if (!c.headless) {
        a->win = SDL_CreateWindow(c.title, int(c.size.w), int(c.size.h),
                                  SDL_WINDOW_RESIZABLE);
        if (!a->win || !SDL_ClaimWindowForGPUDevice(a->dev, a->win)) {
            set_error(SDL_GetError());
            if (a->win)
                SDL_DestroyWindow(a->win);
            a->gdev.reset();
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            delete a;
            return nullptr;
        }
    }
    ui_init(a, c);
    return a;
}

void app_quit(App *a) {
    if (!a)
        return;
    SDL_WaitForGPUIdle(a->dev);
    // Before the device dies: the renderer backend holds pipelines and
    // buffers on it, and the platform backend holds the torn-out
    // windows.
    ui_quit(a);
    for (const auto &e : a->pipelines)
        SDL_ReleaseGPUGraphicsPipeline(a->dev, e.pipeline);
    for (App::SceneItem &it : a->scene.items) {
        if (it.field.buf && !it.field.external)
            SDL_ReleaseGPUBuffer(a->dev, it.field.buf);
        if (it.field.staging)
            SDL_ReleaseGPUTransferBuffer(a->dev, it.field.staging);
        if (it.particles.buf && !it.particles.external)
            SDL_ReleaseGPUBuffer(a->dev, it.particles.buf);
        if (it.particles.staging)
            SDL_ReleaseGPUTransferBuffer(a->dev, it.particles.staging);
    }
    if (a->win) {
        SDL_ReleaseWindowFromGPUDevice(a->dev, a->win);
        SDL_DestroyWindow(a->win);
    }
    // gpud's ~Device waits idle, destroys the SDL device and quits its
    // own subsystem ref; ours pairs with app_init's InitSubSystem.
    a->gdev.reset();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    delete a;
}

void app_on_frame(App *a, void (*fn)(void *), void *user) {
    if (a && fn)
        a->frame_cbs.push_front({fn, user});
}

void app_on_event(App *a, void (*fn)(const Event &, void *), void *user) {
    if (a && fn)
        a->event_cbs.push_front({fn, user});
}

void app_on_ui(App *a, void (*fn)(void *), void *user) {
    if (a && fn)
        a->ui_cbs.push_front({fn, user});
}

void app_request_quit(App *a) {
    if (a)
        a->quit = true;
}

namespace {

// forward_list push_front reversed registration; fire in registration
// order by walking a copied reverse. Lists are tiny; per frame is fine.
template <typename L, typename F> void in_order(const L &l, F f) {
    std::vector<typename L::value_type> v(l.begin(), l.end());
    std::for_each(v.rbegin(), v.rend(), f);
}

// Events posted through the automation seam, delivered exactly where
// SDL's own are: same callbacks, same order, same frame.
void deliver_posted(App *a) {
    std::vector<Event> queued;
    queued.swap(a->posted);
    for (const Event &e : queued)
        in_order(a->event_cbs, [&](const App::Ecb &c) { c.fn(e, c.user); });
}

void poll(App *a) {
    deliver_posted(a); // the automation seam is never gated by the UI
    const bool ui = ui_on(a);
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        const bool typing = ui && ui_event(a, ev);
        if (ev.type == SDL_EVENT_QUIT)
            a->quit = true;
        // Once a panel is torn out, this window is no longer the last
        // one, so closing it stops producing SDL_EVENT_QUIT — and the
        // app would run on with only a floating panel to show for it.
        if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && a->win &&
            ev.window.windowID == SDL_GetWindowID(a->win))
            a->quit = true;
        if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
            if (typing)
                continue; // a panel has the keyboard
            const Event e{ev.type == SDL_EVENT_KEY_DOWN ? Event::Type::KeyDown
                                                        : Event::Type::KeyUp,
                          std::int32_t(ev.key.scancode), ev.key.repeat};
            in_order(a->event_cbs, [&](const App::Ecb &c) { c.fn(e, c.user); });
        }
    }
}

} // namespace

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
        if (a->quit)
            break;

        const bool ui = ui_on(a);
        if (ui) {
            ui_begin(a);
            in_order(a->ui_cbs, [](const App::Cb &c) { c.fn(c.user); });
            ui_end(a);
        }

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(a->dev);
        SDL_GPUTexture *swap = nullptr;
        const bool have = SDL_WaitAndAcquireGPUSwapchainTexture(
                              cmd, a->win, &swap, nullptr, nullptr) &&
                          swap;
        if (have) {
            int tw = 0, th = 0;
            SDL_GetWindowSizeInPixels(a->win, &tw, &th);
            ++a->stats.frames;
            scene_draw(a->scene, cmd, swap, Uint32(tw), Uint32(th),
                       SDL_GetGPUSwapchainTextureFormat(a->dev, a->win));
            if (ui)
                ui_draw(a, cmd, swap);
        }
        // Outside the acquire: a torn-out panel is its own window and
        // must keep presenting while this one is minimized.
        if (ui)
            ui_viewports(a);
        if (have)
            SDL_SubmitGPUCommandBuffer(cmd);
        else
            SDL_CancelGPUCommandBuffer(cmd); // minimized: not an error
    }
}

Field field_create(App *a, const FieldDesc &d) {
    if (!a)
        return {};
    if (d.dtype != DType::f32)
        return set_error("fields hold f32 values only for now"), Field{};
    if (!d.extent.w || !d.extent.h)
        return set_error("a field needs a non-zero extent"), Field{};

    const Uint32 bytes = d.extent.w * d.extent.h * 4;
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bci.size = bytes;
    SDL_GPUBuffer *buf = SDL_CreateGPUBuffer(a->dev, &bci);
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = bytes;
    SDL_GPUTransferBuffer *staging = SDL_CreateGPUTransferBuffer(a->dev, &tci);
    if (!buf || !staging) {
        set_error(SDL_GetError());
        if (buf)
            SDL_ReleaseGPUBuffer(a->dev, buf);
        if (staging)
            SDL_ReleaseGPUTransferBuffer(a->dev, staging);
        return {};
    }
    App::SceneItem &it = a->scene.items.emplace_back();
    it.app = a;
    it.kind = App::ItemKind::Field;
    it.field = {d.extent.w, d.extent.h, Sint32(d.map), d.lo,
                d.hi,       buf,        staging,       false};
    return Field{&it};
}

bool field_update(Field f, const void *data, DType t, std::size_t count) {
    App::SceneItem *it = static_cast<App::SceneItem *>(f.p);
    if (!it || !data)
        return set_error("field_update: null"), false;
    App *a = it->app;
    if (it->field.external)
        return set_error("this field reads a caller-owned source, "
                         "re-resolved each frame — update the producer, "
                         "not the field"),
               false;
    if (t != DType::f32)
        return set_error("fields hold f32 values only for now"), false;
    if (count != std::size_t(it->field.w) * it->field.h)
        return set_error("field_update: count must equal w*h"), false;
    // cycle=true: per-frame streaming; the frame in flight may still
    // read the previous contents (SDL's sanctioned ring).
    void *map = SDL_MapGPUTransferBuffer(a->dev, it->field.staging, true);
    if (!map)
        return set_error(SDL_GetError()), false;
    std::memcpy(map, data, count * 4);
    SDL_UnmapGPUTransferBuffer(a->dev, it->field.staging);
    it->field.dirty = true;
    return true;
}

void app_step(App *a) {
    if (!a)
        return;
    deliver_posted(a);
    in_order(a->frame_cbs, [](const App::Cb &c) { c.fn(c.user); });
    // A headless frame builds the UI too: the panels run, their
    // geometry exists, and nothing is drawn — which is what makes the
    // callbacks testable without a display.
    if (ui_on(a)) {
        ui_begin(a);
        in_order(a->ui_cbs, [](const App::Cb &c) { c.fn(c.user); });
        ui_end(a);
    }
}

void app_post_event(App *a, const Event &e) {
    if (a)
        a->posted.push_back(e);
}

Stats app_stats(App *a) { return a ? a->stats : Stats{}; }

bool app_shot(App *a, const char *path) {
    if (!a || !path)
        return set_error("shot: null"), false;
    // Sized from the first field so the shot carries no letterbox
    // bars; 512 square when the scene has none.
    Uint32 w = 512, h = 512;
    for (const App::SceneItem &it : a->scene.items)
        if (it.kind == App::ItemKind::Field && it.field.w) {
            w = it.field.w * 2;
            h = it.field.h * 2;
            break;
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

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(a->dev);
    ++a->stats.frames;
    scene_draw(a->scene, cmd, tex, w, h, ti.format);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion reg{};
    reg.texture = tex;
    reg.w = w;
    reg.h = h;
    reg.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(cp, &reg, &dst);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *fe = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(a->dev, true, &fe, 1);
    SDL_ReleaseGPUFence(a->dev, fe);

    void *pixels = SDL_MapGPUTransferBuffer(a->dev, tb, false);
    SDL_Surface *s = SDL_CreateSurfaceFrom(
        int(w), int(h), SDL_PIXELFORMAT_RGBA32, pixels, int(w * 4));
    const bool ok = SDL_SaveBMP(s, path);
    if (!ok)
        set_error(SDL_GetError());
    SDL_DestroySurface(s);
    SDL_UnmapGPUTransferBuffer(a->dev, tb);
    SDL_ReleaseGPUTransferBuffer(a->dev, tb);
    SDL_ReleaseGPUTexture(a->dev, tex);
    return ok;
}

void scene_range(App *a, const Range2 &r) {
    if (a)
        a->scene.range = r;
}

Particles particles_create(App *a, const ParticlesDesc &d) {
    if (!a)
        return {};
    if (!(d.radius > 0.0f))
        return set_error("particles need a radius above zero — they are "
                         "drawn as discs, not as points"),
               Particles{};

    App::SceneItem &it = a->scene.items.emplace_back();
    it.app = a;
    it.kind = App::ItemKind::Particles;
    for (int c = 0; c < 4; ++c)
        it.particles.color[c] = d.color[c];
    it.particles.radius = d.radius;
    return Particles{&it};
}

bool particles_update(Particles p, const float *xy, std::size_t count) {
    App::SceneItem *it = static_cast<App::SceneItem *>(p.p);
    if (!it || (!xy && count))
        return set_error("particles_update: null"), false;
    App *a = it->app;
    App::ParticlesState &ps = it->particles;
    if (ps.external)
        return set_error("these particles read a caller-owned source, "
                         "re-resolved each frame — update the producer, "
                         "not the item"),
               false;
    if (!count) {
        ps.count = 0; // an empty cloud is not an error
        return true;
    }

    // Grow rather than refuse: a cloud that gains points is ordinary.
    if (count > ps.capacity) {
        if (ps.buf)
            SDL_ReleaseGPUBuffer(a->dev, ps.buf);
        if (ps.staging)
            SDL_ReleaseGPUTransferBuffer(a->dev, ps.staging);
        const Uint32 bytes = Uint32(count * 8);
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        bci.size = bytes;
        ps.buf = SDL_CreateGPUBuffer(a->dev, &bci);
        SDL_GPUTransferBufferCreateInfo tci{};
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size = bytes;
        ps.staging = SDL_CreateGPUTransferBuffer(a->dev, &tci);
        if (!ps.buf || !ps.staging) {
            ps.capacity = ps.count = 0;
            return set_error(SDL_GetError()), false;
        }
        ps.capacity = count;
    }

    void *map = SDL_MapGPUTransferBuffer(a->dev, ps.staging, true);
    if (!map)
        return set_error(SDL_GetError()), false;
    std::memcpy(map, xy, count * 8);
    SDL_UnmapGPUTransferBuffer(a->dev, ps.staging);
    ps.count = count;
    ps.dirty = true;
    return true;
}

gpud::Device *app_device(App *a) { return a ? a->gdev.get() : nullptr; }

ImGuiContext *app_ui_context(App *a) { return a ? a->ui.ctx : nullptr; }

ImPlotContext *app_plot_context(App *a) { return a ? a->ui.plot : nullptr; }

Field field_from_source(App *a, gpud::BufferSource src, const FieldDesc &d) {
    if (!a)
        return {};
    if (!src.fn)
        return set_error("a field needs a source that can answer — the "
                         "BufferSource's fn is null"),
               Field{};
    if (d.dtype != DType::f32)
        return set_error("fields hold f32 values only for now"), Field{};
    if (!d.extent.w || !d.extent.h)
        return set_error("a field needs a non-zero extent"), Field{};
    App::SceneItem &it = a->scene.items.emplace_back();
    it.app = a;
    it.kind = App::ItemKind::Field;
    it.field = {d.extent.w, d.extent.h, Sint32(d.map), d.lo, d.hi,
                nullptr,    nullptr,    false,         true};
    it.field.src = src;
    return Field{&it};
}

Particles particles_from_source(App *a, gpud::BufferSource src,
                                const ParticlesDesc &d) {
    if (!a)
        return {};
    if (!src.fn)
        return set_error("particles need a source that can answer — the "
                         "BufferSource's fn is null"),
               Particles{};
    if (!(d.radius > 0.0f))
        return set_error("particles need a radius above zero — they are "
                         "drawn as discs, not as points"),
               Particles{};

    App::SceneItem &it = a->scene.items.emplace_back();
    it.app = a;
    it.kind = App::ItemKind::Particles;
    it.particles.external = true;
    it.particles.src = src;
    for (int c = 0; c < 4; ++c)
        it.particles.color[c] = d.color[c];
    it.particles.radius = d.radius;
    return Particles{&it};
}

} // namespace impl
} // namespace sv
