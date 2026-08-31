// The Cloud item, whole: state, push block, shaders, three ops rows,
// and the exported functions. Same shape as the 2D Particles kind,
// because it answers the same three doors — a caller's Update, a Sync
// it pulls from, or a device buffer it re-resolves every frame.
//
// What differs is only what 3D forces: positions are xyz triples, the
// radius is in world units, the mode picks which pass the item draws
// in, and a cloud may carry a SECOND channel of per-point values for a
// colormap to read. Both channels are one `Channel` with one set of
// doors, because they are the same problem twice.

#include "World.h"

#include "../core/Error.h"
#include "bytecode/cloud_fsmain_spirv.h"
#include "bytecode/cloud_vsmain_spirv.h"

#include <gpud/Vulkan.h>

#include <string>
#include <vector>

namespace sv {
namespace {

struct Channel {
    nvrhi::BufferHandle buf;     // owned, when the host writes it
    nvrhi::BufferHandle wrapped; // the producer's, re-resolved per frame
    std::vector<float> shadow;
    std::size_t count = 0;    // points the host last wrote
    std::size_t capacity = 0; // points the buffer holds
    bool dirty = false;
    bool external = false;
    gpud::BufferSource src{};
    HostSource host{};
    std::uint64_t host_gen = 0;

    bool live() const { return external || host || count; }
    nvrhi::IBuffer *bound() const {
        return external ? wrapped.Get() : buf.Get();
    }
};

struct CloudState {
    Channel pos;
    Channel val; // the colour source; idle until a map wants one
    nvrhi::BindingSetHandle bset;
    // What the set was built against. It is rebuilt when a buffer's
    // IDENTITY changes and never on a schedule: a producer may hand
    // over a different buffer at any frame, and a set built on the old
    // one samples memory that has moved on.
    nvrhi::IBuffer *bound_pos = nullptr, *bound_val = nullptr;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float radius = 0.05f;
    std::uint32_t mode = 0;
    std::uint32_t map = 0;
    float map_scale = 1.0f;
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
    std::uint32_t map;
    float map_scale;
    float pad0, pad1, pad2;
};

CloudState &state_of(impl::WorldItem &it) {
    return *static_cast<CloudState *>(it.state);
}

// A channel is xyz triples: twelve bytes a point, whichever source
// they came from, so the byte count IS the point count.
bool channel_upload(Channel &ch, const impl::Gpu &gpu, const char *name,
                    const float *xyz, std::size_t count) {
    if (!count) {
        ch.count = 0; // an empty channel is not an error
        return true;
    }
    if (count > ch.capacity) {
        ch.buf = gpu.dev->createBuffer(
            nvrhi::BufferDesc()
                .setByteSize(count * 12)
                .setStructStride(4)
                .setInitialState(nvrhi::ResourceStates::ShaderResource)
                .setKeepInitialState(true)
                .setDebugName(name));
        if (!ch.buf) {
            ch.capacity = ch.count = 0;
            return set_error(std::string(name) + ": buffer creation failed"),
                   false;
        }
        ch.capacity = count;
    }
    ch.shadow.assign(xyz, xyz + count * 3);
    ch.count = count;
    ch.dirty = true;
    return true;
}

void channel_pull_host(Channel &ch, const impl::Gpu &gpu, const char *name) {
    std::size_t bytes = 0;
    std::uint64_t gen = 0;
    const void *data = ch.host.fn(ch.host.user, &bytes, &gen);
    if (!data || gen == ch.host_gen)
        return;
    ch.host_gen = gen;
    if (bytes % 12)
        return set_error(std::string(name) + " are xyz triples, and " +
                         std::to_string(bytes / 4) +
                         " floats were published — not a multiple of three");
    channel_upload(ch, gpu, name, static_cast<const float *>(data), bytes / 12);
}

void channel_prepare(Channel &ch, const impl::Gpu &gpu, nvrhi::ICommandList *cl,
                     Stats *stats, const char *name) {
    if (ch.host)
        channel_pull_host(ch, gpu, name);
    if (ch.dirty && !ch.external && ch.buf && ch.count) {
        cl->writeBuffer(ch.buf, ch.shadow.data(), ch.count * 12);
        ch.dirty = false;
        ++stats->uploads;
    }
}

// Ask the producer where its data is NOW and wrap it. The size is
// where the count comes from, so nothing sits between asking and using.
void channel_resolve(Channel &ch, const impl::Gpu &gpu, const char *name) {
    if (!ch.external)
        return;
    gpud::Buffer *b = ch.src.current();
    const std::uint64_t native = b ? gpud::vulkan::native_buffer(*b) : 0;
    ch.count = b ? b->bytes() / 12 : 0;
    if (!native || !ch.count) {
        ch.wrapped = nullptr;
        return;
    }
    ch.wrapped = gpu.dev->createHandleForNativeBuffer(
        nvrhi::ObjectTypes::VK_Buffer,
        nvrhi::Object(reinterpret_cast<void *>(native)),
        nvrhi::BufferDesc()
            .setByteSize(b->bytes())
            .setStructStride(4)
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setDebugName(name));
}

void recentre(CloudState &cs) {
    impl::Vec3 sum{};
    const Channel &ch = cs.pos;
    for (std::size_t i = 0; i < ch.count && i * 3 + 2 < ch.shadow.size(); ++i)
        sum = sum + impl::Vec3{ch.shadow[i * 3], ch.shadow[i * 3 + 1],
                               ch.shadow[i * 3 + 2]};
    cs.centre = ch.count ? sum * (1.0f / float(ch.count)) : impl::Vec3{};
}

void prepare(impl::WorldItem &it, nvrhi::ICommandList *cl) {
    CloudState &cs = state_of(it);
    const bool was_dirty = cs.pos.dirty;
    channel_prepare(cs.pos, it.gpu, cl, it.stats, "cloud positions");
    channel_prepare(cs.val, it.gpu, cl, it.stats, "cloud values");
    if (cs.pos.dirty != was_dirty)
        recentre(cs);
}

void submit(impl::WorldItem &it, const WorldView &view,
            std::vector<DrawCmd> &out) {
    CloudState &cs = state_of(it);
    if (!cs.pos.live())
        return;

    const std::uint16_t d = impl::depth_key(view.world_to_clip, cs.centre);
    const PassId pass = it.ops->pass;
    const std::uint64_t key = pass == PassId::Transparent
                                  ? transparent_key(it.pipeline_id, d)
                                  : opaque_key(it.pipeline_id, it.id, d);
    out.push_back({.key = key, .seq = 0, .pass = pass, .item = &it, .part = 0});
}

void draw(impl::WorldItem &it, const DrawCmd &, nvrhi::ICommandList *cl,
          nvrhi::IFramebuffer *fb, const WorldView &view) {
    CloudState &cs = state_of(it);
    const WorldPipelineEntry *pe = world_pipeline_for(
        it.gpu, *it.pipelines, it.stats, it.ops, it.ops->pass, fb);
    if (!pe || !view.view_cb)
        return;

    channel_resolve(cs.pos, it.gpu, "cloud positions (external)");
    channel_resolve(cs.val, it.gpu, "cloud values (external)");
    nvrhi::IBuffer *pos = cs.pos.bound();
    if (!pos || !cs.pos.count)
        return;

    // A cloud with no values of its own binds its POSITIONS a second
    // time. One layout then serves every cloud there is, and the
    // shader never reads the slot unless a map is on.
    const bool mapped =
        cs.map != 0 && cs.val.bound() && cs.val.count >= cs.pos.count;
    nvrhi::IBuffer *val = mapped ? cs.val.bound() : pos;

    if (!cs.bset || cs.bound_pos != pos || cs.bound_val != val) {
        cs.bset = it.gpu.dev->createBindingSet(
            nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, view.view_cb))
                .addItem(nvrhi::BindingSetItem::PushConstants(
                    1, sizeof(CloudParams)))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, pos))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, val)),
            pe->layout);
        cs.bound_pos = pos;
        cs.bound_val = val;
    }
    if (!cs.bset)
        return;

    CloudParams p{};
    for (int c = 0; c < 4; ++c)
        p.color[c] = cs.color[c];
    p.radius = cs.radius;
    p.count = std::uint32_t(cs.pos.count);
    p.mode = cs.mode;
    // Fewer values than points would read past the end of the value
    // buffer, so the map waits for the two to agree rather than the
    // shader reading whatever follows.
    p.map = mapped ? cs.map : 0u;
    p.map_scale = cs.map_scale;
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
        nvrhi::DrawArguments().setVertexCount(std::uint32_t(cs.pos.count) * 6));
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
    .storage_count = 2,
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
    .storage_count = 2,
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
    .storage_count = 2,
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
    cs->pos.host = host;
    cs->pos.src = src;
    cs->pos.external = bool(src);
    for (int c = 0; c < 4; ++c)
        cs->color[c] = d.color[c];
    cs->radius = d.radius;
    cs->mode = d.mode == CloudMode::Solid ? 0u : 1u;
    cs->map = std::uint32_t(d.map);
    cs->map_scale = d.map_scale > 0.0f ? d.map_scale : 1.0f;
    it.state = cs;
    return Cloud{&it};
}

