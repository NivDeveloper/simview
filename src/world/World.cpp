// A world's frame, in one place: fill the camera constants, let every
// item describe its draws, order them, replay them by pass.
//
// The order of those four is the contract. Uploads happen before any
// pass is open (the 2D scene's rule, for the same reason); the view
// block is written before any state is set, because the version a draw
// reads is chosen when its state is; and the sort happens once over
// every command rather than per pass, so a pass is a range, not a
// filter.

#include "World.h"

#include "../core/Error.h"
#include "../platform/Device.h"

#include <algorithm>
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
    float light_to_clip[16];
    float shadow[4]; // on, 1/map px, depth bias, unused
};

void copy_mat(float (&dst)[16], const impl::Mat4 &m) {
    std::memcpy(dst, m.m, sizeof m.m);
}

// Everything the items reported about where they are. The near plane
// and the frustum both come out of it, which is why it is collected
// once, after prepare — an item's extent is only settled when its
// uploads are.
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
    const float znear = impl::camera_znear(w.camera, scene);
    const impl::Mat4 v = impl::camera_view(w.camera);
    const impl::Mat4 p = impl::camera_proj(w.camera, aspect, znear);
    const impl::Mat4 vp = impl::mat_mul(p, v);
    return {.world_to_clip = vp,
            .world_to_view = v,
            .view_to_clip = p,
            .camera_pos = impl::camera_position(w.camera),
            .znear = znear,
            .distance = w.camera.distance,
            .tw = tw,
            .th = th,
            .focal_px = impl::focal_px(w.camera, th),
            .orthographic =
                w.camera.projection == impl::Projection::Orthographic,
            .frustum = impl::frustum_of(vp)};
}

