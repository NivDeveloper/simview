#pragma once

// Internal to src/ — the engine's arithmetic, and the only file that
// holds any. Linear algebra for the 3D stratum, and
// deliberately the smallest one that closes the 3D path: a vector, a
// quaternion, a 4x4, and the camera the world looks through.
//
// Header-only and free of every SDK type on purpose. It is the one
// piece of the world a check can reach with no device at all, which is
// what makes the projection, the pose and the orbit provable by
// arithmetic rather than by looking at pixels (world_math_check).
//
// Conventions, all four load-bearing:
//   - Matrices are COLUMN-MAJOR, m[col * 4 + row], composed as
//     clip = P * V * point. That is the layout uploaded to the shader,
//     which declares column_major to match.
//   - View space is right-handed, looking down -Z, +Y up.
//   - The WORLD is Z-UP: +Z is the vertical axis a turntable yaws
//     around, and the grid lies in the XY plane.
//   - Depth is REVERSE-Z: the near plane is 1 and infinity is 0. Every
//     other half of that decision (the D32 target, the 0.0 clear, the
//     GreaterOrEqual test) lives with the pass table; this file owns
//     the matrix.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sv {
namespace impl {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(Vec3 v) {
    const float n = length(v);
    return n > 0.0f ? v * (1.0f / n) : v;
}

// Unit quaternions only: every operation here either preserves the norm
// or renormalizes, because a drifting camera rotation shears the view.
struct Quat {
    float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;
};

inline Quat axis_angle(Vec3 axis, float radians) {
    const Vec3 a = normalize(axis);
    const float h = 0.5f * radians;
    const float s = std::sin(h);
    return {std::cos(h), a.x * s, a.y * s, a.z * s};
}

// Hamilton product: (a * b) rotates by b first, then by a.
inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

inline Quat conjugate(Quat q) { return {q.w, -q.x, -q.y, -q.z}; }

inline Quat normalize(Quat q) {
    const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (!(n > 0.0f))
        return {};
    const float k = 1.0f / n;
    return {q.w * k, q.x * k, q.y * k, q.z * k};
}

inline Vec3 rotate(Quat q, Vec3 v) {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

inline Mat4 mat_mul(const Mat4 &a, const Mat4 &b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i < 4; ++i) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += a.m[k * 4 + i] * b.m[c * 4 + k];
            r.m[c * 4 + i] = s;
        }
    return r;
}

// The point transform, w divided out — what a check uses to ask where
// a world point lands in normalized device coordinates.
inline Vec3 transform_point(const Mat4 &a, Vec3 p, float *w_out = nullptr) {
    float o[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i)
        o[i] = a.m[0 * 4 + i] * p.x + a.m[1 * 4 + i] * p.y +
               a.m[2 * 4 + i] * p.z + a.m[3 * 4 + i];
    if (w_out)
        *w_out = o[3];
    const float k = o[3] != 0.0f ? 1.0f / o[3] : 1.0f;
    return {o[0] * k, o[1] * k, o[2] * k};
}

inline Mat4 mat_translate(Vec3 t) {
    Mat4 r{};
    r.m[12] = t.x;
    r.m[13] = t.y;
    r.m[14] = t.z;
    return r;
}

inline Mat4 mat_from_quat(Quat q) {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat4 r{};
    r.m[0] = 1.0f - 2.0f * (yy + zz);
    r.m[1] = 2.0f * (xy + wz);
    r.m[2] = 2.0f * (xz - wy);
    r.m[4] = 2.0f * (xy - wz);
    r.m[5] = 1.0f - 2.0f * (xx + zz);
    r.m[6] = 2.0f * (yz + wx);
    r.m[8] = 2.0f * (xz + wy);
    r.m[9] = 2.0f * (yz - wx);
    r.m[10] = 1.0f - 2.0f * (xx + yy);
    return r;
}

// The general inverse, by cofactors. A camera matrix could be inverted
// far more cheaply, but clip_to_world must invert the PROJECTION too,
// and having the general one proved by a check is what keeps a future
// cursor ray from repeating vklib's near-plane convention bug.
inline Mat4 mat_inverse(const Mat4 &a) {
    const float *m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] +
             m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] +
             m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] +
              m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] +
              m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] +
             m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] +
             m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    const float det =
        m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    Mat4 r{};
    if (det == 0.0f)
        return r;
    const float k = 1.0f / det;
    for (int i = 0; i < 16; ++i)
        r.m[i] = inv[i] * k;
    return r;
}

// An axis-aligned box in world units, and the union of nothing: a box
// that has not been given a point yet is INVALID rather than empty at
// the origin, because a zero-extent box at the origin is a real
// answer and "I do not know" is not.
struct Aabb {
    Vec3 lo{}, hi{};
    bool valid = false;
};

