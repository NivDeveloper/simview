#include "../world/World.h"
#include "../core/App.h"
#include "Icons.h"
#include "Ui.h"

#include <simview/World.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sv {

namespace {

constexpr float kFlyPerSecond = 1.0f; // orbit distances, before the multiplier
constexpr float kFlyShift = 4.0f;
constexpr float kFlyWheel = 0.25f; // e-folds of speed per wheel notch
constexpr float kPadTurn = 500.0f; // pixels a second at full deflection
constexpr float kPadDolly = 10.0f; // wheel notches a second at full pull
constexpr float kPadSpeed = 4.0f;  // wheel notches a second on the D-pad

const impl::ActionValue &val(impl::App *a, const char *id) {
    static const impl::ActionValue none;
    const impl::ActionRow *r = impl::effective_row(a->input.table, id);
    return r ? r->value : none;
}

} // namespace

// The camera's handlers: a delta is pixels a drag moved, a rate a stick
// at so many pixels a second. In orbit the turn pivots on the focus and
// the slide pans; in flight it pivots on the eye and the slide walks.
void world_camera_act(impl::App *a, impl::WorldState &w, float dt) {
    const impl::ActionValue &turn = val(a, "camera.turn");
    const impl::ActionValue &slide = val(a, "camera.slide");
    const impl::ActionValue &depth = val(a, "camera.depth");
    const float tx = turn.x + turn.rx * kPadTurn * dt;
    const float ty = turn.y + turn.ry * kPadTurn * dt;

    if (a->input.camera == CameraMode::Orbit) {
        if (tx != 0.0f || ty != 0.0f)
            w.camera.orbit(tx, ty);
        // A stick pushes the camera the way it leans; a drag carries
        // the scene with the pointer. Opposite signs, one pan.
        const float sx = slide.x - slide.rx * kPadTurn * dt;
        const float sy = slide.y - slide.ry * kPadTurn * dt;
        if (sx != 0.0f || sy != 0.0f) {
            w.followed = nullptr; // the reader took the focus back
            w.camera.pan(sx, sy);
        }
        const float dz = depth.x + depth.rx * kPadDolly * dt;
        if (dz != 0.0f)
            w.camera.dolly(dz);
        return;
    }

    const impl::ActionValue &fast = val(a, "camera.fast");
    const impl::ActionValue &speed = val(a, "camera.speed");
    const float notches = speed.x + speed.rx * kPadSpeed * dt;
    if (notches != 0.0f)
        w.fly_speed = std::clamp(w.fly_speed * std::exp(notches * kFlyWheel),
                                 0.05f, 20.0f);
    const float v = kFlyPerSecond * w.camera.distance() * w.fly_speed *
                    (fast.down ? kFlyShift : 1.0f) * dt;
    const float right = slide.rx, ahead = -slide.ry, up = depth.rx;
    if (right != 0.0f || ahead != 0.0f || up != 0.0f)
        w.camera.move(right * v, up * v, ahead * v);
    if (tx != 0.0f || ty != 0.0f)
        w.camera.turn(tx, ty);
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

    // An entry names its key beside it, from the row that binds it.
    const auto entry = [&](const char *name, const char *id) {
        std::string s = name;
        if (const impl::ActionRow *r =
                impl::effective_row(a->input.table, id)) {
            const std::vector<Binding> &bs =
                impl::effective(*r, Device::Keyboard);
            if (!bs.empty())
                s += "  (" + chip_text(chip_for(bs[0], nullptr, false)) + ")";
        }
        return s;
    };

    // A mode is offered only where the app made one.
    if (!a->input.modes.empty()) {
        ImGui::SeparatorText("mode");
        const char *active = impl::app_active_mode(a);
        if (ImGui::Selectable("none", active == nullptr))
            impl::app_enter_mode(a, nullptr);
        for (const auto &m : a->input.modes) {
            const std::string id = "mode." + m->name;
            const bool on = active && m->name == active;
            if (ImGui::Selectable(entry(m->name.c_str(), id.c_str()).c_str(),
                                  on))
                impl::app_enter_mode(a, on ? nullptr : m->name.c_str());
        }
    }

    ImGui::SeparatorText("navigate");
    const bool fly = a->input.camera == CameraMode::Fly;
    if (ImGui::Selectable(entry(fly ? "orbit" : "fly", "mode.camera").c_str()))
        impl::app_camera_mode(a, fly ? CameraMode::Orbit : CameraMode::Fly);
    const bool cross = a->input.style == PointerStyle::Crosshair;
    if (ImGui::Selectable(
            entry(cross ? "cursor" : "crosshair", "mode.pointer").c_str()))
        impl::app_pointer_style(a, cross ? PointerStyle::Cursor
                                         : PointerStyle::Crosshair);
    if (ImGui::Selectable(entry("settings", "settings").c_str()))
        a->ui.settings_open = !a->ui.settings_open;
}

