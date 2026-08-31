#pragma once

// Internal to src/ — what a world item IS, and what it hands the world.
//
// The 2D scene records its draws where they are built. A world cannot:
// what is drawn where depends on the camera, so an item SUBMITS a
// description and the world decides the order. That one indirection is
// the whole optimization surface — sorting lives behind it today, and
// culling or batching would land behind it without an item noticing.
//
// Like a 2D kind, an item is data: one static WorldItemOps per kind,
// carrying its functions, its shaders and its pipeline shape. Nothing
// switches on a kind and nothing enumerates them.

#include "../render/Shader.h"
#include "../render/Target.h"

#include "Math.h"
#include "Passes.h"
#include <simview/App.h>

#include <cstdint>
#include <vector>

namespace sv {

namespace impl {
struct WorldItem;
} // namespace impl

// What every item needs about the camera this frame. The matrices are
// uploaded once per world; this is the CPU-side copy items sort with.
struct WorldView {
    impl::Mat4 world_to_clip{}, world_to_view{}, view_to_clip{};
    impl::Vec3 camera_pos{};
    float znear = 1e-4f;
    // How far the camera orbits its focus — the ONE length that says
    // how big the view is, which is what anything sized to the view
    // rather than to the world scales by.
    float distance = 1.0f;
    std::uint32_t tw = 0, th = 0;
    // Pixels per world unit at one unit of view depth, and whether
    // the depth divides it. Together they answer "how big is this
    // going to be on screen", which is the question a level of detail
    // is really asking.
    float focal_px = 0.0f;
    bool orthographic = false;
    // The planes this frame draws inside of. The world tests items
    // against it before their submit; an item that wants it for its
    // own parts has it here.
    impl::Frustum frustum{};

    // The caster's half of the frame. `shadow_map` is always a real
    // texture — one texel when nothing casts — because the binding
    // layout is the same either way, and two layouts for one item
    // would split the pipeline cache on a world's lighting.
    impl::Mat4 light_to_clip{};
    nvrhi::ITexture *shadow_map = nullptr;
    nvrhi::ISampler *shadow_sampler = nullptr;
    std::uint32_t shadow_px = 0; // the map's edge, 0 when off
    // Whether a light SHINES on the ground, which is a different
    // question from whether the map is live: the ground takes a tone
    // from the first and a shadow from the second.
    bool lit_ground = false;
    float shadow_bias = 0.0f; // in light-space depth units
    // The world's constant buffer, holding exactly these numbers. An
    // item binds it beside its own storage, which is why the binding
    // set is the item's to make and not the world's.
    nvrhi::IBuffer *view_cb = nullptr;
};

// One recorded draw, not yet ordered. POD and small on purpose: the
// sort moves these, so nothing here may be expensive to copy.
struct DrawCmd {
    std::uint64_t key = 0;
    std::uint32_t seq = 0; // submission index — the deterministic tail
    PassId pass = PassId::Opaque;
    impl::WorldItem *item = nullptr;
    std::uint32_t part = 0; // which draw of the item's own, if it has several
};

struct WorldPipelineEntry {
    const struct WorldItemOps *ops;
    PassId pass;
    nvrhi::Format color, depth;
    std::uint32_t samples;
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BindingLayoutHandle layout;
};

struct WorldItemOps {
    const char *name;
    PassId pass;

    // With no pass open: resolve a pull source, upload what the host
    // changed. Exactly the 2D contract.
    void (*prepare)(impl::WorldItem &, nvrhi::ICommandList *) = nullptr;

    // Describe this frame's draws. An item computes its own depth key
    // — only it knows where its geometry is.
    void (*submit)(impl::WorldItem &, const WorldView &,
                   std::vector<DrawCmd> &) = nullptr;

    // Record one of them. Bindings and push constants are the item's
    // own; the pipeline comes from the world's cache.
    void (*draw)(impl::WorldItem &, const DrawCmd &, nvrhi::ICommandList *,
                 nvrhi::IFramebuffer *, const WorldView &) = nullptr;

    void (*release)(impl::WorldItem &) = nullptr;

