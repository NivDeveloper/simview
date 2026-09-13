// The pointer: one position whichever device moved it, in one of two
// styles — a cursor on the picture, or a crosshair at its centre with
// the picture turning under it — and the gestures read off it.

#include "../core/App.h"
#include "../world/World.h"
#include "Ui.h"

#include <imgui.h>

#include <cmath>

namespace sv {

namespace {

using impl::ActionRow;
using impl::ActionValue;
using impl::Input;

constexpr float kCursorPerSecond = 1000.0f; // px at full deflection
constexpr float kClickPx = 6.0f;            // ImGui's own drag threshold
constexpr float kWheelDepth = 0.1f;         // of the point's depth, a notch
constexpr float kPushPerSecond = 1.0f;

const ActionValue &val(impl::App *a, const char *id) {
    static const ActionValue none;
    const ActionRow *r = impl::effective_row(a->input.table, id);
    return r ? r->value : none;
}

impl::ModeState *active_mode(Input &in) {
    return in.active_mode >= 0 && std::size_t(in.active_mode) < in.modes.size()
               ? in.modes[std::size_t(in.active_mode)].get()
               : nullptr;
}

bool inside(const impl::WorldState &w, float x, float y) {
    return x >= w.rect[0] && x <= w.rect[0] + w.rect[2] && y >= w.rect[1] &&
           y <= w.rect[1] + w.rect[3];
}

// A world point in window pixels through the view the camera has NOW:
// a sweep is measured against the picture the turn just made.
bool project(impl::WorldState &w, impl::Vec3 p, float *x, float *y) {
    float cw = 0.0f;
    const impl::Vec3 c =
        impl::transform_point(world_view_now(w).world_to_clip, p, &cw);
    if (cw <= 0.0f)
        return false;
    *x = w.rect[0] + (c.x + 1.0f) * 0.5f * w.rect[2];
    *y = w.rect[1] + (1.0f - c.y) * 0.5f * w.rect[3];
    return true;
}

} // namespace

void ui_pointer_style(impl::App *a, PointerStyle s) {
    if (!a)
        return;
    Input &in = a->input;
    if (s == PointerStyle::Crosshair) {
        impl::WorldState *w = a->pointed ? a->pointed : a->world.get();
        if (!w || a->aimed == w)
            return;
        in.style = PointerStyle::Crosshair;
        a->aimed = w;
        a->grabbed = nullptr;
        w->followed = nullptr; // the crosshair moves the focus itself
        w->aim_valid = false;
        // Forgotten at take-off: a release can land in another window,
        // and a press remembered from before would move it forever.
        in.held.reset();
        in.look_dx = in.look_dy = in.wheel = 0.0f;
        if (a->ui.ctx) {
            ImGui::SetCurrentContext(a->ui.ctx);
            ImGui::GetIO().ConfigFlags |=
                ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard;
        }
        if (a->platform.win)
            SDL_SetWindowRelativeMouseMode(a->platform.win, true);
        return;
    }
    in.style = PointerStyle::Cursor;
    if (!a->aimed)
        return;
    if (a->platform.win)
        SDL_SetWindowRelativeMouseMode(a->platform.win, false);
    if (a->ui.ctx) {
        ImGui::SetCurrentContext(a->ui.ctx);
        ImGui::GetIO().ConfigFlags &=
            ~(ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard);
    }
    a->aimed = nullptr;
    a->grabbed = nullptr;
}

// The pad's half of the cursor, before NewFrame: its pointer stick
// steps the cursor (quadratically: a small tilt is fine control) and
// the OS cursor is warped along; its pointer buttons are the mouse's.
void ui_pointer_feed(impl::App *a) {
    if (!a || !a->ui.ctx || a->input.style != PointerStyle::Cursor)
        return;
    Input &in = a->input;
    const impl::ActionTable &t = in.table;
    ImGui::SetCurrentContext(a->ui.ctx);
    ImGuiIO &io = ImGui::GetIO();

    const auto stick_of = [&](const char *id, float *x, float *y) {
        *x = *y = 0.0f;
        const int ctx = t.find_context("cursor");
        const int row = ctx < 0 ? -1 : t.find(ctx, id);
        if (row < 0)
            return;
        for (const Binding &b :
             impl::effective(t.rows[std::size_t(row)], Device::Pad)) {
            if (b.shape != Shape::Plain)
                continue;
            if (b.controls[0] == ControlOf(Pad::RS)) {
                *x += in.pad.axes[2];
                *y += in.pad.axes[3];
            } else if (b.controls[0] == ControlOf(Pad::LS)) {
                *x += in.pad.axes[0];
                *y += in.pad.axes[1];
            }
        }
    };
    float sx, sy;
    stick_of("pointer.move", &sx, &sy);
    if (sx != 0.0f || sy != 0.0f) {
        const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f;
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        ImVec2 p = io.MousePos;
        if (p.x < vp->Pos.x || p.x > vp->Pos.x + vp->Size.x ||
            p.y < vp->Pos.y || p.y > vp->Pos.y + vp->Size.y)
            p = ImVec2(vp->Pos.x + vp->Size.x * 0.5f,
                       vp->Pos.y + vp->Size.y * 0.5f);
        p.x += sx * std::fabs(sx) * kCursorPerSecond * dt;
        p.y += sy * std::fabs(sy) * kCursorPerSecond * dt;
        p.x =
            std::fmin(std::fmax(p.x, vp->Pos.x), vp->Pos.x + vp->Size.x - 1.0f);
        p.y =
            std::fmin(std::fmax(p.y, vp->Pos.y), vp->Pos.y + vp->Size.y - 1.0f);
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(p.x, p.y);
        if (a->platform.win) {
            in.warped = true;
            in.warp_x = p.x - vp->Pos.x;
            in.warp_y = p.y - vp->Pos.y;
            SDL_WarpMouseInWindow(a->platform.win, in.warp_x, in.warp_y);
        }
        in.last = Device::Pad;
    }

    // The pad's pointer buttons are the mouse's.
    const auto mirror = [&](const char *id, int button) {
        const int ctx = t.find_context("cursor");
        const int row = ctx < 0 ? -1 : t.find(ctx, id);
        if (row < 0)
            return;
        for (const Binding &b :
             impl::effective(t.rows[std::size_t(row)], Device::Pad)) {
            if (b.shape != Shape::Plain || b.controls[0].device != Device::Pad)
                continue;
            const int code = b.controls[0].code;
            if (code < 0 || code >= impl::kPadControls)
                continue;
            if (in.pad.pressed[code]) {
                io.AddMouseButtonEvent(button, true);
                in.mouse_down[button] = true;
                in.mouse_pressed[button] = true;
            }
            if (in.pad.released[code]) {
                io.AddMouseButtonEvent(button, false);
                in.mouse_down[button] = false;
                in.mouse_released[button] = true;
            }
        }
    };
    // Not while a cell is listening for the pad: a mirrored press would
    // read as the mouse's to a capture of the other hand.
    if (!a->ui.capture.active) {
        mirror("pointer.primary", 0);
        mirror("pointer.secondary", 1);
    }
}

void ui_inject(impl::App *a, const Event &e) {
    if (!a || !a->ui.ctx)
        return;
    ImGui::SetCurrentContext(a->ui.ctx);
    ImGuiIO &io = ImGui::GetIO();
    const Control c = e.control;
    if (c.device == Device::Keyboard) {
        const bool down = e.type == Event::Type::Down;
        if (e.type != Event::Type::Down && e.type != Event::Type::Up)
            return;
        if (c.code == int(Key::LeftShift))
            io.AddKeyEvent(ImGuiMod_Shift, down);
        else if (c.code == int(Key::LeftCtrl))
            io.AddKeyEvent(ImGuiMod_Ctrl, down);
        else if (c.code == int(Key::LeftAlt))
            io.AddKeyEvent(ImGuiMod_Alt, down);
        return;
    }
    if (c.device != Device::Mouse)
        return;
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    if (c.code == int(Mouse::Move) && e.type == Event::Type::Move)
        io.AddMousePosEvent(e.x, e.y);
    else if (c.code == int(Mouse::Wheel) && e.type == Event::Type::Delta)
        io.AddMouseWheelEvent(0.0f, e.y);
    else if (c.code >= 0 && c.code < 3 && e.type == Event::Type::Down)
        io.AddMouseButtonEvent(c.code, true);
    else if (c.code >= 0 && c.code < 3 && e.type == Event::Type::Up)
        io.AddMouseButtonEvent(c.code, false);
}

// Hover, press, stroke, release, and the double-click, on the world
// the pointer means. A crosshair's stroke runs from where its aim
// point was last frame to the centre, so a turn sweeps it.
void world_gestures(impl::App *a, impl::WorldState &w, float dt) {
    Input &in = a->input;
    const ActionValue &prim = val(a, "pointer.primary");
    const ActionValue &sec = val(a, "pointer.secondary");
    const ActionValue &drag = val(a, "pointer.drag");
    const ActionValue &depth = val(a, "pointer.depth");
    const ActionValue &frame = val(a, "camera.frame");
    const bool aimed = a->aimed == &w;

    float px, py;
    if (aimed) {
        px = w.rect[0] + w.rect[2] * 0.5f;
        py = w.rect[1] + w.rect[3] * 0.5f;
    } else {
        const ImVec2 m = ImGui::GetIO().MousePos;
        px = m.x;
        py = m.y;
    }
    const bool over = aimed || (a->pointed == &w && inside(w, px, py));

    // What the pointer is over, every frame no button is down: the
    // highlight the picture shows.
    w.hovered_now = over;
    w.hovered = nullptr;
    Pick p{};
    if (over && !prim.down && !sec.down && world_pick(w, px, py, &p) &&
        !p.Ground()) {
        w.hovered = static_cast<impl::WorldItem *>(p.cloud.p);
        w.hover_index = std::uint32_t(p.index);
    }

    // A double-click makes the point the pivot, distance untouched.
    if (frame.pressed && over && world_pick(w, px, py, &p)) {
        w.followed = nullptr;
        w.camera.frame({p.point[0], p.point[1], p.point[2]},
                       w.camera.distance());
    }

    if (prim.pressed && over) {
        a->grabbed = &w;
        const bool hit = world_pick(w, px, py, &p);
        w.grab_item = hit ? p.cloud : impl::Cloud{};
        w.grab_index = hit ? p.index : -1;
        in.drag_px = 0.0f;
    }

    // The pointer's own step this frame: what a drag would read, whether
    // or not a mode is listening — a click is judged by it.
    impl::ModeState *m = active_mode(in);
    if (a->grabbed == &w && prim.down) {
        float x0 = px, y0 = py;
        if (aimed) {
            if (!w.aim_valid || !project(w, w.aim, &x0, &y0)) {
                x0 = px;
                y0 = py;
            }
        } else {
            const ImVec2 step = ImGui::GetIO().MouseDelta;
            x0 = px - step.x;
            y0 = py - step.y;
        }
        const bool moved = x0 != px || y0 != py;
        in.drag_px += std::hypot(px - x0, py - y0);
        const float d = depth.x * kWheelDepth + depth.rx * kPushPerSecond * dt;
        if (m && (m->stroke.fn || m->carry.fn) && (moved || d != 0.0f))
            world_stroke(w, x0, y0, px, py, d,
                         m->stroke.fn ? &m->stroke : nullptr,
                         m->carry.fn ? &m->carry : nullptr);
    }
    (void)drag;

    // The crosshair's aim point, for next frame's sweep: what it is on,
    // else the focus.
    if (aimed) {
        w.aim_valid = true;
        if (world_pick(w, px, py, &p))
            w.aim = {p.point[0], p.point[1], p.point[2]};
        else
            w.aim = w.camera.focus();
    }

    // A click is a release that never dragged.
    if (prim.released && a->grabbed == &w) {
        if (in.drag_px < kClickPx && over && world_pick(w, px, py, &p))
            world_picked(w, p);
        a->grabbed = nullptr;
    }
}

} // namespace sv
