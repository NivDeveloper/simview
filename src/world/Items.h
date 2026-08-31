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

#include "../scene/Kinds.h"
#include "../scene/Target.h"
#include "Math.h"
#include "Passes.h"

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
    std::uint32_t tw = 0, th = 0;
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

    // The item's extent in world units, if it has one. Null in W1 and
    // read by nobody: it is the hook a bounds-driven near plane and a
    // shadow frustum both need, declared where they will look for it.
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

} // namespace sv
