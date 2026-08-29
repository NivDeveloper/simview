#pragma once

#include <cstdint>
#include <functional>

namespace sv {

enum class Key : std::int32_t {
    A = 4, B = 5, C = 6, D = 7, E = 8, F = 9, G = 10, H = 11, I = 12,
    J = 13, K = 14, L = 15, M = 16, N = 17, O = 18, P = 19, Q = 20,
    R = 21, S = 22, T = 23, U = 24, V = 25, W = 26, X = 27, Y = 28,
    Z = 29,
    N1 = 30, N2 = 31, N3 = 32, N4 = 33, N5 = 34, N6 = 35, N7 = 36,
    N8 = 37, N9 = 38, N0 = 39,
    Escape = 41, Space = 44,
    Right = 79, Left = 80, Down = 81, Up = 82,
};

struct Event {
    enum class Type : std::int32_t { KeyDown, KeyUp };
    Type type;
    std::int32_t key;
    bool repeat;
};

inline bool Is(const Event &e, Key k) {
    return e.key == static_cast<std::int32_t>(k);
}

}
