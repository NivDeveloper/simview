#pragma once

#include "../core/Math.h"

#include <cmath>
#include <cstdint>

namespace sv {
namespace impl {

// Perspective or orthographic. The two differ ONLY in the projection
// matrix: the pose, the turntable and the depth convention are shared,
// so a caller switches one setting and everything else holds.
enum class Projection : int { Perspective = 0, Orthographic = 1 };

// Reverse-Z: near maps to 1, far to 0. Linear in view depth, so it
// needs a real far plane where the perspective form needs none.
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

// Near maps to 1, infinity to 0. m[1][1] is POSITIVE: the renderer
// flips the viewport height, and a second flip renders upside down.
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

// A turntable: the camera sits `distance` from `focus` along its own
// view-back vector, and every mutation re-derives the position from
// that one relation rather than carrying a second copy of it.
class Camera3 {
  public:
    // ── what it is looking at ────────────────────────────────────────

    void frame(Vec3 focus, float distance) {
        focus_ = focus;
        distance_ = distance > 0.0f ? distance : distance_;
    }

    // Derived, not tabulated: the yaw carries world +X to the
    // view-back at azimuth 0, the pitch lifts it out of the XY plane.
    void look(float azimuth_rad, float elevation_rad) {
        constexpr float kHalfPi = 1.5707963268f;
        pose_ =
            normalize(axis_angle({0.0f, 0.0f, 1.0f}, azimuth_rad + kHalfPi) *
                      axis_angle({1.0f, 0.0f, 0.0f}, kHalfPi - elevation_rad));
    }

    void set_fov(float fovy_rad) { fovy_ = fovy_rad; }
    void set_mode(Projection p) { mode_ = p; }

    Vec3 focus() const { return focus_; }
    float distance() const { return distance_; }
    Quat pose() const { return pose_; }
    float fovy() const { return fovy_; }
    Projection mode() const { return mode_; }
    bool orthographic() const { return mode_ == Projection::Orthographic; }

    // ── where that puts it ──────────────────────────────────────
    // Never stored: two copies of one fact drift.

    Vec3 position() const {
        return focus_ + pose_ * Vec3{0.0f, 0.0f, 1.0f} * distance_;
    }
    Vec3 right() const { return pose_ * Vec3{1.0f, 0.0f, 0.0f}; }
    Vec3 up() const { return pose_ * Vec3{0.0f, 1.0f, 0.0f}; }
    Vec3 forward() const { return pose_ * Vec3{0.0f, 0.0f, -1.0f}; }

    // ── what a gesture does to it ────────────────────────────────────

    // Yaw about the WORLD up, pitch about the camera's OWN right:
    // the first is what keeps the horizon level.
    void orbit(float dx, float dy) {
        pose_ = normalize(axis_angle({0.0f, 0.0f, 1.0f}, -dx * kOrbit) * pose_ *
                          axis_angle({1.0f, 0.0f, 0.0f}, -dy * kOrbit));
    }

    // The focus slides in the screen plane, at a rate proportional to
    // distance, so the same drag covers the same fraction of the view
    // whether the camera is close in or far out.
    void pan(float dx, float dy) {
        const float k = kPan * distance_;
        focus_ = focus_ + right() * (-dx * k) + up() * (dy * k);
    }

    // Multiplicative, so one wheel click covers the same visual step at
    // every scale, and clamped so the camera can neither pass through
    // the focus nor leave the depth range behind.
    void dolly(float scroll) {
        float d = distance_ * std::exp(-scroll * kDolly);
        if (d < kMinDistance)
            d = kMinDistance;
        if (d > kMaxDistance)
            d = kMaxDistance;
        distance_ = d;
    }

    // ── the matrices it hands the renderer ───────────────────────────

    Mat4 view() const {
        return mat_from_quat(conjugate(pose_)) * mat_translate(-position());
    }

    // Follows the orbit scale: precision is governed by the NEAR
    // distance. There is no far plane at all.
    float znear() const {
        const float n = distance_ * 1e-3f;
        return n > 1e-4f ? n : 1e-4f;
    }

    // Only ever CLOSER than the default: an item may report no
    // bounds, and geometry it did not account for must not be sliced.
    float znear(const Aabb &scene) const {
        const float base = znear();
        if (!scene.valid)
            return base;
        // Half the distance to the nearest thing: a plane exactly ON
        // the geometry clips half of it away.
        const float want = aabb_distance(scene, position()) * 0.5f;
        if (want >= base)
            return base;
        return want > 1e-5f ? want : 1e-5f;
    }

    // Orthographic depth is LINEAR and needs a far plane.
    float zfar() const { return distance_ * 20.0f; }

    // The orthographic box is built to match it, so switching
    // projections holds the subject the same size.
    float view_height() const {
        return 2.0f * distance_ * std::tan(0.5f * fovy_);
    }

    Mat4 proj(float aspect) const { return proj(aspect, znear()); }

    Mat4 proj(float aspect, float near_plane) const {
        return mode_ == Projection::Orthographic
                   ? proj_ortho_reverse_z(view_height(), aspect, near_plane,
                                          zfar())
                   : proj_reverse_z(fovy_, aspect, near_plane);
    }

    // Perspective divides this by the depth; orthographic does not.
    float focal_px(std::uint32_t th) const {
        if (mode_ == Projection::Orthographic) {
            const float h = view_height();
            return h > 0.0f ? float(th) / h : 0.0f;
        }
        return float(th) / (2.0f * std::tan(0.5f * fovy_));
    }

    // The range a dolly is held inside: it can neither pass through
    // the focus nor leave the depth convention behind, and both ends
    // are part of what the camera promises.
    static constexpr float kMinDistance = 0.05f;
    static constexpr float kMaxDistance = 1000.0f;

  private:
    static constexpr float kOrbit = 0.005f;
    static constexpr float kPan = 0.0015f;
    static constexpr float kDolly = 0.1f;

    Vec3 focus_{};
    float distance_ = 5.0f;
    Quat pose_{};
    float fovy_ = 0.7853981634f;
    Projection mode_ = Projection::Perspective;
};

// Taken through the REAL projection, so it is the number the depth
// buffer will hold under either one.
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

// The pass is the outer loop and never enters the key. Opaque leads
// with the pipeline, transparent with depth.

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

} // namespace impl
} // namespace sv
