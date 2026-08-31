// The world's arithmetic, with no device in the room.
//
// Every claim the 3D path makes about where something lands is made
// here first: the projection, the pose, the turntable's invariants and
// the ordering keys. A shot check can only say the picture changed; it
// takes numbers to say the picture is RIGHT, and these numbers are
// checkable on a machine with no GPU at all.

#include "harness/Check.h"

#include "world/Math.h"

#include <cmath>
#include <cstdio>

using namespace sv;
using namespace sv::impl;

namespace {

constexpr float kPi = 3.14159265358979f;

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // The projection, spelled as reverse-Z: the near plane is ONE and
    // the far distance approaches zero. Every other half of the
    // decision (the clear, the compare) is written against these two.
    const float n = 0.005f;
    const Mat4 P = proj_reverse_z(kPi * 0.5f, 1.0f, n);
    CHECK(near(transform_point(P, {0.0f, 0.0f, -n}).z, 1.0f, 1e-6f));
    CHECK_LT(transform_point(P, {0.0f, 0.0f, -1e9f * n}).z, 1e-4f);

    // A point at twice the near distance sits at half the depth, dead
    // centre — the one place where the whole matrix is legible.
    const Vec3 ndc = transform_point(P, {0.0f, 0.0f, -2.0f * n});
    CHECK(near(ndc.x, 0.0f, 1e-6f));
    CHECK(near(ndc.y, 0.0f, 1e-6f));
    CHECK(near(ndc.z, 0.5f, 1e-6f));

    // Monotone, and strictly: two distances that share a depth are two
    // draws the sort cannot separate.
    float last = 2.0f;
    for (int i = 1; i <= 10; ++i) {
        const float d = transform_point(P, {0.0f, 0.0f, -n * float(i * i)}).z;
        CHECK_LT(d, last);
        last = d;
    }

    // The pose: the camera sits where the spherical angles say, and
    // the horizon stays level. Z-up, so elevation lifts z.
    Camera3 c;
    const float az = -0.25f * kPi, el = kPi / 6.0f;
    c.q = camera_pose(az, el);
    const Vec3 p = camera_position(c);
    CHECK(near(p.x, c.distance * std::cos(el) * std::cos(az), 1e-4f));
    CHECK(near(p.y, c.distance * std::cos(el) * std::sin(az), 1e-4f));
    CHECK(near(p.z, c.distance * std::sin(el), 1e-4f));
    CHECK_GT(camera_up(c).z, 0.0f);
    CHECK(near(camera_right(c).z, 0.0f, 1e-5f));

    // And the view matrix agrees with it: the focus is straight ahead
    // at exactly the orbit distance. This is the assertion a transposed
    // upload fails.
    const Mat4 V = camera_view(c);
    const Vec3 f = transform_point(V, c.focus);
    CHECK(near(f.x, 0.0f, 1e-4f));
    CHECK(near(f.y, 0.0f, 1e-4f));
    CHECK(near(f.z, -c.distance, 1e-4f));

    // The turntable's three invariants: an orbit changes where the
    // camera looks from but not how far, a pan moves the subject but
    // not the orientation, a dolly does the reverse.
    Camera3 o = c;
    camera_orbit(o, 37.0f, -19.0f);
    CHECK(near(length(camera_position(o) - o.focus), c.distance, 1e-3f));
    CHECK(near(std::sqrt(o.q.w * o.q.w + o.q.x * o.q.x + o.q.y * o.q.y +
                         o.q.z * o.q.z),
               1.0f, 1e-5f));

    Camera3 pn = c;
    camera_pan(pn, 12.0f, -8.0f);
    CHECK(near(pn.q.w, c.q.w, 1e-6f));
    CHECK(near(pn.q.z, c.q.z, 1e-6f));
    CHECK_GT(length(pn.focus - c.focus), 0.0f);

    Camera3 dz = c;
    camera_dolly(dz, 3.0f);
    CHECK_LT(dz.distance, c.distance);
    CHECK(near(dz.focus.x, c.focus.x, 1e-6f));
    camera_dolly(dz, -1e6f);
    CHECK(near(dz.distance, dz.max_distance, 1e-3f));
    camera_dolly(dz, 1e6f);
    CHECK(near(dz.distance, dz.min_distance, 1e-6f));

    // The inverse is real: a cursor ray will invert exactly this
    // product, and a wrong one there looks plausible on screen.
    const Mat4 M = mat_mul(P, V), I = mat_mul(M, mat_inverse(M));
    for (int i = 0; i < 16; ++i)
        CHECK(near(I.m[i], (i % 5 == 0) ? 1.0f : 0.0f, 1e-3f));

    // The keys the sort reads. Opaque must ascend as things get
    // nearer, transparent must ascend as they get FARTHER — the two
    // orders a single comparator produces from one std::sort.
    const float zn = camera_znear(c);
    const Vec3 fwd = camera_forward(c);
    const std::uint16_t d_near = depth_key(V, c.focus + fwd * -2.0f, zn);
    const std::uint16_t d_mid = depth_key(V, c.focus, zn);
    const std::uint16_t d_far = depth_key(V, c.focus + fwd * 2.0f, zn);
    CHECK_GT(d_near, d_mid);
    CHECK_GT(d_mid, d_far);
    CHECK_LT(opaque_key(0, 0, d_near), opaque_key(0, 0, d_mid));
    CHECK_LT(opaque_key(0, 0, d_mid), opaque_key(0, 0, d_far));
    CHECK_LT(transparent_key(0, d_far), transparent_key(0, d_mid));
    CHECK_LT(transparent_key(0, d_mid), transparent_key(0, d_near));
    // State leads in the opaque key: a nearer draw with a later
    // pipeline still sorts behind an earlier pipeline's far one.
    CHECK_LT(opaque_key(0, 0, d_far), opaque_key(1, 0, d_near));

    return check::summary("world math");
}
