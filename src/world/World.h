#pragma once

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
    std::vector<const WorldItemOps *> ops_seen; // index = pipeline id
    int untracked_pulls = 0;
    bool cull = true; // off only from the probe, to compare pictures

    Camera3 camera{};
    CameraDesc home{}; // what the chrome's "home" returns to
    WorldItem *grid = nullptr;
    WorldItem *axes = nullptr;
    bool controls = true;
    // The flight's speed, in orbit distances a second. The wheel
    // scales it: in flight there is nothing to dolly toward.
    float fly_speed = 1.0f;

    // At most four: a fixed set keeps every shader's lighting one
    // loop. Empty means one light at the camera.
    struct Light {
        Vec3 direction{0.0f, 0.0f, 1.0f}; // toward the light, world space
        float color[3] = {1.0f, 1.0f, 1.0f};
        float intensity = 0.7f;
    };
    std::vector<Light> lights;
    float ambient[3] = {0.3f, 0.3f, 0.3f};

    // Volatile: the renderer versions it, so a binding set built once
    // stays valid as the value changes.
    nvrhi::BufferHandle view_cb;

    // Rebuilt every frame, kept so the allocation is not.
    std::vector<DrawCmd> cmds;

    // What the last draw looked through, for a pick to look back
    // through: a click lands on the picture the reader saw.
    WorldView last_view{};
    bool viewed = false;
    // The picture's rect in window points, set by whoever draws it.
    float rect[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    struct PickCb {
        void (*fn)(const Pick &, void *);
        void *user;
        void (*free)(void *);
    };
    std::vector<PickCb> picks;

    // A stroke of the pointer through the picture, handed to whoever
    // asked, while the cut tool is on.
    struct StrokeCb {
        void (*fn)(const Stroke &, void *);
        void *user;
        void (*free)(void *);
    };
    std::vector<StrokeCb> strokes;
    int tool = 0; // sv::Tool
    bool stroking = false;
    float stroke_x = 0.0f, stroke_y = 0.0f; // window points, last frame
    Cloud stroke_item{};                    // what the drag began on
    std::int32_t stroke_index = -1;

    // The item and element the focus tracks, when it tracks one.
    WorldItem *followed = nullptr;
    std::uint32_t follow_index = 0;
    // What the pointer is over this frame, for the picture to brighten.
    WorldItem *hovered = nullptr;
    std::uint32_t hover_index = 0;
    bool hovered_now = false; // the pointer was over the world at all
    // Whether a device-resident item should keep a host copy this
    // frame: only while something asks the world what is where.
    bool want_host = false;

    // A tier is a triangle budget, not a look.
    struct Mesh {
        int shape = 0;
        int tier = 0;
        nvrhi::BufferHandle vertices;
        nvrhi::BufferHandle indices;
        std::uint32_t index_count = 0;
        std::uint32_t triangles = 0;
    };
    // A deque: an item keeps the address of the shape it resolved.
    std::deque<Mesh> meshes;

    // The readback: one compute pipeline every device-resident item
    // shares, made on first use.
    nvrhi::ComputePipelineHandle readback;
    nvrhi::BindingLayoutHandle readback_layout;

    // Drawn into when the device can multisample, resolved into the
    // caller's target at the end of the frame.
    nvrhi::TextureHandle ms_color, ms_depth;
    nvrhi::FramebufferHandle ms_fb;
    std::uint32_t ms_w = 0, ms_h = 0, samples = 1;
};

} // namespace impl

// The Platform is here for its timing: a world stamps each pass.
void world_draw_into(impl::WorldState &, impl::Platform &,
                     nvrhi::ICommandList *, nvrhi::IFramebuffer *,
                     std::uint32_t w, std::uint32_t h);

void world_draw(impl::WorldState &, impl::Platform &, nvrhi::ICommandList *,
                impl::RenderTarget &);

void world_release(impl::WorldState &);

// A click at window point (x, y), through the last view drawn: the
// nearest hit over every visible item. False when nothing was hit, or
// nothing has been drawn yet.
bool world_pick(impl::WorldState &, float x, float y, Pick *);

// Hand a pick to everyone who asked.
void world_picked(impl::WorldState &, const Pick &);

// A stroke from window point (x0, y0) to (x1, y1), through the last
// view drawn, to everyone who asked. Nothing has been drawn: nothing.
void world_stroke(impl::WorldState &, float x0, float y0, float x1, float y1);

// The readback pipeline, made on first use: `src` at binding 0, `dst`
// at 1, a count in push constants. Null when the device refused.
nvrhi::IComputePipeline *world_readback(impl::WorldState &);

void world_add_grid(impl::WorldState &);
void world_add_axes(impl::WorldState &);

impl::WorldItem &world_item_add(impl::WorldState &, const WorldItemOps *);

// Belongs in an item's prepare: the list is the frame's, and by draw
// time it already has a pass open.
const impl::WorldState::Mesh *world_mesh(impl::WorldState &, int shape,
                                         int tier, nvrhi::ICommandList *);

// No command list, so a submit may ask it.
const impl::WorldState::Mesh *world_mesh_ready(const impl::WorldState &,
                                               int shape, int tier);

} // namespace sv
