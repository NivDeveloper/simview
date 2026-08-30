// The Field kind, whole: its state, its uniform block, its shaders,
// its ops table, and the three exported functions a user reaches it
// through. Nothing outside this file knows a FieldState exists.

#include "../core/Engine.h"

#include "bytecode/display_fsmain_spirv.h"
#include "bytecode/display_vsmain_spirv.h"

#include <cstring>

namespace sv {
namespace {

struct FieldState {
    Uint32 w = 0, h = 0;
    Sint32 cmap = 0;
    float lo = 0, hi = 1;
    SDL_GPUBuffer *buf = nullptr;
    SDL_GPUTransferBuffer *staging = nullptr;
    bool dirty = false;    // staging holds a newer grid than buf
    bool external = false; // buf resolves from src: no staging, no release
    // The pull source an external field re-asks at every draw.
    gpud::BufferSource src{};
};

// std140-compatible: 4-byte scalars only, matching display.slang's
// Params.
struct DrawParams {
    Uint32 w, h;
    Sint32 cmap;
    Uint32 pad0;
    float lo, hi;
    float uvscale[2];
    float uvoff[2];
};

FieldState &state_of(impl::SceneItem &it) {
    return *static_cast<FieldState *>(it.state);
}

const FieldState &state_of(const impl::SceneItem &it) {
    return *static_cast<const FieldState *>(it.state);
}

void prepare(impl::SceneItem &it, SDL_GPUCommandBuffer *cmd) {
    FieldState &f = state_of(it);
    if (f.external) {
        gpud::Buffer *b = f.src.current();
        f.buf = b ? gpud::sdl::native_buffer(*b) : nullptr;
    }
    if (f.w && f.dirty && !f.external) {
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
        const SDL_GPUTransferBufferLocation loc{f.staging, 0};
        const SDL_GPUBufferRegion reg{f.buf, 0, f.w * f.h * 4};
        SDL_UploadToGPUBuffer(cp, &loc, &reg, false);
        SDL_EndGPUCopyPass(cp);
        f.dirty = false;
        ++it.app->stats.uploads;
    }
}

void draw(impl::SceneItem &it, SDL_GPUCommandBuffer *cmd,
          SDL_GPURenderPass *pass, const Placement &at) {
    FieldState &f = state_of(it);
    if (!f.w || !f.buf)
        return;
    SDL_GPUGraphicsPipeline *pipe = pipeline_for(it.app, &kFieldOps, at.format);
    if (!pipe)
        return;

    DrawParams p{};
    p.w = f.w;
    p.h = f.h;
    p.cmap = f.cmap;
    p.lo = f.lo;
    p.hi = f.hi;
    p.uvscale[0] = at.sx;
    p.uvscale[1] = at.sy;
    p.uvoff[0] = (1.0f - at.sx) * 0.5f;
    p.uvoff[1] = (1.0f - at.sy) * 0.5f;
    SDL_PushGPUFragmentUniformData(cmd, 0, &p, sizeof p);
    SDL_BindGPUGraphicsPipeline(pass, pipe);
    SDL_BindGPUFragmentStorageBuffers(pass, 0, &f.buf, 1);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    ++it.app->stats.draws;
}

void release(impl::SceneItem &it, SDL_GPUDevice *dev) {
    FieldState *f = static_cast<FieldState *>(it.state);
    if (!f)
        return;
    if (f->buf && !f->external)
        SDL_ReleaseGPUBuffer(dev, f->buf);
    if (f->staging)
        SDL_ReleaseGPUTransferBuffer(dev, f->staging);
    delete f;
    it.state = nullptr;
}

bool grid(const impl::SceneItem &it, Extent2 *out) {
    const FieldState &f = state_of(it);
    if (!f.w)
        return false;
    *out = {f.w, f.h};
    return true;
}

} // namespace

// clang-format off: the table reads as a table
const KindOps kFieldOps{
    .name = "field",
    .prepare = prepare,
    .draw = draw,
    .release = release,
    .grid = grid,
    .vs = {display_vsmain_spirv, display_vsmain_spirv_len, "vsmain", 0, 0},
    .fs = {display_fsmain_spirv, display_fsmain_spirv_len, "fsmain", 1, 1},
    .blend = {},
};
// clang-format on

namespace impl {

Field field_create(Scene s, const FieldDesc &d) {
    SceneState *sc = static_cast<SceneState *>(s.p);
    if (!sc)
        return {};
    App *a = sc->app;
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

    SceneItem &it = sc->items.emplace_back();
    it.app = a;
    it.ops = &kFieldOps;
    it.state = new FieldState{.w = d.extent.w,
                              .h = d.extent.h,
                              .cmap = Sint32(d.map),
                              .lo = d.lo,
                              .hi = d.hi,
                              .buf = buf,
                              .staging = staging};
    return Field{&it};
}

bool field_update(Field f, const void *data, DType t, std::size_t count) {
    SceneItem *it = static_cast<SceneItem *>(f.p);
    // The kind check comes FIRST and is a pointer comparison, so a
    // handle that is not a field's is refused before anything is
    // dereferenced through it.
    if (!it || it->ops != &kFieldOps)
        return set_error("field_update: this is not a field handle"), false;
    if (!data)
        return set_error("field_update: null"), false;
    App *a = it->app;
    FieldState &fs = state_of(*it);
    if (fs.external)
        return set_error("this field reads a caller-owned source, "
                         "re-resolved each frame — update the producer, "
                         "not the field"),
               false;
    if (t != DType::f32)
        return set_error("fields hold f32 values only for now"), false;
    if (count != std::size_t(fs.w) * fs.h)
        return set_error("field_update: count must equal w*h"), false;
    // cycle=true: per-frame streaming; the frame in flight may still
    // read the previous contents (SDL's sanctioned ring).
    void *map = SDL_MapGPUTransferBuffer(a->dev, fs.staging, true);
    if (!map)
        return set_error(SDL_GetError()), false;
    std::memcpy(map, data, count * 4);
    SDL_UnmapGPUTransferBuffer(a->dev, fs.staging);
    fs.dirty = true;
    return true;
}

Field field_from_source(Scene s, gpud::BufferSource src, const FieldDesc &d) {
    SceneState *sc = static_cast<SceneState *>(s.p);
    if (!sc)
        return {};
    if (!src.fn)
        return set_error("a field needs a source that can answer — the "
                         "BufferSource's fn is null"),
               Field{};
    if (d.dtype != DType::f32)
        return set_error("fields hold f32 values only for now"), Field{};
    if (!d.extent.w || !d.extent.h)
        return set_error("a field needs a non-zero extent"), Field{};

    SceneItem &it = sc->items.emplace_back();
    it.app = sc->app;
    it.ops = &kFieldOps;
    it.state = new FieldState{.w = d.extent.w,
                              .h = d.extent.h,
                              .cmap = Sint32(d.map),
                              .lo = d.lo,
                              .hi = d.hi,
                              .external = true,
                              .src = src};
    return Field{&it};
}

} // namespace impl
} // namespace sv
