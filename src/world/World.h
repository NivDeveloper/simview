#pragma once

// Internal to src/ — a world: a camera, its items, and the one buffer
// that tells every shader where the camera is.
//
// The 2D sibling is SceneState. What differs is not the list of items
// but the two-phase draw: a scene records in registration order, a
// world collects, orders, and replays by pass. Everything else — the
// three data doors, the Sync gates, the untracked-pull accounting — is
// the same machinery, because a world sits behind the same frame.

#include "../scene/Target.h"
#include "Items.h"

#include <cstdint>
#include <list>
#include <memory>
#include <vector>

namespace sv {
namespace impl {

struct WorldState {
    Gpu gpu;
    Stats *stats = nullptr;
    std::vector<WorldPipelineEntry> *pipelines = nullptr;
    std::vector<SyncGate> *gates = nullptr; // the App's, flipped per frame
    std::list<WorldItem> items;             // an item's ADDRESS is its handle
    // Every ops an item has used, in first-use order: an item's index
    // here is its pipeline id.
    std::vector<const WorldItemOps *> ops_seen;
    int untracked_pulls = 0;

    Camera3 camera{};

    // One volatile constant buffer per world, written once a frame
    // before any state is set. Volatile because the renderer versions
    // it internally: the binding sets items make against it stay valid
    // as the value changes, which is what lets a set be built once.
    nvrhi::BufferHandle view_cb;

    // Rebuilt every frame, kept so the allocation is not.
    std::vector<DrawCmd> cmds;
};

} // namespace impl

// Fill the constants, collect, order, replay. The whole frame of a
// world; the caller has already resized the target.
void world_draw(impl::WorldState &, nvrhi::ICommandList *,
                impl::RenderTarget &);

void world_release(impl::WorldState &);

// The internal items every world may carry. Registered like any other
// item, through the same ops table — three kinds through the contract
// on day one is what proves the contract is one.
void world_add_grid(impl::WorldState &);
void world_add_axes(impl::WorldState &);

// A new item, wired to the world's device, counters and cache. The
// kinds' own create functions call this and fill in the state.
impl::WorldItem &world_item_add(impl::WorldState &, const WorldItemOps *);

} // namespace sv
