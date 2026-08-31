#pragma once

// Internal to src/ — the scene's state and its five functions. The
// public Scene.h is the builder; this is what it drives.
//
// An item carries the GPU PAIR and a COUNTER BLOCK, never the App.
// That is what lets a kind's file include nothing above this layer:
// the upload needs the renderer device, the pull-model resolve needs
// the compute device, the draw bumps a counter — and those are the
// whole of what a kind asks of the world outside its own state.

#include "../core/Error.h"
#include "Kinds.h"

#include <simview/App.h>
#include <simview/Scene.h>
#include <simview/sync/Sync.h>

#include <gpud/Device.h>
#include <nvrhi/nvrhi.h>

#include <list>
#include <vector>

namespace sv {
namespace impl {

struct PipelineEntry;

// The two devices a kind may speak to: NVRHI for its own resources,
// gpud for resolving a pull source's native buffer.
struct Gpu {
    nvrhi::IDevice *dev = nullptr;
    gpud::Device *gdev = nullptr;
};

struct SceneItem {
    Gpu gpu;                                         // the upload + resolve
    Stats *stats = nullptr;                          // the counters
    std::vector<PipelineEntry> *pipelines = nullptr; // the cache
    const KindOps *ops = nullptr;                    // what it IS
    void *state = nullptr; // known only to the kind's own file
};

// What is drawn to a target, in registration order. std::list because
// an item's ADDRESS is the public handle.
struct SceneState {
    Gpu gpu;
    Stats *stats = nullptr;
    std::vector<PipelineEntry> *pipelines = nullptr;
    std::vector<SyncGate> *gates = nullptr; // the App's, flipped per frame
    std::list<SceneItem> items;
    // Pull sources with no Sync behind them: the frame's device wait
    // must then cover everything submitted, not just shown stamps.
    int untracked_pulls = 0;
    // The range every item maps into. Unset means: the first item with
    // a natural grid, in cells, so a lattice and the points over it
    // share coordinates; with no such item, the unit square.
    Range2 range{};
};

// Keyed on BOTH: a request that matched on format alone would
// silently bind another kind's pipeline. The layout rides along so a
// kind can make binding sets against it.
struct PipelineEntry {
    const KindOps *kind;
    nvrhi::Format format;
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BindingLayoutHandle layout;
};

} // namespace impl

// Prepare every item, then ONE pass — the clear belongs to the scene,
// not to an item, or only the first item could composite.
void scene_draw(impl::SceneState &, nvrhi::ICommandList *,
                nvrhi::IFramebuffer *, std::uint32_t tw, std::uint32_t th,
                nvrhi::Format);

// Release every item and empty the list.
void scene_release(impl::SceneState &);

// The first item with a natural grid, if any — what a scene's default
// range and a shot's dimensions are both derived from.
bool scene_grid(const impl::SceneState &, Extent2 *);

// The pipeline cache, and its teardown. Returns null (and reports)
// when a pipeline cannot be built; the entry carries the layout the
// kind's binding sets must be created against.
const impl::PipelineEntry *pipeline_for(const impl::Gpu &,
                                        std::vector<impl::PipelineEntry> &,
                                        Stats *, const KindOps *,
                                        nvrhi::IFramebuffer *);
void pipelines_release(std::vector<impl::PipelineEntry> &);

} // namespace sv
