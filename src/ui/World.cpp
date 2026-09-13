#include "../world/World.h"
#include "../core/App.h"
#include "Icons.h"
#include "Ui.h"

#include <simview/World.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace sv {

// From two facts the caller establishes: whether the pointer is over
// this world, and whether a drag on it is under way.
void world_camera_gesture(impl::WorldState &w, bool hovered, bool active) {
    ImGuiIO &io = ImGui::GetIO();
    const bool left = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool right = ImGui::IsMouseDown(ImGuiMouseButton_Right);

    // With the cut tool on, a left drag is a stroke through the picture
    // — from where the pointer was last frame to where it is — and the
    // right button orbits in its place.
    const bool cutting = w.tool == int(Tool::Cut) && !w.strokes.empty();
    if (active && left && cutting) {
        if (w.stroking && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
            world_stroke(w, w.stroke_x, w.stroke_y, io.MousePos.x,
                         io.MousePos.y);
        w.stroking = true;
        w.stroke_x = io.MousePos.x;
        w.stroke_y = io.MousePos.y;
    } else {
        w.stroking = false;
    }

    // No latch of our own: whoever the press landed on owns the drag
    // until the release, and both callers get that for free.
    if (active && (right || (left && !cutting))) {
        // Per-frame deltas, not a remembered cursor: there is no state
        // of ours to seed on the first frame of a drag, so the jump
        // that a remembered position causes cannot happen.
        const ImVec2 d = io.MouseDelta;
        const bool pan = io.KeyShift || (right && !cutting);
        if (d.x != 0.0f || d.y != 0.0f) {
            if (pan) {
                w.followed = nullptr; // the reader took the focus back
                w.camera.pan(d.x, d.y);
            } else {
                w.camera.orbit(d.x, d.y);
            }
        }
    }
    if (hovered && io.MouseWheel != 0.0f)
        w.camera.dolly(io.MouseWheel);

    // What the pointer is over, every frame it is over the world with
    // no button down: the highlight the picture shows.
    w.hovered_now = hovered;
    w.hovered = nullptr;
    if (hovered && !left && !right) {
        Pick p{};
        if (world_pick(w, io.MousePos.x, io.MousePos.y, &p) && !p.Ground()) {
            w.hovered = static_cast<impl::WorldItem *>(p.cloud.p);
            w.hover_index = std::uint32_t(p.index);
        }
    }

    // A click is a release that never dragged, and ImGui keeps the
    // distance; a double-click makes the point the pivot, distance
    // and pose untouched.
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const ImVec2 dragged = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        Pick p{};
        if (dragged.x == 0.0f && dragged.y == 0.0f &&
            world_pick(w, io.MousePos.x, io.MousePos.y, &p))
            world_picked(w, p);
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        Pick p{};
        if (world_pick(w, io.MousePos.x, io.MousePos.y, &p)) {
            w.followed = nullptr;
            w.camera.frame({p.point[0], p.point[1], p.point[2]},
                           w.camera.distance());
        }
    }
}

namespace {

constexpr float kFlyPerSecond = 1.0f; // orbit distances, before the multiplier
constexpr float kFlyShift = 4.0f;
constexpr float kFlyWheel = 0.25f; // e-folds of speed per wheel notch
constexpr float kPadTurn = 500.0f; // pixels a second at full deflection

// The keys, the captured pointer and the pad, once a frame. The keys
// and the pointer are a flight's; the pad steers without one. The step
// scales with the orbit distance as a pan does.
void world_camera_steer(impl::WorldState &w, impl::Input &in, float dt,
                        bool flying) {
    const auto down = [&](Key k) {
        return flying && in.held.test(std::size_t(k));
    };
    const impl::Gamepad &g = in.pad;
    if (in.wheel != 0.0f)
        w.fly_speed = std::clamp(w.fly_speed * std::exp(in.wheel * kFlyWheel),
                                 0.05f, 20.0f);

    // Both devices at once, and a key plus a stick is still ONE full
    // deflection: swapping hands mid-move must not double the speed.
    const auto axis = [](float x) { return std::clamp(x, -1.0f, 1.0f); };
    const float v = kFlyPerSecond * w.camera.distance() * w.fly_speed *
                    (down(Key::LeftShift) || g.fast ? kFlyShift : 1.0f) * dt;
    const float ahead = axis((down(Key::W) ? 1.0f : 0.0f) -
                             (down(Key::S) ? 1.0f : 0.0f) - g.ly);
    const float right = axis((down(Key::D) ? 1.0f : 0.0f) -
                             (down(Key::A) ? 1.0f : 0.0f) + g.lx);
    const float up = axis((down(Key::E) ? 1.0f : 0.0f) -
                          (down(Key::Q) ? 1.0f : 0.0f) + g.rt - g.lt);
    if (ahead != 0.0f || right != 0.0f || up != 0.0f)
        w.camera.move(right * v, up * v, ahead * v);

    const float dx = (flying ? in.look_dx : 0.0f) + g.rx * kPadTurn * dt;
    const float dy = (flying ? in.look_dy : 0.0f) + g.ry * kPadTurn * dt;
    if (dx != 0.0f || dy != 0.0f)
        w.camera.turn(dx, dy);
    in.look_dx = in.look_dy = in.wheel = 0.0f;
}

// Forgotten at take-off: a release can land in another window, and a
// press remembered from before the flight would move it forever.
void forget_keys(impl::App *a) {
    a->input.held.reset();
    a->input.look_dx = a->input.look_dy = a->input.wheel = 0.0f;
}

} // namespace

