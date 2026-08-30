// The scene: what belongs to every kind rather than to any one of
// them. Three things live here on purpose — the CLEAR, because an item
// that cleared would erase whatever the item before it drew; the ONE
// aspect-fit every item shares, which is what makes a point land on
// the cell it belongs to; and the two-phase order, because a copy pass
// cannot be nested inside a render pass.

#include "../engine/Engine.h"

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

// The one format a view's texture is asked for: a colour target that
// can also be sampled, and universally supported as both.
constexpr SDL_GPUTextureFormat kViewFormat =
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

void views_resize(impl::App *a) {
    for (impl::App::ViewState &v : a->views) {
        if (v.tex && v.w == v.want_w && v.h == v.want_h)
            continue;
        if (v.tex)
            SDL_ReleaseGPUTexture(a->dev, v.tex);

        SDL_GPUTextureCreateInfo ti{
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = kViewFormat,
            .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                     SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = v.want_w,
            .height = v.want_h,
            .layer_count_or_depth = 1,
            .num_levels = 1,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0,
        };
        v.tex = SDL_CreateGPUTexture(a->dev, &ti);
        if (!v.tex) {
            set_error(std::string("view texture: ") + SDL_GetError());
            v.w = v.h = 0;
            continue;
        }
        v.w = v.want_w;
        v.h = v.want_h;
    }
}

void views_draw(impl::App *a, SDL_GPUCommandBuffer *cmd) {
    for (impl::App::ViewState &v : a->views)
        if (v.tex)
            scene_draw(v.scene, cmd, v.tex, v.w, v.h, kViewFormat);
}

void scene_release(impl::App *a, impl::SceneState &sc) {
    for (impl::SceneItem &it : sc.items)
        if (it.ops && it.ops->release)
            it.ops->release(it, a->dev);
    sc.items.clear();
}

void scene_draw(impl::SceneState &sc, SDL_GPUCommandBuffer *cmd,
                SDL_GPUTexture *target, Uint32 tw, Uint32 th,
                SDL_GPUTextureFormat tf) {
    for (impl::SceneItem &it : sc.items)
        if (it.ops && it.ops->prepare)
            it.ops->prepare(it, cmd);

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

    SDL_GPUColorTargetInfo ct{};
    ct.texture = target;
    ct.clear_color = {0.09f, 0.09f, 0.10f, 1.0f};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    for (impl::SceneItem &it : sc.items)
        if (it.ops && it.ops->draw)
            it.ops->draw(it, cmd, pass, at);
    SDL_EndGPURenderPass(pass);
}

namespace impl {

void scene_range(Scene s, const Range2 &r) {
    if (SceneState *sc = static_cast<SceneState *>(s.p))
        sc->range = r;
}

Scene app_scene(App *a) { return a ? Scene{&a->scene} : Scene{}; }

} // namespace impl
} // namespace sv
