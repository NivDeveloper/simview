#pragma once

// Controls → actions → contexts, with no device in the room: the table
// of what is bound, the frame's device state, and the one resolve that
// turns the second into action values through the first.

#include <simview/Event.h>
#include <simview/Input.h>

#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

namespace sv {
namespace impl {

constexpr int kPadControls = 18;

// What a Drag reads: the pointer's own step this frame, whichever
// device moved it. A pseudo-control, so it never collides with a look.
constexpr Control kPointer{Device::None, 1};

// The device state for one frame, as the resolver reads it. Edges are
// the frame's own, recorded as events arrive, so a press and a release
// inside one frame are both seen.
struct Snapshot {
    std::bitset<512> key_down, key_pressed, key_released;
    bool mouse_down[3] = {}, mouse_pressed[3] = {}, mouse_released[3] = {};
    bool double_click = false;
    float move_dx = 0.0f, move_dy = 0.0f;       // Mouse.Move's step
    float pointer_dx = 0.0f, pointer_dy = 0.0f; // the pointer's step
    float wheel = 0.0f;
    bool pad_down[kPadControls] = {};
    bool pad_pressed[kPadControls] = {};
    bool pad_released[kPadControls] = {};
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f, lt = 0.0f, rt = 0.0f;
    bool mouse_free = true; // no panel owns the pointer
    float dt = 1.0f / 60.0f;
};

// An action's value this frame: a button's three facts, a delta (px,
// notches) and a rate (deflection, clamped to one).
struct ActionValue {
    bool down = false, pressed = false, released = false;
    float x = 0.0f, y = 0.0f;
    float rx = 0.0f, ry = 0.0f;
    bool any() const {
        return down || pressed || released || x != 0.0f || y != 0.0f ||
               rx != 0.0f || ry != 0.0f;
    }
};

// One device's bindings for a row, at one layer.
struct Layer {
    std::vector<Binding> km, pad;
    bool km_set = false, pad_set = false;
};

// One action in one context: its three layers, file over code over
// defaults, and what the last resolve found.
struct ActionRow {
    int context = 0;
    std::string id, label;
    ActionKind kind = ActionKind::Button;
    Layer defaults, code, file;
    bool enabled = true; // the engine gates a row per frame
    bool claims = true;  // a passive row reads and shadows nothing
    ActionValue value;
    std::uint32_t alive_km = 0, alive_pad = 0; // bindings not shadowed
    int live = -1;                             // the binding that spoke
    Device live_device = Device::None;
};

struct ContextState {
    std::string name;
    int priority = 0;
    bool active = false;
};

struct ActionTable {
    std::vector<ContextState> contexts;
    std::vector<ActionRow> rows;
    std::vector<std::string> unknown; // file lines nobody owns, kept

    int context(const char *name, int priority);
    int find_context(const char *name) const;
    int find(int context, const char *id) const;
    // A row, made or refreshed: the desc's controls become its defaults.
    ActionRow &add(int context, const ActionDesc &);
};

// Pad when any control is the pad's, Keyboard for the other hand, None
// for an empty binding.
Device device_of(const Binding &);
// Well formed: the shape's control count, one hand, a Drag on a button.
bool binding_valid(const Binding &, std::string *why);
// The bindings a row uses on a device, file over code over defaults.
const std::vector<Binding> &effective(const ActionRow &, Device);
void set_layer(Layer &, Device, const std::vector<Binding> &);

void resolve(ActionTable &, const Snapshot &);

// Spellings: the keycap word, and the file's canonical name.
const char *key_name(Key);
const char *mouse_name(Mouse);
const char *pad_name(Pad);
std::string control_word(Control);
std::string control_name(Control);
bool control_parse(const std::string &, Control *);
std::string binding_text(const Binding &);
bool binding_parse(const std::string &, Binding *, std::string *why);

// The file: every row with a file layer, one line each, a side that is
// not overridden written as `-` and an empty one as `none`.
std::string table_save(const ActionTable &, const char *title);
void table_load(ActionTable &, const std::string &text);

// The first control of the hand with a press edge, or an axis past a
// half; None when nothing was pressed.
Control capture_scan(const Snapshot &, Device);

} // namespace impl
} // namespace sv
