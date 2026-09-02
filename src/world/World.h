#pragma once

// Internal to src/ — a world: a camera, its items, and the one buffer
// that tells every shader where the camera is.
//
// The 2D sibling is SceneState. What differs is not the list of items
// but the two-phase draw: a scene records in registration order, a
// world collects, orders, and replays by pass. Everything else — the
// three data doors, the Sync gates, the untracked-pull accounting — is
// the same machinery, because a world sits behind the same frame.

#include "../platform/Timing.h"
#include "../render/Target.h"
#include "Geometry.h"
#include "Items.h"

#include <cstdint>
#include <deque>
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
    // Off only from the test probe. Culling is supposed to be
    // invisible — the same picture, fewer draws — and the only way to
    // assert that is to render both and compare.
    bool cull = true;

    Camera3 camera{};
    // The pose world_camera was last given: what "home" means, since a
    // caller's opening view is the one they composed and the presets
    // are all departures from it.
    CameraDesc home{};
    // The two built-in items, kept so the chrome can hide them. Null
    // when the world was created without them.
    WorldItem *grid = nullptr;
    WorldItem *axes = nullptr;
    bool controls = true;

    // At most four, because a fixed set in the view block is what
    // keeps every shader's lighting one loop with no branch on which
    // lights exist. EMPTY means a single light at the camera, so a
    // world nobody lit still reads as three-dimensional and lighting
    // costs a caller nothing until it wants to spend something.
    struct Light {
        Vec3 direction{0.0f, 0.0f, 1.0f}; // toward the light, world space
        float color[3] = {1.0f, 1.0f, 1.0f};
        float intensity = 0.7f;
    };
    std::vector<Light> lights;
    float ambient[3] = {0.3f, 0.3f, 0.3f};

    // One volatile constant buffer per world, written once a frame
    // before any state is set. Volatile because the renderer versions
    // it internally: the binding sets items make against it stay valid
    // as the value changes, which is what lets a set be built once.
    nvrhi::BufferHandle view_cb;

    // Rebuilt every frame, kept so the allocation is not.
    std::vector<DrawCmd> cmds;

    // The built-in shapes, made once and shared by every item that
    // asks for one. A tier is a triangle budget, not a look: the same
    // sphere serves a dozen instances and fifty thousand only if
    // nobody minds it being wrong for one of them.
    struct Mesh {
        int shape = 0;
        int tier = 0;
        nvrhi::BufferHandle vertices;
        nvrhi::BufferHandle indices;
        std::uint32_t index_count = 0;
        std::uint32_t triangles = 0;
    };
    // A deque, not a vector: an item KEEPS the address of the shape it
    // resolved, and a second shape appearing in the same world would
    // move the first one out from under it. The same reason
    // SceneState holds its items in a list.
    std::deque<Mesh> meshes;

    // Where the world actually draws when the device can multisample:
    // a colour and depth pair carrying several samples a pixel, which
    // is RESOLVED into the caller's target at the end of the frame.
    // A silhouette is a hard edge — the one thing no amount of
    // shading fixes — and this is what softens it.
    nvrhi::TextureHandle ms_color, ms_depth;
    nvrhi::FramebufferHandle ms_fb;
    std::uint32_t ms_w = 0, ms_h = 0, samples = 1;
};

} // namespace impl

// Fill the constants, collect, order, replay. The whole frame of a
// world, against whatever framebuffer it draws into — a view's target
// or the window itself. The depth attachment is the framebuffer's
// own: a world cannot draw without one.
// The Platform is here for its timing: a world stamps its passes
// separately, which is the only way an attribution — this much in the
// opaque pass, that much in the transparent — can be read off a frame
// at all.
void world_draw_into(impl::WorldState &, impl::Platform &,
                     nvrhi::ICommandList *, nvrhi::IFramebuffer *,
                     std::uint32_t w, std::uint32_t h);

// The same, for a world shown in a panel.
void world_draw(impl::WorldState &, impl::Platform &, nvrhi::ICommandList *,
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

// A built-in shape, made on first ask and kept. `shape` is
// CloudShape's Sphere or Cube; the tier is the item's choice of
// triangle budget.
// The list is the FRAME's, and the call belongs in an item's prepare:
// a second immediate list open at the same time is refused, and by
// draw time the frame's already has a pass on it.
const impl::WorldState::Mesh *world_mesh(impl::WorldState &, int shape,
                                         int tier, nvrhi::ICommandList *);

// The same mesh if it has already been made, and null if it has not.
// No command list, so it is what a submit can ask: by then the choice
// is between things prepare already built.
const impl::WorldState::Mesh *world_mesh_ready(const impl::WorldState &,
                                               int shape, int tier);

} // namespace sv
