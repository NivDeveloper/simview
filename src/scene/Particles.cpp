// The Particles kind, whole. Same shape as Field.cpp: state, push
// block, shaders, ops table, and the exported functions.

#include "Scene.h"

#include "bytecode/particles_fsmain_spirv.h"
#include "bytecode/particles_vsmain_spirv.h"

#include <gpud/Vulkan.h>

#include <cstring>
#include <string>
#include <vector>

namespace sv {
namespace {

struct ParticlesState {
    nvrhi::BufferHandle buf;
    nvrhi::BindingSetHandle bset;
    std::vector<float> shadow;
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

// Matches particles.slang's PParams (std430 push block).
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
// shadow. Shared by Update and the host pull. Growth re-creates the
// buffer, so the binding set is dropped and remade at the next draw.
bool upload(ParticlesState &ps, const impl::Gpu &gpu, const float *xy,
            std::size_t count) {
    if (!count) {
        ps.count = 0; // an empty cloud is not an error
        return true;
    }
    if (count > ps.capacity) {
        ps.buf = gpu.dev->createBuffer(
            nvrhi::BufferDesc()
                .setByteSize(count * 8)
                .setStructStride(8)
                .setInitialState(nvrhi::ResourceStates::ShaderResource)
                .setKeepInitialState(true)
                .setDebugName("particles"));
        ps.bset = nullptr;
        if (!ps.buf) {
            ps.capacity = ps.count = 0;
            return set_error("particles buffer: creation failed"), false;
        }
        ps.capacity = count;
    }
    ps.shadow.assign(xy, xy + count * 2);
    ps.count = count;
    ps.dirty = true;
    return true;
}

// The buffer IS the cloud: interleaved xy pairs, so its size is the
// point count, whichever source it came from.
void pull_host(ParticlesState &ps, const impl::Gpu &gpu) {
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
    upload(ps, gpu, static_cast<const float *>(data), bytes / 8);
}

void prepare(impl::SceneItem &it, nvrhi::ICommandList *cl) {
    ParticlesState &ps = state_of(it);
    if (ps.host)
        pull_host(ps, it.gpu);
    if (ps.dirty && !ps.external && ps.buf && ps.count) {
        cl->writeBuffer(ps.buf, ps.shadow.data(), ps.count * 8);
        ps.dirty = false;
        ++it.stats->uploads;
    }
}

bool rebind_external(impl::SceneItem &it, ParticlesState &ps,
                     const impl::PipelineEntry *pe) {
    gpud::Buffer *b = ps.src.current();
    const std::uint64_t native = b ? gpud::vulkan::native_buffer(*b) : 0;
    ps.count = b ? b->bytes() / 8 : 0;
    if (!native || !ps.count) {
        ps.bset = nullptr;
        return false;
    }
    auto wrapped = it.gpu.dev->createHandleForNativeBuffer(
        nvrhi::ObjectTypes::VK_Buffer,
        nvrhi::Object(reinterpret_cast<void *>(native)),
        nvrhi::BufferDesc()
            .setByteSize(b->bytes())
            .setStructStride(8)
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setDebugName("particles (external)"));
    if (!wrapped)
        return false;
    ps.bset = it.gpu.dev->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(
                nvrhi::BindingSetItem::PushConstants(0, sizeof(PointParams)))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, wrapped)),
        pe->layout);
    return ps.bset != nullptr;
}

void draw(impl::SceneItem &it, nvrhi::ICommandList *cl, nvrhi::IFramebuffer *fb,
          const Placement &at) {
    ParticlesState &ps = state_of(it);
    const impl::PipelineEntry *pe =
        pipeline_for(it.gpu, *it.pipelines, it.stats, &kParticlesOps, fb);
    if (!pe)
        return;
    // Resolved at the bind, count included: bytes()/8 IS the point
    // count, and nothing sits between asking and using.
    if (ps.external) {
        if (!rebind_external(it, ps, pe))
            return;
    } else if (!ps.bset && ps.buf) {
        ps.bset = it.gpu.dev->createBindingSet(
            nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::PushConstants(
                    0, sizeof(PointParams)))
                .addItem(
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(0, ps.buf)),
            pe->layout);
    }
    if (!ps.bset || !ps.count)
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
    cl->setGraphicsState(
        nvrhi::GraphicsState()
            .setPipeline(pe->pipeline)
            .setFramebuffer(fb)
            .addBindingSet(ps.bset)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(float(at.tw), float(at.th)))));
    cl->setPushConstants(&p, sizeof p);
    // Six corners, one instance per point.
    cl->draw(nvrhi::DrawArguments().setVertexCount(6).setInstanceCount(
        std::uint32_t(ps.count)));
    ++it.stats->draws;
}

void release(impl::SceneItem &it) {
    delete static_cast<ParticlesState *>(it.state);
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
    .vs = {particles_vsmain_spirv, particles_vsmain_spirv_len, "vsmain"},
    .fs = {particles_fsmain_spirv, particles_fsmain_spirv_len, "fsmain"},
    .blend = nvrhi::BlendState::RenderTarget()
                 .enableBlend()
                 .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                 .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                 .setSrcBlendAlpha(nvrhi::BlendFactor::One)
                 .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha),
    .storage_stage = nvrhi::ShaderType::Vertex,
    .push_bytes = sizeof(PointParams),
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
    it.gpu = sc->gpu;
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
    return upload(ps, it->gpu, xy, count);
}

Particles particles_from_source(Scene s, gpud::BufferSource src,
                                const ParticlesDesc &d) {
    SceneState *sc = static_cast<SceneState *>(s.p);
    if (!sc)
        return {};
    if (!src)
        return set_error("particles need a source that can answer — the "
                         "BufferSource's fn is null"),
               Particles{};
    if (!(d.radius > 0.0f))
        return set_error("particles need a radius above zero — they are "
                         "drawn as discs, not as points"),
               Particles{};

    SceneItem &it = sc->items.emplace_back();
    it.gpu = sc->gpu;
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
