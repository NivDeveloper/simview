// The wire item, whole: segments over a point set as thin quads of a
// pixel width. Positions are a channel like a cloud's, the edges two
// indices each and fixed, and a mask of one float an edge hides some.

#include "World.h"

#include "../core/Error.h"
#include "Channel.h"
#include "bytecode/wire_fsmain_spirv.h"
#include "bytecode/wire_vsmain_spirv.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sv {
namespace {

struct WireState {
    impl::Channel pos;   // xyz per point
    impl::Channel alive; // one float an edge; idle until a mask is given
    std::vector<std::uint32_t> pairs;
    nvrhi::BufferHandle edges;
    std::size_t count = 0; // edges
    std::uint32_t max_index = 0;
    bool edges_dirty = true;
    bool complained = false;
    nvrhi::BindingSetHandle bset;
    nvrhi::IBuffer *bound_pos = nullptr, *bound_alive = nullptr;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float width = 1.5f;
    impl::Vec3 centre{}, lo{}, hi{};
    bool known = false;
};

// Matches wire.slang's WParams.
struct WireParams {
    float color[4];
    float width;
    std::uint32_t count;
    std::uint32_t masked;
    std::uint32_t highlight;
};

constexpr std::uint32_t kNoHighlight = ~std::uint32_t(0);

WireState &state_of(impl::WorldItem &it) {
    return *static_cast<WireState *>(it.state);
}

std::uint32_t highlight_of(const impl::WorldItem &it) {
    const impl::WorldState *w = it.owner;
    if (!w)
        return kNoHighlight;
    if (w->hovered == &it)
        return w->hover_index;
    if (w->followed == &it)
        return w->follow_index;
    return kNoHighlight;
}

void resummarize(WireState &ws) {
    impl::Vec3 sum{};
    impl::Aabb box{};
    const impl::Channel &ch = ws.pos;
    std::size_t n = 0;
    for (std::size_t k = 0; k < ch.count && k * 3 + 2 < ch.shadow.size(); ++k) {
        const impl::Vec3 p{ch.shadow[k * 3], ch.shadow[k * 3 + 1],
                           ch.shadow[k * 3 + 2]};
        sum = sum + p;
        impl::aabb_add(box, p, p);
        ++n;
    }
    ws.centre = n ? sum * (1.0f / float(n)) : impl::Vec3{};
    ws.lo = box.lo;
    ws.hi = box.hi;
    ws.known = box.valid;
}

bool bounds(const impl::WorldItem &it, impl::Vec3 *lo, impl::Vec3 *hi) {
    const WireState &ws = *static_cast<const WireState *>(it.state);
    if (!ws.known)
        return false;
    *lo = ws.lo;
    *hi = ws.hi;
    return true;
}

// The nearest edge the ray passes within the click's slop of, over the
// host's copies; a masked-out edge is not there to be picked.
bool pick(const impl::WorldItem &it, const PickQuery &pq, PickHit *out) {
    const WireState &ws = *static_cast<const WireState *>(it.state);
    const std::vector<float> &s = ws.pos.shadow;
    const std::size_t np = std::min(ws.pos.count, s.size() / 3);
    const std::vector<float> &m = ws.alive.shadow;
    const bool masked = ws.alive.count >= ws.count && m.size() >= ws.count;
    if (!(pq.focal_px > 0.0f))
        return false;
    const float half = std::max(ws.width * 0.5f, pq.slop_px) / pq.focal_px;
    const impl::Ray &ray = pq.ray;
    bool any = false;
    for (std::size_t k = 0; k < ws.count; ++k) {
        if (masked && m[k] <= 0.0f)
            continue;
        const std::uint32_t a = ws.pairs[k * 2], b = ws.pairs[k * 2 + 1];
        if (a >= np || b >= np)
            continue;
        const impl::Vec3 p{s[a * 3], s[a * 3 + 1], s[a * 3 + 2]};
        const impl::Vec3 q{s[b * 3], s[b * 3 + 1], s[b * 3 + 2]};
        // The closest approach of the ray and the segment.
        const impl::Vec3 u = q - p, w0 = p - ray.o;
        const float bb = impl::dot(ray.d, u), c = impl::dot(u, u);
        const float dd = impl::dot(ray.d, w0), e = impl::dot(u, w0);
        const float denom = c - bb * bb;
        const float sp = std::clamp(
            denom > 1e-12f ? (bb * dd - e) / denom : 0.0f, 0.0f, 1.0f);
        const float t = std::max(0.0f, dd + sp * bb);
        const impl::Vec3 on = p + u * sp;
        const impl::Vec3 gap = on - (ray.o + ray.d * t);
        const float r = half * (pq.orthographic ? 1.0f : t);
        if (impl::dot(gap, gap) > r * r)
            continue;
        if (!any || t < out->t) {
            out->t = t;
            out->index = std::int32_t(k);
            out->point = on;
            any = true;
        }
    }
    return any;
}

bool locate(const impl::WorldItem &it, std::uint32_t k, impl::Vec3 *p) {
    const WireState &ws = *static_cast<const WireState *>(it.state);
    const std::vector<float> &s = ws.pos.shadow;
    if (k >= ws.count)
        return false;
    const std::uint32_t a = ws.pairs[k * 2], b = ws.pairs[k * 2 + 1];
    if (std::size_t(a) * 3 + 2 >= s.size() ||
        std::size_t(b) * 3 + 2 >= s.size())
        return false;
    *p = impl::Vec3{s[a * 3] + s[b * 3], s[a * 3 + 1] + s[b * 3 + 1],
                    s[a * 3 + 2] + s[b * 3 + 2]} *
         0.5f;
    return true;
}

void prepare(impl::WorldItem &it, nvrhi::ICommandList *cl) {
    WireState &ws = state_of(it);
    const bool moved =
        impl::channel_prepare(ws.pos, it.gpu, cl, it.stats, "wire positions");
    impl::channel_prepare(ws.alive, it.gpu, cl, it.stats, "wire mask");
    if (moved)
        resummarize(ws);
    impl::channel_resolve(ws.pos, it.gpu, "wire positions (external)");
    impl::channel_resolve(ws.alive, it.gpu, "wire mask (external)");
    if (it.owner && it.owner->want_host) {
        impl::channel_readback(ws.pos, it, cl, "wire positions (readback)");
        impl::channel_readback(ws.alive, it, cl, "wire mask (readback)");
    }

    // The edges are fixed, so they go up once, on the frame's own list.
    if (ws.edges_dirty && ws.count) {
        ws.edges = it.gpu.dev->createBuffer(
            nvrhi::BufferDesc()
                .setByteSize(ws.count * 2 * sizeof(std::uint32_t))
                .setStructStride(4)
                .setInitialState(nvrhi::ResourceStates::ShaderResource)
                .setKeepInitialState(true)
                .setDebugName("wire edges"));
        if (!ws.edges)
            return set_error("wire edges: buffer creation failed");
        cl->writeBuffer(ws.edges, ws.pairs.data(),
                        ws.count * 2 * sizeof(std::uint32_t));
        ws.bset = nullptr;
        ws.edges_dirty = false;
        ++it.stats->uploads;
    }
}

void submit(impl::WorldItem &it, const WorldView &view,
            std::vector<DrawCmd> &out) {
    WireState &ws = state_of(it);
    if (!ws.pos.live() || !ws.count)
        return;
    const std::uint16_t d = impl::depth_key(view.world_to_clip, ws.centre);
    const PassId pass = it.ops->pass;
    const std::uint64_t key = pass == PassId::Transparent
                                  ? impl::transparent_key(it.pipeline_id, d)
                                  : impl::opaque_key(it.pipeline_id, it.id, d);
    out.push_back({.key = key, .seq = 0, .pass = pass, .item = &it, .part = 0});
}

void draw(impl::WorldItem &it, const DrawCmd &, nvrhi::ICommandList *cl,
          nvrhi::IFramebuffer *fb, const WorldView &view) {
    WireState &ws = state_of(it);
    const WorldPipelineEntry *pe = world_pipeline_for(
        it.gpu, *it.pipelines, it.stats, it.ops, it.ops->pass, fb);
    if (!pe || !view.view_cb)
        return;

    nvrhi::IBuffer *pos = ws.pos.bound();
    if (!pos || !ws.pos.count || !ws.edges || !ws.count)
        return;
    // An edge naming a point past the end is drawn as nothing, and said
    // once: the shader must not read past the buffer.
    if (ws.max_index >= ws.pos.count) {
        if (!ws.complained)
            set_error("a wire's edge names point " +
                      std::to_string(ws.max_index) + " of " +
                      std::to_string(ws.pos.count));
        ws.complained = true;
        return;
    }

    const bool masked = ws.alive.bound() && ws.alive.count >= ws.count;
    nvrhi::IBuffer *alive = masked ? ws.alive.bound() : pos;
    if (!ws.bset || ws.bound_pos != pos || ws.bound_alive != alive) {
        ws.bset = it.gpu.dev->createBindingSet(
            nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, view.view_cb))
                .addItem(
                    nvrhi::BindingSetItem::PushConstants(1, sizeof(WireParams)))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, pos))
                .addItem(
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(2, ws.edges))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, alive)),
            pe->layout);
        ws.bound_pos = pos;
        ws.bound_alive = alive;
    }
    if (!ws.bset)
        return;

    WireParams p{};
    for (int c = 0; c < 4; ++c)
        p.color[c] = ws.color[c];
    p.width = ws.width;
    p.count = std::uint32_t(ws.count);
    p.masked = masked ? 1u : 0u;
    p.highlight = highlight_of(it);
    cl->setGraphicsState(
        nvrhi::GraphicsState()
            .setPipeline(pe->pipeline)
            .setFramebuffer(fb)
            .addBindingSet(ws.bset)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(float(view.tw), float(view.th)))));
    cl->setPushConstants(&p, sizeof p);
    cl->draw(
        nvrhi::DrawArguments().setVertexCount(std::uint32_t(ws.count) * 6));
    ++it.stats->draws;
    it.triangles = std::uint64_t(ws.count) * 2;
    it.stats->triangles += it.triangles;
}

