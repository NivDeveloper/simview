#pragma once

#include "../render/Shader.h"
#include "../render/Target.h"

#include "../core/Math.h"
#include "Camera.h"
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
    // Pixels per world unit at unit view depth; perspective divides
    // by the depth, orthographic does not.
    float focal_px = 0.0f;
    bool orthographic = false;
    // The planes this frame draws inside of. The world tests items
    // against it before their submit; an item that wants it for its
    // own parts has it here.
    impl::Frustum frustum{};
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

    // False means "I do not know", not "empty": an item on the device
    // cannot walk its data, and the answer is to draw it.
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
    // Known before the framebuffer is, so the sort key can group by
    // it without waiting for the cache.
    std::uint32_t pipeline_id = 0;
    // What this item asked the device for last frame. The drill-down
    // from Stats::triangles, and the only way to ask a level-of-detail
    // rule what it actually chose rather than what it could have.
    std::uint64_t triangles = 0;
    // Off means SKIPPED at submit, not removed: the grid a reader
    // switched away is one they can switch back, and rebuilding its
    // buffers to do that would make the toggle cost what the item cost.
    bool visible = true;
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

// In pixels of radius — what a level of detail is chosen by, rather
// than the count of shapes.
inline float screen_radius(const WorldView &v, impl::Vec3 centre,
                           float radius) {
    if (v.orthographic)
        return radius * v.focal_px;
    // View space looks down -Z, so the depth is the negated z.
    const float depth = -impl::transform_point(v.world_to_view, centre).z;
    return depth > 1e-6f ? radius * v.focal_px / depth : 1e9f;
}

} // namespace sv
