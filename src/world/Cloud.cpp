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
#include "bytecode/cloud_shadow_fsmain_spirv.h"
#include "bytecode/cloud_shadow_vsmain_spirv.h"
#include "bytecode/cloud_vsmain_spirv.h"
#include "bytecode/mesh_fsmain_spirv.h"
#include "bytecode/mesh_shadow_fsmain_spirv.h"
#include "bytecode/mesh_shadow_vsmain_spirv.h"
#include "bytecode/mesh_vsmain_spirv.h"

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
    // A second set for the shadow pass: its layout has no shadow map
    // in it, because that is the texture the pass writes.
    nvrhi::BindingSetHandle shadow_bset;
    nvrhi::IBuffer *shadow_pos = nullptr;
    const impl::WorldState::Mesh *shadow_mesh = nullptr;
    // What the set was built against. It is rebuilt when a buffer's
    // IDENTITY changes and never on a schedule: a producer may hand
    // over a different buffer at any frame, and a set built on the old
    // one samples memory that has moved on.
    nvrhi::IBuffer *bound_pos = nullptr, *bound_val = nullptr;
    nvrhi::ITexture *bound_shadow = nullptr;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float radius = 0.05f;
    std::uint32_t mode = 0;
    std::uint32_t map = 0;
    float map_scale = 1.0f;
    int shape = 0; // CloudShape
    // The shape this item needs, resolved in prepare; and the one the
    // binding set was built against, so a tier changing under it
    // rebuilds — the count decides the tier and the count moves.
    const impl::WorldState::Mesh *mesh = nullptr;
    const impl::WorldState::Mesh *bound_mesh = nullptr;
    // The centroid the sort keys on. One number for the whole cloud:
    // a cloud is one draw, so it takes one place in the order.
    impl::Vec3 centre{};
    // Where the points are, in the units they were given in — the
    // radius is added when the box is handed out, so changing the
    // radius cannot leave a stale box behind.
    //
    // `known` is false for a cloud whose points live on the device and
    // were never declared: the host has nothing to walk, and a box it
    // invented would cull geometry it cannot see.
    impl::Vec3 lo{}, hi{};
    bool known = false;
    bool declared = false; // by the caller, for data the host never sees
};

// Matches cloud.slang's CParams, and mesh.slang's MParams less the
// mode the mesh path has no use for.
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

// One walk, both summaries. The centroid places the cloud in the draw
// order and the box decides whether it is drawn at all, and walking
// the points twice to learn two things about them would be silly.
void resummarize(CloudState &cs) {
    if (cs.declared)
        return;

    impl::Vec3 sum{};
    impl::Aabb box{};
    const Channel &ch = cs.pos;
    std::size_t n = 0;
    for (std::size_t i = 0; i < ch.count && i * 3 + 2 < ch.shadow.size(); ++i) {
        const impl::Vec3 p{ch.shadow[i * 3], ch.shadow[i * 3 + 1],
                           ch.shadow[i * 3 + 2]};
        sum = sum + p;
        impl::aabb_add(box, p, p);
        ++n;
    }

    cs.centre = n ? sum * (1.0f / float(n)) : impl::Vec3{};
    cs.lo = box.lo;
    cs.hi = box.hi;
    cs.known = box.valid;
}

// The radius goes on here rather than into the stored box: a point is
// drawn as a shape AROUND it, and the shape is what has to be inside
// the frustum.
bool bounds(const impl::WorldItem &it, impl::Vec3 *lo, impl::Vec3 *hi) {
    const CloudState &cs = *static_cast<const CloudState *>(it.state);
    if (!cs.known)
        return false;
    const impl::Vec3 r{cs.radius, cs.radius, cs.radius};
    *lo = cs.lo + r * -1.0f;
    *hi = cs.hi + r;
    return true;
}

// Everything that touches memory happens here, with the frame's list
// open and no pass on it: the host's uploads, the producer's current
// buffer, and the shape this item will need. Draw only binds.
void prepare(impl::WorldItem &it, nvrhi::ICommandList *cl) {
    CloudState &cs = state_of(it);
    const bool was_dirty = cs.pos.dirty;
    channel_prepare(cs.pos, it.gpu, cl, it.stats, "cloud positions");
    channel_prepare(cs.val, it.gpu, cl, it.stats, "cloud values");
    if (cs.pos.dirty != was_dirty)
        resummarize(cs);
    channel_resolve(cs.pos, it.gpu, "cloud positions (external)");
    channel_resolve(cs.val, it.gpu, "cloud values (external)");

    // Both tiers of the shape, because WHICH one is a question about
    // the camera and the camera is not resolved yet. Making a mesh
    // needs this command list; picking one needs the view; so the
    // making happens here and the picking in submit.
    if (cs.shape != 0 && it.owner) {
        cs.mesh = world_mesh(*it.owner, cs.shape, 0, cl);
        if (cs.shape != 2)
            world_mesh(*it.owner, cs.shape, 1, cl);
    }
}

