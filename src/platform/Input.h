#pragma once

#include "../core/Actions.h"

#include <simview/Event.h>
#include <simview/Input.h>

#include <bitset>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_Gamepad;

namespace sv {

struct Stroke;

namespace impl {

struct App;
struct WorldState;

// The one pad the frame reads: its buttons with this frame's edges,
// its six axes past a dead zone (a stick's up is NEGATIVE, as SDL
// reports it; the triggers run 0..1), and whether it is here at all.
struct PadState {
    bool present = false;
    bool down[kPadControls] = {};
    bool pressed[kPadControls] = {};
    bool released[kPadControls] = {};
    float axes[6] = {}; // lx ly rx ry lt rt
};

struct StrokeCb {
    void (*fn)(const Stroke &, void *) = nullptr;
    void *user = nullptr;
    void (*free)(void *) = nullptr;
};

// An app's mode: its entry, its own context, and what it listens for.
struct ModeState {
    App *app = nullptr;
    std::string name;
    std::vector<Binding> enter;
    int context = -1;
    StrokeCb stroke, carry;
};

// An app's handler for one row, fired from the frame's values.
struct ActionCb {
    int row = -1;
    void (*fn)(float, float, void *) = nullptr;
    void *user = nullptr;
    void (*free)(void *) = nullptr;
};

// Device truth for the frame, and the model that reads it.
struct Input {
    // Events posted through the automation seam, delivered by the
    // next Step or loop iteration exactly like SDL's own.
    std::vector<Event> posted;
    std::bitset<512> held, pressed, released; // keys by scancode
    bool mouse_down[3] = {}, mouse_pressed[3] = {}, mouse_released[3] = {};
    float look_dx = 0.0f, look_dy = 0.0f; // relative motion, crosshair only
    float wheel = 0.0f;
    PadState pad;
    SDL_Gamepad *pad_device = nullptr;
    Device last = Device::Mouse; // the device in the reader's hands

    ActionTable table;
    std::vector<ActionCb> callbacks;
    std::vector<std::unique_ptr<ModeState>> modes; // an address is a handle
    CameraMode camera = CameraMode::Orbit;
    PointerStyle style = PointerStyle::Cursor;
    int active_mode = -1;

    // The gesture engine's memory between frames.
    float click_timer = 1.0f; // since the last primary press
    float click_x = 0.0f, click_y = 0.0f;
    float drag_px = 0.0f; // since the primary press
    bool primary_held = false;
    bool file_applied = false;
};

// Raw SDL axes in, the pad's axes out: sticks past a dead zone,
// triggers straight through. The probe goes through the same
// arithmetic a device does.
void pad_axes(PadState &, const std::int16_t raw[6]);

// The ONE writer of device truth, for SDL's events and posted ones
// alike: a key's held bit and edge, a mouse button, a look, a wheel
// notch, a pad button or axis.
void note_event(App *, const Event &);

// This frame's edges and relative motion are spent.
void clear_edges(Input &);

void pad_close(App *);

// Deliver the posted events, exactly where SDL's own land.
void deliver_posted(App *);

// SDL's queue: feed ImGui, set quit, note every device event; then
// the pad, polled, since a held stick reports nothing new.
void poll(App *);

} // namespace impl
} // namespace sv