void world_fly_begin(impl::App *a, impl::WorldState &w) {
    if (!a || a->flying)
        return;
    a->flying = &w;
    w.followed = nullptr; // a flight moves the focus itself
    forget_keys(a);
    if (a->ui.ctx) {
        ImGui::SetCurrentContext(a->ui.ctx);
        ImGui::GetIO().ConfigFlags |=
            ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
    }
    if (a->platform.win)
        SDL_SetWindowRelativeMouseMode(a->platform.win, true);
}

void world_fly_end(impl::App *a) {
    if (!a || !a->flying)
        return;
    if (a->platform.win)
        SDL_SetWindowRelativeMouseMode(a->platform.win, false);
    if (a->ui.ctx) {
        ImGui::SetCurrentContext(a->ui.ctx);
        ImGui::GetIO().ConfigFlags &=
            ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard);
    }
    a->flying = nullptr;
}

bool ui_fly_begin(impl::App *a) {
    if (!a)
        return false;
    bool any = bool(a->world);
    for (const impl::View &v : a->views)
        any = any || bool(v.world);
    if (!any)
        return false;
    if (impl::WorldState *w = a->pointed ? a->pointed : a->world.get())
        world_fly_begin(a, *w);
    return true;
}

// A 3D scene has no chrome to put a panel in, so its controls sit ON
// the picture and must cost it almost nothing.

constexpr float kDeg = 3.14159265f / 180.0f;

// A preset keeps the focus and the distance the caller composed and
// turns the camera only — a "front view" that also jumped the zoom
// would be a different scene, not a different angle.
void world_look(impl::WorldState &w, float az_deg, float el_deg) {
    w.camera.look(az_deg * kDeg, el_deg * kDeg);
}

// DATA, so the menu is a loop and a check applies the same table.
// Home is not in it: an opening view is not an angle.
constexpr Preset kViews[] = {{"front", -90.0f, 0.0f},
                             {"side", 0.0f, 0.0f},
                             {"top", -90.0f, 89.0f},
                             {"corner", -45.0f, 30.0f}};

const Preset *world_presets(std::size_t *count) {
    if (count)
        *count = sizeof kViews / sizeof kViews[0];
    return kViews;
}

void world_menu(impl::App *a, impl::WorldState &w) {
    ImGui::SeparatorText("view");
    // Home is not a preset: it is whatever the caller composed, and no
    // angle in a table can stand for that.
    if (ImGui::Selectable("home"))
        world_camera(impl::World{&w}, w.home);
    for (const Preset &v : kViews)
        if (ImGui::Selectable(v.name))
            world_look(w, v.az, v.el);

    ImGui::SeparatorText("projection");
    const bool ortho = w.camera.orthographic();
    if (impl::icon_button(Icon::Perspective, "persp", "perspective", !ortho))
        w.camera.set_mode(impl::Projection::Perspective);
    ImGui::SameLine();
    if (impl::icon_button(Icon::Orthographic, "ortho", "orthographic", ortho))
        w.camera.set_mode(impl::Projection::Orthographic);

    if (w.grid || w.axes) {
        ImGui::SeparatorText("show");
        if (w.grid)
            ImGui::Checkbox("grid", &w.grid->visible);
        if (w.axes)
            ImGui::Checkbox("axes", &w.axes->visible);
    }

    // A tool is offered only where something listens for it.
    if (!w.strokes.empty()) {
        ImGui::SeparatorText("tool");
        if (ImGui::Selectable("camera", w.tool == int(Tool::Camera)))
            w.tool = int(Tool::Camera);
        if (ImGui::Selectable("cut", w.tool == int(Tool::Cut)))
            w.tool = int(Tool::Cut);
    }

    ImGui::SeparatorText("navigate");
    if (ImGui::Selectable("fly  (Tab)"))
        world_fly_begin(a, w);
}