// A sphere is worth its triangles when it is big enough on screen for
// them to show, and worth nothing when it is a speck — so the tier is
// chosen from the projected RADIUS, not from how many there are. The
// count still gets a say through a triangle budget, because the two
// guard different failures: screen size alone would hand a fine mesh
// to a crowd that cannot afford one, and a count alone gives a cheap
// mesh to a dozen spheres filling the frame.
//
// The budget is measured (bench/instances.cpp: the fine tier runs at
// about half a millisecond per million triangles here, so twenty
// million is a frame's worth and the tier stops at some twenty
// thousand large spheres). The pixel threshold is read off the mesh
// instead: the cheap sphere carries twelve segments around its
// silhouette, so at six pixels of radius a segment is about three
// pixels — right where a facet starts to read as a facet.
constexpr float kFineRadiusPx = 6.0f;
constexpr std::size_t kTriangleBudget = 20u << 20;

void choose_tier(impl::WorldItem &it, CloudState &cs, const WorldView &view) {
    if (cs.shape == 0 || cs.shape == 2 || !it.owner)
        return;

    const float px = screen_radius(view, cs.centre, cs.radius);
    const impl::WorldState::Mesh *fine =
        world_mesh_ready(*it.owner, cs.shape, 1);
    const bool affordable =
        fine && cs.pos.count * fine->triangles <= kTriangleBudget;
    const int tier = px >= kFineRadiusPx && affordable ? 1 : 0;
    if (const impl::WorldState::Mesh *m =
            world_mesh_ready(*it.owner, cs.shape, tier))
        cs.mesh = m;
}

void submit(impl::WorldItem &it, const WorldView &view,
            std::vector<DrawCmd> &out) {
    CloudState &cs = state_of(it);
    if (!cs.pos.live())
        return;
    choose_tier(it, cs, view);

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

    nvrhi::IBuffer *pos = cs.pos.bound();
    if (!pos || !cs.pos.count)
        return;

    // A cloud with no values of its own binds its POSITIONS a second
    // time. One layout then serves every cloud there is, and the
    // shader never reads the slot unless a map is on.
    const bool mapped =
        cs.map != 0 && cs.val.bound() && cs.val.count >= cs.pos.count;
    nvrhi::IBuffer *val = mapped ? cs.val.bound() : pos;

    if (!cs.bset || cs.bound_pos != pos || cs.bound_val != val ||
        cs.bound_shadow != view.shadow_map) {
        cs.bset = it.gpu.dev->createBindingSet(
            nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, view.view_cb))
                .addItem(nvrhi::BindingSetItem::PushConstants(
                    1, sizeof(CloudParams)))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, pos))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, val))
                .addItem(nvrhi::BindingSetItem::Texture_SRV(kShadowMapBinding,
                                                            view.shadow_map))
                .addItem(nvrhi::BindingSetItem::Sampler(kShadowSamplerBinding,
                                                        view.shadow_sampler)),
            pe->layout);
        cs.bound_pos = pos;
        cs.bound_val = val;
        cs.bound_shadow = view.shadow_map;
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
    it.triangles = std::uint64_t(cs.pos.count) * 2;
    it.stats->triangles += it.triangles;
}

