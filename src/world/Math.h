#pragma once

// Internal to src/ — the only linear algebra in the engine, and
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

#include <cmath>
#include <cstdint>

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

// A turntable: the camera sits `distance` from `focus` along its own
// view-back vector, and every mutation re-derives the position from
// that one relation rather than carrying a second copy of it.
struct Camera3 {
    Vec3 focus{};
    float distance = 5.0f;
    Quat q{};
    float fovy = 0.7853981634f;
    float min_distance = 0.05f, max_distance = 1000.0f;
    float orbit_speed = 0.005f, pan_speed = 0.0015f, dolly_speed = 0.1f;
};

// Where the camera is, in world coordinates. Never stored: two copies
// of one fact drift, and the pose is the one that must win.
inline Vec3 camera_position(const Camera3 &c) {
    return c.focus + rotate(c.q, Vec3{0.0f, 0.0f, 1.0f}) * c.distance;
}

inline Vec3 camera_right(const Camera3 &c) {
    return rotate(c.q, Vec3{1.0f, 0.0f, 0.0f});
}

inline Vec3 camera_up(const Camera3 &c) {
    return rotate(c.q, Vec3{0.0f, 1.0f, 0.0f});
}

inline Vec3 camera_forward(const Camera3 &c) {
    return rotate(c.q, Vec3{0.0f, 0.0f, -1.0f});
}

// The pose that puts the camera at (azimuth, elevation) on a sphere
// around the focus, horizon level. Derived rather than tabulated: the
// yaw carries world +X to the camera's view-back at azimuth 0, and the
// pitch tilts that back vector up out of the XY plane by elevation.
inline Quat camera_pose(float azimuth_rad, float elevation_rad) {
    constexpr float kHalfPi = 1.5707963268f;
    return normalize(axis_angle({0.0f, 0.0f, 1.0f}, azimuth_rad + kHalfPi) *
                     axis_angle({1.0f, 0.0f, 0.0f}, kHalfPi - elevation_rad));
}

// Yaw about the WORLD up axis, pitch about the camera's OWN right: the
// first keeps the horizon level however far the view has tilted, and
// the second is what makes dragging up feel like lifting the camera.
inline void camera_orbit(Camera3 &c, float dx, float dy) {
    c.q = normalize(axis_angle({0.0f, 0.0f, 1.0f}, -dx * c.orbit_speed) * c.q *
                    axis_angle({1.0f, 0.0f, 0.0f}, -dy * c.orbit_speed));
}

// The focus slides in the screen plane, at a rate proportional to
// distance, so the same drag covers the same fraction of the view
// whether the camera is close in or far out.
inline void camera_pan(Camera3 &c, float dx, float dy) {
    const float k = c.pan_speed * c.distance;
    c.focus = c.focus + camera_right(c) * (-dx * k) + camera_up(c) * (dy * k);
}

// Multiplicative, so one wheel click covers the same visual step at
// every scale, and clamped so the camera can neither pass through the
// focus nor leave the depth range behind.
inline void camera_dolly(Camera3 &c, float scroll) {
    float d = c.distance * std::exp(-scroll * c.dolly_speed);
    if (d < c.min_distance)
        d = c.min_distance;
    if (d > c.max_distance)
        d = c.max_distance;
    c.distance = d;
}

inline Mat4 camera_view(const Camera3 &c) {
    return mat_mul(mat_from_quat(conjugate(c.q)),
                   mat_translate(camera_position(c) * -1.0f));
}

// The near plane follows the orbit scale. Depth precision is governed
// by the NEAR distance, not the far one, so a fixed 0.01 would throw
// away resolution at every scale but one; there is no far plane at all.
inline float camera_znear(const Camera3 &c) {
    const float n = c.distance * 1e-3f;
    return n > 1e-4f ? n : 1e-4f;
}

// Infinite-far reverse-Z: the near plane maps to 1 and infinity to 0,
// which is what makes a float depth buffer precise across the whole
// range. m[1][1] is POSITIVE — the renderer flips the viewport height
// itself, and a second flip here would render the world upside down.
inline Mat4 proj_reverse_z(float fovy, float aspect, float znear) {
    const float f = 1.0f / std::tan(0.5f * fovy);
    Mat4 r{};
    for (float &v : r.m)
        v = 0.0f;
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[11] = -1.0f;
    r.m[14] = znear;
    return r;
}

// The depth a point WOULD be written at, quantized — the sort key's
// only ingredient, and the same number the depth buffer will hold, so
// the ordering the sort produces is the ordering the test enforces.
inline std::uint16_t depth_key(const Mat4 &view, Vec3 p, float znear) {
    const Vec3 v = transform_point(view, p);
    const float z = -v.z > znear ? -v.z : znear;
    float d = znear / z;
    if (d < 0.0f)
        d = 0.0f;
    if (d > 1.0f)
        d = 1.0f;
    return static_cast<std::uint16_t>(d * 65535.0f);
}

} // namespace impl

// Ordering, per pass. The pass is the outer loop and never enters the
// key: encoding it would make the top bits a constant.
//
// Opaque leads with the pipeline, because a state change costs more
// than the overdraw the depth test would have saved, and only breaks
// ties by nearness. Transparent leads with depth, because there the
// order IS the result.
inline std::uint64_t opaque_key(std::uint32_t pipeline, std::uint32_t item,
                                std::uint16_t depth) {
    return (std::uint64_t(pipeline & 0xffff) << 48) |
           (std::uint64_t(item & 0xffff) << 32) |
           (std::uint64_t(std::uint16_t(65535 - depth)) << 16);
}

inline std::uint64_t transparent_key(std::uint32_t pipeline,
                                     std::uint16_t depth) {
    return (std::uint64_t(depth) << 48) |
           (std::uint64_t(pipeline & 0xffff) << 32);
}

} // namespace sv
