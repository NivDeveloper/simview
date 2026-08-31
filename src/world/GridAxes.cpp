// The two items every world may carry: the ground grid and the axes
// at the origin. Both are ORIENTATION — without them a cloud floats in
// a void with no scale and no sense of which way is up.
//
// They are ordinary items through the ordinary contract, not a special
// case inside the world's draw loop. Three kinds through one contract
// on the day it lands is the only way to know the contract is real.

#include "World.h"

#include "bytecode/axes3_fsmain_spirv.h"
#include "bytecode/axes3_vsmain_spirv.h"
#include "bytecode/grid3_fsmain_spirv.h"
#include "bytecode/grid3_vsmain_spirv.h"

namespace sv {
namespace {

// Matches grid3.slang's GParams.
struct GridParams {
    float color[4];
    float size;
    float cell;
    float thick_alpha;
    float pad0;
};

// Matches axes3.slang's AParams.
struct AxesParams {
    float length;
    float pad0, pad1, pad2;
};

struct GridState {
    nvrhi::BindingSetHandle bset;
    nvrhi::ITexture *bound_shadow = nullptr;
    float color[4] = {0.58f, 0.61f, 0.66f, 0.34f};
    // The finest cell is a WORLD length and stays one: a grid is a
    // ruler, and the level-of-detail picks which decade of it to draw.
    // The extent is not — see grid_draw.
    float cell = 0.1f;
    float thick_alpha = 0.85f;
};

struct AxesState {
    nvrhi::BindingSetHandle bset;
    nvrhi::ITexture *bound_shadow = nullptr;
    float length = 1.0f;
};

// Neither is sorted against anything: both their passes order by
// submission, so each draws where it was registered.
void submit_here(impl::WorldItem &it, const WorldView &,
                 std::vector<DrawCmd> &out) {
    out.push_back(
        {.key = 0, .seq = 0, .pass = it.ops->pass, .item = &it, .part = 0});
}

// The view block and the shadow map — neither item reads a buffer of
// its own, because both compute their geometry from the vertex id.
//
// The grid is the surface a shadow is actually READ on, so the map is
// not optional here even though the grid casts none. Rebuilt when the
// map's identity changes, which is what turning shadows on does.
bool bind(impl::WorldItem &it, nvrhi::BindingSetHandle &bset,
          const WorldPipelineEntry *pe, const WorldView &view,
          nvrhi::ITexture *&bound, std::uint32_t push_bytes) {
    if (!bset || bound != view.shadow_map) {
        bset = it.gpu.dev->createBindingSet(
            nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, view.view_cb))
                .addItem(nvrhi::BindingSetItem::PushConstants(1, push_bytes))
                .addItem(nvrhi::BindingSetItem::Texture_SRV(kShadowMapBinding,
                                                            view.shadow_map))
                .addItem(nvrhi::BindingSetItem::Sampler(kShadowSamplerBinding,
                                                        view.shadow_sampler)),
            pe->layout);
        bound = view.shadow_map;
    }
    return bset != nullptr;
}

void grid_draw(impl::WorldItem &it, const DrawCmd &, nvrhi::ICommandList *cl,
               nvrhi::IFramebuffer *fb, const WorldView &view) {
    GridState &gs = *static_cast<GridState *>(it.state);
    const WorldPipelineEntry *pe = world_pipeline_for(
        it.gpu, *it.pipelines, it.stats, it.ops, it.ops->pass, fb);
    if (!pe || !view.view_cb ||
        !bind(it, gs.bset, pe, view, gs.bound_shadow, sizeof(GridParams)))
        return;

    GridParams p{};
    for (int c = 0; c < 4; ++c)
        p.color[c] = gs.color[c];
    // The extent FOLLOWS THE CAMERA. A fixed one is wrong in both
    // directions: zoomed out past it the whole grid sits beyond its
    // own fade and disappears, and zoomed in the fade never engages,
    // so the far field grazes the plane and turns to moire. Twelve
    // orbit distances is past the edge of any view at this field of
    // view, and the fade below covers the last two thirds of it.
    p.size = view.distance * 12.0f;
    p.cell = gs.cell;
    p.thick_alpha = gs.thick_alpha;
    cl->setGraphicsState(
        nvrhi::GraphicsState()
            .setPipeline(pe->pipeline)
            .setFramebuffer(fb)
            .addBindingSet(gs.bset)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(float(view.tw), float(view.th)))));
    cl->setPushConstants(&p, sizeof p);
    cl->draw(nvrhi::DrawArguments().setVertexCount(6));
    ++it.stats->draws;
}

