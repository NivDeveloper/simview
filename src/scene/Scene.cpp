// The scene: what belongs to every kind rather than to any one of
// them. Three things live here on purpose — the CLEAR, because an item
// that cleared would erase whatever the item before it drew; the ONE
// aspect-fit every item shares, which is what makes a point land on
// the cell it belongs to; and the prepare-then-draw order, because
// uploads must not interleave with the pass.

#include "Target.h"

#include <string>

namespace sv {
namespace {

// The range every item maps into. An explicit range wins; else the
// first item with a natural grid, in cells; else the unit square.
Range2 effective_range(const impl::SceneState &sc) {
    if (sc.range.x1 > sc.range.x0 && sc.range.y1 > sc.range.y0)
        return sc.range;
    for (const impl::SceneItem &it : sc.items) {
        Extent2 g{};
        if (it.ops && it.ops->grid && it.ops->grid(it, &g))
            return {0.0, 0.0, double(g.w), double(g.h)};
    }
    return {0.0, 0.0, 1.0, 1.0};
}

} // namespace

bool scene_grid(const impl::SceneState &sc, Extent2 *out) {
    for (const impl::SceneItem &it : sc.items)
        if (it.ops && it.ops->grid && it.ops->grid(it, out))
            return true;
    return false;
}

void target_draw(impl::SceneState &sc, nvrhi::ICommandList *cl,
                 impl::RenderTarget &t) {
    if (t.fb)
        scene_draw(sc, cl, t.fb, t.w, t.h, kTargetFormat);
}

void scene_release(impl::SceneState &sc) {
    for (impl::SceneItem &it : sc.items)
        if (it.ops && it.ops->release)
            it.ops->release(it);
    sc.items.clear();
}

void scene_draw(impl::SceneState &sc, nvrhi::ICommandList *cl,
                nvrhi::IFramebuffer *fb, std::uint32_t tw, std::uint32_t th,
                nvrhi::Format tf) {
    for (impl::SceneItem &it : sc.items)
        if (it.ops && it.ops->prepare)
            it.ops->prepare(it, cl);

    Placement at{};
    at.tw = tw;
    at.th = th;
    at.format = tf;
    at.range = effective_range(sc);
    const float wa = float(tw) / float(th);
    const float ra =
        float((at.range.x1 - at.range.x0) / (at.range.y1 - at.range.y0));
    if (wa > ra)
        at.sx = wa / ra; // target wider: bars left and right
    else
        at.sy = ra / wa; // target taller: bars above and below

    cl->clearTextureFloat(fb->getDesc().colorAttachments[0].texture,
                          nvrhi::AllSubresources,
                          nvrhi::Color(0.09f, 0.09f, 0.10f, 1.0f));
    for (impl::SceneItem &it : sc.items)
        if (it.ops && it.ops->draw)
            it.ops->draw(it, cl, fb, at);
}

namespace impl {

void scene_range(Scene s, const Range2 &r) {
    if (SceneState *sc = static_cast<SceneState *>(s.p))
        sc->range = r;
}

// Track a Sync for the per-frame flip. The list is the App's, so a Sync
// drawn in two scenes is tracked once; the registry holds its own
// reference, so a Sync that dies first leaves a dead gate, not a
// dangling one.
void scene_untracked_pull(Scene s) {
    if (SceneState *sc = static_cast<SceneState *>(s.p))
        ++sc->untracked_pulls;
}

void scene_track(Scene s, SyncGate g) {
    SceneState *sc = static_cast<SceneState *>(s.p);
    if (!sc || !sc->gates || !g)
        return;
    // Publishes stamp themselves with the compute device's submitted
    // ticket — taken on the PRODUCER's thread (submitted() is in
    // gpud's thread-safe carve-out), so a frame can wait GPU-side for
    // exactly the work that filled what it shows.
    if (sc->gpu.gdev)
        sync_gate_set_stamper(
            g,
            +[](void *u) {
                return static_cast<gpud::Device *>(u)->submitted().value;
            },
            sc->gpu.gdev);
    for (SyncGate have : *sc->gates)
        if (have.p == g.p)
            return;
    sync_gate_retain(g);
    sc->gates->push_back(g);
}

} // namespace impl
} // namespace sv
