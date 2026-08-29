// The impl definitions for Types.h, App.h and native.h — thin walls
// over the engine.

#include "../engine/Engine.h"

#include <simview/gpud.h>
#include <simview/simview.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

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
    return a;
}

void app_quit(App *a) {
    if (!a)
        return;
    SDL_WaitForGPUIdle(a->dev);
    for (const auto &e : a->pipelines)
        SDL_ReleaseGPUGraphicsPipeline(a->dev, e.pipeline);
    if (a->field.buf && !a->field.external)
        SDL_ReleaseGPUBuffer(a->dev, a->field.buf);
    if (a->field.staging)
        SDL_ReleaseGPUTransferBuffer(a->dev, a->field.staging);
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
    deliver_posted(a);
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT)
            a->quit = true;
        if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
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

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(a->dev);
        SDL_GPUTexture *swap = nullptr;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, a->win, &swap, nullptr,
                                                   nullptr) ||
            !swap) {
            SDL_CancelGPUCommandBuffer(cmd); // minimized: not an error
            continue;
        }
        int tw = 0, th = 0;
        SDL_GetWindowSizeInPixels(a->win, &tw, &th);
        render_field(a, cmd, swap, Uint32(tw), Uint32(th),
                     SDL_GetGPUSwapchainTextureFormat(a->dev, a->win));
        SDL_SubmitGPUCommandBuffer(cmd);
    }
}

Field field_create(App *a, const FieldDesc &d) {
    if (!a)
        return {};
    if (a->field.w)
        return set_error("one field per App for now — a second field is a "
                         "planned addition"),
               Field{};
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
    a->field = {d.extent.w, d.extent.h, Sint32(d.map), d.lo,
                d.hi,       buf,        staging,       false};
    return Field{a};
}

bool field_update(Field f, const void *data, DType t, std::size_t count) {
    App *a = static_cast<App *>(f.p);
    if (!a || !data)
        return set_error("field_update: null"), false;
    if (a->field.external)
        return set_error("this field reads a caller-owned source, "
                         "re-resolved each frame — update the producer, "
                         "not the field"),
               false;
    if (t != DType::f32)
        return set_error("fields hold f32 values only for now"), false;
    if (count != std::size_t(a->field.w) * a->field.h)
        return set_error("field_update: count must equal w*h"), false;
    // cycle=true: per-frame streaming; the frame in flight may still
    // read the previous contents (SDL's sanctioned ring).
    void *map = SDL_MapGPUTransferBuffer(a->dev, a->field.staging, true);
    if (!map)
        return set_error(SDL_GetError()), false;
    std::memcpy(map, data, count * 4);
    SDL_UnmapGPUTransferBuffer(a->dev, a->field.staging);
    a->field.dirty = true;
    return true;
}

void app_step(App *a) {
    if (!a)
        return;
    deliver_posted(a);
    in_order(a->frame_cbs, [](const App::Cb &c) { c.fn(c.user); });
}

void app_post_event(App *a, const Event &e) {
    if (a)
        a->posted.push_back(e);
}

Stats app_stats(App *a) { return a ? a->stats : Stats{}; }

bool app_shot(App *a, const char *path) {
    if (!a || !path)
        return set_error("shot: null"), false;
    const Uint32 w = a->field.w ? a->field.w * 2 : 512;
    const Uint32 h = a->field.h ? a->field.h * 2 : 512;

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
    render_field(a, cmd, tex, w, h, ti.format);
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

gpud::Device *app_device(App *a) { return a ? a->gdev.get() : nullptr; }

Field field_from_source(App *a, gpud::BufferSource src, const FieldDesc &d) {
    if (!a)
        return {};
    if (!src.fn)
        return set_error("a field needs a source that can answer — the "
                         "BufferSource's fn is null"),
               Field{};
    if (a->field.w)
        return set_error("one field per App for now — a second field is a "
                         "planned addition"),
               Field{};
    if (d.dtype != DType::f32)
        return set_error("fields hold f32 values only for now"), Field{};
    if (!d.extent.w || !d.extent.h)
        return set_error("a field needs a non-zero extent"), Field{};
    a->field = {d.extent.w, d.extent.h, Sint32(d.map), d.lo, d.hi,
                nullptr,    nullptr,    false,         true};
    a->field.src = src;
    return Field{a};
}

} // namespace impl
} // namespace sv
