// The frame's actions: the engine's contexts and defaults, the device
// snapshot, one resolve, and every handler in order — the modes, the
// pointer, the gestures, the camera, then the app's own.

#include "../core/App.h"
#include "../world/World.h"
#include "Ui.h"

#include <imgui.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace sv {

namespace {

using impl::ActionRow;
using impl::ActionTable;
using impl::ActionValue;
using impl::Input;
using impl::ModeState;

constexpr float kDoubleClickSeconds = 0.3f;
constexpr float kDoubleClickPx = 6.0f;

ActionRow &add(ActionTable &t, const char *ctx, const char *id,
               const char *label, ActionKind kind,
               std::initializer_list<Binding> bs) {
    std::vector<Binding> v(bs);
    return t.add(t.find_context(ctx),
                 {id, label, kind, v.data(), std::int32_t(v.size())});
}

// Every engine action, bound on both hands. A row's label is the
// context's word for it: camera.turn is "orbit" here and "look" there.
void engine_defaults(ActionTable &t) {
    add(t, "base", "settings", "settings", ActionKind::Button,
        {Ctl(Key::F1), Ctl(Pad::Start)});

    add(t, "orbit", "mode.camera", "fly", ActionKind::Button,
        {Ctl(Key::Tab), Ctl(Pad::Back)});
    add(t, "orbit", "camera.turn", "orbit", ActionKind::Axis2,
        {Drag(Mouse::Left), Drag(Mouse::Right), Drag(Pad::A), Drag(Pad::B)});
    add(t, "orbit", "camera.slide", "pan", ActionKind::Axis2,
        {With(Key::LeftShift, Drag(Mouse::Left)), Drag(Mouse::Middle),
         Ctl(Pad::LS)});
    add(t, "orbit", "camera.depth", "zoom", ActionKind::Axis,
        {Ctl(Mouse::Wheel), Axis(Pad::LT, Pad::RT)});
    add(t, "orbit", "camera.frame", "frame", ActionKind::Button,
        {Ctl(Mouse::DoubleClick)});

    add(t, "fly", "mode.camera", "orbit", ActionKind::Button,
        {Ctl(Key::Tab), Ctl(Pad::Back)});
    add(t, "fly", "camera.slide", "move", ActionKind::Axis2,
        {Axis2(Key::A, Key::D, Key::S, Key::W), Ctl(Pad::LS)});
    add(t, "fly", "camera.depth", "down / up", ActionKind::Axis,
        {Axis(Key::Q, Key::E), Axis(Pad::LT, Pad::RT)});
    add(t, "fly", "camera.fast", "faster", ActionKind::Button,
        {Ctl(Key::LeftShift), Ctl(Pad::L3)});
    add(t, "fly", "camera.speed", "speed", ActionKind::Axis,
        {Ctl(Mouse::Wheel), Axis(Pad::Down, Pad::Up)});
    add(t, "fly", "camera.frame", "frame", ActionKind::Button,
        {Ctl(Mouse::DoubleClick)});

    add(t, "cursor", "mode.pointer", "crosshair", ActionKind::Button,
        {With(Key::LeftCtrl, Ctl(Key::Tab)), Ctl(Pad::R3)});
    add(t, "cursor", "pointer.move", "pointer", ActionKind::Axis2,
        {Ctl(Mouse::Move), Ctl(Pad::RS)})
        .claims = false;
    add(t, "cursor", "pointer.primary", "pick", ActionKind::Button,
        {Ctl(Mouse::Left), Ctl(Pad::A)});
    add(t, "cursor", "pointer.secondary", "", ActionKind::Button,
        {Ctl(Mouse::Right), Ctl(Pad::B)});

    add(t, "crosshair", "mode.pointer", "cursor", ActionKind::Button,
        {With(Key::LeftCtrl, Ctl(Key::Tab)), Ctl(Key::Escape), Ctl(Pad::R3),
         Ctl(Pad::B)});
    add(t, "crosshair", "camera.turn", "look", ActionKind::Axis2,
        {Ctl(Mouse::Move), Ctl(Pad::RS)});
    add(t, "crosshair", "pointer.primary", "pick", ActionKind::Button,
        {Ctl(Mouse::Left), Ctl(Pad::A)});

    add(t, "stroke", "pointer.drag", "", ActionKind::Axis2,
        {Drag(Mouse::Left), Drag(Pad::A)});
    add(t, "stroke", "pointer.depth", "pull / push", ActionKind::Axis,
        {Ctl(Mouse::Wheel), Axis(Pad::LT, Pad::RT)});

    add(t, "mode", "mode.exit", "exit", ActionKind::Button,
        {Ctl(Key::Escape), Ctl(Pad::B)});
}

ModeState *active_mode(Input &in) {
    return in.active_mode >= 0 && std::size_t(in.active_mode) < in.modes.size()
               ? in.modes[std::size_t(in.active_mode)].get()
               : nullptr;
}

void activate(ActionTable &t, const char *name) {
    const int i = t.find_context(name);
    if (i >= 0)
        t.contexts[std::size_t(i)].active = true;
}

// Which contexts are on this frame, and the one gated row.
void set_active(impl::App *a) {
    Input &in = a->input;
    ActionTable &t = in.table;
    for (impl::ContextState &c : t.contexts)
        c.active = false;
    activate(t, "base");
    activate(t, in.camera == CameraMode::Orbit ? "orbit" : "fly");
    activate(t, in.style == PointerStyle::Cursor ? "cursor" : "crosshair");
    ModeState *m = active_mode(in);
    if (m) {
        activate(t, "mode");
        t.contexts[std::size_t(m->context)].active = true;
        if (m->stroke.fn || m->carry.fn)
            activate(t, "stroke");
    }
    const int depth = t.find(t.find_context("stroke"), "pointer.depth");
    if (depth >= 0)
        t.rows[std::size_t(depth)].enabled =
            m && m->carry.fn && in.primary_held;
}

impl::Snapshot snapshot(impl::App *a, bool ui) {
    Input &in = a->input;
    impl::Snapshot s;
    s.key_down = in.held;
    s.key_pressed = in.pressed;
    s.key_released = in.released;
    for (int i = 0; i < 3; ++i) {
        s.mouse_down[i] = in.mouse_down[i];
        s.mouse_pressed[i] = in.mouse_pressed[i];
        s.mouse_released[i] = in.mouse_released[i];
    }
    for (int i = 0; i < impl::kPadControls; ++i) {
        s.pad_down[i] = in.pad.down[i];
        s.pad_pressed[i] = in.pad.pressed[i];
        s.pad_released[i] = in.pad.released[i];
    }
    s.lx = in.pad.axes[0];
    s.ly = in.pad.axes[1];
    s.rx = in.pad.axes[2];
    s.ry = in.pad.axes[3];
    s.lt = in.pad.axes[4];
    s.rt = in.pad.axes[5];
    s.wheel = in.wheel;
    if (!ui) {
        s.mouse_free = false;
        return s;
    }
    const ImGuiIO &io = ImGui::GetIO();
    s.dt = io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f;
    if (a->aimed) {
        // Under a crosshair the mouse's step is a look and the pointer
        // stays put; the panels are blind, so nothing owns it.
        s.move_dx = in.look_dx;
        s.move_dy = in.look_dy;
        s.mouse_free = true;
    } else {
        s.move_dx = s.pointer_dx = io.MouseDelta.x;
        s.move_dy = s.pointer_dy = io.MouseDelta.y;
        s.mouse_free = !io.WantCaptureMouse || a->pointed || a->grabbed;
    }
    return s;
}

const ActionValue &val(impl::App *a, const char *id) {
    static const ActionValue none;
    const ActionRow *r = impl::effective_row(a->input.table, id);
    return r ? r->value : none;
}

void pointer_at(impl::App *a, float *x, float *y) {
    if (a->aimed) {
        *x = a->aimed->rect[0] + a->aimed->rect[2] * 0.5f;
        *y = a->aimed->rect[1] + a->aimed->rect[3] * 0.5f;
        return;
    }
    const ImVec2 p = ImGui::GetIO().MousePos;
    *x = p.x;
    *y = p.y;
}

// A second primary press soon and near the first is a double-click,
// on any device; timed by the frame's own dt, so headless is exact.
bool double_click(impl::App *a, const impl::Snapshot &s) {
    Input &in = a->input;
    in.click_timer += s.dt;
    if (!val(a, "pointer.primary").pressed)
        return false;
    float x, y;
    pointer_at(a, &x, &y);
    const bool again =
        in.click_timer < kDoubleClickSeconds &&
        std::hypot(x - in.click_x, y - in.click_y) < kDoubleClickPx;
    in.click_timer = again ? 1.0f : 0.0f;
    in.click_x = x;
    in.click_y = y;
    return again;
}

void dispatch_modes(impl::App *a) {
    Input &in = a->input;
    if (val(a, "settings").pressed)
        a->ui.settings_open = !a->ui.settings_open;
    if (val(a, "mode.camera").pressed)
        impl::app_camera_mode(a, in.camera == CameraMode::Orbit
                                     ? CameraMode::Fly
                                     : CameraMode::Orbit);
    if (val(a, "mode.pointer").pressed)
        impl::app_pointer_style(a, in.style == PointerStyle::Cursor
                                       ? PointerStyle::Crosshair
                                       : PointerStyle::Cursor);
    if (val(a, "mode.exit").pressed)
        impl::app_enter_mode(a, nullptr);
    for (std::size_t i = 0; i < in.modes.size(); ++i) {
        const std::string id = "mode." + in.modes[i]->name;
        if (val(a, id.c_str()).pressed)
            impl::app_enter_mode(a, int(i) == in.active_mode
                                        ? nullptr
                                        : in.modes[i]->name.c_str());
    }
}

// A button fires on its press; an axis every frame it is non-zero,
// its rate and its delta summed. A callback may bind another, so the
// count is taken first.
void dispatch_callbacks(impl::App *a) {
    Input &in = a->input;
    const std::size_t n = in.callbacks.size();
    for (std::size_t i = 0; i < n; ++i) {
        const impl::ActionCb cb = in.callbacks[i];
        const ActionRow &r = in.table.rows[std::size_t(cb.row)];
        if (impl::effective_row(in.table, r.id.c_str()) != &r)
            continue;
        const ActionValue &v = r.value;
        switch (r.kind) {
        case ActionKind::Button:
            if (v.pressed)
                cb.fn(0.0f, 0.0f, cb.user);
            break;
        case ActionKind::Axis:
            if (v.rx != 0.0f || v.x != 0.0f)
                cb.fn(v.rx + v.x, 0.0f, cb.user);
            break;
        case ActionKind::Axis2:
            if (v.any())
                cb.fn(v.rx + v.x, v.ry + v.y, cb.user);
            break;
        }
    }
}

// Every binding a desc carries that is well formed; a malformed one
// is refused by name and dropped.
std::vector<Binding> vetted(const ActionDesc &d) {
    std::vector<Binding> out;
    for (std::int32_t i = 0; i < d.count; ++i) {
        std::string why;
        if (impl::binding_valid(d.controls[i], &why))
            out.push_back(d.controls[i]);
        else
            set_error(std::string("binding of \"") + (d.id ? d.id : "") +
                      "\" refused: " + why);
    }
    return out;
}

int bind_row(impl::App *a, int ctx, const ActionDesc &d,
             void (*fn)(float, float, void *), void *user,
             void (*free)(void *)) {
    if (!a || !d.id || !*d.id || ctx < 0) {
        if (free)
            free(user);
        return -1;
    }
    ActionTable &t = a->input.table;
    const std::vector<Binding> bs = vetted(d);
    t.add(ctx, {d.id, d.label, d.kind, bs.data(), std::int32_t(bs.size())});
    const int row = t.find(ctx, d.id);
    if (fn)
        a->input.callbacks.push_back({row, fn, user, free});
    else if (free)
        free(user);
    return row;
}

} // namespace

