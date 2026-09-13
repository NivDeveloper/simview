#pragma once

#include <cstdint>
#include <functional>

namespace sv {

enum class Key : std::int32_t {
    A = 4,
    B = 5,
    C = 6,
    D = 7,
    E = 8,
    F = 9,
    G = 10,
    H = 11,
    I = 12,
    J = 13,
    K = 14,
    L = 15,
    M = 16,
    N = 17,
    O = 18,
    P = 19,
    Q = 20,
    R = 21,
    S = 22,
    T = 23,
    U = 24,
    V = 25,
    W = 26,
    X = 27,
    Y = 28,
    Z = 29,
    N1 = 30,
    N2 = 31,
    N3 = 32,
    N4 = 33,
    N5 = 34,
    N6 = 35,
    N7 = 36,
    N8 = 37,
    N9 = 38,
    N0 = 39,
    Return = 40,
    Escape = 41,
    Backspace = 42,
    Tab = 43,
    Space = 44,
    Grave = 53,
    F1 = 58,
    Right = 79,
    Left = 80,
    Down = 81,
    Up = 82,
    LeftCtrl = 224,
    LeftShift = 225,
    LeftAlt = 226,
};

enum class Device : std::int32_t {
    None = 0,
    Keyboard = 1,
    Mouse = 2,
    Pad = 3,
};

struct Control {
    Device device = Device::None;
    std::int32_t code = 0;
};

inline bool operator==(Control a, Control b) {
    return a.device == b.device && a.code == b.code;
}

inline bool operator!=(Control a, Control b) { return !(a == b); }

struct Event {
    enum class Type : std::int32_t { Down, Up, Move, Delta };
    Type type = Type::Down;
    Control control;
    float x = 0.0f;
    float y = 0.0f;
    bool repeat = false;
};

inline bool Is(const Event &e, Key k) {
    return e.control.device == Device::Keyboard &&
           e.control.code == static_cast<std::int32_t>(k);
}

inline Event KeyDown(Key k, bool repeat = false) {
    return {Event::Type::Down,
            {Device::Keyboard, static_cast<std::int32_t>(k)},
            0.0f,
            0.0f,
            repeat};
}

inline Event KeyUp(Key k) {
    return {Event::Type::Up,
            {Device::Keyboard, static_cast<std::int32_t>(k)},
            0.0f,
            0.0f,
            false};
}

}
