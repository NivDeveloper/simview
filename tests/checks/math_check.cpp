// The engine's arithmetic, with no device in the room.
//
// Every claim the 3D path makes about where something lands is made
// here first: the projection, the pose, the turntable's invariants and
// the ordering keys. Then the reductions' half — binning a set of
// values into cells, and tracing a contour through a field. A shot
// check can only say the picture changed; it takes numbers to say the
// picture is RIGHT, and these numbers are checkable on a machine with
// no GPU at all.

#include "harness/Check.h"

#include "core/Math.h"
#include "world/Camera.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

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
    c.look(az, el);
    const Vec3 p = c.position();
    CHECK(near(p.x, c.distance() * std::cos(el) * std::cos(az), 1e-4f));
    CHECK(near(p.y, c.distance() * std::cos(el) * std::sin(az), 1e-4f));
    CHECK(near(p.z, c.distance() * std::sin(el), 1e-4f));
    CHECK_GT(c.up().z, 0.0f);
    CHECK(near(c.right().z, 0.0f, 1e-5f));

    // And the view matrix agrees with it: the focus is straight ahead
    // at exactly the orbit distance. This is the assertion a transposed
    // upload fails.
    const Mat4 V = c.view();
    const Vec3 f = transform_point(V, c.focus());
    CHECK(near(f.x, 0.0f, 1e-4f));
    CHECK(near(f.y, 0.0f, 1e-4f));
    CHECK(near(f.z, -c.distance(), 1e-4f));

    // The turntable's three invariants: an orbit changes where the
    // camera looks from but not how far, a pan moves the subject but
    // not the orientation, a dolly does the reverse.
    Camera3 o = c;
    o.orbit(37.0f, -19.0f);
    CHECK(near(length(o.position() - o.focus()), c.distance(), 1e-3f));
    CHECK(near(std::sqrt(o.pose().w * o.pose().w + o.pose().x * o.pose().x +
                         o.pose().y * o.pose().y + o.pose().z * o.pose().z),
               1.0f, 1e-5f));

    Camera3 pn = c;
    pn.pan(12.0f, -8.0f);
    CHECK(near(pn.pose().w, c.pose().w, 1e-6f));
    CHECK(near(pn.pose().z, c.pose().z, 1e-6f));
    CHECK_GT(length(pn.focus() - c.focus()), 0.0f);

    Camera3 dz = c;
    dz.dolly(3.0f);
    CHECK_LT(dz.distance(), c.distance());
    CHECK(near(dz.focus().x, c.focus().x, 1e-6f));
    dz.dolly(-1e6f);
    CHECK(near(dz.distance(), Camera3::kMaxDistance, 1e-3f));
    dz.dolly(1e6f);
    CHECK(near(dz.distance(), Camera3::kMinDistance, 1e-6f));

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
    oc.set_mode(Projection::Orthographic);
    CHECK(near(oc.view_height(),
               2.0f * oc.distance() * std::tan(0.5f * oc.fovy()), 1e-5f));
    const Vec3 pp =
        transform_point(mat_mul(c.proj(1.0f), c.view()),
                        c.focus() + c.up() * (0.5f * c.view_height()));
    const Vec3 po =
        transform_point(mat_mul(oc.proj(1.0f), oc.view()),
                        oc.focus() + oc.up() * (0.5f * oc.view_height()));
    CHECK(near(pp.y, po.y, 1e-3f));

    // The keys the sort reads. Opaque must ascend as things get
    // nearer, transparent must ascend as they get FARTHER — the two
    // orders a single comparator produces from one std::sort. Taken
    // through the real projection, so BOTH must order alike.
    const Vec3 fwd = c.forward();
    for (Projection proj :
         {Projection::Perspective, Projection::Orthographic}) {
        Camera3 k = c;
        k.set_mode(proj);
        const Mat4 M = mat_mul(k.proj(1.0f), k.view());
        CHECK_GT(depth_key(M, k.focus() + fwd * -2.0f),
                 depth_key(M, k.focus()));
        CHECK_GT(depth_key(M, k.focus()), depth_key(M, k.focus() + fwd * 2.0f));
    }
    const Mat4 VP = mat_mul(c.proj(1.0f), V);
    const std::uint16_t d_near = depth_key(VP, c.focus() + fwd * -2.0f);
    const std::uint16_t d_mid = depth_key(VP, c.focus());
    const std::uint16_t d_far = depth_key(VP, c.focus() + fwd * 2.0f);
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
    fc.frame({0.0f, 0.0f, 0.0f}, 10.0f);
    fc.look(0.0f, 0.0f);
    const Mat4 F = mat_mul(fc.proj(1.0f), fc.view());
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
    CHECK(frustum_intersects(fr, at(fc.position(), 2.0f)));
    // A box so large it contains the whole frustum.
    CHECK(frustum_intersects(fr, at({0.0f, 0.0f, 0.0f}, 1000.0f)));

    Camera3 orthoc = fc;
    orthoc.set_mode(Projection::Orthographic);
    const Frustum orf = frustum_of(mat_mul(orthoc.proj(1.0f), orthoc.view()));
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
    nc.frame({}, 1000.0f);
    const float base = nc.znear();
    CHECK_LT(std::fabs(nc.znear(Aabb{}) - base), 1e-9f);
    // A subject half a unit from the eye: the default would put the
    // near plane a full unit out and clip it away entirely.
    const Vec3 eye = nc.position();
    CHECK_GT(base, 0.5f);
    CHECK_LT(nc.znear(at(eye + nc.forward() * 0.5f, 0.01f)), 0.5f);
    // Nothing but distant geometry does NOT push it out.
    CHECK_LT(nc.znear(at({0.0f, 0.0f, 0.0f}, 1.0f)), base + 1e-6f);
    // And it never reaches zero, whatever the box says.
    CHECK_GT(nc.znear(at(eye, 5.0f)), 0.0f);

    // ── projected size ───────────────────────────────────────────────
    // Halving the distance doubles the pixels; the orthographic camera
    // does not care how far away anything is, which is the whole of
    // the difference between them.
    const float f1 = fc.focal_px(400);
    CHECK_GT(f1, 0.0f);
    const float ortho_px = orthoc.focal_px(400);
    CHECK_GT(ortho_px, 0.0f);
    std::printf("  focal: %.1f px per unit at unit depth, ortho %.1f flat\n",
                double(f1), double(ortho_px));

    // ── binning ──────────────────────────────────────────────────
    // A span always has WIDTH: a set of identical values would
    // otherwise put every one of them in the same cell and the picture
    // would be one spike saying nothing.
    const impl::Span flat = impl::span_of({2.5, 2.5, 2.5});
    CHECK_GT(flat.hi, flat.lo);
    const impl::Span s01 = impl::span_of({0.0, 0.25, 1.0});
    CHECK_EQ(s01.lo, 0.0);
    CHECK_EQ(s01.hi, 1.0);

    // The ends land in the end cells, and anything outside is CLAMPED
    // rather than dropped or wrapped.
    CHECK_EQ(impl::bin_of(0.0, s01, 8), 0);
    CHECK_EQ(impl::bin_of(1.0, s01, 8), 7);
    CHECK_EQ(impl::bin_of(-5.0, s01, 8), 0);
    CHECK_EQ(impl::bin_of(5.0, s01, 8), 7);

    // ── contours ─────────────────────────────────────────────────
    // A flat field has no level to cross, so it traces nothing — the
    // case a marching square gets wrong by emitting a segment on every
    // cell of an empty picture.
    std::vector<double> flat_field(64, 0.0);
    std::vector<double> cx, cy;
    impl::contours(flat_field, 8, {0.0, 1.0}, {0.0, 1.0}, 0.0, cx, cy);
    CHECK_EQ(int(cx.size()), 0);

    // One hot cell is a closed curve around it: segments come in
    // PAIRS of points, and every point is inside the field's own span.
    std::vector<double> spot(64, 0.0);
    spot[3 * 8 + 4] = 10.0;
    impl::contours(spot, 8, {0.0, 1.0}, {0.0, 1.0}, 10.0, cx, cy);
    std::printf("  contours: %d points around one hot cell\n", int(cx.size()));
    CHECK_GT(int(cx.size()), 0);
    CHECK_EQ(int(cx.size()) % 2, 0);
    CHECK_EQ(int(cx.size()), int(cy.size()));
    // And it is AROUND that cell rather than merely on the canvas.
    // The hot cell is column 4 and row 3 of eight over the unit
    // square, so its centre is (0.5625, 0.4375) and every point of the
    // curve belongs within a cell or two of it — which is the claim a
    // coordinate mapping that is off by a cell actually breaks.
    double far_x = 0.0, far_y = 0.0;
    for (std::size_t i = 0; i < cx.size(); ++i) {
        far_x = std::max(far_x, std::abs(cx[i] - 0.5625));
        far_y = std::max(far_y, std::abs(cy[i] - 0.4375));
    }
    std::printf("  contours: furthest point %.3f, %.3f from the hot cell\n",
                far_x, far_y);
    CHECK_GT(0.20, far_x);
    CHECK_GT(0.20, far_y);

    return check::summary("math");
}
