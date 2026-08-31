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
    float znear[4];
};

void copy_mat(float (&dst)[16], const impl::Mat4 &m) {
    std::memcpy(dst, m.m, sizeof m.m);
}

// The camera, resolved against the target this frame is drawn into.
WorldView view_of(const impl::WorldState &w, std::uint32_t tw,
                  std::uint32_t th) {
    const float aspect = th ? float(tw) / float(th) : 1.0f;
    const float znear = impl::camera_znear(w.camera);
    const impl::Mat4 v = impl::camera_view(w.camera);
    const impl::Mat4 p = impl::proj_reverse_z(w.camera.fovy, aspect, znear);
    return {.world_to_clip = impl::mat_mul(p, v),
            .world_to_view = v,
            .camera_pos = impl::camera_position(w.camera),
            .znear = znear,
            .tw = tw,
            .th = th};
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

    const impl::Mat4 proj = impl::proj_reverse_z(
        w.camera.fovy, view.th ? float(view.tw) / float(view.th) : 1.0f,
        view.znear);
    ViewConstants c{};
    copy_mat(c.world_to_clip, view.world_to_clip);
    copy_mat(c.world_to_view, view.world_to_view);
    copy_mat(c.view_to_clip, proj);
    copy_mat(c.clip_to_world, impl::mat_inverse(view.world_to_clip));
    c.camera_pos[0] = view.camera_pos.x;
    c.camera_pos[1] = view.camera_pos.y;
    c.camera_pos[2] = view.camera_pos.z;
    c.viewport[0] = float(view.tw);
    c.viewport[1] = float(view.th);
    c.viewport[2] = view.tw ? 1.0f / float(view.tw) : 0.0f;
    c.viewport[3] = view.th ? 1.0f / float(view.th) : 0.0f;
    c.znear[0] = view.znear;
    cl->writeBuffer(w.view_cb, &c, sizeof c);
    return true;
}

} // namespace

impl::WorldItem &world_item_add(impl::WorldState &w, const WorldItemOps *ops) {
    impl::WorldItem &it = w.items.emplace_back();
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

void world_draw(impl::WorldState &w, nvrhi::ICommandList *cl,
                impl::RenderTarget &t) {
    if (!t.fb || !t.depth)
        return;

    for (impl::WorldItem &it : w.items)
        if (it.ops && it.ops->prepare)
            it.ops->prepare(it, cl);

    WorldView view = view_of(w, t.w, t.h);
    if (!write_view_cb(w, cl, view))
        return;
    view.view_cb = w.view_cb;

    w.cmds.clear();
    for (impl::WorldItem &it : w.items)
        if (it.ops && it.ops->submit)
            it.ops->submit(it, view, w.cmds);
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

    cl->clearTextureFloat(t.fb->getDesc().colorAttachments[0].texture,
                          nvrhi::AllSubresources,
                          nvrhi::Color(0.09f, 0.09f, 0.10f, 1.0f));
    // Zero, not one: under reverse-Z the far plane is 0, so this is
    // the "nothing has been drawn yet" value.
    cl->clearDepthStencilTexture(t.depth, nvrhi::AllSubresources, true, 0.0f,
                                 false, 0);

    std::size_t i = 0;
    for (const PassDesc &pd : kPasses) {
        const PassId id = PassId(&pd - kPasses);
        std::size_t j = i;
        while (j < w.cmds.size() && w.cmds[j].pass == id)
            ++j;
        if (pd.enabled && j > i) {
            cl->beginMarker(pd.name);
            for (std::size_t k = i; k < j; ++k) {
                const DrawCmd &c = w.cmds[k];
                if (c.item && c.item->ops && c.item->ops->draw)
                    c.item->ops->draw(*c.item, c, cl, t.fb, view);
            }
            cl->endMarker();
        }
        i = j;
    }
}

void world_release(impl::WorldState &w) {
    for (impl::WorldItem &it : w.items)
        if (it.ops && it.ops->release)
            it.ops->release(it);
    w.items.clear();
    w.ops_seen.clear();
    w.cmds.clear();
    w.view_cb = nullptr;
}

} // namespace sv
