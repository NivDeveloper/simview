#include "World.h"

#include "../core/Error.h"
#include "../platform/Device.h"
#include "bytecode/readback_main_spirv.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace sv {
namespace {

// MUST MATCH shaders/world_view.slang, field for field. Matrices
// first: they are 16-byte aligned by construction, and the trailing
// float4s pack behind them without a hole.
struct ViewConstants {
    float world_to_clip[16];
    float world_to_view[16];
    float view_to_clip[16];
    float clip_to_world[16];
    float camera_pos[4];
    float viewport[4];
    float depth[4];
    float light_dir[4][4];
    float light_rgb[4][4];
    float ambient[4];
};

void copy_mat(float (&dst)[16], const impl::Mat4 &m) {
    std::memcpy(dst, m.m, sizeof m.m);
}

// Collected once, after prepare: an item's extent is only known
// once its data has landed.
impl::Aabb scene_bounds(const impl::WorldState &w) {
    impl::Aabb b{};
    for (const impl::WorldItem &it : w.items) {
        impl::Vec3 lo{}, hi{};
        if (it.ops && it.ops->bounds && it.ops->bounds(it, &lo, &hi))
            impl::aabb_add(b, lo, hi);
    }
    return b;
}

// The camera, resolved against the target this frame is drawn into
// and against what is in front of it.
WorldView view_of(const impl::WorldState &w, std::uint32_t tw, std::uint32_t th,
                  const impl::Aabb &scene) {
    const float aspect = th ? float(tw) / float(th) : 1.0f;
    const float znear = w.camera.znear(scene);
    const impl::Mat4 v = w.camera.view();
    const impl::Mat4 p = w.camera.proj(aspect, znear);
    const impl::Mat4 vp = p * v;
    return {.world_to_clip = vp,
            .world_to_view = v,
            .view_to_clip = p,
            .camera_pos = w.camera.position(),
            .znear = znear,
            .distance = w.camera.distance(),
            .tw = tw,
            .th = th,
            .focal_px = w.camera.focal_px(th),
            .orthographic = w.camera.orthographic(),
            .frustum = impl::frustum_of(vp)};
}

bool write_view_cb(impl::WorldState &w, nvrhi::ICommandList *cl,
                   const WorldView &view) {
    if (!w.view_cb) {
        // Volatile: the renderer versions it, so a binding set made
        // once stays valid. One write per world per frame.
        w.view_cb =
            w.gpu.dev->createBuffer(nvrhi::BufferDesc()
                                        .setByteSize(sizeof(ViewConstants))
                                        .setIsConstantBuffer(true)
                                        .setIsVolatile(true)
                                        .setMaxVersions(16)
                                        .setDebugName("world view constants"));
        if (!w.view_cb)
            return set_error("world: view constant buffer creation failed"),
                   false;
    }

    ViewConstants c{};
    copy_mat(c.world_to_clip, view.world_to_clip);
    copy_mat(c.world_to_view, view.world_to_view);
    copy_mat(c.view_to_clip, view.view_to_clip);
    copy_mat(c.clip_to_world, impl::mat_inverse(view.world_to_clip));
    c.camera_pos[0] = view.camera_pos.x;
    c.camera_pos[1] = view.camera_pos.y;
    c.camera_pos[2] = view.camera_pos.z;
    c.viewport[0] = float(view.tw);
    c.viewport[1] = float(view.th);
    c.viewport[2] = view.tw ? 1.0f / float(view.tw) : 0.0f;
    c.viewport[3] = view.th ? 1.0f / float(view.th) : 0.0f;
    c.depth[0] = view.znear;
    c.depth[1] = w.camera.zfar();
    c.depth[2] = w.camera.orthographic() ? 1.0f : 0.0f;

    // VIEW space, where an impostor knows its own normal: one
    // transform a frame instead of a normal matrix per shader.
    const std::size_t n = w.lights.size() < 4 ? w.lights.size() : 4;
    if (n == 0) {
        c.light_dir[0][2] = 1.0f;
        c.light_dir[0][3] = 0.7f;
        for (int k = 0; k < 3; ++k)
            c.light_rgb[0][k] = 1.0f;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const impl::Vec3 d = impl::normalize(impl::conjugate(w.camera.pose()) *
                                             w.lights[i].direction);
        c.light_dir[i][0] = d.x;
        c.light_dir[i][1] = d.y;
        c.light_dir[i][2] = d.z;
        c.light_dir[i][3] = w.lights[i].intensity;
        for (int k = 0; k < 3; ++k)
            c.light_rgb[i][k] = w.lights[i].color[k];
    }
    for (int k = 0; k < 3; ++k)
        c.ambient[k] = w.ambient[k];
    c.ambient[3] = float(n ? n : 1);
    cl->writeBuffer(w.view_cb, &c, sizeof c);
    return true;
}

} // namespace

