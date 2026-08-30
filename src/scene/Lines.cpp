// The Lines kind, whole: segments as instanced quads. Written after
// the ops table, to measure what a third kind costs — this file, its
// shader, and its lines on the public surface. If anything else had
// to change, the commit message says what.

#include "Scene.h"

#include "bytecode/lines_fsmain_spirv.h"
#include "bytecode/lines_vsmain_spirv.h"

#include <cstring>
#include <string>

namespace sv {
namespace {

struct LinesState {
    SDL_GPUBuffer *buf = nullptr;
    SDL_GPUTransferBuffer *staging = nullptr;
    std::size_t count = 0;    // segments the host last wrote
    std::size_t capacity = 0; // segments the buffer holds
    bool dirty = false;
    bool external = false;
    gpud::BufferSource src{};
    HostSource host{};
    std::uint64_t host_gen = 0;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float width = 1.5f;
};

// Matches lines.slang's LParams.
struct LineParams {
    float x0, y0, x1, y1;
    float fit[2];
    float viewport[2];
    float color[4];
    float width;
    float pad0, pad1, pad2;
};

LinesState &state_of(impl::SceneItem &it) {
    return *static_cast<LinesState *>(it.state);
}

// Stage the segments: grow rather than refuse, then fill the staging.
// Shared by Update and the host pull.
bool upload(LinesState &ls, SDL_GPUDevice *dev, const float *xyxy,
            std::size_t count) {
    if (!count) {
        ls.count = 0;
        return true;
    }

    if (count > ls.capacity) {
        if (ls.buf)
            SDL_ReleaseGPUBuffer(dev, ls.buf);
        if (ls.staging)
            SDL_ReleaseGPUTransferBuffer(dev, ls.staging);
        const Uint32 bytes = Uint32(count * 16);
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        bci.size = bytes;
        ls.buf = SDL_CreateGPUBuffer(dev, &bci);
        SDL_GPUTransferBufferCreateInfo tci{};
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size = bytes;
        ls.staging = SDL_CreateGPUTransferBuffer(dev, &tci);
        if (!ls.buf || !ls.staging) {
            ls.capacity = ls.count = 0;
            return set_error(SDL_GetError()), false;
        }
        ls.capacity = count;
    }

    void *map = SDL_MapGPUTransferBuffer(dev, ls.staging, true);
    if (!map)
        return set_error(SDL_GetError()), false;
    std::memcpy(map, xyxy, count * 16);
    SDL_UnmapGPUTransferBuffer(dev, ls.staging);
    ls.count = count;
    ls.dirty = true;
    return true;
}

// The buffer IS the set of segments: four floats each, whichever
// source it came from.
void pull_host(LinesState &ls, SDL_GPUDevice *dev) {
    std::size_t bytes = 0;
    std::uint64_t gen = 0;
    const void *data = ls.host.fn(ls.host.user, &bytes, &gen);
    if (!data || gen == ls.host_gen)
        return;
    ls.host_gen = gen;
    if (bytes % 16)
        return set_error("a segment is four floats, and " +
                         std::to_string(bytes / 4) +
                         " floats were published — not a multiple of four");
    upload(ls, dev, static_cast<const float *>(data), bytes / 16);
}

void prepare(impl::SceneItem &it, SDL_GPUCommandBuffer *cmd) {
    LinesState &ls = state_of(it);
    if (ls.host)
        pull_host(ls, it.dev);
    if (ls.dirty && !ls.external && ls.buf && ls.count) {
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
        const SDL_GPUTransferBufferLocation loc{ls.staging, 0};
        const SDL_GPUBufferRegion reg{ls.buf, 0, Uint32(ls.count * 16)};
        SDL_UploadToGPUBuffer(cp, &loc, &reg, false);
        SDL_EndGPUCopyPass(cp);
        ls.dirty = false;
        ++it.stats->uploads;
    }
}

void draw(impl::SceneItem &it, SDL_GPUCommandBuffer *cmd,
          SDL_GPURenderPass *pass, const Placement &at) {
    LinesState &ls = state_of(it);
    // Resolved at the bind, count included.
    if (ls.external) {
        gpud::Buffer *b = ls.src.current();
        ls.buf = b ? gpud::sdl::native_buffer(*b) : nullptr;
        ls.count = b ? b->bytes() / 16 : 0;
    }
    if (!ls.buf || !ls.count)
        return;
    SDL_GPUGraphicsPipeline *pipe =
        pipeline_for(it.dev, *it.pipelines, it.stats, &kLinesOps, at.format);
    if (!pipe)
        return;

    LineParams p{};
    p.x0 = float(at.range.x0);
    p.y0 = float(at.range.y0);
    p.x1 = float(at.range.x1);
    p.y1 = float(at.range.y1);
    p.fit[0] = at.sx;
    p.fit[1] = at.sy;
    p.viewport[0] = float(at.tw);
    p.viewport[1] = float(at.th);
    for (int c = 0; c < 4; ++c)
        p.color[c] = ls.color[c];
    p.width = ls.width;
    SDL_PushGPUVertexUniformData(cmd, 0, &p, sizeof p);
    SDL_BindGPUGraphicsPipeline(pass, pipe);
    SDL_BindGPUVertexStorageBuffers(pass, 0, &ls.buf, 1);
    // Six corners, one instance per segment; first_* stay 0.
    SDL_DrawGPUPrimitives(pass, 6, Uint32(ls.count), 0, 0);
    ++it.stats->draws;
}

void release(impl::SceneItem &it, SDL_GPUDevice *dev) {
    LinesState *ls = static_cast<LinesState *>(it.state);
    if (!ls)
        return;
    if (ls->buf && !ls->external)
        SDL_ReleaseGPUBuffer(dev, ls->buf);
    if (ls->staging)
        SDL_ReleaseGPUTransferBuffer(dev, ls->staging);
    delete ls;
    it.state = nullptr;
}

} // namespace

// clang-format off: the table reads as a table
const KindOps kLinesOps{
    .name = "lines",
    .prepare = prepare,
    .draw = draw,
    .release = release,
    .grid = nullptr,
    .vs = {lines_vsmain_spirv, lines_vsmain_spirv_len, "vsmain", 1, 1},
    .fs = {lines_fsmain_spirv, lines_fsmain_spirv_len, "fsmain", 0, 0},
    .blend = {.enabled = true},
};
// clang-format on

namespace impl {

namespace {

Lines lines_new(Scene s, const LinesDesc &d, HostSource host) {
    SceneState *sc = static_cast<SceneState *>(s.p);
    if (!sc)
        return {};
    if (!(d.width > 0.0f))
        return set_error("lines need a width above zero — they are drawn "
                         "as quads, not as line primitives"),
               Lines{};

    SceneItem &it = sc->items.emplace_back();
    it.dev = sc->dev;
    it.stats = sc->stats;
    it.pipelines = sc->pipelines;
    it.ops = &kLinesOps;
    LinesState *ls = new LinesState{};
    ls->host = host;
    for (int c = 0; c < 4; ++c)
        ls->color[c] = d.color[c];
    ls->width = d.width;
    it.state = ls;
    return Lines{&it};
}

} // namespace

Lines lines_create(Scene s, const LinesDesc &d) {
    return lines_new(s, d, HostSource{});
}

Lines lines_from_host(Scene s, HostSource src, const LinesDesc &d) {
    if (!src)
        return set_error("lines need a source that can answer — the "
                         "HostSource's fn is null"),
               Lines{};
    return lines_new(s, d, src);
}

bool lines_update(Lines l, const float *xyxy, std::size_t count) {
    SceneItem *it = static_cast<SceneItem *>(l.p);
    if (!it || it->ops != &kLinesOps)
        return set_error("lines_update: this is not a lines handle"), false;
    if (!xyxy && count)
        return set_error("lines_update: null"), false;
    SDL_GPUDevice *dev = it->dev;
    LinesState &ls = state_of(*it);
    if (ls.external)
        return set_error("these lines read a caller-owned source, "
                         "re-resolved each frame — update the producer, "
                         "not the item"),
               false;
    if (ls.host)
        return set_error("these lines read a Sync — publish to it, not "
                         "the item"),
               false;
    return upload(ls, dev, xyxy, count);
}

Lines lines_from_source(Scene s, gpud::BufferSource src, const LinesDesc &d) {
    SceneState *sc = static_cast<SceneState *>(s.p);
    if (!sc)
        return {};
    if (!src.fn)
        return set_error("lines need a source that can answer — the "
                         "BufferSource's fn is null"),
               Lines{};
    if (!(d.width > 0.0f))
        return set_error("lines need a width above zero — they are drawn "
                         "as quads, not as line primitives"),
               Lines{};

    SceneItem &it = sc->items.emplace_back();
    it.dev = sc->dev;
    it.stats = sc->stats;
    it.pipelines = sc->pipelines;
    it.ops = &kLinesOps;
    LinesState *ls = new LinesState{};
    ls->external = true;
    ls->src = src;
    for (int c = 0; c < 4; ++c)
        ls->color[c] = d.color[c];
    ls->width = d.width;
    it.state = ls;
    return Lines{&it};
}

} // namespace impl
} // namespace sv