bool write_view_cb(impl::WorldState &w, nvrhi::ICommandList *cl,
                   const WorldView &view) {
    if (!w.view_cb) {
        // Volatile: the renderer versions the contents internally, so
        // a binding set made once stays valid as the camera moves. The
        // version count must cover every write in flight — one per
        // world per frame, and frames in flight is one.
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
    c.depth[1] = impl::camera_zfar(w.camera);
    c.depth[2] =
        w.camera.projection == impl::Projection::Orthographic ? 1.0f : 0.0f;

    // Lights reach the shader in VIEW space, where an impostor knows
    // its own normal. Rotating them here is one transform a frame
    // instead of a normal matrix in every shader that shades anything.
    // An unlit world gets one light at the camera — already view space,
    // so it is the one direction that needs no rotating.
    const std::size_t n = w.lights.size() < 4 ? w.lights.size() : 4;
    if (n == 0) {
        c.light_dir[0][2] = 1.0f;
        c.light_dir[0][3] = 0.7f;
        for (int k = 0; k < 3; ++k)
            c.light_rgb[0][k] = 1.0f;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const impl::Vec3 d = impl::normalize(
            impl::rotate(impl::conjugate(w.camera.q), w.lights[i].direction));
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

    copy_mat(c.light_to_clip, view.light_to_clip);
    c.shadow[0] = view.shadow_px ? 1.0f : 0.0f;
    c.shadow[1] = view.shadow_px ? 1.0f / float(view.shadow_px) : 0.0f;
    c.shadow[2] = view.shadow_bias;
    // Whether a light LIGHTS the ground, which is a different question
    // from whether the map is live: the ground only takes a tone when
    // something is shining on it, and a shadow is that tone's absence.
    // Keeping the two apart is also what lets a check render the same
    // scene with the lookup off and subtract.
    c.shadow[3] = view.lit_ground ? 1.0f : 0.0f;

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

// How wide the map is when a light casts, and the one texel it is
// when none does. Square, and a power of two, so a rotating light
// does not change how coarse the map is — a shadow whose softness
// breathes as the scene turns reads as flicker.
//
// 1024 and not more because the box is FITTED to the scene: the map
// spends its texels on what is there rather than on the world, so a
// thousand across is a thousand across whatever the scale. Measured
// on examples/orbit: 3.45 ms a frame here against 5.11 ms at 2048,
// which is a third of the pass for a sharpness nothing in a
// scientific picture is reading.
constexpr std::uint32_t kShadowPx = 1024;

// The light that casts, or null. At most one — world_light refuses a
// second, so finding the first IS finding the only one.
const impl::WorldState::Light *caster_of(const impl::WorldState &w) {
    for (const auto &l : w.lights)
        if (l.shadow)
            return &l;
    return nullptr;
}

// The map and the sampler, sized to whether anything casts. Turning
// shadows on is a resize and turning them off is a resize back, so
// nothing here is created twice or leaks a 16 MB texture into a world
// that stopped wanting one.
bool ensure_shadow(impl::WorldState &w, bool on) {
    w.shadow.depth_only = true;
    w.shadow.want_w = w.shadow.want_h = on ? kShadowPx : 1;
    target_resize(w.gpu, w.shadow);
    if (!w.shadow.fb || !w.shadow.depth)
        return false;

    if (!w.shadow_sampler) {
        // A COMPARISON sampler: the hardware does the depth test per
        // texel and filters the RESULTS, which is what makes four
        // taps a soft edge instead of four hard ones.
        w.shadow_sampler = w.gpu.dev->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Border)
                .setBorderColor(nvrhi::Color(1.0f))
                .setReductionType(nvrhi::SamplerReductionType::Comparison));
        if (!w.shadow_sampler)
            return set_error("world: shadow sampler creation failed"), false;
    }
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

    for (impl::WorldItem &it : w.items)
        if (it.ops && it.ops->prepare)
            it.ops->prepare(it, cl);

    // The light's box is fitted to what the items reported, which is
    // why the shadow pass had to wait for bounds: a fixed box either
    // clips the scene out of the map or spends its texels on nothing.
    const impl::Aabb scene = scene_bounds(w);
    const impl::WorldState::Light *any_caster = caster_of(w);
    const impl::WorldState::Light *caster = w.cast ? any_caster : nullptr;
    const impl::LightFit fit =
        caster ? impl::light_fit(caster->direction, scene, kShadowPx)
               : impl::LightFit{};
    const bool shadows = fit.valid && ensure_shadow(w, true);
    if (!shadows && !ensure_shadow(w, false))
        return;
    w.light_to_clip = fit.world_to_clip;

    WorldView view = view_of(w, tw, th, scene);
    view.light_to_clip = fit.world_to_clip;
    view.shadow_map = w.shadow.depth.Get();
    view.shadow_sampler = w.shadow_sampler.Get();
    view.shadow_px = shadows ? kShadowPx : 0;
    view.shadow_bias = w.shadow_bias;
    view.lit_ground = any_caster != nullptr;
    if (!write_view_cb(w, cl, view))
        return;
    view.view_cb = w.view_cb;

    // Culling is the WORLD's, not the item's, for the same reason the
    // ordering is: an item that had to remember would eventually
    // forget, and the one that forgot would be the one drawn wrong.
    // An item with no bounds is drawn — see WorldItemOps::bounds.
    const impl::Frustum light_frustum =
        shadows ? impl::frustum_of(fit.world_to_clip) : impl::Frustum{};

    w.cmds.clear();
    for (impl::WorldItem &it : w.items) {
        if (!it.ops || !it.ops->submit)
            continue;
        impl::Vec3 lo{}, hi{};
        const bool bounded = it.ops->bounds && it.ops->bounds(it, &lo, &hi);
        const impl::Aabb box{lo, hi, bounded};

        if (w.cull && bounded && !impl::frustum_intersects(view.frustum, box)) {
            if (w.stats)
                ++w.stats->culled;
        } else {
            it.ops->submit(it, view, w.cmds);
        }

        // The caster's half is a SEPARATE decision, against the
        // light's frustum: geometry behind the camera can still throw
        // a shadow into the picture, so an item the view rejected may
        // very well belong in this pass.
        if (shadows && it.ops->draw_shadow &&
            (!w.cull || !bounded ||
             impl::frustum_intersects(light_frustum, box)))
            w.cmds.push_back({.key = 0,
                              .seq = 0,
                              .pass = PassId::Shadow,
                              .item = &it,
                              .part = 0});
    }
    for (std::size_t i = 0; i < w.cmds.size(); ++i)
        w.cmds[i].seq = std::uint32_t(i);

    // One sort over everything, pass included: a pass is then a
    // contiguous range and the loop below never filters. The sequence
    // number is the tail that makes ties deterministic without asking
    // for a stable sort.
    std::sort(w.cmds.begin(), w.cmds.end(),
              [](const DrawCmd &a, const DrawCmd &b) {
                  if (a.pass != b.pass)
                      return a.pass < b.pass;
                  if (a.key != b.key)
                      return a.key < b.key;
                  return a.seq < b.seq;
              });

    // ONE, not zero: the shadow map is forward-Z, so "nothing has
    // been drawn yet" is the far plane at 1. Every other depth buffer
    // here clears to 0 and this one must not.
    if (shadows)
        cl->clearDepthStencilTexture(w.shadow.depth, nvrhi::AllSubresources,
                                     true, 1.0f, false, 0);
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
            // The shadow pass draws into the LIGHT's target, at the
            // map's size rather than the view's — the one row whose
            // framebuffer is not the frame's.
            const bool is_shadow = id == PassId::Shadow;
            nvrhi::IFramebuffer *into = is_shadow ? w.shadow.fb.Get() : fb;

            // A pass is a timing section as well as a marker. The
            // marker names it in a capture; the section is what puts a
            // number beside the name, which is the only way a claim
            // about what a pass costs can be checked.
            cl->beginMarker(pd.name);
            timing_begin(pl, cl, pd.name);
            for (std::size_t k = i; k < j; ++k) {
                const DrawCmd &c = w.cmds[k];
                if (!c.item || !c.item->ops)
                    continue;
                if (is_shadow) {
                    if (c.item->ops->draw_shadow)
                        c.item->ops->draw_shadow(*c.item, c, cl, into, view);
                } else if (c.item->ops->draw) {
                    c.item->ops->draw(*c.item, c, cl, into, view);
                }
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

void world_release(impl::WorldState &w) {
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
    // Every device object the world owns, dropped HERE and not in the
    // destructor: a WorldState outlives the renderer at teardown, and
    // a handle released after its device takes a mutex that is gone.
    target_release(w.shadow);
    w.shadow_sampler = nullptr;
}

} // namespace sv