impl::WorldItem &world_item_add(impl::WorldState &w, const WorldItemOps *ops) {
    impl::WorldItem &it = w.items.emplace_back();
    it.owner = &w;
    it.gpu = w.gpu;
    it.stats = w.stats;
    it.pipelines = w.pipelines;
    it.ops = ops;
    it.id = std::uint32_t(w.items.size());
    it.pipeline_id = std::uint32_t(w.ops_seen.size());
    for (std::size_t i = 0; i < w.ops_seen.size(); ++i)
        if (w.ops_seen[i] == ops) {
            it.pipeline_id = std::uint32_t(i);
            break;
        }
    if (it.pipeline_id == w.ops_seen.size())
        w.ops_seen.push_back(ops);
    return it;
}

void world_draw(impl::WorldState &w, impl::Platform &pl,
                nvrhi::ICommandList *cl, impl::RenderTarget &t) {
    if (t.fb && t.depth)
        world_draw_into(w, pl, cl, t.fb, t.w, t.h);
}

namespace {

// The multisampled pair the world draws into, made to fit the target
// it will be resolved onto. Recreated only when the size changes: it
// is the largest allocation a world holds.
bool ensure_msaa(impl::WorldState &w, nvrhi::IFramebuffer *fb, std::uint32_t tw,
                 std::uint32_t th) {
    if (w.samples <= 1)
        return false;
    if (w.ms_fb && w.ms_w == tw && w.ms_h == th)
        return true;

    w.ms_fb = nullptr;
    w.ms_color = nullptr;
    w.ms_depth = nullptr;
    const auto &fbd = fb->getDesc();
    const auto make = [&](nvrhi::Format f, bool color, const char *name) {
        // A multisampled texture is its OWN dimension, not a 2D one
        // that happens to carry samples — the renderer refuses the
        // second spelling by name.
        auto d = nvrhi::TextureDesc()
                     .setWidth(tw)
                     .setHeight(th)
                     .setFormat(f)
                     .setDimension(nvrhi::TextureDimension::Texture2DMS)
                     .setSampleCount(w.samples)
                     .setIsRenderTarget(true)
                     .setKeepInitialState(true)
                     .setDebugName(name);
        d.initialState = color ? nvrhi::ResourceStates::RenderTarget
                               : nvrhi::ResourceStates::DepthWrite;
        return w.gpu.dev->createTexture(d);
    };
    w.ms_color = make(fbd.colorAttachments[0].texture->getDesc().format, true,
                      "world colour (multisampled)");
    w.ms_depth = make(fbd.depthAttachment.texture->getDesc().format, false,
                      "world depth (multisampled)");
    if (w.ms_color && w.ms_depth)
        w.ms_fb =
            w.gpu.dev->createFramebuffer(nvrhi::FramebufferDesc()
                                             .addColorAttachment(w.ms_color)
                                             .setDepthAttachment(w.ms_depth));
    if (!w.ms_fb) {
        // Not fatal: the world draws unsampled rather than not at all.
        set_error("world: multisampled target creation failed — drawing "
                  "without it");
        w.samples = 1;
        return false;
    }
    w.ms_w = tw;
    w.ms_h = th;
    return true;
}

} // namespace