// The same item, drawn as geometry: one instance of a built-in shape
// per point, in one indexed draw. Everything before the draw call is
// the billboard path's — the same channels, the same colours, the
// same lights — which is why a shape is a field and not a kind.
void draw_mesh(impl::WorldItem &it, const DrawCmd &, nvrhi::ICommandList *cl,
               nvrhi::IFramebuffer *fb, const WorldView &view) {
    CloudState &cs = state_of(it);
    const WorldPipelineEntry *pe = world_pipeline_for(
        it.gpu, *it.pipelines, it.stats, it.ops, it.ops->pass, fb);
    if (!pe || !view.view_cb || !it.owner)
        return;

    nvrhi::IBuffer *pos = cs.pos.bound();
    const impl::WorldState::Mesh *mesh = cs.mesh;
    if (!pos || !cs.pos.count || !mesh)
        return;

    const bool mapped =
        cs.map != 0 && cs.val.bound() && cs.val.count >= cs.pos.count;
    nvrhi::IBuffer *val = mapped ? cs.val.bound() : pos;

    if (!cs.bset || cs.bound_pos != pos || cs.bound_val != val ||
        cs.bound_mesh != mesh || cs.bound_shadow != view.shadow_map) {
        cs.bset = it.gpu.dev->createBindingSet(
            nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, view.view_cb))
                .addItem(nvrhi::BindingSetItem::PushConstants(
                    1, sizeof(CloudParams)))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, pos))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, val))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    3, mesh->vertices))
                .addItem(nvrhi::BindingSetItem::Texture_SRV(kShadowMapBinding,
                                                            view.shadow_map))
                .addItem(nvrhi::BindingSetItem::Sampler(kShadowSamplerBinding,
                                                        view.shadow_sampler)),
            pe->layout);
        cs.bound_pos = pos;
        cs.bound_val = val;
        cs.bound_mesh = mesh;
        cs.bound_shadow = view.shadow_map;
    }
    if (!cs.bset)
        return;

    CloudParams p{};
    for (int c = 0; c < 4; ++c)
        p.color[c] = cs.color[c];
    p.radius = cs.radius;
    p.count = std::uint32_t(cs.pos.count);
    p.mode = cs.mode;
    p.map = mapped ? cs.map : 0u;
    p.map_scale = cs.map_scale;
    cl->setGraphicsState(
        nvrhi::GraphicsState()
            .setPipeline(pe->pipeline)
            .setFramebuffer(fb)
            .addBindingSet(cs.bset)
            .setIndexBuffer({mesh->indices, nvrhi::Format::R32_UINT, 0})
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(float(view.tw), float(view.th)))));
    cl->setPushConstants(&p, sizeof p);
    cl->drawIndexed(nvrhi::DrawArguments()
                        .setVertexCount(mesh->index_count)
                        .setInstanceCount(std::uint32_t(cs.pos.count)));
    ++it.stats->draws;
    it.triangles = std::uint64_t(cs.pos.count) * mesh->triangles;
    it.stats->triangles += it.triangles;
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

// The same item from the light's side: one pipeline, one binding set,
// depth only. It keeps its OWN set because the colour set carries the
// shadow map — which is the texture this pass is writing, and a
// descriptor cannot be both.
void draw_shadow(impl::WorldItem &it, const DrawCmd &, nvrhi::ICommandList *cl,
                 nvrhi::IFramebuffer *fb, const WorldView &view) {
    CloudState &cs = state_of(it);
    const WorldPipelineEntry *pe = world_pipeline_for(
        it.gpu, *it.pipelines, it.stats, it.ops, PassId::Shadow, fb);
    if (!pe || !view.view_cb || !view.shadow_px)
        return;

    nvrhi::IBuffer *pos = cs.pos.bound();
    if (!pos || !cs.pos.count)
        return;
    const bool geometry = cs.shape != 0;
    const impl::WorldState::Mesh *mesh = cs.mesh;
    if (geometry && !mesh)
        return;

    if (!cs.shadow_bset || cs.shadow_pos != pos ||
        cs.shadow_mesh != (geometry ? mesh : nullptr)) {
        // Every slot the layout declares, including the value buffer
        // this pass never reads: a set that skips one does not match
        // its layout, and what comes back is a null set and an item
        // that silently draws nothing.
        auto bsd =
            nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, view.view_cb))
                .addItem(nvrhi::BindingSetItem::PushConstants(
                    1, sizeof(CloudParams)))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, pos))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, pos));
        if (geometry)
            bsd.addItem(
                nvrhi::BindingSetItem::StructuredBuffer_SRV(3, mesh->vertices));
        cs.shadow_bset = it.gpu.dev->createBindingSet(bsd, pe->layout);
        cs.shadow_pos = pos;
        cs.shadow_mesh = geometry ? mesh : nullptr;
    }
    if (!cs.shadow_bset)
        return set_error("cloud: shadow binding set failed");

    CloudParams p{};
    p.radius = cs.radius;
    p.count = std::uint32_t(cs.pos.count);
    cl->setGraphicsState(
        nvrhi::GraphicsState()
            .setPipeline(pe->pipeline)
            .setFramebuffer(fb)
            .addBindingSet(cs.shadow_bset)
            .setIndexBuffer(geometry ? nvrhi::IndexBufferBinding{
                                           mesh->indices,
                                           nvrhi::Format::R32_UINT, 0}
                                     : nvrhi::IndexBufferBinding{})
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(float(view.shadow_px),
                                float(view.shadow_px)))));
    cl->setPushConstants(&p, sizeof p);
    if (geometry)
        cl->drawIndexed(nvrhi::DrawArguments()
                            .setVertexCount(mesh->index_count)
                            .setInstanceCount(std::uint32_t(cs.pos.count)));
    else
        cl->draw(nvrhi::DrawArguments().setVertexCount(
            std::uint32_t(cs.pos.count) * 6));
    ++it.stats->draws;
}

