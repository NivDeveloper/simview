#pragma once

// Internal to src/ — the camera, and the depth convention everything
// drawn through it agrees on.
//
// Not in core/Math.h with the arithmetic, because none of this is
// arithmetic: a turntable, a reverse-Z projection and a near plane
// that follows the orbit are DECISIONS about how this engine looks at
// a scene, and a file of pure functions should not be carrying them.
// The draw-ordering keys are here for the same reason: they are made
// of the depth THIS camera writes, under this projection.
//
// A class rather than a struct of fields with free functions taking
// it. The state is one relation — a focus, a distance and a pose —
// and every question about the camera is derived from it rather than
// stored beside it, which is only enforceable if the fields are not
// there to be set independently.

#include "../core/Math.h"

#include <cmath>
#include <cstdint>

namespace sv {
namespace impl {

// Perspective or orthographic. The two differ ONLY in the projection
// matrix: the pose, the turntable and the depth convention are shared,
// so a caller switches one setting and everything else holds.
enum class Projection : int { Perspective = 0, Orthographic = 1 };

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

    // The pose that puts the camera at (azimuth, elevation) on a
    // sphere around the focus, horizon level. Derived rather than
    // tabulated: the yaw carries world +X to the camera's view-back at
    // azimuth 0, and the pitch tilts that back vector up out of the XY
    // plane by elevation.
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

    // ── where that puts it ───────────────────────────────────────────
    //
    // Never stored: two copies of one fact drift, and the pose is the
    // one that must win.

    Vec3 position() const {
        return focus_ + rotate(pose_, Vec3{0.0f, 0.0f, 1.0f}) * distance_;
    }
    Vec3 right() const { return rotate(pose_, Vec3{1.0f, 0.0f, 0.0f}); }
    Vec3 up() const { return rotate(pose_, Vec3{0.0f, 1.0f, 0.0f}); }
    Vec3 forward() const { return rotate(pose_, Vec3{0.0f, 0.0f, -1.0f}); }

    // ── what a gesture does to it ────────────────────────────────────

    // Yaw about the WORLD up axis, pitch about the camera's OWN right:
    // the first keeps the horizon level however far the view has
    // tilted, and the second is what makes dragging up feel like
    // lifting the camera.
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
        return mat_mul(mat_from_quat(conjugate(pose_)),
                       mat_translate(position() * -1.0f));
    }

    // The near plane follows the orbit scale. Depth precision is
    // governed by the NEAR distance, not the far one, so a fixed 0.01
    // would throw away resolution at every scale but one; there is no
    // far plane at all.
    float znear() const {
        const float n = distance_ * 1e-3f;
        return n > 1e-4f ? n : 1e-4f;
    }

    // Told what is actually out there, it only ever moves CLOSER than
    // that default, never further: items are free to report no bounds
    // at all, and geometry an item did not account for must not be
    // sliced away by a plane derived from geometry it did.
    //
    // The default alone is a real failure at range — orbiting a
    // thousand units out puts the near plane a whole unit from the
    // eye, and a subject panned close to the camera is simply gone.
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

    // Orthographic depth is LINEAR, so it needs a far plane where the
    // perspective one needs none, and the range is what its precision
    // is spent on. Twenty orbits past the camera covers a scene
    // arranged around the focus without spending the buffer on empty
    // distance.
    float zfar() const { return distance_ * 20.0f; }

    // The world height the view covers at the focus. In perspective
    // this is what the frustum subtends there; the orthographic box is
    // built to match it, so switching projections holds the subject the
    // same size and only changes the convergence.
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

    // How many pixels one world unit covers at one unit of view depth.
    // Perspective divides this by the depth; orthographic does not,
    // which is the whole difference between the two and the reason this
    // is a number and a flag rather than two functions.
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

} // namespace impl
} // namespace sv