void actions_init(impl::App *a) {
    ActionTable &t = a->input.table;
    t.context("base", 0);
    t.context("orbit", 1);
    t.context("fly", 1);
    t.context("cursor", 2);
    t.context("crosshair", 2);
    t.context("stroke", 3);
    t.context("mode", 4);
    engine_defaults(t);
    // Resolved once with nothing pressed, so the bar has its rows
    // before the first frame — it is drawn ahead of the frame's own
    // resolve, and a window measured empty stays clipped a frame.
    set_active(a);
    impl::resolve(t, impl::Snapshot{});
}

void actions_quit(impl::App *a) {
    Input &in = a->input;
    for (impl::ActionCb &cb : in.callbacks)
        if (cb.free)
            cb.free(cb.user);
    in.callbacks.clear();
    for (auto &m : in.modes) {
        if (m->stroke.free)
            m->stroke.free(m->stroke.user);
        if (m->carry.free)
            m->carry.free(m->carry.user);
    }
    in.modes.clear();
}

impl::WorldState *ui_target(impl::App *a) {
    if (a->aimed)
        return a->aimed;
    if (a->grabbed)
        return a->grabbed;
    return a->pointed ? a->pointed : a->world.get();
}

void actions_frame(impl::App *a, bool ui) {
    Input &in = a->input;
    if (ui && a->world) {
        // SCREEN coordinates, as the pointer is once viewports are on.
        const ImGuiIO &io = ImGui::GetIO();
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        a->world->rect[0] = vp->Pos.x;
        a->world->rect[1] = vp->Pos.y;
        a->world->rect[2] = vp->Size.x;
        a->world->rect[3] = vp->Size.y;
        if (!io.WantCaptureMouse && !a->pointed)
            a->pointed = a->world.get();
    }

    set_active(a);
    impl::Snapshot s = snapshot(a, ui);
    impl::resolve(in.table, s);
    if (ui && double_click(a, s)) {
        s.double_click = true;
        impl::resolve(in.table, s);
    }

    dispatch_modes(a);
    if (ui)
        if (impl::WorldState *w = ui_target(a)) {
            world_gestures(a, *w, s.dt);
            world_camera_act(a, *w, s.dt);
        }
    dispatch_callbacks(a);

    in.primary_held = val(a, "pointer.primary").down;
    impl::clear_edges(in);
}

