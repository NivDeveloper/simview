#pragma once

#include "Event.h"

#include <cstdint>
#include <vector>

namespace sv {

enum class Mouse : std::int32_t {
    Left = 0,
    Right = 1,
    Middle = 2,
    Move = 3,
    Wheel = 4,
    DoubleClick = 5,
};

enum class Pad : std::int32_t {
    A = 0,
    B = 1,
    X = 2,
    Y = 3,
    LB = 4,
    RB = 5,
    LT = 6,
    RT = 7,
    L3 = 8,
    R3 = 9,
    Start = 10,
    Back = 11,
    Up = 12,
    Down = 13,
    Left = 14,
    Right = 15,
    LS = 16,
    RS = 17,
};

enum class Shape : std::int32_t {
    None = 0,
    Plain = 1,
    Axis = 2,
    Axis2 = 3,
    Drag = 4,
};

struct Binding {
    Shape shape = Shape::None;
    Control controls[4] = {};
    Control modifier = {};
};

enum class ActionKind : std::int32_t {
    Button = 0,
    Axis = 1,
    Axis2 = 2,
};

enum class CameraMode : std::int32_t {
    Orbit = 0,
    Fly = 1,
};

enum class PointerStyle : std::int32_t {
    Cursor = 0,
    Crosshair = 1,
};

struct ActionDesc {
    const char *id = nullptr;
    const char *label = nullptr;
    ActionKind kind = ActionKind::Button;
    const Binding *controls = nullptr;
    std::int32_t count = 0;
};

struct ModeDesc {
    const char *name = nullptr;
    const Binding *enter = nullptr;
    std::int32_t count = 0;
};

inline Control ControlOf(Key k) {
    return {Device::Keyboard, static_cast<std::int32_t>(k)};
}

inline Control ControlOf(Mouse m) {
    return {Device::Mouse, static_cast<std::int32_t>(m)};
}

inline Control ControlOf(Pad p) {
    return {Device::Pad, static_cast<std::int32_t>(p)};
}

inline Binding Ctl(Key k) { return {Shape::Plain, {ControlOf(k)}, {}}; }
inline Binding Ctl(Mouse m) { return {Shape::Plain, {ControlOf(m)}, {}}; }
inline Binding Ctl(Pad p) { return {Shape::Plain, {ControlOf(p)}, {}}; }

inline Binding Axis(Key neg, Key pos) {
    return {Shape::Axis, {ControlOf(neg), ControlOf(pos)}, {}};
}

inline Binding Axis(Pad neg, Pad pos) {
    return {Shape::Axis, {ControlOf(neg), ControlOf(pos)}, {}};
}

inline Binding Axis2(Key l, Key r, Key d, Key u) {
    return {Shape::Axis2,
            {ControlOf(l), ControlOf(r), ControlOf(d), ControlOf(u)},
            {}};
}

inline Binding Axis2(Pad l, Pad r, Pad d, Pad u) {
    return {Shape::Axis2,
            {ControlOf(l), ControlOf(r), ControlOf(d), ControlOf(u)},
            {}};
}

inline Binding Drag(Mouse button) {
    return {Shape::Drag, {ControlOf(button)}, {}};
}

inline Binding Drag(Pad button) {
    return {Shape::Drag, {ControlOf(button)}, {}};
}

inline Binding With(Key modifier, Binding b) {
    b.modifier = ControlOf(modifier);
    return b;
}

inline Binding With(Pad modifier, Binding b) {
    b.modifier = ControlOf(modifier);
    return b;
}

inline Event MouseDown(Mouse m) {
    return {Event::Type::Down, ControlOf(m), 0.0f, 0.0f, false};
}

inline Event MouseUp(Mouse m) {
    return {Event::Type::Up, ControlOf(m), 0.0f, 0.0f, false};
}

inline Event MouseMove(float x, float y) {
    return {Event::Type::Move, ControlOf(Mouse::Move), x, y, false};
}

inline Event Look(float dx, float dy) {
    return {Event::Type::Delta, ControlOf(Mouse::Move), dx, dy, false};
}

inline Event MouseWheel(float dy) {
    return {Event::Type::Delta, ControlOf(Mouse::Wheel), 0.0f, dy, false};
}

inline Event PadDown(Pad p) {
    return {Event::Type::Down, ControlOf(p), 0.0f, 0.0f, false};
}

inline Event PadUp(Pad p) {
    return {Event::Type::Up, ControlOf(p), 0.0f, 0.0f, false};
}

inline Event PadAxis(Pad p, float x, float y = 0.0f) {
    return {Event::Type::Delta, ControlOf(p), x, y, false};
}

struct Action {
    const char *id = nullptr;
    const char *label = nullptr;
    std::vector<Binding> controls;
};

struct ModeSpec {
    const char *name = nullptr;
    std::vector<Binding> enter;
};

}