void world_draw_into(impl::WorldState &w, impl::Platform &pl,
                     nvrhi::ICommandList *cl, nvrhi::IFramebuffer *out,
                     std::uint32_t tw, std::uint32_t th) {
    if (!out || !out->getDesc().depthAttachment.texture || !tw || !th)
        return;

    // Everything below draws into `fb`, which is the multisampled pair
    // when there is one and the caller's target when there is not.
    const bool ms = ensure_msaa(w, out, tw, th);
    nvrhi::IFramebuffer *fb = ms ? w.ms_fb.Get() : out;
    nvrhi::ITexture *depth = fb->getDesc().depthAttachment.texture;

    w.want_host = !w.picks.empty() || w.followed || w.hovered_now;
    for (impl::WorldItem &it : w.items)
        if (it.ops && it.ops->prepare)
            it.ops->prepare(it, cl);

    // After prepare, so the element's position is this frame's. An
    // element that cannot be located any more ends the following.
    if (w.followed) {
        impl::Vec3 p{};
        if (w.followed->ops && w.followed->ops->locate &&
            w.followed->ops->locate(*w.followed, w.follow_index, &p))
            w.camera.frame(p, w.camera.distance());
        else
            w.followed = nullptr;
    }

    WorldView view = view_of(w, tw, th, scene_bounds(w));
    if (!write_view_cb(w, cl, view))
        return;
    view.view_cb = w.view_cb;
    w.last_view = view;
    w.viewed = true;

    // The WORLD's, not the item's: an item that had to remember
    // would eventually forget.
    w.cmds.clear();
    for (impl::WorldItem &it : w.items) {
        if (!it.ops || !it.ops->submit || !it.visible)
            continue;
        impl::Vec3 lo{}, hi{};
        if (w.cull && it.ops->bounds && it.ops->bounds(it, &lo, &hi) &&
            !impl::frustum_intersects(view.frustum, {lo, hi, true})) {
            if (w.stats)
                ++w.stats->culled;
            continue;
        }
        it.ops->submit(it, view, w.cmds);
    }
    for (std::size_t i = 0; i < w.cmds.size(); ++i)
        w.cmds[i].seq = std::uint32_t(i);

    // One sort, pass included, so a pass is a contiguous range. The
    // sequence number is the tie-break that makes it deterministic.
    std::sort(w.cmds.begin(), w.cmds.end(),
              [](const DrawCmd &a, const DrawCmd &b) {
                  if (a.pass != b.pass)
                      return a.pass < b.pass;
                  if (a.key != b.key)
                      return a.key < b.key;
                  return a.seq < b.seq;
              });

    cl->clearTextureFloat(fb->getDesc().colorAttachments[0].texture,
                          nvrhi::AllSubresources,
                          nvrhi::Color(0.09f, 0.09f, 0.10f, 1.0f));
    // Zero, not one: under reverse-Z the far plane is 0, so this is
    // the "nothing has been drawn yet" value.
    cl->clearDepthStencilTexture(depth, nvrhi::AllSubresources, true, 0.0f,
                                 false, 0);

    std::size_t i = 0;
    for (const PassDesc &pd : kPasses) {
        const PassId id = PassId(&pd - kPasses);
        std::size_t j = i;
        while (j < w.cmds.size() && w.cmds[j].pass == id)
            ++j;
        if (pd.enabled && j > i) {
            // A section as well as a marker: the number beside the
            // name is how a claim about a pass's cost is checked.
            cl->beginMarker(pd.name);
            timing_begin(pl, cl, pd.name);
            for (std::size_t k = i; k < j; ++k) {
                const DrawCmd &c = w.cmds[k];
                if (c.item && c.item->ops && c.item->ops->draw)
                    c.item->ops->draw(*c.item, c, cl, fb, view);
            }
            timing_end(pl, cl);
            cl->endMarker();
        }
        i = j;
    }

    // One picture out of several samples a pixel, into the target the
    // caller asked for. Everything above drew at the higher rate and
    // knew nothing about it.
    if (ms)
        cl->resolveTexture(out->getDesc().colorAttachments[0].texture,
                           nvrhi::AllSubresources, w.ms_color,
                           nvrhi::AllSubresources);
}

const impl::WorldState::Mesh *world_mesh_ready(const impl::WorldState &w,
                                               int shape, int tier) {
    for (const auto &m : w.meshes)
        if (m.shape == shape && m.tier == tier)
            return &m;
    return nullptr;
}