const WorldItemOps kCloudSolidOps{
    .name = "cloud (solid)",
    .pass = PassId::Opaque,
    .prepare = prepare,
    .submit = submit,
    .draw = draw,
    .release = release,
    .draw_shadow = draw_shadow,
    .shadow_vs = {cloud_shadow_vsmain_spirv, cloud_shadow_vsmain_spirv_len,
                  "vsmain"},
    .shadow_fs = {cloud_shadow_fsmain_spirv, cloud_shadow_fsmain_spirv_len,
                  "fsmain"},
    .bounds = bounds,
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
    .bounds = bounds,
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
    .bounds = bounds,
    .vs = {cloud_vsmain_spirv, cloud_vsmain_spirv_len, "vsmain"},
    .fs = {cloud_fsmain_spirv, cloud_fsmain_spirv_len, "fsmain"},
    .blend = kAlphaBlend,
    .topology = nvrhi::PrimitiveType::TriangleList,
    .storage_count = 2,
    .push_bytes = sizeof(CloudParams),
};
// clang-format on

// clang-format off: the tables read as tables
const WorldItemOps kMeshSolidOps{
    .name = "mesh (solid)",
    .pass = PassId::Opaque,
    .prepare = prepare,
    .submit = submit,
    .draw = draw_mesh,
    .release = release,
    .draw_shadow = draw_shadow,
    .shadow_vs = {mesh_shadow_vsmain_spirv, mesh_shadow_vsmain_spirv_len,
                  "vsmain"},
    .shadow_fs = {mesh_shadow_fsmain_spirv, mesh_shadow_fsmain_spirv_len,
                  "fsmain"},
    .bounds = bounds,
    .vs = {mesh_vsmain_spirv, mesh_vsmain_spirv_len, "vsmain"},
    .fs = {mesh_fsmain_spirv, mesh_fsmain_spirv_len, "fsmain"},
    .blend = kOpaqueBlend,
    .topology = nvrhi::PrimitiveType::TriangleList,
    .storage_count = 3,
    .push_bytes = sizeof(CloudParams),
};

const WorldItemOps kMeshAdditiveOps{
    .name = "mesh (additive)",
    .pass = PassId::Transparent,
    .prepare = prepare,
    .submit = submit,
    .draw = draw_mesh,
    .release = release,
    .bounds = bounds,
    .vs = {mesh_vsmain_spirv, mesh_vsmain_spirv_len, "vsmain"},
    .fs = {mesh_fsmain_spirv, mesh_fsmain_spirv_len, "fsmain"},
    .blend = kAdditiveBlend,
    .topology = nvrhi::PrimitiveType::TriangleList,
    .storage_count = 3,
    .push_bytes = sizeof(CloudParams),
};

const WorldItemOps kMeshAlphaOps{
    .name = "mesh (alpha)",
    .pass = PassId::Transparent,
    .prepare = prepare,
    .submit = submit,
    .draw = draw_mesh,
    .release = release,
    .bounds = bounds,
    .vs = {mesh_vsmain_spirv, mesh_vsmain_spirv_len, "vsmain"},
    .fs = {mesh_fsmain_spirv, mesh_fsmain_spirv_len, "fsmain"},
    .blend = kAlphaBlend,
    .topology = nvrhi::PrimitiveType::TriangleList,
    .storage_count = 3,
    .push_bytes = sizeof(CloudParams),
};
// clang-format on

// Six rows: what it is drawn AS decides the shaders and the draw,
// what it is drawn LIKE decides the pass and the blend. The pipeline
// cache sees one entry per pair, which is what the sort key groups by.
const WorldItemOps *ops_for(CloudMode m, CloudShape shape) {
    const bool mesh = shape != CloudShape::Billboard;
    switch (m) {
    case CloudMode::Additive:
        return mesh ? &kMeshAdditiveOps : &kCloudAdditiveOps;
    case CloudMode::Alpha:
        return mesh ? &kMeshAlphaOps : &kCloudAlphaOps;
    case CloudMode::Solid:
    default:
        return mesh ? &kMeshSolidOps : &kCloudSolidOps;
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

    WorldItem &it = world_item_add(*ws, ops_for(d.mode, d.shape));
    CloudState *cs = new CloudState{};
    cs->pos.host = host;
    cs->pos.src = src;
    cs->pos.external = bool(src);
    for (int c = 0; c < 4; ++c)
        cs->color[c] = d.color[c];
    cs->radius = d.radius;
    cs->mode = d.mode == CloudMode::Solid ? 0u : 1u;
    cs->shape = int(d.shape);
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
    resummarize(*cs);
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