void release(impl::WorldItem &it) {
    delete static_cast<WireState *>(it.state);
    it.state = nullptr;
}

const nvrhi::BlendState::RenderTarget kWireBlend =
    nvrhi::BlendState::RenderTarget()
        .enableBlend()
        .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
        .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
        .setSrcBlendAlpha(nvrhi::BlendFactor::One)
        .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
const nvrhi::BlendState::RenderTarget kWireAdditive =
    nvrhi::BlendState::RenderTarget()
        .enableBlend()
        .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
        .setDestBlend(nvrhi::BlendFactor::One)
        .setSrcBlendAlpha(nvrhi::BlendFactor::Zero)
        .setDestBlendAlpha(nvrhi::BlendFactor::One);

// clang-format off: the tables read as tables
const WorldItemOps kWireSolidOps{
    .name = "wire (solid)",
    .pass = PassId::Opaque,
    .prepare = prepare,
    .submit = submit,
    .draw = draw,
    .release = release,
    .bounds = bounds,
    .pick = pick,
    .locate = locate,
    .vs = {wire_vsmain_spirv, wire_vsmain_spirv_len, "vsmain"},
    .fs = {wire_fsmain_spirv, wire_fsmain_spirv_len, "fsmain"},
    .blend = kWireBlend,
    .topology = nvrhi::PrimitiveType::TriangleList,
    .storage_count = 3,
    .push_bytes = sizeof(WireParams),
};

