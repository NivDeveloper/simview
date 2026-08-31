// The Cloud item, whole: state, push block, shaders, three ops rows,
// and the exported functions. Same shape as the 2D Particles kind,
// because it answers the same three doors — a caller's Update, a Sync
// it pulls from, or a device buffer it re-resolves every frame.
//
// What differs is only what 3D forces: positions are xyz triples, the
// radius is in world units, and the mode picks which pass the item
// draws in.

#include "World.h"

#include "../core/Error.h"
#include "bytecode/cloud_fsmain_spirv.h"
#include "bytecode/cloud_vsmain_spirv.h"

#include <gpud/Vulkan.h>

#include <cstring>
#include <string>
#include <vector>

namespace sv {
namespace {

struct CloudState {
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
    float radius = 0.05f;
    std::uint32_t mode = 0;
    // The centroid the sort keys on. One number for the whole cloud:
    // a cloud is one draw, so it takes one place in the order.
    impl::Vec3 centre{};
};

// Matches cloud.slang's CParams.
struct CloudParams {
    float color[4];
    float radius;
    std::uint32_t count;
    std::uint32_t mode;
    float pad0;
};

CloudState &state_of(impl::WorldItem &it) {
    return *static_cast<CloudState *>(it.state);
}

// A cloud is xyz triples: twelve bytes a point, whichever source they
// came from, so the byte count IS the point count.
bool upload(CloudState &cs, const impl::Gpu &gpu, const float *xyz,
            std::size_t count) {
    if (!count) {
        cs.count = 0; // an empty cloud is not an error
        return true;
    }
    if (count > cs.capacity) {
        cs.buf = gpu.dev->createBuffer(
            nvrhi::BufferDesc()
                .setByteSize(count * 12)
                .setStructStride(4)
                .setInitialState(nvrhi::ResourceStates::ShaderResource)
                .setKeepInitialState(true)
                .setDebugName("cloud"));
        cs.bset = nullptr;
        if (!cs.buf) {
            cs.capacity = cs.count = 0;
            return set_error("cloud buffer: creation failed"), false;
        }
        cs.capacity = count;
    }
    cs.shadow.assign(xyz, xyz + count * 3);
    cs.count = count;
    cs.dirty = true;
    return true;
}

void recentre(CloudState &cs) {
    impl::Vec3 sum{};
    for (std::size_t i = 0; i < cs.count && i * 3 + 2 < cs.shadow.size(); ++i)
        sum = sum + impl::Vec3{cs.shadow[i * 3], cs.shadow[i * 3 + 1],
                               cs.shadow[i * 3 + 2]};
    cs.centre = cs.count ? sum * (1.0f / float(cs.count)) : impl::Vec3{};
}

void pull_host(CloudState &cs, const impl::Gpu &gpu) {
    std::size_t bytes = 0;
    std::uint64_t gen = 0;
    const void *data = cs.host.fn(cs.host.user, &bytes, &gen);
    if (!data || gen == cs.host_gen)
        return;
    cs.host_gen = gen;
    if (bytes % 12)
        return set_error("a cloud is xyz triples, and " +
                         std::to_string(bytes / 4) +
                         " floats were published — not a multiple of three");
    upload(cs, gpu, static_cast<const float *>(data), bytes / 12);
    recentre(cs);
}

void prepare(impl::WorldItem &it, nvrhi::ICommandList *cl) {
    CloudState &cs = state_of(it);
    if (cs.host)
        pull_host(cs, it.gpu);
    if (cs.dirty && !cs.external && cs.buf && cs.count) {
        cl->writeBuffer(cs.buf, cs.shadow.data(), cs.count * 12);
        cs.dirty = false;
        ++it.stats->uploads;
    }
}

void submit(impl::WorldItem &it, const WorldView &view,
            std::vector<DrawCmd> &out) {
    CloudState &cs = state_of(it);
    // A device-resident cloud answers its count only at the bind, so
    // an empty one here is still worth a command.
    if (!cs.external && !cs.count)
        return;

    const std::uint16_t d =
        impl::depth_key(view.world_to_view, cs.centre, view.znear);
    const PassId pass = it.ops->pass;
    const std::uint64_t key = pass == PassId::Transparent
                                  ? transparent_key(it.pipeline_id, d)
                                  : opaque_key(it.pipeline_id, it.id, d);
    out.push_back({.key = key, .seq = 0, .pass = pass, .item = &it, .part = 0});
}

// A device buffer is re-resolved every frame: the producer may have
// handed us a different one since, and its size is where the count
// comes from.
bool rebind_external(impl::WorldItem &it, CloudState &cs,
                     const WorldPipelineEntry *pe, nvrhi::IBuffer *view_cb) {
    gpud::Buffer *b = cs.src.current();
    const std::uint64_t native = b ? gpud::vulkan::native_buffer(*b) : 0;
    cs.count = b ? b->bytes() / 12 : 0;
    if (!native || !cs.count) {
        cs.bset = nullptr;
        return false;
    }
    auto wrapped = it.gpu.dev->createHandleForNativeBuffer(
        nvrhi::ObjectTypes::VK_Buffer,
        nvrhi::Object(reinterpret_cast<void *>(native)),
        nvrhi::BufferDesc()
            .setByteSize(b->bytes())
            .setStructStride(4)
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setDebugName("cloud (external)"));
    if (!wrapped)
        return false;
    cs.bset = it.gpu.dev->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, view_cb))
            .addItem(
                nvrhi::BindingSetItem::PushConstants(1, sizeof(CloudParams)))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, wrapped)),
        pe->layout);
    return cs.bset != nullptr;
}