void axes_draw(impl::WorldItem &it, const DrawCmd &, nvrhi::ICommandList *cl,
               nvrhi::IFramebuffer *fb, const WorldView &view) {
    AxesState &as = *static_cast<AxesState *>(it.state);
    const WorldPipelineEntry *pe = world_pipeline_for(
        it.gpu, *it.pipelines, it.stats, it.ops, it.ops->pass, fb);
    if (!pe || !view.view_cb ||
        !bind(it, as.bset, pe, view, as.bound_shadow, sizeof(AxesParams)))
        return;

    AxesParams p{};
    p.length = as.length;
    cl->setGraphicsState(
        nvrhi::GraphicsState()
            .setPipeline(pe->pipeline)
            .setFramebuffer(fb)
            .addBindingSet(as.bset)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(float(view.tw), float(view.th)))));
    cl->setPushConstants(&p, sizeof p);
    cl->draw(nvrhi::DrawArguments().setVertexCount(6));
    ++it.stats->draws;
}

void grid_release(impl::WorldItem &it) {
    delete static_cast<GridState *>(it.state);
    it.state = nullptr;
}

void axes_release(impl::WorldItem &it) {
    delete static_cast<AxesState *>(it.state);
    it.state = nullptr;
}

// clang-format off: the tables read as tables
// The grid is the FLOOR, so it draws before anything translucent and
// lets that wash over it; the axes are a gizmo and belong on top.
const WorldItemOps kGridOps{
    .name = "grid",
    .pass = PassId::Ground,
    .prepare = nullptr,
    .submit = submit_here,
    .draw = grid_draw,
    .release = grid_release,
    .bounds = nullptr,
    .vs = {grid3_vsmain_spirv, grid3_vsmain_spirv_len, "vsmain"},
    .fs = {grid3_fsmain_spirv, grid3_fsmain_spirv_len, "fsmain"},
    .blend = nvrhi::BlendState::RenderTarget()
                 .enableBlend()
                 .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                 .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                 .setSrcBlendAlpha(nvrhi::BlendFactor::One)
                 .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha),
    .topology = nvrhi::PrimitiveType::TriangleList,
    .storage_count = 0,
    .push_bytes = sizeof(GridParams),
};

const WorldItemOps kAxesOps{
    .name = "axes",
    .pass = PassId::Overlay,
    .prepare = nullptr,
    .submit = submit_here,
    .draw = axes_draw,
    .release = axes_release,
    .bounds = nullptr,
    .vs = {axes3_vsmain_spirv, axes3_vsmain_spirv_len, "vsmain"},
    .fs = {axes3_fsmain_spirv, axes3_fsmain_spirv_len, "fsmain"},
    .blend = nvrhi::BlendState::RenderTarget(),
    .topology = nvrhi::PrimitiveType::LineList,
    .storage_count = 0,
    .push_bytes = sizeof(AxesParams),
};
// clang-format on

} // namespace

void world_add_grid(impl::WorldState &w) {
    impl::WorldItem &it = world_item_add(w, &kGridOps);
    it.state = new GridState{};
}

void world_add_axes(impl::WorldState &w) {
    impl::WorldItem &it = world_item_add(w, &kAxesOps);
    it.state = new AxesState{};
}

} // namespace sv
