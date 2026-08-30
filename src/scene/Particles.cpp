// The Particles kind, whole. Same shape as Field.cpp: state, uniform
// block, shaders, ops table, and the exported functions — with the
// bindings in the opposite stages, which is why nothing about the
// draw could be hoisted out of the kind.

#include "Scene.h"

#include "bytecode/particles_fsmain_spirv.h"
#include "bytecode/particles_vsmain_spirv.h"

#include <cstring>
#include <string>

namespace sv {
namespace {

struct ParticlesState {
    SDL_GPUBuffer *buf = nullptr;
    SDL_GPUTransferBuffer *staging = nullptr;
    std::size_t count = 0;    // points the host last wrote
    std::size_t capacity = 0; // points the buffer holds
    bool dirty = false;
    bool external = false;
    gpud::BufferSource src{};
    HostSource host{};
    std::uint64_t host_gen = 0;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float radius = 3.0f;
};

// Matches particles.slang's PParams: 4-byte scalars and a float4 on a
// 16-byte boundary.
struct PointParams {
    float x0, y0, x1, y1;
    float fit[2];
    float viewport[2];
    float color[4];
    float radius;
    float pad0, pad1, pad2;
};

ParticlesState &state_of(impl::SceneItem &it) {
    return *static_cast<ParticlesState *>(it.state);
}

// Stage a cloud: grow the buffer rather than refuse, then fill the
// staging. Shared by Update and the host pull.
bool upload(ParticlesState &ps, SDL_GPUDevice *dev, const float *xy,
            std::size_t count) {
    if (!count) {
        ps.count = 0; // an empty cloud is not an error
        return true;
    }

    // Grow rather than refuse: a cloud that gains points is ordinary.
    if (count > ps.capacity) {
        if (ps.buf)
            SDL_ReleaseGPUBuffer(dev, ps.buf);
        if (ps.staging)
            SDL_ReleaseGPUTransferBuffer(dev, ps.staging);
        const Uint32 bytes = Uint32(count * 8);
        SDL_GPUBufferCreateInfo bci{};
        bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        bci.size = bytes;
        ps.buf = SDL_CreateGPUBuffer(dev, &bci);
        SDL_GPUTransferBufferCreateInfo tci{};
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tci.size = bytes;
        ps.staging = SDL_CreateGPUTransferBuffer(dev, &tci);
        if (!ps.buf || !ps.staging) {
            ps.capacity = ps.count = 0;
            return set_error(SDL_GetError()), false;
        }
        ps.capacity = count;
    }

    void *map = SDL_MapGPUTransferBuffer(dev, ps.staging, true);
    if (!map)
        return set_error(SDL_GetError()), false;
    std::memcpy(map, xy, count * 8);
    SDL_UnmapGPUTransferBuffer(dev, ps.staging);
    ps.count = count;
    ps.dirty = true;
    return true;
}

// The buffer IS the cloud: interleaved xy pairs, so its size is the
// point count, whichever source it came from.
void pull_host(ParticlesState &ps, SDL_GPUDevice *dev) {
    std::size_t bytes = 0;
    std::uint64_t gen = 0;
    const void *data = ps.host.fn(ps.host.user, &bytes, &gen);
    if (!data || gen == ps.host_gen)
        return;
    ps.host_gen = gen;
    if (bytes % 8)
        return set_error("a cloud is xy pairs, and " +
                         std::to_string(bytes / 4) +
                         " floats were published — an odd count");
    upload(ps, dev, static_cast<const float *>(data), bytes / 8);
}

void prepare(impl::SceneItem &it, SDL_GPUCommandBuffer *cmd) {
    ParticlesState &ps = state_of(it);
    if (ps.host)
        pull_host(ps, it.dev);
    if (ps.dirty && !ps.external && ps.buf && ps.count) {
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
        const SDL_GPUTransferBufferLocation loc{ps.staging, 0};
        const SDL_GPUBufferRegion reg{ps.buf, 0, Uint32(ps.count * 8)};
        SDL_UploadToGPUBuffer(cp, &loc, &reg, false);
        SDL_EndGPUCopyPass(cp);
        ps.dirty = false;
        ++it.stats->uploads;
    }
}

void draw(impl::SceneItem &it, SDL_GPUCommandBuffer *cmd,
          SDL_GPURenderPass *pass, const Placement &at) {
    ParticlesState &ps = state_of(it);
    // Resolved at the bind, count included: bytes()/8 IS the point
    // count, and nothing sits between asking and using.
    if (ps.external) {
        gpud::Buffer *b = ps.src.current();
        ps.buf = b ? gpud::sdl::native_buffer(*b) : nullptr;
        ps.count = b ? b->bytes() / 8 : 0;
    }
    if (!ps.buf || !ps.count)
        return;
    SDL_GPUGraphicsPipeline *pipe = pipeline_for(
        it.dev, *it.pipelines, it.stats, &kParticlesOps, at.format);
    if (!pipe)
        return;

    PointParams p{};
    p.x0 = float(at.range.x0);
    p.y0 = float(at.range.y0);
    p.x1 = float(at.range.x1);
    p.y1 = float(at.range.y1);
    p.fit[0] = at.sx;
    p.fit[1] = at.sy;
    p.viewport[0] = float(at.tw);
    p.viewport[1] = float(at.th);
    for (int c = 0; c < 4; ++c)
        p.color[c] = ps.color[c];
    p.radius = ps.radius;
    SDL_PushGPUVertexUniformData(cmd, 0, &p, sizeof p);
    SDL_BindGPUGraphicsPipeline(pass, pipe);
    SDL_BindGPUVertexStorageBuffers(pass, 0, &ps.buf, 1);
    // Six corners, one instance per point. first_* must stay 0: SDL
    // says the built-in IDs are not compatible with them.
    SDL_DrawGPUPrimitives(pass, 6, Uint32(ps.count), 0, 0);
    ++it.stats->draws;
}

void release(impl::SceneItem &it, SDL_GPUDevice *dev) {
    ParticlesState *ps = static_cast<ParticlesState *>(it.state);
    if (!ps)
        return;
    if (ps->buf && !ps->external)
        SDL_ReleaseGPUBuffer(dev, ps->buf);
    if (ps->staging)
        SDL_ReleaseGPUTransferBuffer(dev, ps->staging);
    delete ps;
    it.state = nullptr;
}

} // namespace

// A cloud has no natural grid: `grid` stays null, and the scene asks
// the next item instead.
// clang-format off: the table reads as a table
const KindOps kParticlesOps{
    .name = "particles",
    .prepare = prepare,
    .draw = draw,
    .release = release,
    .grid = nullptr,
    .vs = {particles_vsmain_spirv, particles_vsmain_spirv_len, "vsmain", 1, 1},
    .fs = {particles_fsmain_spirv, particles_fsmain_spirv_len, "fsmain", 0, 0},
    .blend = {.enabled = true},
};
// clang-format on

namespace impl {

namespace {

Particles particles_new(Scene s, const ParticlesDesc &d, HostSource host) {
    SceneState *sc = static_cast<SceneState *>(s.p);
    if (!sc)
        return {};
    if (!(d.radius > 0.0f))
        return set_error("particles need a radius above zero — they are "
                         "drawn as discs, not as points"),
               Particles{};

    SceneItem &it = sc->items.emplace_back();
    it.dev = sc->dev;
    it.stats = sc->stats;
    it.pipelines = sc->pipelines;
    it.ops = &kParticlesOps;
    ParticlesState *ps = new ParticlesState{};
    ps->host = host;
    for (int c = 0; c < 4; ++c)
        ps->color[c] = d.color[c];
    ps->radius = d.radius;
    it.state = ps;
    return Particles{&it};
}

} // namespace

Particles particles_create(Scene s, const ParticlesDesc &d) {
    return particles_new(s, d, HostSource{});
}

Particles particles_from_host(Scene s, HostSource src, const ParticlesDesc &d) {
    if (!src)
        return set_error("particles need a source that can answer — the "
                         "HostSource's fn is null"),
               Particles{};
    return particles_new(s, d, src);
}

bool particles_update(Particles p, const float *xy, std::size_t count) {
    SceneItem *it = static_cast<SceneItem *>(p.p);
    if (!it || it->ops != &kParticlesOps)
        return set_error("particles_update: this is not a particles handle"),
               false;
    if (!xy && count)
        return set_error("particles_update: null"), false;
    SDL_GPUDevice *dev = it->dev;
    ParticlesState &ps = state_of(*it);
    if (ps.external)
        return set_error("these particles read a caller-owned source, "
                         "re-resolved each frame — update the producer, "
                         "not the item"),
               false;
    if (ps.host)
        return set_error("these particles read a Sync — publish to it, "
                         "not the item"),
               false;
    return upload(ps, dev, xy, count);
}

Particles particles_from_source(Scene s, gpud::BufferSource src,
                                const ParticlesDesc &d) {
    SceneState *sc = static_cast<SceneState *>(s.p);
    if (!sc)
        return {};
    if (!src.fn)
        return set_error("particles need a source that can answer — the "
                         "BufferSource's fn is null"),
               Particles{};
    if (!(d.radius > 0.0f))
        return set_error("particles need a radius above zero — they are "
                         "drawn as discs, not as points"),
               Particles{};

    SceneItem &it = sc->items.emplace_back();
    it.dev = sc->dev;
    it.stats = sc->stats;
    it.pipelines = sc->pipelines;
    it.ops = &kParticlesOps;
    ParticlesState *ps = new ParticlesState{};
    ps->external = true;
    ps->src = src;
    for (int c = 0; c < 4; ++c)
        ps->color[c] = d.color[c];
    ps->radius = d.radius;
    it.state = ps;
    return Particles{&it};
}

} // namespace impl
} // namespace sv