// One line of the keys — modes, the app's labelled actions, the engine's
// toggles — and one of what the hand in use does under the camera, the
// pointer and the mode; a row shows its first binding left alive.
std::vector<Chip> world_legend(impl::App *a, impl::WorldState &w) {
    (void)w;
    std::vector<Chip> chips;
    const impl::Input &in = a->input;
    const impl::ActionTable &t = in.table;
    const Device hand = in.last == Device::Pad && in.pad.present
                            ? Device::Pad
                            : Device::Keyboard;
    const auto chip = [&](const impl::ActionRow &r, const char *label,
                          bool lit) {
        const std::vector<Binding> &bs = impl::effective(r, hand);
        const std::uint32_t alive =
            hand == Device::Pad ? r.alive_pad : r.alive_km;
        for (std::size_t i = 0; i < bs.size(); ++i)
            if (alive & (1u << i)) {
                chips.push_back(chip_for(bs[i], label, lit));
                return;
            }
    };
    const auto row = [&](const char *id) { return impl::effective_row(t, id); };

    for (std::size_t i = 0; i < in.modes.size(); ++i) {
        const std::string id = "mode." + in.modes[i]->name;
        if (const impl::ActionRow *r = row(id.c_str()))
            chip(*r, in.modes[i]->name.c_str(), int(i) == in.active_mode);
    }
    const int base = t.find_context("base");
    for (const impl::ActionRow &r : t.rows)
        if (r.context == base && !r.label.empty() && r.id != "settings" &&
            r.id.rfind("mode.", 0) != 0 && row(r.id.c_str()) == &r)
            chip(r, r.label.c_str(), false);
    if (const impl::ActionRow *r = row("mode.camera"))
        chip(*r, r->label.c_str(), false);
    if (const impl::ActionRow *r = row("mode.pointer"))
        chip(*r, r->label.c_str(), false);
    if (const impl::ActionRow *r = row("settings"))
        chip(*r, "settings", false);
    chips.push_back(Chip{});

    const char *mode = impl::app_active_mode(a);
    const bool orbit = in.camera == CameraMode::Orbit;
    const char *ids[] = {"camera.turn",     "camera.slide", "camera.depth",
                         "camera.fast",     "camera.speed", "camera.frame",
                         "pointer.primary", "pointer.drag", "pointer.depth",
                         "mode.exit"};
    for (const char *id : ids) {
        const impl::ActionRow *r = row(id);
        if (!r || !r->enabled)
            continue;
        const char *label = r->label.c_str();
        if (r->id == "pointer.drag")
            label = mode ? mode : "";
        else if (r->id == "camera.turn" && orbit)
            label = "orbit";
        chip(*r, label, false);
    }
    return chips;
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
    // Under a crosshair there is no pointer to press a button with: the
    // bar IS the control.
    if (a->aimed != &w) {
        if (impl::icon_button(Icon::Cube, "view", "camera and what is drawn"))
            ImGui::OpenPopup("##world_menu");
        if (ImGui::BeginPopup("##world_menu")) {
            world_menu(a, w);
            ImGui::EndPopup();
        }
        ImGui::SameLine(0.0f, ImGui::GetFontSize());
    }
    ImGui::BeginGroup();
    impl::draw_chips(world_legend(a, w));
    ImGui::EndGroup();
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::Begin("##world_controls", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoBringToFrontOnFocus))
        world_controls(a, *a->world, vp->WorkPos);
    ImGui::End();
    ImGui::PopStyleVar();
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