void draw(impl::WorldItem &it, const DrawCmd &, nvrhi::ICommandList *cl,
          nvrhi::IFramebuffer *fb, const WorldView &view) {
    CloudState &cs = state_of(it);
    const WorldPipelineEntry *pe = world_pipeline_for(
        it.gpu, *it.pipelines, it.stats, it.ops, it.ops->pass, fb);
    if (!pe)
        return;

    nvrhi::IBuffer *view_cb = view.view_cb;
    if (!view_cb)
        return;
    if (cs.external) {
        if (!rebind_external(it, cs, pe, view_cb))
            return;
    } else if (!cs.bset && cs.buf) {
        cs.bset = it.gpu.dev->createBindingSet(
            nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, view_cb))
                .addItem(nvrhi::BindingSetItem::PushConstants(
                    1, sizeof(CloudParams)))
                .addItem(
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(1, cs.buf)),
            pe->layout);
    }
    if (!cs.bset || !cs.count)
        return;

    CloudParams p{};
    for (int c = 0; c < 4; ++c)
        p.color[c] = cs.color[c];
    p.radius = cs.radius;
    p.count = std::uint32_t(cs.count);
    p.mode = cs.mode;
    cl->setGraphicsState(
        nvrhi::GraphicsState()
            .setPipeline(pe->pipeline)
            .setFramebuffer(fb)
            .addBindingSet(cs.bset)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(float(view.tw), float(view.th)))));
    cl->setPushConstants(&p, sizeof p);
    // Six vertices a point, no instancing: the vertex id carries both
    // which point and which corner.
    cl->draw(
        nvrhi::DrawArguments().setVertexCount(std::uint32_t(cs.count) * 6));
    ++it.stats->draws;
}

void release(impl::WorldItem &it) {
    delete static_cast<CloudState *>(it.state);
    it.state = nullptr;
}

// One shader pair, three rows: the mode decides which pass a cloud
// draws in and how it blends, and a row is where that decision lives
// so the pipeline cache sees one (ops, pass) pair per appearance.
// clang-format off: the tables read as tables
const nvrhi::BlendState::RenderTarget kOpaqueBlend =
    nvrhi::BlendState::RenderTarget();
const nvrhi::BlendState::RenderTarget kAdditiveBlend =
    nvrhi::BlendState::RenderTarget()
        .enableBlend()
        .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
        .setDestBlend(nvrhi::BlendFactor::One)
        .setSrcBlendAlpha(nvrhi::BlendFactor::Zero)
        .setDestBlendAlpha(nvrhi::BlendFactor::One);
const nvrhi::BlendState::RenderTarget kAlphaBlend =
    nvrhi::BlendState::RenderTarget()
        .enableBlend()
        .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
        .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
        .setSrcBlendAlpha(nvrhi::BlendFactor::One)
        .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);

