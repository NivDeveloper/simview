// Input: SDL's events and the posted ones, delivered to the same
// callbacks in the same order — which is what makes a posted event a
// faithful stand-in for a keypress.

#include "Input.h"

#include "../core/App.h"
#include "../ui/Ui.h"

#include <vector>

namespace sv {
namespace impl {
namespace {

// What a flight keeps for itself. Space and R are not here, so a
// pause or a restart still works in the air.
bool flight_key(const Event &e) {
    return Is(e, Key::W) || Is(e, Key::A) || Is(e, Key::S) || Is(e, Key::D) ||
           Is(e, Key::Q) || Is(e, Key::E) || Is(e, Key::LeftShift) ||
           Is(e, Key::Tab) || Is(e, Key::Escape);
}

void note_key(App *a, const Event &e) {
    if (e.key >= 0 && std::size_t(e.key) < a->input.held.size())
        a->input.held.set(std::size_t(e.key), e.type == Event::Type::KeyDown);
}

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

} // namespace

void pad_axes(Input &in, const std::int16_t raw[6]) {
    in.pad.lx = stick(raw[0]);
    in.pad.ly = stick(raw[1]);
    in.pad.rx = stick(raw[2]);
    in.pad.ry = stick(raw[3]);
    in.pad.lt = trigger(raw[4]);
    in.pad.rt = trigger(raw[5]);
}

void pad_buttons(App *a, bool fast, bool back) {
    Gamepad &g = a->input.pad;
    if (back && !g.back && a->flying)
        world_fly_end(a);
    g.fast = fast;
    g.back = back;
}

void pad_close(App *a) {
    if (!a || !a->input.pad_device)
        return;
    SDL_CloseGamepad(a->input.pad_device);
    a->input.pad_device = nullptr;
    a->input.pad = Gamepad{};
}

void dispatch_key(App *a, const Event &e) {
    note_key(a, e);
    a->input.last_pad = false;
    const bool press = e.type == Event::Type::KeyDown && !e.repeat;
    if (a->flying) {
        if (press && (Is(e, Key::Escape) || Is(e, Key::Tab)))
            world_fly_end(a);
        if (flight_key(e))
            return;
    } else if (press && Is(e, Key::Tab) && ui_fly_begin(a)) {
        return;
    }
    in_order(a->input.event_cbs, [&](const Ecb &c) { c.fn(e, c.user); });
}

// Events posted through the automation seam, delivered exactly where
// SDL's own are: same callbacks, same order, same frame.
void deliver_posted(App *a) {
    std::vector<Event> queued;
    queued.swap(a->input.posted);
    for (const Event &e : queued)
        dispatch_key(a, e);
}

void poll(App *a) {
    deliver_posted(a); // the automation seam is never gated by the UI
    const bool ui = ui_on(a);
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
        // again, so the flight ends rather than waiting to be found.
        // THIS window: a popup viewport closing loses focus too.
        if (ev.type == SDL_EVENT_WINDOW_FOCUS_LOST && a->flying &&
            a->platform.win &&
            ev.window.windowID == SDL_GetWindowID(a->platform.win))
            world_fly_end(a);
        if (ev.type == SDL_EVENT_GAMEPAD_ADDED)
            pad_open(a, ev.gdevice.which);
        if (ev.type == SDL_EVENT_GAMEPAD_REMOVED)
            pad_removed(a, ev.gdevice.which);
        if (a->flying && ev.type == SDL_EVENT_MOUSE_MOTION) {
            a->input.look_dx += ev.motion.xrel;
            a->input.look_dy += ev.motion.yrel;
            a->input.last_pad = false;
        }
        if (a->flying && ev.type == SDL_EVENT_MOUSE_WHEEL) {
            a->input.wheel += ev.wheel.y;
            a->input.last_pad = false;
        }
        if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
            const Event e{ev.type == SDL_EVENT_KEY_DOWN ? Event::Type::KeyDown
                                                        : Event::Type::KeyUp,
                          std::int32_t(ev.key.scancode), ev.key.repeat};
            // The state is true whoever has the keyboard; the edge is
            // the panel's while it is typing.
            if (typing && !a->flying) {
                note_key(a, e);
                continue;
            }
            dispatch_key(a, e);
        }
    }

    // Polled, not evented: a held stick reports nothing new, and the
    // camera wants its state once a frame.
    if (SDL_Gamepad *pad = a->input.pad_device) {
        std::int16_t raw[6];
        for (int i = 0; i < 6; ++i)
            raw[i] = SDL_GetGamepadAxis(
                pad, SDL_GamepadAxis(int(SDL_GAMEPAD_AXIS_LEFTX) + i));
        pad_axes(a->input, raw);
        pad_buttons(a, SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK),
                    SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST));
        const Gamepad &g = a->input.pad;
        if (g.lx != 0.0f || g.ly != 0.0f || g.rx != 0.0f || g.ry != 0.0f ||
            g.lt != 0.0f || g.rt != 0.0f || g.fast || g.back)
            a->input.last_pad = true;
    }
}

} // namespace impl
} // namespace sv