const impl::WorldState::Mesh *world_mesh(impl::WorldState &w, int shape,
                                         int tier, nvrhi::ICommandList *cl) {
    if (const impl::WorldState::Mesh *m = world_mesh_ready(w, shape, tier))
        return m;

    // Sphere tiers are a triangle budget: 108 triangles for a crowd,
    // 972 for a handful. A cube has one tier because twelve triangles
    // is already the whole shape.
    const impl::MeshData data =
        shape == 2 ? impl::make_cube() : impl::make_sphere(tier == 0 ? 2u : 8u);

    impl::WorldState::Mesh m;
    m.shape = shape;
    m.tier = tier;
    m.triangles = data.triangle_count();
    m.index_count = data.index_count();
    m.vertices = w.gpu.dev->createBuffer(
        nvrhi::BufferDesc()
            .setByteSize(data.vertices.size() * sizeof(float))
            .setStructStride(4)
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setDebugName("mesh vertices"));
    m.indices = w.gpu.dev->createBuffer(
        nvrhi::BufferDesc()
            .setByteSize(data.indices.size() * sizeof(std::uint32_t))
            .setIsIndexBuffer(true)
            .setInitialState(nvrhi::ResourceStates::IndexBuffer)
            .setKeepInitialState(true)
            .setDebugName("mesh indices"));
    if (!m.vertices || !m.indices)
        return set_error("world mesh: buffer creation failed"), nullptr;

    // On the frame's own list, which is why this is asked for in
    // prepare: it is open and has no pass yet, and a second immediate
    // list open beside it is refused outright.
    cl->writeBuffer(m.vertices, data.vertices.data(),
                    data.vertices.size() * sizeof(float));
    cl->writeBuffer(m.indices, data.indices.data(),
                    data.indices.size() * sizeof(std::uint32_t));

    w.meshes.push_back(m);
    return &w.meshes.back();
}

bool world_pick(impl::WorldState &w, float x, float y, Pick *out) {
    if (!out || !w.viewed || w.rect[2] <= 0.0f || w.rect[3] <= 0.0f)
        return false;
    const float u = (x - w.rect[0]) / w.rect[2];
    const float v = (y - w.rect[1]) / w.rect[3];
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
        return false;

    // Two depths unprojected, the near plane and one behind it, so one
    // arithmetic serves both projections. Reverse-Z: near is 1.
    const impl::Mat4 inv = impl::mat_inverse(w.last_view.world_to_clip);
    const float nx = 2.0f * u - 1.0f, ny = 1.0f - 2.0f * v;
    const impl::Vec3 near = impl::transform_point(inv, {nx, ny, 1.0f});
    const impl::Vec3 mid = impl::transform_point(inv, {nx, ny, 0.5f});
    // Six pixels of slop, ImGui's own drag threshold: what a click
    // cannot resolve, a pick should not demand.
    const PickQuery q{.ray = {near, impl::normalize(mid - near)},
                      .focal_px = w.last_view.focal_px,
                      .orthographic = w.last_view.orthographic,
                      .slop_px = 6.0f};

    bool any = false;
    PickHit best{};
    impl::WorldItem *who = nullptr;
    for (impl::WorldItem &it : w.items) {
        PickHit h{};
        if (!it.visible || !it.ops || !it.ops->pick || !it.ops->pick(it, q, &h))
            continue;
        if (!any || h.t < best.t) {
            best = h;
            who = &it;
            any = true;
        }
    }
    if (!any)
        return false;

    // The grid answers as the GROUND: no item, no index.
    out->point[0] = best.point.x;
    out->point[1] = best.point.y;
    out->point[2] = best.point.z;
    out->distance = best.t;
    out->index = who == w.grid ? -1 : best.index;
    out->cloud = who == w.grid ? impl::Cloud{} : impl::Cloud{who};
    return true;
}

void world_picked(impl::WorldState &w, const Pick &p) {
    for (const impl::WorldState::PickCb &c : w.picks)
        if (c.fn)
            c.fn(p, c.user);
}

void world_stroke(impl::WorldState &w, float x0, float y0, float x1, float y1) {
    if (!w.viewed || w.rect[2] <= 0.0f || w.rect[3] <= 0.0f ||
        w.strokes.empty())
        return;
    Stroke s{};
    s.from[0] = (x0 - w.rect[0]) / w.rect[2];
    s.from[1] = (y0 - w.rect[1]) / w.rect[3];
    s.to[0] = (x1 - w.rect[0]) / w.rect[2];
    s.to[1] = (y1 - w.rect[1]) / w.rect[3];
    for (int k = 0; k < 16; ++k)
        s.clip[k] = w.last_view.world_to_clip.m[k];
    for (const impl::WorldState::StrokeCb &c : w.strokes)
        if (c.fn)
            c.fn(s, c.user);
}

