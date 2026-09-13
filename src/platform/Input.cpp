// Input: SDL's events and the posted ones, noted into one device truth
// in the same order — which is what makes a posted event a faithful
// stand-in for a keypress, a click or a stick.

#include "Input.h"

#include "../core/App.h"
#include "../ui/Ui.h"

#include <cmath>
#include <vector>

namespace sv {
namespace impl {
namespace {

constexpr std::int16_t kDeadZone = 8000; // SDL_gamepad.h's suggestion

// A stick at rest never reads exactly zero; past the zone the rest of
// the travel is stretched back to the full range.
float stick(std::int16_t v) {
    const float mag = float(v < 0 ? -int(v) : int(v));
    if (mag <= float(kDeadZone))
        return 0.0f;
    const float n = (mag - float(kDeadZone)) / float(32767 - kDeadZone);
    return (v < 0 ? -1.0f : 1.0f) * (n > 1.0f ? 1.0f : n);
}

float trigger(std::int16_t v) { return v > 0 ? float(v) / 32767.0f : 0.0f; }

void pad_open(App *a, SDL_JoystickID id) {
    if (a->input.pad_device)
        return;
    a->input.pad_device = SDL_OpenGamepad(id);
    a->input.pad.present = a->input.pad_device != nullptr;
    if (a->input.pad_device)
        SDL_Log("simview: gamepad: %s",
                SDL_GetGamepadName(a->input.pad_device));
}

void pad_removed(App *a, SDL_JoystickID id) {
    if (!a->input.pad_device || SDL_GetGamepadID(a->input.pad_device) != id)
        return;
    pad_close(a);
}

// SDL's button names to the pad's: the fourteen a Pad names.
struct PadButton {
    SDL_GamepadButton sdl;
    Pad pad;
};

constexpr PadButton kPadButtons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, Pad::A},
    {SDL_GAMEPAD_BUTTON_EAST, Pad::B},
    {SDL_GAMEPAD_BUTTON_WEST, Pad::X},
    {SDL_GAMEPAD_BUTTON_NORTH, Pad::Y},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Pad::LB},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Pad::RB},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, Pad::L3},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, Pad::R3},
    {SDL_GAMEPAD_BUTTON_START, Pad::Start},
    {SDL_GAMEPAD_BUTTON_BACK, Pad::Back},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, Pad::Up},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, Pad::Down},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, Pad::Left},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Pad::Right},
};

int mouse_of(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_LEFT:
        return int(Mouse::Left);
    case SDL_BUTTON_RIGHT:
        return int(Mouse::Right);
    case SDL_BUTTON_MIDDLE:
        return int(Mouse::Middle);
    default:
        return -1;
    }
}

} // namespace

// A trigger past a half is down, and its edges are noted as a
// button's, so it clicks and gates a drag as a mouse button does.
void trigger_edges(PadState &p) {
    const int codes[2] = {int(Pad::LT), int(Pad::RT)};
    for (int i = 0; i < 2; ++i) {
        const bool d = p.axes[4 + i] > 0.5f;
        if (d && !p.down[codes[i]])
            p.pressed[codes[i]] = true;
        if (!d && p.down[codes[i]])
            p.released[codes[i]] = true;
        p.down[codes[i]] = d;
    }
}

void pad_axes(PadState &p, const std::int16_t raw[6]) {
    for (int i = 0; i < 4; ++i)
        p.axes[i] = stick(raw[i]);
    p.axes[4] = trigger(raw[4]);
    p.axes[5] = trigger(raw[5]);
    trigger_edges(p);
}

void note_event(App *a, const Event &e) {
    Input &in = a->input;
    const Control c = e.control;
    const std::int32_t k = c.code;
    switch (c.device) {
    case Device::Keyboard:
        if (k < 0 || std::size_t(k) >= in.held.size())
            return;
        if (e.type == Event::Type::Down) {
            if (!e.repeat)
                in.pressed.set(std::size_t(k));
            in.held.set(std::size_t(k));
        } else if (e.type == Event::Type::Up) {
            in.held.reset(std::size_t(k));
            in.released.set(std::size_t(k));
        }
        in.last = Device::Keyboard;
        return;
    case Device::Mouse:
        if (k >= 0 && k < 3) {
            if (e.type == Event::Type::Down) {
                in.mouse_down[k] = true;
                in.mouse_pressed[k] = true;
            } else if (e.type == Event::Type::Up) {
                in.mouse_down[k] = false;
                in.mouse_released[k] = true;
            }
        } else if (k == int(Mouse::Move) && e.type == Event::Type::Delta) {
            in.look_dx += e.x;
            in.look_dy += e.y;
        } else if (k == int(Mouse::Wheel) && e.type == Event::Type::Delta) {
            in.wheel += e.y;
        }
        in.last = Device::Mouse;
        return;
    case Device::Pad:
        if (k < 0 || k >= kPadControls)
            return;
        in.pad.present = true;
        if (e.type == Event::Type::Delta) {
            if (k == int(Pad::LS)) {
                in.pad.axes[0] = e.x;
                in.pad.axes[1] = e.y;
            } else if (k == int(Pad::RS)) {
                in.pad.axes[2] = e.x;
                in.pad.axes[3] = e.y;
            } else if (k == int(Pad::LT)) {
                in.pad.axes[4] = e.x;
            } else if (k == int(Pad::RT)) {
                in.pad.axes[5] = e.x;
            }
            trigger_edges(in.pad);
            if (e.x != 0.0f || e.y != 0.0f)
                in.last = Device::Pad;
            return;
        }
        if (e.type == Event::Type::Down) {
            if (!in.pad.down[k])
                in.pad.pressed[k] = true;
            in.pad.down[k] = true;
        } else if (e.type == Event::Type::Up) {
            in.pad.down[k] = false;
            in.pad.released[k] = true;
        }
        in.last = Device::Pad;
        return;
    default:
        return;
    }
}

