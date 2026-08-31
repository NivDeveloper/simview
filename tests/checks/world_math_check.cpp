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

    // Orthographic, the other projection: linear in view depth, so it
    // needs a real far plane where the perspective one needs none.
    const float of = 100.0f, oh = 4.0f, oa = 1.5f;
    const Mat4 O = proj_ortho_reverse_z(oh, oa, n, of);
    CHECK(near(transform_point(O, {0.0f, 0.0f, -n}).z, 1.0f, 1e-6f));
    CHECK(near(transform_point(O, {0.0f, 0.0f, -of}).z, 0.0f, 1e-6f));
    CHECK(near(transform_point(O, {0.0f, 0.0f, -0.5f * (n + of)}).z, 0.5f,
               1e-5f));
    // The box's own corner is the edge of the frame.
    const Vec3 corner = transform_point(O, {oh * oa * 0.5f, oh * 0.5f, -1.0f});
    CHECK(near(corner.x, 1.0f, 1e-5f));
    CHECK(near(corner.y, 1.0f, 1e-5f));

    // What makes it orthographic: distance does not shrink anything.
    // The same offset lands at the same place at any depth, which is
    // exactly what the perspective matrix must NOT do.
    CHECK(near(transform_point(O, {0.5f, 0.0f, -1.0f}).x,
               transform_point(O, {0.5f, 0.0f, -50.0f}).x, 1e-6f));
    CHECK_GT(transform_point(P, {0.5f, 0.0f, -1.0f}).x,
             transform_point(P, {0.5f, 0.0f, -50.0f}).x * 2.0f);

    // The two projections frame the SUBJECT alike: the orthographic
    // box is built from the height the frustum subtends at the focus,
    // so switching holds the subject still and changes only the
    // convergence.
    Camera3 oc = c;
    oc.projection = Projection::Orthographic;
    CHECK(near(camera_view_height(oc),
               2.0f * oc.distance * std::tan(0.5f * oc.fovy), 1e-5f));
    const Vec3 pp = transform_point(
        mat_mul(camera_proj(c, 1.0f), camera_view(c)),
        c.focus + camera_up(c) * (0.5f * camera_view_height(c)));
    const Vec3 po = transform_point(
        mat_mul(camera_proj(oc, 1.0f), camera_view(oc)),
        oc.focus + camera_up(oc) * (0.5f * camera_view_height(oc)));
    CHECK(near(pp.y, po.y, 1e-3f));

    // The keys the sort reads. Opaque must ascend as things get
    // nearer, transparent must ascend as they get FARTHER — the two
    // orders a single comparator produces from one std::sort. Taken
    // through the real projection, so BOTH must order alike.
    const Vec3 fwd = camera_forward(c);
    for (Projection proj :
         {Projection::Perspective, Projection::Orthographic}) {
        Camera3 k = c;
        k.projection = proj;
        const Mat4 M = mat_mul(camera_proj(k, 1.0f), camera_view(k));
        CHECK_GT(depth_key(M, k.focus + fwd * -2.0f), depth_key(M, k.focus));
        CHECK_GT(depth_key(M, k.focus), depth_key(M, k.focus + fwd * 2.0f));
    }
    const Mat4 VP = mat_mul(camera_proj(c, 1.0f), V);
    const std::uint16_t d_near = depth_key(VP, c.focus + fwd * -2.0f);
    const std::uint16_t d_mid = depth_key(VP, c.focus);
    const std::uint16_t d_far = depth_key(VP, c.focus + fwd * 2.0f);
    CHECK_GT(d_near, d_mid);
    CHECK_GT(d_mid, d_far);
    CHECK_LT(opaque_key(0, 0, d_near), opaque_key(0, 0, d_mid));
    CHECK_LT(opaque_key(0, 0, d_mid), opaque_key(0, 0, d_far));
    CHECK_LT(transparent_key(0, d_far), transparent_key(0, d_mid));
    CHECK_LT(transparent_key(0, d_mid), transparent_key(0, d_near));
    // State leads in the opaque key: a nearer draw with a later
    // pipeline still sorts behind an earlier pipeline's far one.
    CHECK_LT(opaque_key(0, 0, d_far), opaque_key(1, 0, d_near));

    // ── boxes ────────────────────────────────────────────────────────
    // "I do not know" is a third answer, distinct from an empty box at
    // the origin: an item that reports nothing must not be culled
    // against a box somebody invented for it.
    Aabb none{};
    CHECK(!none.valid);
    CHECK(frustum_intersects(Frustum{}, none));

    Aabb b{};
    aabb_add(b, {-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f});
    CHECK(b.valid);
    aabb_add(b, {0.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 5.0f});
    CHECK_LT(std::fabs(b.hi.z - 5.0f), 1e-6f);
    CHECK_LT(std::fabs(b.lo.z + 1.0f), 1e-6f);

    // Zero inside, and the true distance outside — not the distance to
    // the centre, which is what a frustum test would over-cull by.
    CHECK_LT(aabb_distance(b, {0.0f, 0.0f, 0.0f}), 1e-6f);
    CHECK_LT(std::fabs(aabb_distance(b, {4.0f, 0.0f, 0.0f}) - 3.0f), 1e-5f);
    CHECK_LT(std::fabs(aabb_distance(b, {-4.0f, -5.0f, 0.0f}) -
                       std::sqrt(9.0f + 16.0f)),
             1e-5f);

    // ── the frustum ──────────────────────────────────────────────────
    Camera3 fc{};
    fc.focus = {0.0f, 0.0f, 0.0f};
    fc.distance = 10.0f;
    fc.q = camera_pose(0.0f, 0.0f);
    const Mat4 F = mat_mul(camera_proj(fc, 1.0f), camera_view(fc));
    const Frustum fr = frustum_of(F);
    // Five, not six: the perspective projection runs to infinity, so
    // its far plane comes out with a zero normal and is dropped rather
    // than tested against at every box.
    std::printf("  perspective frustum: %d planes\n", fr.count);
    CHECK_EQ(fr.count, 5);

    const auto at = [](Vec3 p, float r) {
        return Aabb{
            {p.x - r, p.y - r, p.z - r}, {p.x + r, p.y + r, p.z + r}, true};
    };
    CHECK(frustum_intersects(fr, at({0.0f, 0.0f, 0.0f}, 0.5f)));
    // Far off to one side, and behind the eye: the two ways out.
    CHECK(!frustum_intersects(fr, at({0.0f, 40.0f, 0.0f}, 0.5f)));
    CHECK(!frustum_intersects(fr, at({40.0f, 0.0f, 0.0f}, 0.5f)));
    // Straddling the eye — the case a corner test gets wrong, and the
    // one the user is standing in.
    CHECK(frustum_intersects(fr, at(camera_position(fc), 2.0f)));
    // A box so large it contains the whole frustum.
    CHECK(frustum_intersects(fr, at({0.0f, 0.0f, 0.0f}, 1000.0f)));

    Camera3 orthoc = fc;
    orthoc.projection = Projection::Orthographic;
    const Frustum orf =
        frustum_of(mat_mul(camera_proj(orthoc, 1.0f), camera_view(orthoc)));
    // Six here: an orthographic depth is linear, so it HAS a far plane
    // and the same extraction finds it.
    std::printf("  orthographic frustum: %d planes\n", orf.count);
    CHECK_EQ(orf.count, 6);
    CHECK(frustum_intersects(orf, at({0.0f, 0.0f, 0.0f}, 0.5f)));
    CHECK(!frustum_intersects(orf, at({0.0f, 40.0f, 0.0f}, 0.5f)));

    // ── the near plane ───────────────────────────────────────────────
    // It only ever moves CLOSER than the orbit-scale default. Items are
    // free to report no bounds, and geometry nobody accounted for must
    // not be sliced away by a plane derived from geometry somebody did.
    Camera3 nc{};
    nc.distance = 1000.0f;
    const float base = camera_znear(nc);
    CHECK_LT(std::fabs(camera_znear(nc, Aabb{}) - base), 1e-9f);
    // A subject half a unit from the eye: the default would put the
    // near plane a full unit out and clip it away entirely.
    const Vec3 eye = camera_position(nc);
    CHECK_GT(base, 0.5f);
    CHECK_LT(camera_znear(nc, at(eye + camera_forward(nc) * 0.5f, 0.01f)),
             0.5f);
    // Nothing but distant geometry does NOT push it out.
    CHECK_LT(camera_znear(nc, at({0.0f, 0.0f, 0.0f}, 1.0f)), base + 1e-6f);
    // And it never reaches zero, whatever the box says.
    CHECK_GT(camera_znear(nc, at(eye, 5.0f)), 0.0f);

    // ── projected size ───────────────────────────────────────────────
    // Halving the distance doubles the pixels; the orthographic camera
    // does not care how far away anything is, which is the whole of
    // the difference between them.
    const float f1 = focal_px(fc, 400);
    CHECK_GT(f1, 0.0f);
    const float ortho_px = focal_px(orthoc, 400);
    CHECK_GT(ortho_px, 0.0f);
    std::printf("  focal: %.1f px per unit at unit depth, ortho %.1f flat\n",
                double(f1), double(ortho_px));

    return check::summary("world math");
}