inline void aabb_add(Aabb &b, Vec3 lo, Vec3 hi) {
    if (!b.valid) {
        b.lo = lo;
        b.hi = hi;
        b.valid = true;
        return;
    }
    b.lo = {b.lo.x < lo.x ? b.lo.x : lo.x, b.lo.y < lo.y ? b.lo.y : lo.y,
            b.lo.z < lo.z ? b.lo.z : lo.z};
    b.hi = {b.hi.x > hi.x ? b.hi.x : hi.x, b.hi.y > hi.y ? b.hi.y : hi.y,
            b.hi.z > hi.z ? b.hi.z : hi.z};
}

// How far a point is from the nearest point of the box — zero when it
// is inside, which is a case the near plane very much has.
inline float aabb_distance(const Aabb &b, Vec3 p) {
    if (!b.valid)
        return 0.0f;
    const float dx =
        p.x < b.lo.x ? b.lo.x - p.x : (p.x > b.hi.x ? p.x - b.hi.x : 0.0f);
    const float dy =
        p.y < b.lo.y ? b.lo.y - p.y : (p.y > b.hi.y ? p.y - b.hi.y : 0.0f);
    const float dz =
        p.z < b.lo.z ? b.lo.z - p.z : (p.z > b.hi.z ? p.z - b.hi.z : 0.0f);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// A half-space, `n . p + d >= 0` inside. Normalized, so `n . p + d` is
// a DISTANCE in world units and a caller can push a plane out by a
// radius without a second thought.
struct Plane {
    Vec3 n{};
    float d = 0.0f;
};

// The five planes a point must be inside of to be drawn. Five, not
// six: the perspective projection here runs to infinity, so its far
// plane is degenerate — the extraction produces a zero normal and it
// is dropped rather than special-cased at every test.
//
// Read straight off the combined matrix, which is what makes this
// correct for BOTH projections without knowing which one it is: the
// clip conditions are -w <= x,y <= w and 0 <= z <= w whatever produced
// the matrix, and each is one row combination.
struct Frustum {
    Plane p[6]{};
    int count = 0;
};

inline Frustum frustum_of(const Mat4 &m) {
    // Column-major storage, so row i is m[i], m[4+i], m[8+i], m[12+i].
    const auto row = [&](int i) {
        return Plane{{m.m[i], m.m[4 + i], m.m[8 + i]}, m.m[12 + i]};
    };
    const auto add = [](Plane a, Plane b, float sign) {
        return Plane{a.n + b.n * sign, a.d + b.d * sign};
    };
    const Plane r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    const Plane raw[6] = {
        add(r3, r0, 1.0f),  // left:   x >= -w
        add(r3, r0, -1.0f), // right:  x <= w
        add(r3, r1, 1.0f),  // bottom: y >= -w
        add(r3, r1, -1.0f), // top:    y <= w
        add(r3, r2, -1.0f), // near:   z <= w
        r2,                 // far:    z >= 0 — degenerate when infinite
    };

    Frustum f;
    for (const Plane &q : raw) {
        const float len = length(q.n);
        if (len < 1e-12f)
            continue;
        f.p[f.count++] = {q.n * (1.0f / len), q.d / len};
    }
    return f;
}

// Whether any part of the box could be drawn. Conservative in the one
// direction that is safe: a box that straddles a corner of the frustum
// can pass every plane test and still be invisible, which costs a draw
// nobody sees. The reverse — a false MISS — would delete geometry, so
// the test is written to make it impossible rather than tight.
inline bool frustum_intersects(const Frustum &f, const Aabb &b) {
    if (!b.valid)
        return true;
    for (int i = 0; i < f.count; ++i) {
        const Plane &q = f.p[i];
        // The corner furthest ALONG the normal. If even that one is
        // behind the plane, every corner is.
        const Vec3 corner{q.n.x >= 0.0f ? b.hi.x : b.lo.x,
                          q.n.y >= 0.0f ? b.hi.y : b.lo.y,
                          q.n.z >= 0.0f ? b.hi.z : b.lo.z};
        if (dot(q.n, corner) + q.d < 0.0f)
            return false;
    }
    return true;
}

// ── hashing ──────────────────────────────────────────────────────────
//
// FNV-1a, a byte at a time. Used to NAME a thing after its own
// contents, where a library offers a registry that can add but never
// replace: two callers with the same content then find one entry and
// a caller with new content gets a new one, with nobody keeping a
// counter.

inline constexpr std::uint32_t kFnvSeed = 2166136261u;

constexpr std::uint32_t fnv1a(std::uint32_t h, std::uint32_t byte) {
    return (h ^ byte) * 16777619u;
}

// ── binning ──────────────────────────────────────────────────────────
//
// Turning a set of values into cells, which is what every reduction of
// a point cloud starts with and none of them should each spell.

struct Span {
    double lo = 0.0, hi = 1.0;
};

// A range that always has width: a degenerate one makes every bin index
// the same bin, and the picture is a single spike that says nothing.
inline Span span_of(const std::vector<double> &v) {
    if (v.empty())
        return {};
    auto [a, b] = std::minmax_element(v.begin(), v.end());
    Span s{*a, *b};
    if (!(s.hi > s.lo))
        s.hi = s.lo + 1.0;
    return s;
}

inline int bin_of(double v, Span s, int bins) {
    const double t = (v - s.lo) / (s.hi - s.lo);
    return std::clamp(int(t * double(bins)), 0, bins - 1);
}

// ── fields ───────────────────────────────────────────────────────────

// Marching squares. For each cell of the binned field and each level,
// the four corners above or below the level pick one of sixteen cases,
// and the crossings are found by linear interpolation along the edges
// they lie on — so a contour follows the DATA rather than the cell
// boundaries and does not staircase.
//
// The two saddle cases are resolved by the cell's own average, which
// is the cheapest rule that is at least self-consistent: whichever way
// it decides, the two crossings it joins are the two the average says
// are on the same side.
//
// Output is pairs of points, which is what a line drawn as SEGMENTS
// takes — one item for every level rather than one per segment.
inline void contours(const std::vector<double> &f, int n, Span sx, Span sy,
                     double top, std::vector<double> &xs,
                     std::vector<double> &ys) {
    xs.clear();
    ys.clear();
    if (n < 2 || top <= 0.0)
        return;

    const double dx = (sx.hi - sx.lo) / double(n);
    const double dy = (sy.hi - sy.lo) / double(n);
    const auto px = [&](double i) { return sx.lo + (i + 0.5) * dx; };
    const auto py = [&](double j) { return sy.lo + (j + 0.5) * dy; };
    const auto at = [&](int i, int j) {
        return f[std::size_t(j) * std::size_t(n) + std::size_t(i)];
    };

    // Four levels, evenly through the field's range. Enough to read a
    // shape; more at 32 bins is noise dressed as detail.
    for (int k = 1; k <= 4; ++k) {
        const double level = top * double(k) / 5.0;
        for (int j = 0; j + 1 < n; ++j)
            for (int i = 0; i + 1 < n; ++i) {
                const double v[4] = {at(i, j), at(i + 1, j), at(i + 1, j + 1),
                                     at(i, j + 1)};
                int code = 0;
                for (int c = 0; c < 4; ++c)
                    if (v[c] > level)
                        code |= 1 << c;
                if (code == 0 || code == 15)
                    continue;

                // The crossing on each edge, as a point.
                const auto lerp = [&](double a, double b) {
                    const double t = (level - a) / (b - a);
                    return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                };
                const double bottom_t = lerp(v[0], v[1]);
                const double right_t = lerp(v[1], v[2]);
                const double top_t = lerp(v[3], v[2]);
                const double left_t = lerp(v[0], v[3]);
                const double ex[4] = {px(double(i) + bottom_t),
                                      px(double(i + 1)), px(double(i) + top_t),
                                      px(double(i))};
                const double ey[4] = {py(double(j)), py(double(j) + right_t),
                                      py(double(j + 1)),
                                      py(double(j) + left_t)};

                const auto emit = [&](int e0, int e1) {
                    xs.push_back(ex[e0]);
                    ys.push_back(ey[e0]);
                    xs.push_back(ex[e1]);
                    ys.push_back(ey[e1]);
                };
                switch (code) {
                case 1:
                case 14:
                    emit(3, 0);
                    break;
                case 2:
                case 13:
                    emit(0, 1);
                    break;
                case 3:
                case 12:
                    emit(3, 1);
                    break;
                case 4:
                case 11:
                    emit(1, 2);
                    break;
                case 6:
                case 9:
                    emit(0, 2);
                    break;
                case 7:
                case 8:
                    emit(3, 2);
                    break;
                case 5:
                case 10: {
                    const double mid = (v[0] + v[1] + v[2] + v[3]) * 0.25;
                    const bool joined = (mid > level) == (code == 5);
                    if (joined) {
                        emit(3, 0);
                        emit(1, 2);
                    } else {
                        emit(0, 1);
                        emit(3, 2);
                    }
                    break;
                }
                default:
                    break;
                }
            }
    }
}
} // namespace impl
} // namespace sv
