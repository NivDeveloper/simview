// A world's registration and its camera controller.
//
// Here rather than in world/ for the same reason view_create is here:
// only ui knows what a panel is, and the camera is driven by a drag on
// one. This file is the ONE place in the engine that reads a mouse —
// and it reads ImGui's item state, never SDL's events, so the gesture
// is answered by whichever panel is under the cursor and nothing else.

#include "../world/World.h"
#include "../core/App.h"
#include "Ui.h"

#include <simview/World.h>

#include <imgui.h>

#include <cmath>
#include <string>

namespace sv {

// Called from view_draw, right after the image: ImGui has just told us
// whether the cursor is over it and whether a drag started there. A
// gesture that began elsewhere must not steer this camera, which is
// what IsItemActive answers and a raw mouse position could not.
void world_camera_input(impl::WorldState &w) {
    ImGuiIO &io = ImGui::GetIO();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    if (active) {
        const ImVec2 d = io.MouseDelta;
        // Per-frame deltas, not a remembered cursor: there is no state
        // of ours to seed on the first frame of a drag, so the jump
        // that a remembered position causes cannot happen.
        const bool pan =
            io.KeyShift || ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (d.x != 0.0f || d.y != 0.0f) {
            if (pan)
                impl::camera_pan(w.camera, d.x, d.y);
            else
                impl::camera_orbit(w.camera, d.x, d.y);
        }
    }
    if (hovered && io.MouseWheel != 0.0f)
        impl::camera_dolly(w.camera, io.MouseWheel);
}

namespace impl {

World world_create(App *a, const WorldDesc &d) {
    if (!a)
        return {};
    if (!d.title || !*d.title)
        return set_error("a world needs a title — it names the panel it "
                         "is shown in"),
               World{};
    if (title_taken(a, d.title))
        return set_error(std::string("\"") + d.title +
                         "\" is already the title of a plot, panel or "
                         "view, and two windows of one name draw into "
                         "each other"),
               World{};

    View &v = a->views.emplace_back();
    v.title = d.title;
    v.app = a;
    // A world tests depth, so its target carries an attachment a 2D
    // view never asks for. Set before the first resize: the framebuffer
    // is built from it.
    v.target.want_depth = true;
    v.world = std::make_unique<WorldState>();
    v.world->gpu = a->scene.gpu;
    v.world->stats = &a->stats;
    v.world->pipelines = &a->world_pipelines;
    v.world->gates = &a->gates;
    v.world->camera.q = camera_pose(-0.7853981634f, 0.5235987756f);
    if (d.grid)
        world_add_grid(*v.world);
    if (d.axes)
        world_add_axes(*v.world);
    a->ui.cbs.push_front(
        {[](void *u) { view_draw(*static_cast<View *>(u)); }, &v});
    return World{v.world.get()};
}

void world_camera(World w, const CameraDesc &d) {
    WorldState *ws = static_cast<WorldState *>(w.p);
    if (!ws)
        return;
    constexpr float kDeg = 3.14159265f / 180.0f;
    ws->camera.focus = {d.focus[0], d.focus[1], d.focus[2]};
    ws->camera.distance = d.distance > 0.0f ? d.distance : 5.0f;
    ws->camera.q = camera_pose(d.azimuth_deg * kDeg, d.elevation_deg * kDeg);
    ws->camera.fovy = d.fov_deg * kDeg;
}

// The gate bookkeeping a scene does, for the same reasons: a Publish
// stamps itself with the compute device's submitted ticket so the
// frame can wait GPU-side for exactly the work behind what it shows,
// and a source with no Sync makes the frame wait for everything.
void world_track(World w, SyncGate g) {
    WorldState *ws = static_cast<WorldState *>(w.p);
    if (!ws || !ws->gates || !g)
        return;
    if (ws->gpu.gdev)
        sync_gate_set_stamper(
            g,
            +[](void *u) {
                return static_cast<gpud::Device *>(u)->submitted().value;
            },
            ws->gpu.gdev);
    for (SyncGate have : *ws->gates)
        if (have.p == g.p)
            return;
    sync_gate_retain(g);
    ws->gates->push_back(g);
}

void world_untracked_pull(World w) {
    if (WorldState *ws = static_cast<WorldState *>(w.p))
        ++ws->untracked_pulls;
}

} // namespace impl
} // namespace sv