const WorldItemOps kCloudSolidOps{
    .name = "cloud (solid)",
    .pass = PassId::Opaque,
    .prepare = prepare,
    .submit = submit,
    .draw = draw,
    .release = release,
    .bounds = nullptr,
    .vs = {cloud_vsmain_spirv, cloud_vsmain_spirv_len, "vsmain"},
    .fs = {cloud_fsmain_spirv, cloud_fsmain_spirv_len, "fsmain"},
    .blend = kOpaqueBlend,
    .topology = nvrhi::PrimitiveType::TriangleList,
    .has_storage = true,
    .push_bytes = sizeof(CloudParams),
};

const WorldItemOps kCloudAdditiveOps{
    .name = "cloud (additive)",
    .pass = PassId::Transparent,
    .prepare = prepare,
    .submit = submit,
    .draw = draw,
    .release = release,
    .bounds = nullptr,
    .vs = {cloud_vsmain_spirv, cloud_vsmain_spirv_len, "vsmain"},
    .fs = {cloud_fsmain_spirv, cloud_fsmain_spirv_len, "fsmain"},
    .blend = kAdditiveBlend,
    .topology = nvrhi::PrimitiveType::TriangleList,
    .has_storage = true,
    .push_bytes = sizeof(CloudParams),
};

const WorldItemOps kCloudAlphaOps{
    .name = "cloud (alpha)",
    .pass = PassId::Transparent,
    .prepare = prepare,
    .submit = submit,
    .draw = draw,
    .release = release,
    .bounds = nullptr,
    .vs = {cloud_vsmain_spirv, cloud_vsmain_spirv_len, "vsmain"},
    .fs = {cloud_fsmain_spirv, cloud_fsmain_spirv_len, "fsmain"},
    .blend = kAlphaBlend,
    .topology = nvrhi::PrimitiveType::TriangleList,
    .has_storage = true,
    .push_bytes = sizeof(CloudParams),
};
// clang-format on

const WorldItemOps *ops_for(CloudMode m) {
    switch (m) {
    case CloudMode::Additive:
        return &kCloudAdditiveOps;
    case CloudMode::Alpha:
        return &kCloudAlphaOps;
    case CloudMode::Solid:
    default:
        return &kCloudSolidOps;
    }
}

} // namespace

namespace impl {

namespace {

Cloud cloud_new(World w, const CloudDesc &d, HostSource host,
                gpud::BufferSource src) {
    WorldState *ws = static_cast<WorldState *>(w.p);
    if (!ws)
        return {};
    if (!(d.radius > 0.0f))
        return set_error("a cloud needs a radius above zero — its points "
                         "are drawn as spheres in world units, not as "
                         "pixels"),
               Cloud{};

    WorldItem &it = world_item_add(*ws, ops_for(d.mode));
    CloudState *cs = new CloudState{};
    cs->host = host;
    cs->src = src;
    cs->external = bool(src);
    for (int c = 0; c < 4; ++c)
        cs->color[c] = d.color[c];
    cs->radius = d.radius;
    cs->mode = d.mode == CloudMode::Solid ? 0u : 1u;
    it.state = cs;
    return Cloud{&it};
}

} // namespace

Cloud cloud_create(World w, const CloudDesc &d) {
    return cloud_new(w, d, HostSource{}, gpud::BufferSource{});
}

Cloud cloud_from_host(World w, HostSource src, const CloudDesc &d) {
    if (!src)
        return set_error("a cloud needs a source that can answer — the "
                         "HostSource's fn is null"),
               Cloud{};
    return cloud_new(w, d, src, gpud::BufferSource{});
}

Cloud cloud_from_source(World w, gpud::BufferSource src, const CloudDesc &d) {
    if (!src)
        return set_error("a cloud needs a source that can answer — the "
                         "BufferSource's fn is null"),
               Cloud{};
    return cloud_new(w, d, HostSource{}, src);
}

bool cloud_update(Cloud c, const float *xyz, std::size_t count) {
    WorldItem *it = static_cast<WorldItem *>(c.p);
    if (!it || !it->ops || !it->state)
        return set_error("cloud_update: this is not a cloud handle"), false;
    if (!xyz && count)
        return set_error("cloud_update: null"), false;
    CloudState &cs = state_of(*it);
    if (cs.external)
        return set_error("this cloud reads a caller-owned source, "
                         "re-resolved each frame — update the producer, "
                         "not the item"),
               false;
    if (cs.host)
        return set_error("this cloud reads a Sync — publish to it, not the "
                         "item"),
               false;
    if (!upload(cs, it->gpu, xyz, count))
        return false;
    recentre(cs);
    return true;
}

} // namespace impl
} // namespace sv