namespace impl {

void app_bind(App *a, const ActionDesc &d, void (*fn)(float, float, void *),
              void *user, void (*free)(void *)) {
    if (!a) {
        if (free)
            free(user);
        return;
    }
    bind_row(a, a->input.table.find_context("base"), d, fn, user, free);
}

// `context.action` names one row; a bare `action` every row of that
// id. Only the hands the bindings name are overridden.
void app_rebind(App *a, const char *key, const Binding *bs,
                std::int32_t count) {
    if (!a || !key || !*key)
        return;
    ActionTable &t = a->input.table;
    std::string ctx, id = key;
    const char *dot = std::strchr(key, '.');
    if (dot && t.find_context(std::string(key, dot).c_str()) >= 0) {
        ctx = std::string(key, dot);
        id = dot + 1;
    }
    std::vector<Binding> km, pad;
    for (std::int32_t i = 0; i < count; ++i) {
        std::string why;
        if (!binding_valid(bs[i], &why)) {
            set_error(std::string("rebind of \"") + key + "\" refused: " + why);
            continue;
        }
        (device_of(bs[i]) == Device::Pad ? pad : km).push_back(bs[i]);
    }
    bool any = false;
    for (ActionRow &r : t.rows) {
        if (r.id != id ||
            (!ctx.empty() && t.contexts[std::size_t(r.context)].name != ctx))
            continue;
        any = true;
        if (!km.empty())
            set_layer(r.code, Device::Keyboard, km);
        if (!pad.empty())
            set_layer(r.code, Device::Pad, pad);
    }
    if (!any)
        set_error(std::string("rebind: no action is named \"") + key + "\"");
}

void app_camera_mode(App *a, CameraMode m) {
    if (!a)
        return;
    a->input.camera = m;
    // A flight is aimed: the crosshair comes with it.
    if (m == CameraMode::Fly && a->input.style == PointerStyle::Cursor)
        ui_pointer_style(a, PointerStyle::Crosshair);
}

CameraMode app_camera_mode(App *a) {
    return a ? a->input.camera : CameraMode::Orbit;
}

void app_pointer_style(App *a, PointerStyle s) {
    if (!a)
        return;
    // A cursor cannot fly: back to the orbit with it.
    if (s == PointerStyle::Cursor && a->input.camera == CameraMode::Fly)
        a->input.camera = CameraMode::Orbit;
    ui_pointer_style(a, s);
}

PointerStyle app_pointer_style(App *a) {
    return a ? a->input.style : PointerStyle::Cursor;
}

bool app_enter_mode(App *a, const char *name) {
    if (!a)
        return false;
    Input &in = a->input;
    if (!name || !*name) {
        in.active_mode = -1;
        a->grabbed = nullptr;
        return true;
    }
    for (std::size_t i = 0; i < in.modes.size(); ++i)
        if (in.modes[i]->name == name) {
            if (int(i) != in.active_mode)
                a->grabbed = nullptr;
            in.active_mode = int(i);
            return true;
        }
    return set_error(std::string("no mode is named \"") + name + "\""), false;
}

const char *app_active_mode(App *a) {
    if (!a)
        return nullptr;
    const ModeState *m = active_mode(a->input);
    return m ? m->name.c_str() : nullptr;
}

Mode mode_create(App *a, const ModeDesc &d) {
    if (!a || !d.name || !*d.name)
        return {};
    Input &in = a->input;
    for (const auto &m : in.modes)
        if (m->name == d.name)
            return set_error(std::string("a mode is already named \"") +
                             d.name + "\""),
                   Mode{};
    auto m = std::make_unique<ModeState>();
    m->app = a;
    m->name = d.name;
    const ActionDesc enter{d.name, d.name, ActionKind::Button, d.enter,
                           d.count};
    m->enter = vetted(enter);
    m->context = in.table.context(("mode:" + m->name).c_str(), 5);
    const std::string id = "mode." + m->name;
    const ActionDesc row{id.c_str(), d.name, ActionKind::Button,
                         m->enter.data(), std::int32_t(m->enter.size())};
    in.table.add(in.table.find_context("base"), row);
    in.modes.push_back(std::move(m));
    return Mode{in.modes.back().get()};
}

const char *mode_name(Mode m) {
    return m.p ? static_cast<ModeState *>(m.p)->name.c_str() : nullptr;
}

namespace {

App *owner_of(Mode m) {
    // A mode knows its app by context: the table it was added to.
    return m.p ? static_cast<ModeState *>(m.p)->app : nullptr;
}

} // namespace

void mode_bind(Mode m, const ActionDesc &d, void (*fn)(float, float, void *),
               void *user, void (*free)(void *)) {
    App *a = owner_of(m);
    if (!a) {
        if (free)
            free(user);
        return;
    }
    bind_row(a, static_cast<ModeState *>(m.p)->context, d, fn, user, free);
}

void mode_on_stroke(Mode m, void (*fn)(const Stroke &, void *), void *user,
                    void (*free)(void *)) {
    ModeState *ms = static_cast<ModeState *>(m.p);
    if (!ms) {
        if (free)
            free(user);
        return;
    }
    if (ms->stroke.free)
        ms->stroke.free(ms->stroke.user);
    ms->stroke = {fn, user, free};
}

void mode_on_carry(Mode m, void (*fn)(const Stroke &, void *), void *user,
                   void (*free)(void *)) {
    ModeState *ms = static_cast<ModeState *>(m.p);
    if (!ms) {
        if (free)
            free(user);
        return;
    }
    if (ms->carry.free)
        ms->carry.free(ms->carry.user);
    ms->carry = {fn, user, free};
}

} // namespace impl
} // namespace sv