CloudState *state_or_error(Cloud c, const char *what) {
    WorldItem *it = static_cast<WorldItem *>(c.p);
    if (!it || !it->ops || !it->state)
        return set_error(std::string(what) + ": this is not a cloud handle"),
               nullptr;
    return static_cast<CloudState *>(it->state);
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
    CloudState *cs = state_or_error(c, "cloud_update");
    if (!cs)
        return false;
    if (!xyz && count)
        return set_error("cloud_update: null"), false;
    if (cs->pos.external)
        return set_error("this cloud reads a caller-owned source, "
                         "re-resolved each frame — update the producer, "
                         "not the item"),
               false;
    if (cs->pos.host)
        return set_error("this cloud reads a Sync — publish to it, not the "
                         "item"),
               false;
    WorldItem *it = static_cast<WorldItem *>(c.p);
    if (!channel_upload(cs->pos, it->gpu, "cloud positions", xyz, count))
        return false;
    recentre(*cs);
    return true;
}

bool cloud_update_values(Cloud c, const float *xyz, std::size_t count) {
    CloudState *cs = state_or_error(c, "cloud_update_values");
    if (!cs)
        return false;
    if (!xyz && count)
        return set_error("cloud_update_values: null"), false;
    if (cs->val.external || cs->val.host)
        return set_error("this cloud's colours read a source — update the "
                         "producer, not the item"),
               false;
    WorldItem *it = static_cast<WorldItem *>(c.p);
    return channel_upload(cs->val, it->gpu, "cloud values", xyz, count);
}

bool cloud_values_from_host(Cloud c, HostSource src) {
    CloudState *cs = state_or_error(c, "cloud values");
    if (!cs)
        return false;
    if (!src)
        return set_error("a cloud's colours need a source that can answer — "
                         "the HostSource's fn is null"),
               false;
    cs->val.host = src;
    cs->val.external = false;
    return true;
}

bool cloud_values_from_source(Cloud c, gpud::BufferSource src) {
    CloudState *cs = state_or_error(c, "cloud values");
    if (!cs)
        return false;
    if (!src)
        return set_error("a cloud's colours need a source that can answer — "
                         "the BufferSource's fn is null"),
               false;
    cs->val.src = src;
    cs->val.external = true;
    return true;
}

// The world an item belongs to: a Sync handed to the VALUE channel
// must be tracked exactly as one handed to the positions is, and the
// sugar has only the cloud in hand when it does that.
World cloud_world(Cloud c) {
    WorldItem *it = static_cast<WorldItem *>(c.p);
    return World{it ? it->owner : nullptr};
}

} // namespace impl
} // namespace sv
