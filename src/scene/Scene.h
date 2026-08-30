#pragma once

// Internal to src/ — the scene's state and its five functions. The
// public Scene.h is the builder; this is what it drives.
//
// An item carries a DEVICE and a COUNTER BLOCK, never the App. That is
// what lets a kind's file include nothing above this layer: the
// upload needs the device and the draw bumps a counter, and those are
// the whole of what a kind asks of the world outside its own state.

#include "../core/Error.h"
#include "Kinds.h"

#include <simview/App.h>
#include <simview/Scene.h>
#include <simview/sync/Sync.h>

#include <SDL3/SDL.h>
#include <gpud/Sdl.h>

#include <list>
#include <vector>

namespace sv {
namespace impl {

struct PipelineEntry;

struct SceneItem {
    SDL_GPUDevice *dev = nullptr;                    // the upload
    Stats *stats = nullptr;                          // the counters
    std::vector<PipelineEntry> *pipelines = nullptr; // the cache
    const KindOps *ops = nullptr;                    // what it IS
    void *state = nullptr; // known only to the kind's own file
};

// What is drawn to a target, in registration order. std::list because
// an item's ADDRESS is the public handle.
struct SceneState {
    SDL_GPUDevice *dev = nullptr;
    Stats *stats = nullptr;
    std::vector<PipelineEntry> *pipelines = nullptr;
    std::vector<SyncGate> *gates = nullptr; // the App's, flipped per frame
    std::list<SceneItem> items;
    // The range every item maps into. Unset means: the first item with
    // a natural grid, in cells, so a lattice and the points over it
    // share coordinates; with no such item, the unit square.
    Range2 range{};
};

// Keyed on BOTH: a request that matched on format alone would
// silently bind another kind's pipeline.
struct PipelineEntry {
    const KindOps *kind;
    SDL_GPUTextureFormat format;
    SDL_GPUGraphicsPipeline *pipeline;
};

} // namespace impl

// Prepare every item, then ONE pass — the clear belongs to the scene,
// not to an item, or only the first item could composite.
void scene_draw(impl::SceneState &, SDL_GPUCommandBuffer *,
                SDL_GPUTexture *target, Uint32 tw, Uint32 th,
                SDL_GPUTextureFormat);

// Release every item and empty the list.
void scene_release(impl::SceneState &);

// The first item with a natural grid, if any — what a scene's default
// range and a shot's dimensions are both derived from.
bool scene_grid(const impl::SceneState &, Extent2 *);

// The pipeline cache, and its teardown.
SDL_GPUGraphicsPipeline *pipeline_for(SDL_GPUDevice *,
                                      std::vector<impl::PipelineEntry> &,
                                      Stats *, const KindOps *,
                                      SDL_GPUTextureFormat);
void pipelines_release(SDL_GPUDevice *, std::vector<impl::PipelineEntry> &);

} // namespace sv
