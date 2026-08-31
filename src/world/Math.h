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
// Perspective or orthographic. The two differ ONLY in the projection
// matrix: the pose, the turntable and the depth convention are shared,
// so a caller switches one field and everything else holds.
enum class Projection : int { Perspective = 0, Orthographic = 1 };

struct Camera3 {
    Vec3 focus{};
    float distance = 5.0f;
    Quat q{};
    float fovy = 0.7853981634f;
    Projection projection = Projection::Perspective;
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

// Orthographic depth is LINEAR, so it needs a far plane where the
// perspective one needs none, and the range is what its precision is
// spent on. Twenty orbits past the camera covers a scene arranged
// around the focus without spending the buffer on empty distance.
inline float camera_zfar(const Camera3 &c) { return c.distance * 20.0f; }

// The world height the view covers at the focus. In perspective this
// is what the frustum subtends there; the orthographic box is built
// to match it, so switching projections holds the subject the same
// size and only changes the convergence.
inline float camera_view_height(const Camera3 &c) {
    return 2.0f * c.distance * std::tan(0.5f * c.fovy);
}

// Orthographic, reverse-Z: the near plane maps to 1 and the FAR one to
// 0. Linear in view depth, so unlike the perspective form it cannot
// run to infinity — the far plane is a real number and the precision
// is spread evenly between the two.
inline Mat4 proj_ortho_reverse_z(float height, float aspect, float znear,
                                 float zfar) {
    Mat4 r{};
    for (float &v : r.m)
        v = 0.0f;
    const float span = zfar - znear;
    r.m[0] = 2.0f / (height * aspect);
    r.m[5] = 2.0f / height;
    r.m[10] = 1.0f / span;
    r.m[14] = zfar / span;
    r.m[15] = 1.0f;
    return r;
}

// Orthographic FORWARD-Z: near maps to 0 and far to 1, which is the
// opposite of everything else here and is not a choice.
//
// A comparison sampler is what makes a soft shadow four taps instead
// of nine, and the renderer hardcodes its comparison to LESS. Under
// reverse-Z that reads every surface as being behind itself, and the
// whole scene comes out in shadow — which looks like a bias problem
// and is not one. So the shadow map, alone in this engine, counts
// depth the other way.
inline Mat4 proj_ortho_forward_z(float height, float aspect, float znear,
                                 float zfar) {
    Mat4 r{};
    for (float &v : r.m)
        v = 0.0f;
    const float span = zfar - znear;
    r.m[0] = 2.0f / (height * aspect);
    r.m[5] = 2.0f / height;
    r.m[10] = -1.0f / span;
    r.m[14] = -znear / span;
    r.m[15] = 1.0f;
    return r;
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

// The projection this camera looks through, against a target of the
// given aspect.
inline Mat4 camera_proj(const Camera3 &c, float aspect) {
    return c.projection == Projection::Orthographic
               ? proj_ortho_reverse_z(camera_view_height(c), aspect,
                                      camera_znear(c), camera_zfar(c))
               : proj_reverse_z(c.fovy, aspect, camera_znear(c));
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

// The near plane, told what is actually out there. It only ever moves
// CLOSER than the orbit-scale default, never further: items are free
// to report no bounds at all, and geometry an item did not account for
// must not be sliced away by a plane derived from geometry it did.
//
// The default alone is a real failure at range — orbiting a thousand
// units out puts the near plane a whole unit from the eye, and a
// subject panned close to the camera is simply gone.
inline float camera_znear(const Camera3 &c, const Aabb &scene) {
    const float base = camera_znear(c);
    if (!scene.valid)
        return base;
    // Half the distance to the nearest thing: a plane exactly ON the
    // geometry clips half of it away.
    const float want = aabb_distance(scene, camera_position(c)) * 0.5f;
    if (want >= base)
        return base;
    return want > 1e-5f ? want : 1e-5f;
}

inline Mat4 camera_proj(const Camera3 &c, float aspect, float znear) {
    return c.projection == Projection::Orthographic
               ? proj_ortho_reverse_z(camera_view_height(c), aspect, znear,
                                      camera_zfar(c))
               : proj_reverse_z(c.fovy, aspect, znear);
}

// How many pixels one world unit covers at one unit of view depth.
// Perspective divides this by the depth; orthographic does not, which
// is the whole difference between the two and the reason this is a
// number and a flag rather than two functions.
inline float focal_px(const Camera3 &c, std::uint32_t th) {
    if (c.projection == Projection::Orthographic) {
        const float h = camera_view_height(c);
        return h > 0.0f ? float(th) / h : 0.0f;
    }
    return float(th) / (2.0f * std::tan(0.5f * c.fovy));
}

// An orthographic reverse-Z projection is what a DIRECTIONAL light
// looks through: its rays are parallel, so there is no eye position
// for a perspective one to be at.
//
// The box is fitted to what is actually in the scene, which is the
// whole reason the shadow pass had to wait for bounds. A fixed box
// either clips the scene out of the map or spends most of its texels
// on empty space, and the second failure looks like a resolution
// problem rather than a fitting one.
struct LightFit {
    Mat4 world_to_clip{};
    float world_per_texel = 0.0f; // how coarse the map is, in world units
    bool valid = false;
};

// A rotation-only view matrix looking along `forward` from `eye`. Up
// is the world's +Z unless the light is nearly vertical, where +Y
// takes over — any fixed up vector degenerates somewhere, and the
// only question is whether the code says where.
inline Mat4 mat_look_along(Vec3 forward, Vec3 eye) {
    const Vec3 f = normalize(forward);
    Vec3 up{0.0f, 0.0f, 1.0f};
    if (std::fabs(dot(f, up)) > 0.99f)
        up = {0.0f, 1.0f, 0.0f};
    const Vec3 r = normalize(cross(f, up));
    const Vec3 u = cross(r, f);

    // Rows are the basis, so the matrix takes world into light space;
    // view space looks down -Z, hence the negated forward.
    Mat4 m{};
    m.m[0] = r.x;
    m.m[4] = r.y;
    m.m[8] = r.z;
    m.m[12] = -dot(r, eye);
    m.m[1] = u.x;
    m.m[5] = u.y;
    m.m[9] = u.z;
    m.m[13] = -dot(u, eye);
    m.m[2] = -f.x;
    m.m[6] = -f.y;
    m.m[10] = -f.z;
    m.m[14] = dot(f, eye);
    m.m[3] = 0.0f;
    m.m[7] = 0.0f;
    m.m[11] = 0.0f;
    m.m[15] = 1.0f;
    return m;
}

// Fit a directional light's box around `scene`. `toward` points AT the
// light, the way a LightDesc spells it, so the rays travel along its
// negation.
inline LightFit light_fit(Vec3 toward, const Aabb &scene, std::uint32_t map_px,
                          float ground_z = 0.0f) {
    LightFit fit{};
    if (!scene.valid || length(toward) <= 0.0f || !map_px)
        return fit;

    const Vec3 dir = normalize(toward);

    // Where the casters ARE is not where their shadows LAND, and the
    // box has to hold both: a fit around the geometry alone leaves the
    // ground outside the map, every lookup there falls off the edge
    // and reads as lit, and the result is a scene with no shadows in
    // it and a shadow pass that ran perfectly.
    //
    // The ground is the world's XY plane, which is a fact about this
    // engine and not an assumption: Z is up and the grid is drawn at
    // z = 0. A low light throws a long shadow, so this is the term
    // that grows the box, and it is capped in the caller's units
    // rather than here.
    Vec3 pts[16];
    int n = 0;
    for (int i = 0; i < 8; ++i) {
        const Vec3 c{i & 1 ? scene.hi.x : scene.lo.x,
                     i & 2 ? scene.hi.y : scene.lo.y,
                     i & 4 ? scene.hi.z : scene.lo.z};
        pts[n++] = c;
        if (dir.z > 1e-3f && c.z > ground_z) {
            const float t = (c.z - ground_z) / dir.z;
            pts[n++] = c - dir * t;
        }
    }

    Vec3 centre = pts[0];
    for (int i = 1; i < n; ++i)
        centre = centre + pts[i];
    centre = centre * (1.0f / float(n));
    const Mat4 rot = mat_look_along(dir * -1.0f, centre);

    // Every point through the rotation, because a box rotated into
    // light space is not a box: the min and max of two opposite
    // corners would fit a volume that misses the other six.
    Vec3 lo{}, hi{};
    for (int i = 0; i < n; ++i) {
        const Vec3 p = transform_point(rot, pts[i]);
        if (i == 0) {
            lo = hi = p;
            continue;
        }
        lo = {p.x < lo.x ? p.x : lo.x, p.y < lo.y ? p.y : lo.y,
              p.z < lo.z ? p.z : lo.z};
        hi = {p.x > hi.x ? p.x : hi.x, p.y > hi.y ? p.y : hi.y,
              p.z > hi.z ? p.z : hi.z};
    }

    // A square box, so a rotating light does not change how coarse the
    // map is — a shadow whose softness breathes as the scene turns
    // reads as flicker.
    float half = 0.5f * (hi.x - lo.x);
    const float half_y = 0.5f * (hi.y - lo.y);
    if (half_y > half)
        half = half_y;
    if (!(half > 0.0f))
        half = 1.0f;
    half *= 1.02f; // a hair of margin, so the edge texel is not the edge

    // Push the eye back past the nearest corner and take the far one
    // as the far plane. Light space looks down -Z, so the box spans
    // -hi.z (nearest) to -lo.z (furthest) in depth.
    const float depth = hi.z - lo.z;
    const float pad = depth * 0.05f + half * 0.01f + 1e-3f;
    const Mat4 view = mat_look_along(dir * -1.0f, centre + dir * (hi.z + pad));

    fit.world_to_clip = mat_mul(
        proj_ortho_forward_z(2.0f * half, 1.0f, pad, depth + 2.0f * pad), view);
    fit.world_per_texel = 2.0f * half / float(map_px);
    fit.valid = true;
    return fit;
}

// The depth a point WOULD be written at, quantized — the sort key's
// only ingredient. Taken through the REAL projection rather than a
// formula of its own, so it is the number the depth buffer will hold
// under either one, and the order the sort produces is the order the
// test enforces.
inline std::uint16_t depth_key(const Mat4 &world_to_clip, Vec3 p) {
    float w = 0.0f;
    const Vec3 c = transform_point(world_to_clip, p, &w);
    // Behind the camera: no depth means anything, and 0 sorts it where
    // it will be clipped anyway.
    if (w <= 0.0f)
        return 0;
    float d = c.z;
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
