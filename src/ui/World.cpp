// A world's registration and its camera controller.
//
// Here rather than in world/ for the same reason view_create is here:
// only ui knows what a panel is, and the camera is driven by a drag on
// one. This file is the ONE place in the engine that reads a mouse —
// and it reads ImGui's item state, never SDL's events, so the gesture
// is answered by whichever panel is under the cursor and nothing else.

#include "../world/World.h"
#include "../core/App.h"
#include "Icons.h"
#include "Ui.h"

#include <simview/World.h>

#include <imgui.h>

#include <cmath>
#include <string>

namespace sv {

// The gesture itself, from two facts the caller establishes: whether
// the pointer is over this world, and whether a drag on it is under
// way. A world in a panel gets both from the image item ImGui already
// latches; the window's world has no item, so the latch below is its.
void world_camera_gesture(impl::WorldState &w, bool hovered, bool active) {
    ImGuiIO &io = ImGui::GetIO();
    const bool left = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool right = ImGui::IsMouseDown(ImGuiMouseButton_Right);

    // No latch of our own: whoever the press landed on OWNS the drag
    // until the release, and both callers get that for free — an item
    // stays active while it is dragged, and a press that began outside
    // every panel keeps the pointer uncaptured however far it wanders.
    // A latch here was written first and removed once a drill proved
    // nothing could tell it apart.
    if (active && (left || right)) {
        // Per-frame deltas, not a remembered cursor: there is no state
        // of ours to seed on the first frame of a drag, so the jump
        // that a remembered position causes cannot happen.
        const ImVec2 d = io.MouseDelta;
        const bool pan = io.KeyShift || right;
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

// The scene's own controls: one button in a corner of the picture,
// and everything behind it.
//
// A 3D scene has no chrome to put a panel in — the picture IS the
// window — so its controls have to sit ON it, and a control sitting on
// a picture has to cost the picture almost nothing. One button does;
// a strip of them would be a panel with extra steps.
//
// What is behind it is what a reader of a 3D scene actually reaches
// for: somewhere known to get back to, the four standard directions,
// and the toggles that change what the picture MEANS rather than
// how it is arranged.

constexpr float kDeg = 3.14159265f / 180.0f;

// A preset keeps the focus and the distance the caller composed and
// turns the camera only — a "front view" that also jumped the zoom
// would be a different scene, not a different angle.
void world_look(impl::WorldState &w, float az_deg, float el_deg) {
    w.camera.q = impl::camera_pose(az_deg * kDeg, el_deg * kDeg);
}

// The presets as DATA, so the menu is a loop and there is no per-entry
// code for two of them to end up sharing. A check applies them from
// this same table, which is what makes "five entries, five views" a
// property of the table rather than of the check's idea of it. Home
// is not in it — a caller's opening view is not an angle.
constexpr Preset kViews[] = {{"front", -90.0f, 0.0f},
                             {"side", 0.0f, 0.0f},
                             {"top", -90.0f, 89.0f},
                             {"corner", -45.0f, 30.0f}};

const Preset *world_presets(std::size_t *count) {
    if (count)
        *count = sizeof kViews / sizeof kViews[0];
    return kViews;
}

void world_menu(impl::WorldState &w) {
    ImGui::SeparatorText("view");
    // Home is not a preset: it is whatever the caller composed, and no
    // angle in a table can stand for that.
    if (ImGui::Selectable("home"))
        world_camera(impl::World{&w}, w.home);
    for (const Preset &v : kViews)
        if (ImGui::Selectable(v.name))
            world_look(w, v.az, v.el);

    ImGui::SeparatorText("projection");
    bool ortho = w.camera.projection == impl::Projection::Orthographic;
    if (impl::icon_button(Icon::Perspective, "persp", "perspective", !ortho))
        w.camera.projection = impl::Projection::Perspective;
    ImGui::SameLine();
    if (impl::icon_button(Icon::Orthographic, "ortho", "orthographic", ortho))
        w.camera.projection = impl::Projection::Orthographic;

    if (w.grid || w.axes) {
        ImGui::SeparatorText("show");
        if (w.grid)
            ImGui::Checkbox("grid", &w.grid->visible);
        if (w.axes)
            ImGui::Checkbox("axes", &w.axes->visible);
    }
}

// Drawn at `at`, which is the top-left of the picture. The caller owns
// the window; this only knows where the corner is.
void world_controls(impl::WorldState &w, ImVec2 at) {
    if (!w.controls)
        return;
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    const float pad = ImGui::GetStyle().WindowPadding.x;
    ImGui::SetCursorScreenPos(ImVec2(at.x + pad, at.y + pad));
    ImGui::PushID(&w);
    if (impl::icon_button(Icon::Cube, "view", "camera and what is drawn"))
        ImGui::OpenPopup("##world_menu");
    if (ImGui::BeginPopup("##world_menu")) {
        world_menu(w);
        ImGui::EndPopup();
    }
    ImGui::PopID();
    // Put the cursor back for whatever the caller draws next, and
    // submit something after moving it: ImGui warns about a cursor
    // moved with nothing following, because that is how a window ends
    // up sized to content it never measured.
    ImGui::SetCursorScreenPos(keep);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// The window's world steers on whatever the panels did not claim.
// WantCaptureMouse is the whole test: it is true while a panel is
// hovered or owns a drag, and false over the scene behind them.
// A world drawn into the WINDOW has no ImGui window to hang a corner
// button on — the panels float over a swapchain nobody owns. So it
// gets one of its own: undecorated, unmoved, unsaved, and exactly the
// size of the button, which is as close to "drawn on the picture" as
// an ImGui item gets.
void ui_world_overlay(impl::App *a) {
    if (!a || !a->world || !a->world->controls)
        return;
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowBgAlpha(0.0f);
    if (ImGui::Begin("##world_controls", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoBringToFrontOnFocus))
        world_controls(*a->world, vp->WorkPos);
    ImGui::End();
}

void ui_world_input(impl::App *a) {
    if (!a || !a->world)
        return;
    const bool free = !ImGui::GetIO().WantCaptureMouse;
    world_camera_gesture(*a->world, free, free);
}

namespace impl {

// A world with no title IS the window: it draws into the swapchain
// and the panels float over it, which is what a program whose subject
// is the 3D scene wants. A titled one is a panel among panels, for a
// layout with several views. The two differ in nothing but the
// framebuffer they end up in.
World world_create(App *a, const WorldDesc &d) {
    if (!a)
        return {};
    if (!d.title || !*d.title) {
        if (a->world)
            return set_error("this app already has a world in its window — "
                             "a second one would draw over the first; give "
                             "it a title and it becomes a panel instead"),
                   World{};
        if (a->platform.win) {
            // The window's framebuffers are built with a depth image
            // from now on. Requested before the first rebuild, and the
            // chain is rebuilt here so the very next frame has one.
            a->platform.sc.want_depth = true;
            if (!swapchain_rebuild(a->platform.sc, a->platform.ndev))
                return World{};
        }
        a->world = std::make_unique<WorldState>();
        a->world->gpu = a->scene.gpu;
        a->world->stats = &a->stats;
        a->world->pipelines = &a->world_pipelines;
        a->world->gates = &a->gates;
        a->world->camera.q = camera_pose(-0.7853981634f, 0.5235987756f);
        a->world->samples = a->platform.vk.samples;
        a->world->controls = d.controls;
        if (d.grid)
            world_add_grid(*a->world);
        if (d.axes)
            world_add_axes(*a->world);
        return World{a->world.get()};
    }
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
    v.world->samples = a->platform.vk.samples;
    v.world->controls = d.controls;
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
    // Remembered whole, because "home" is the view the caller composed
    // and every preset is a departure from it.
    ws->home = d;
    ws->camera.fovy = d.fov_deg * kDeg;
    // The projection is the ONLY thing an orthographic camera changes:
    // the pose, the turntable and the depth convention are shared, so
    // switching it mid-run holds everything else still.
    ws->camera.projection = d.projection == sv::Projection::Orthographic
                                ? impl::Projection::Orthographic
                                : impl::Projection::Perspective;
}

bool world_light(World w, const LightDesc &d) {
    WorldState *ws = static_cast<WorldState *>(w.p);
    if (!ws)
        return false;
    if (ws->lights.size() >= 4)
        return set_error("a world takes at most four lights — the set is "
                         "fixed so every shader's lighting is one loop with "
                         "no branch on which lights exist"),
               false;
    WorldState::Light l;
    l.direction =
        normalize(Vec3{d.direction[0], d.direction[1], d.direction[2]});
    if (length(l.direction) <= 0.0f)
        return set_error("a light needs a direction that is not the zero "
                         "vector — it names where the light comes FROM"),
               false;
    for (int k = 0; k < 3; ++k)
        l.color[k] = d.color[k];
    l.intensity = d.intensity;
    ws->lights.push_back(l);
    return true;
}

void world_ambient(World w, const float rgb[3]) {
    if (WorldState *ws = static_cast<WorldState *>(w.p))
        for (int k = 0; k < 3; ++k)
            ws->ambient[k] = rgb[k];
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