void clear_edges(Input &in) {
    in.pressed.reset();
    in.released.reset();
    for (int i = 0; i < 3; ++i)
        in.mouse_pressed[i] = in.mouse_released[i] = false;
    for (int i = 0; i < kPadControls; ++i)
        in.pad.pressed[i] = in.pad.released[i] = false;
    in.look_dx = in.look_dy = in.wheel = 0.0f;
}

void pad_close(App *a) {
    if (!a || !a->input.pad_device)
        return;
    SDL_CloseGamepad(a->input.pad_device);
    a->input.pad_device = nullptr;
    a->input.pad = PadState{};
}

// Events posted through the automation seam, delivered exactly where
// SDL's own are: the device truth, and ImGui's queue for the pointer.
void deliver_posted(App *a) {
    std::vector<Event> queued;
    queued.swap(a->input.posted);
    for (const Event &e : queued) {
        note_event(a, e);
        ui_inject(a, e);
    }
}

void poll(App *a) {
    deliver_posted(a); // the automation seam is never gated by the UI
    const bool ui = ui_on(a);
    Input &in = a->input;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        const bool typing = ui && ui_event(a, ev);
        if (ev.type == SDL_EVENT_QUIT)
            a->platform.quit = true;
        // Once a panel is torn out, this window is no longer the last
        // one, so closing it stops producing SDL_EVENT_QUIT — and the
        // app would run on with only a floating panel to show for it.
        if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && a->platform.win &&
            ev.window.windowID == SDL_GetWindowID(a->platform.win))
            a->platform.quit = true;
        // A window that lost focus cannot show its own hidden cursor
        // again, so the crosshair ends rather than waiting to be found.
        if (ev.type == SDL_EVENT_WINDOW_FOCUS_LOST && a->aimed &&
            a->platform.win &&
            ev.window.windowID == SDL_GetWindowID(a->platform.win))
            ui_pointer_style(a, PointerStyle::Cursor);
        if (ev.type == SDL_EVENT_GAMEPAD_ADDED)
            pad_open(a, ev.gdevice.which);
        if (ev.type == SDL_EVENT_GAMEPAD_REMOVED)
            pad_removed(a, ev.gdevice.which);
        if (ev.type == SDL_EVENT_MOUSE_MOTION) {
            // A warp's echo lands where the pad put the cursor.
            const bool echo = in.warped &&
                              std::fabs(ev.motion.x - in.warp_x) < 2.0f &&
                              std::fabs(ev.motion.y - in.warp_y) < 2.0f;
            if (echo)
                in.warped = false;
            else if (a->aimed)
                note_event(a, Look(ev.motion.xrel, ev.motion.yrel));
            else
                in.last = Device::Mouse;
        }
        if (ev.type == SDL_EVENT_MOUSE_WHEEL)
            note_event(a, MouseWheel(ev.wheel.y));
        if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            const int m = mouse_of(ev.button.button);
            if (m >= 0)
                note_event(a, ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                                  ? MouseDown(Mouse(m))
                                  : MouseUp(Mouse(m)));
        }
        if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
            const Event e{ev.type == SDL_EVENT_KEY_DOWN ? Event::Type::Down
                                                        : Event::Type::Up,
                          {Device::Keyboard, std::int32_t(ev.key.scancode)},
                          0.0f,
                          0.0f,
                          ev.key.repeat};
            // A press is the panel's while it is typing; a release is
            // always noted, so no key is left held.
            if (typing && !a->aimed && e.type == Event::Type::Down)
                continue;
            note_event(a, e);
        }
    }

    // Polled, not evented: a held stick reports nothing new, and the
    // frame wants its state once.
    if (SDL_Gamepad *pad = in.pad_device) {
        std::int16_t raw[6];
        for (int i = 0; i < 6; ++i)
            raw[i] = SDL_GetGamepadAxis(
                pad, SDL_GamepadAxis(int(SDL_GAMEPAD_AXIS_LEFTX) + i));
        pad_axes(in.pad, raw);
        for (const PadButton &b : kPadButtons) {
            const bool d = SDL_GetGamepadButton(pad, b.sdl);
            if (d != in.pad.down[int(b.pad)])
                note_event(a, d ? PadDown(b.pad) : PadUp(b.pad));
        }
        for (float v : in.pad.axes)
            if (v != 0.0f)
                in.last = Device::Pad;
    }
}

} // namespace impl
} // namespace sv