    // The same geometry from the LIGHT's side, depth only. Null means
    // this item casts no shadow — which is the right answer for a
    // ground plane and for anything drawn as an overlay, and the
    // world reads it rather than asking the item to remember.
    //
    // A fragment stage even though nothing is written: a billboard
    // has to discard outside its disc or it casts a square, and one
    // shape of pipeline is simpler than two.
    void (*draw_shadow)(impl::WorldItem &, const DrawCmd &,
                        nvrhi::ICommandList *, nvrhi::IFramebuffer *,
                        const WorldView &) = nullptr;
    Shader shadow_vs, shadow_fs;

    // The item's extent in world units, if it has one. Read by the
    // world, not by the item: it is what decides whether `submit` is
    // called at all, and where the near plane goes.
    //
    // FALSE means "I do not know", which is not the same as empty. An
    // item whose data lives on the device cannot walk it to find out,
    // and the answer to that is to draw it — never to guess a box and
    // cull against the guess.
    bool (*bounds)(const impl::WorldItem &, impl::Vec3 *lo,
                   impl::Vec3 *hi) = nullptr;

    Shader vs, fs;
    nvrhi::BlendState::RenderTarget blend;
    nvrhi::PrimitiveType topology = nvrhi::PrimitiveType::TriangleList;

    // How many storage buffers the item binds, at bindings 1..n: the
    // grid and the axes compute their geometry from the vertex id and
    // bind none, a cloud binds its positions and its colour source.
    std::uint32_t storage_count = 0;
    std::uint32_t push_bytes = 0;
};

namespace impl {

struct WorldState;

struct WorldItem {
    WorldState *owner = nullptr; // the world it was added to
    Gpu gpu;
    Stats *stats = nullptr;
    std::vector<WorldPipelineEntry> *pipelines = nullptr;
    const WorldItemOps *ops = nullptr;
    void *state = nullptr; // known only to the item's own file
    std::uint32_t id = 0;  // dense within a world, breaking key ties
    // Which pipeline this item will resolve to, known before the
    // framebuffer is: the ops decide it, so items that share an ops
    // share this, and the sort key can group them without waiting for
    // the cache.
    std::uint32_t pipeline_id = 0;
    // What this item asked the device for last frame. The drill-down
    // from Stats::triangles, and the only way to ask a level-of-detail
    // rule what it actually chose rather than what it could have.
    std::uint64_t triangles = 0;
};

} // namespace impl

// The world's pipeline cache. Keyed on (ops, pass, formats): the same
// item drawn in two passes wants two depth states, and a target with a
// depth attachment is not compatible with one without.
const WorldPipelineEntry *world_pipeline_for(const impl::Gpu &,
                                             std::vector<WorldPipelineEntry> &,
                                             Stats *, const WorldItemOps *,
                                             PassId, nvrhi::IFramebuffer *);

void world_pipelines_release(std::vector<WorldPipelineEntry> &);

// Where a pipeline sits in the cache — the sort key's state field.
std::uint32_t world_pipeline_id(const std::vector<WorldPipelineEntry> &,
                                const WorldPipelineEntry *);

// Where the shadow map and its comparison sampler sit in every world
// item's binding set. Above the storage buffers rather than beside
// them: the binding offsets are all zeroed, so a slot means one thing
// across every descriptor type, and leaving room means a fourth
// storage buffer does not renumber the shadow map.
inline constexpr std::uint32_t kShadowMapBinding = 8;
inline constexpr std::uint32_t kShadowSamplerBinding = 9;

// How big something of this world radius, at this world point, comes
// out on screen — in pixels of radius. What a level of detail should
// be chosen by: a shape is worth triangles when it is large enough
// for them to show, and the count of shapes has nothing to do with it.
inline float screen_radius(const WorldView &v, impl::Vec3 centre,
                           float radius) {
    if (v.orthographic)
        return radius * v.focal_px;
    // View space looks down -Z, so the depth is the negated z.
    const float depth = -impl::transform_point(v.world_to_view, centre).z;
    return depth > 1e-6f ? radius * v.focal_px / depth : 1e9f;
}

} // namespace sv