// Drawn at `at`, which is the top-left of the picture. The caller owns
// the window; this only knows where the corner is.
void world_controls(impl::App *a, impl::WorldState &w, ImVec2 at) {
    if (!w.controls)
        return;
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    const float pad = ImGui::GetStyle().WindowPadding.x;
    ImGui::SetCursorScreenPos(ImVec2(at.x + pad, at.y + pad));
    ImGui::PushID(&w);
    if (a->flying == &w) {
        // No pointer to press a button with: the hint IS the control,
        // and it names the device in the reader's hands.
        ImGui::TextDisabled(
            a->input.last_pad
                ? "B  release    left stick  move    right stick  look    "
                  "triggers  down / up    L3  faster"
                : "Esc  release    W A S D  move    Q E  down / up    "
                  "Shift  faster    wheel  speed");
    } else {
        if (impl::icon_button(Icon::Cube, "view", "camera and what is drawn"))
            ImGui::OpenPopup("##world_menu");
        if (ImGui::BeginPopup("##world_menu")) {
            world_menu(a, w);
            ImGui::EndPopup();
        }
        if (w.tool == int(Tool::Cut) && !w.strokes.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("cut  drag    orbit  right-drag");
        }
    }
    ImGui::PopID();
    // ImGui warns about a cursor moved with nothing following.
    ImGui::SetCursorScreenPos(keep);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// WantCaptureMouse is the whole test: true while a panel is hovered
// or owns a drag.
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
        world_controls(a, *a->world, vp->WorkPos);
    ImGui::End();
}

void ui_world_input(impl::App *a) {
    if (!a)
        return;
    const float dt = ImGui::GetIO().DeltaTime;
    if (a->flying) {
        world_camera_steer(*a->flying, a->input, dt, true);
        return;
    }
    if (a->world) {
        const ImGuiIO &io = ImGui::GetIO();
        const bool free = !io.WantCaptureMouse;
        if (free)
            a->pointed = a->world.get();
        // SCREEN coordinates, as the pointer is once viewports are on:
        // a window is rarely at the screen's origin.
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        a->world->rect[0] = vp->Pos.x;
        a->world->rect[1] = vp->Pos.y;
        a->world->rect[2] = vp->Size.x;
        a->world->rect[3] = vp->Size.y;
        world_camera_gesture(*a->world, free, free);
    }
    // A pad needs no flight: it has no hotkeys to collide with and no
    // pointer to hide. It steers the world under the pointer, else the
    // window's.
    if (impl::WorldState *t = a->pointed ? a->pointed : a->world.get())
        world_camera_steer(*t, a->input, dt, false);
}

namespace impl {

// No title means the world IS the window; a titled one is a panel.
// They differ in nothing but the framebuffer.
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
        a->world->camera.look(-0.7853981634f, 0.5235987756f);
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
    v.world->camera.look(-0.7853981634f, 0.5235987756f);
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
    ws->camera.frame({d.focus[0], d.focus[1], d.focus[2]},
                     d.distance > 0.0f ? d.distance : 5.0f);
    ws->camera.look(d.azimuth_deg * kDeg, d.elevation_deg * kDeg);
    ws->camera.set_fov(d.fov_deg * kDeg);
    ws->camera.set_mode(d.projection == sv::Projection::Orthographic
                            ? impl::Projection::Orthographic
                            : impl::Projection::Perspective);
    // Remembered whole, because "home" is the view the caller composed
    // and every preset is a departure from it.
    ws->home = d;
    ws->followed = nullptr;
}

void world_on_pick(World w, void (*fn)(const Pick &, void *), void *user,
                   void (*free)(void *)) {
    WorldState *ws = static_cast<WorldState *>(w.p);
    if (!ws) {
        if (free)
            free(user);
        return;
    }
    ws->picks.push_back({fn, user, free});
}

void world_follow(World w, Cloud c, std::int32_t index) {
    WorldState *ws = static_cast<WorldState *>(w.p);
    if (!ws)
        return;
    WorldItem *it = static_cast<WorldItem *>(c.p);
    if (!it || index < 0) {
        ws->followed = nullptr;
        return;
    }
    if (it->owner != ws)
        return set_error("a world follows only its own items — this pick "
                         "came from another world");
    ws->followed = it;
    ws->follow_index = std::uint32_t(index);
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

// A Publish stamps itself with the compute device's submitted ticket;
// a source with no Sync makes the frame wait for everything.
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