const WorldItemOps kWireAdditiveOps{
    .name = "wire (additive)",
    .pass = PassId::Transparent,
    .prepare = prepare,
    .submit = submit,
    .draw = draw,
    .release = release,
    .bounds = bounds,
    .pick = pick,
    .locate = locate,
    .vs = {wire_vsmain_spirv, wire_vsmain_spirv_len, "vsmain"},
    .fs = {wire_fsmain_spirv, wire_fsmain_spirv_len, "fsmain"},
    .blend = kWireAdditive,
    .topology = nvrhi::PrimitiveType::TriangleList,
    .storage_count = 3,
    .push_bytes = sizeof(WireParams),
};

const WorldItemOps kWireAlphaOps{
    .name = "wire (alpha)",
    .pass = PassId::Transparent,
    .prepare = prepare,
    .submit = submit,
    .draw = draw,
    .release = release,
    .bounds = bounds,
    .pick = pick,
    .locate = locate,
    .vs = {wire_vsmain_spirv, wire_vsmain_spirv_len, "vsmain"},
    .fs = {wire_fsmain_spirv, wire_fsmain_spirv_len, "fsmain"},
    .blend = kWireBlend,
    .topology = nvrhi::PrimitiveType::TriangleList,
    .storage_count = 3,
    .push_bytes = sizeof(WireParams),
};
// clang-format on

const WorldItemOps *ops_for(CloudMode m) {
    switch (m) {
    case CloudMode::Additive:
        return &kWireAdditiveOps;
    case CloudMode::Alpha:
        return &kWireAlphaOps;
    case CloudMode::Solid:
    default:
        return &kWireSolidOps;
    }
}

} // namespace