namespace impl {

// Both ends projected through the view the stroke was drawn in, then a
// segment crossing in the picture's own coordinates. An end behind the
// camera has no place in the picture, so it never crosses.
bool stroke_crosses(const Stroke &s, const float p[3], const float q[3]) {
    Mat4 m;
    for (int k = 0; k < 16; ++k)
        m.m[k] = s.clip[k];
    float wp = 0.0f, wq = 0.0f;
    const Vec3 a = transform_point(m, {p[0], p[1], p[2]}, &wp);
    const Vec3 b = transform_point(m, {q[0], q[1], q[2]}, &wq);
    if (wp <= 0.0f || wq <= 0.0f)
        return false;
    const float ax = (a.x + 1.0f) * 0.5f, ay = (1.0f - a.y) * 0.5f;
    const float bx = (b.x + 1.0f) * 0.5f, by = (1.0f - b.y) * 0.5f;
    const float cx = s.from[0], cy = s.from[1], dx = s.to[0], dy = s.to[1];
    const auto side = [](float x0, float y0, float x1, float y1, float x,
                         float y) {
        return (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
    };
    const float o1 = side(ax, ay, bx, by, cx, cy);
    const float o2 = side(ax, ay, bx, by, dx, dy);
    const float o3 = side(cx, cy, dx, dy, ax, ay);
    const float o4 = side(cx, cy, dx, dy, bx, by);
    return ((o1 > 0.0f) != (o2 > 0.0f)) && ((o3 > 0.0f) != (o4 > 0.0f)) &&
           o1 != 0.0f && o2 != 0.0f && o3 != 0.0f && o4 != 0.0f;
}

void world_on_stroke(World w, void (*fn)(const Stroke &, void *), void *user,
                     void (*free)(void *)) {
    WorldState *ws = static_cast<WorldState *>(w.p);
    if (!ws) {
        if (free)
            free(user);
        return;
    }
    ws->strokes.push_back({fn, user, free});
}

void world_tool(World w, int t) {
    if (WorldState *ws = static_cast<WorldState *>(w.p))
        ws->tool = t;
}

int world_tool(World w) {
    const WorldState *ws = static_cast<const WorldState *>(w.p);
    return ws ? ws->tool : 0;
}

} // namespace impl

nvrhi::IComputePipeline *world_readback(impl::WorldState &w) {
    if (w.readback)
        return w.readback.Get();
    auto cs =
        w.gpu.dev->createShader(nvrhi::ShaderDesc()
                                    .setShaderType(nvrhi::ShaderType::Compute)
                                    .setEntryName("main"),
                                readback_main_spirv, readback_main_spirv_len);
    w.readback_layout = w.gpu.dev->createBindingLayout(
        nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Compute)
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1))
            .addItem(nvrhi::BindingLayoutItem::PushConstants(2, 16))
            .setBindingOffsets(nvrhi::VulkanBindingOffsets()
                                   .setShaderResourceOffset(0)
                                   .setSamplerOffset(0)
                                   .setConstantBufferOffset(0)
                                   .setUnorderedAccessViewOffset(0)));
    if (cs && w.readback_layout)
        w.readback = w.gpu.dev->createComputePipeline(
            nvrhi::ComputePipelineDesc().setComputeShader(cs).addBindingLayout(
                w.readback_layout));
    if (!w.readback)
        set_error("world: the readback pipeline failed — device-resident "
                  "items cannot be picked");
    return w.readback.Get();
}

void world_release(impl::WorldState &w) {
    w.readback = nullptr;
    w.readback_layout = nullptr;
    for (impl::WorldState::PickCb &c : w.picks)
        if (c.free)
            c.free(c.user);
    w.picks.clear();
    for (impl::WorldState::StrokeCb &c : w.strokes)
        if (c.free)
            c.free(c.user);
    w.strokes.clear();
    w.followed = nullptr;
    w.hovered = nullptr;
    for (impl::WorldItem &it : w.items)
        if (it.ops && it.ops->release)
            it.ops->release(it);
    w.items.clear();
    w.ops_seen.clear();
    w.meshes.clear();
    w.ms_fb = nullptr;
    w.ms_color = nullptr;
    w.ms_depth = nullptr;
    w.cmds.clear();
    w.view_cb = nullptr;
}

} // namespace sv
