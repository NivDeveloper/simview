#pragma once

#include "../core/Callbacks.h"

#include <simview/Event.h>

#include <bitset>
#include <cstdint>
#include <forward_list>
#include <vector>

struct SDL_Gamepad;

namespace sv {
namespace impl {

struct App;

// Six axes, -1..1 past the dead zone, read once a frame. A stick's up
// is NEGATIVE, as SDL reports it; the triggers run 0..1.
struct Gamepad {
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f, lt = 0.0f, rt = 0.0f;
    bool fast = false; // the left stick pressed: the pad's Shift
    bool back = false; // B: the pad's Escape
    bool present = false;
};

struct Input {
    std::forward_list<Ecb> event_cbs;
    // Events posted through the automation seam, delivered by the
    // next Step or loop iteration exactly like SDL's own.
    std::vector<Event> posted;
    // Which keys are down, by scancode. A flight reads a key's STATE
    // once a frame, where a hotkey reads its edge.
    std::bitset<512> held;
    // The captured pointer's motion and wheel this frame. Only a
    // flight fills them: ImGui is blind to the mouse for its duration.
    float look_dx = 0.0f, look_dy = 0.0f, wheel = 0.0f;
    // The one pad the frame reads, opened on arrival and closed on
    // removal. A second is ignored: nothing here has two hands.
    SDL_Gamepad *pad_device = nullptr;
    Gamepad pad;
    // Whether the pad produced the last input, so a hint can show the
    // device in the reader's hands rather than a fixed one.
    bool last_pad = false;
};

// Raw SDL axes in, the Gamepad above out: sticks past a dead zone,
// triggers straight through. The probe goes through the same
// arithmetic a device does.
void pad_axes(Input &, const std::int16_t raw[6]);

// The two buttons the camera reads. B's rising edge ends a flight, so
// this wants the app and not just its input.
void pad_buttons(App *, bool fast, bool back);

void pad_close(App *);

// Deliver the posted events, exactly where SDL's own land.
void deliver_posted(App *);

// One key, from SDL or from PostEvent. A flight keeps its own keys and
// Tab begins one; everything else reaches the sim's callbacks.
void dispatch_key(App *, const Event &);

// SDL's queue: feed ImGui, set quit, hand keys to the sim's callbacks.
void poll(App *);

} // namespace impl
} // namespace sv