namespace impl {

namespace {

Wire wire_new(World w, const WireDesc &d, const std::uint32_t *edges,
              std::size_t count, HostSource host, gpud::BufferSource src) {
    WorldState *ws = static_cast<WorldState *>(w.p);
    if (!ws)
        return {};
    if (!(d.width > 0.0f))
        return set_error("a wire needs a width above zero — its edges are "
                         "drawn that many pixels across"),
               Wire{};
    if (count && !edges)
        return set_error("a wire's edges are null"), Wire{};

    WorldItem &it = world_item_add(*ws, ops_for(d.mode));
    WireState *st = new WireState{};
    st->pos.host = host;
    st->pos.src = src;
    st->pos.external = bool(src);
    st->alive.width = 1;
    st->pairs.assign(edges, edges + count * 2);
    st->count = count;
    for (std::uint32_t k : st->pairs)
        st->max_index = std::max(st->max_index, k);
    for (int c = 0; c < 4; ++c)
        st->color[c] = d.color[c];
    st->width = d.width;
    it.state = st;
    return Wire{&it};
}

WireState *state_or_error(Wire w, const char *what) {
    WorldItem *it = static_cast<WorldItem *>(w.p);
    if (!it || !it->ops || !it->state)
        return set_error(std::string(what) + ": this is not a wire handle"),
               nullptr;
    return static_cast<WireState *>(it->state);
}

} // namespace

Wire wire_create(World w, const std::uint32_t *edges, std::size_t count,
                 const WireDesc &d) {
    return wire_new(w, d, edges, count, HostSource{}, gpud::BufferSource{});
}

Wire wire_from_host(World w, HostSource src, const std::uint32_t *edges,
                    std::size_t count, const WireDesc &d) {
    if (!src)
        return set_error("a wire needs a source that can answer — the "
                         "HostSource's fn is null"),
               Wire{};
    return wire_new(w, d, edges, count, src, gpud::BufferSource{});
}

Wire wire_from_source(World w, gpud::BufferSource src,
                      const std::uint32_t *edges, std::size_t count,
                      const WireDesc &d) {
    if (!src)
        return set_error("a wire needs a source that can answer — the "
                         "BufferSource's fn is null"),
               Wire{};
    return wire_new(w, d, edges, count, HostSource{}, src);
}

bool wire_update(Wire w, const float *xyz, std::size_t count) {
    WireState *ws = state_or_error(w, "wire_update");
    if (!ws)
        return false;
    if (!xyz && count)
        return set_error("wire_update: null"), false;
    if (ws->pos.external)
        return set_error("this wire reads a caller-owned source, re-resolved "
                         "each frame — update the producer, not the item"),
               false;
    if (ws->pos.host)
        return set_error("this wire reads a Sync — publish to it, not the "
                         "item"),
               false;
    WorldItem *it = static_cast<WorldItem *>(w.p);
    if (!channel_upload(ws->pos, it->gpu, "wire positions", xyz, count))
        return false;
    resummarize(*ws);
    return true;
}

bool wire_update_mask(Wire w, const float *alive, std::size_t count) {
    WireState *ws = state_or_error(w, "wire_update_mask");
    if (!ws)
        return false;
    if (!alive && count)
        return set_error("wire_update_mask: null"), false;
    if (ws->alive.external || ws->alive.host)
        return set_error("this wire's mask reads a source — update the "
                         "producer, not the item"),
               false;
    WorldItem *it = static_cast<WorldItem *>(w.p);
    return channel_upload(ws->alive, it->gpu, "wire mask", alive, count);
}

bool wire_mask_from_host(Wire w, HostSource src) {
    WireState *ws = state_or_error(w, "wire mask");
    if (!ws)
        return false;
    if (!src)
        return set_error("a wire's mask needs a source that can answer — "
                         "the HostSource's fn is null"),
               false;
    ws->alive.host = src;
    ws->alive.external = false;
    return true;
}

bool wire_mask_from_source(Wire w, gpud::BufferSource src) {
    WireState *ws = state_or_error(w, "wire mask");
    if (!ws)
        return false;
    if (!src)
        return set_error("a wire's mask needs a source that can answer — "
                         "the BufferSource's fn is null"),
               false;
    ws->alive.src = src;
    ws->alive.external = true;
    return true;
}

World wire_world(Wire w) {
    WorldItem *it = static_cast<WorldItem *>(w.p);
    return World{it ? it->owner : nullptr};
}

} // namespace impl
} // namespace sv
