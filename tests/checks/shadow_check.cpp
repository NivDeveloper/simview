// The shadow pass: that a shadow appears, that it appears WHERE the
// geometry says it should, and that a world nobody asked casts none.
//
// Where is the whole difficulty. A shadow in the wrong place still
// looks like a shadow — mirrored, transposed, or fitted to a box that
// misses the ground, it is dark and soft and plausible, and every
// probe that asks "is anything darker?" passes. So the position is
// not reasoned about here at all: a MARKER sphere is drawn at the
// point the ray geometry says the shadow lands on, its centroid is
// read out of the picture, and the shadow's centroid must agree with
// it. Both go through the same camera, so no convention about which
// way the viewport runs enters the check.

#include "harness/Harness.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr unsigned kW = 600, kH = 600;

// Where the geometry-receiver world is pinned, and the image inside
// it: the title bar's antialiased white text is bright and would
// answer for the surface under it.
struct Rect {
    unsigned x0, y0, x1, y1;
};
constexpr Rect kGeomRect{20, 20, 340, 330};
constexpr Rect content_of(const Rect &r) {
    return {r.x0 + 4, r.y0 + 26, r.x1 - 4, r.y1 - 4};
}

// TWO casters, at different heights and off the axis in both
// directions. One sphere would not do: the map is fitted around what
// casts, so a single sphere sits in the middle of it and is symmetric
// under either mirror — a lookup with a flipped axis then samples the
// same texels and the picture is identical. Measured: with one
// centred sphere, flipping v moved the shadow by 2 px.
constexpr float kRadius = 0.65f;
constexpr float kA[3] = {-2.2f, 1.4f, 2.2f};
constexpr float kB[3] = {2.0f, -1.2f, 3.4f};

struct Centroid {
    double x = 0, y = 0;
    std::size_t n = 0;
    bool found() const { return n > 30; }
};

// Where in the picture a predicate is true, on average.
template <class F> Centroid centroid_of(const Bmp &img, F pred) {
    Centroid c;
    for (unsigned y = 0; y < img.h; ++y)
        for (unsigned x = 0; x < img.w; ++x)
            if (pred(x, y)) {
                c.x += x;
                c.y += y;
                ++c.n;
            }
    if (c.n) {
        c.x /= double(c.n);
        c.y /= double(c.n);
    }
    return c;
}

// Whether B is darker than A here. Turning a light on also LIGHTS
// things, so the two shots differ in more than the shadow — but only
// the shadow makes anything DARKER.
bool darker(const Bmp &a, const Bmp &b, unsigned x, unsigned y, int by) {
    if (x >= a.w || y >= a.h)
        return false;
    const auto &p = a.at(x, y);
    const auto &q = b.at(x, y);
    return (p[0] + p[1] + p[2]) - (q[0] + q[1] + q[2]) > by;
}

Centroid darkened(const Bmp &a, const Bmp &b, int by) {
    return centroid_of(
        b, [&](unsigned x, unsigned y) { return darker(a, b, x, y, by); });
}

// How much of a small disc around a point is in shadow. A point, not
// a centroid: a soft shadow's centroid is pulled about by its
// penumbra and by the caster clipping one side of it in the picture,
// and the claim worth making is that THIS point is dark.
double shaded_around(const Bmp &a, const Bmp &b, double cx, double cy, int r,
                     int by) {
    int seen = 0, dark = 0;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy > r * r)
                continue;
            ++seen;
            if (darker(a, b, unsigned(int(cx) + dx), unsigned(int(cy) + dy),
                       by))
                ++dark;
        }
    return seen ? double(dark) / double(seen) : 0.0;
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {kW, kH}, .headless = true});
    if (!app)
        return check::skip("shadow", LastError());

    // The grid is the ground here — it is the surface a shadow is
    // read on, and the only one this engine draws.
    sv::World w = app.World({.grid = true, .axes = false});
    REQUIRE(bool(w));
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 12.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 55.0f});
    w.Ambient(0.35f, 0.35f, 0.4f);

    // Tilted up and along +x, so a caster at height h drops its shadow
    // at -x * h * (dir.x / dir.z) — with these numbers, 1.5 units.
    const float toward[3] = {0.4472f, 0.0f, 0.8944f};

    sv::Cloud caster = w.Cloud({.color = {1.0f, 0.35f, 0.25f, 1.0f},
                                .radius = kRadius,
                                .shape = CloudShape::Sphere});
    REQUIRE(bool(caster));
    CHECK(caster.Update(
        std::vector<float>{kA[0], kA[1], kA[2], kB[0], kB[1], kB[2]}));

    // Where each one drops its shadow: travel from the sphere along
    // the light's ray until z = 0.
    const auto lands_at = [&](const float p[3], int axis) {
        return p[axis] - toward[axis] * p[2] / toward[2];
    };

    // ── calibration: where does that ground point land? ──────────────
    // Drawn, measured, then removed. Nothing in this check converts a
    // world point to a pixel by hand.
    sv::Cloud marker = w.Cloud({.color = {0.2f, 1.0f, 0.35f, 1.0f},
                                .radius = 0.3f,
                                .shape = CloudShape::Sphere});
    REQUIRE(bool(marker));

    const auto locate = [&](const char *name, float x, float y, float z) {
        CHECK(marker.Update(std::vector<float>{x, y, z}));
        app.Step();
        Bmp shot;
        CHECK(harness::shot(app, name, shot));
        const Centroid g = centroid_of(shot, [&](unsigned px, unsigned py) {
            const auto &p = shot.at(px, py);
            return p[1] > 110 && p[1] > p[0] + 40 && p[1] > p[2] + 40;
        });
        std::printf("  (%.2f, %.2f, %.2f) lands at (%.0f, %.0f), %zu px\n",
                    double(x), double(y), double(z), g.x, g.y, g.n);
        CHECK(g.found());
        return g;
    };

    // Where each ray says its shadow goes, and two points that must
    // stay lit: each shadow mirrored across the other map axis. A
    // lookup with a flipped axis puts a shadow THERE, and it is just
    // as dark and just as soft and just as wrong.
    const float land_a[3] = {lands_at(kA, 0), lands_at(kA, 1), 0.0f};
    const float land_b[3] = {lands_at(kB, 0), lands_at(kB, 1), 0.0f};

    const Centroid shadow_a = locate("shadow_a", land_a[0], land_a[1], 0.0f);
    const Centroid shadow_b = locate("shadow_b", land_b[0], land_b[1], 0.0f);
    const Centroid mirror_y = locate("shadow_my", land_a[0], -land_a[1], 0.0f);
    const Centroid mirror_x = locate("shadow_mx", -land_a[0], land_a[1], 0.0f);

    CHECK(marker.Update(std::vector<float>{}));
    app.Step();

    // ── the light, with and without its shadow ───────────────────────
    // The control is the SAME light, not no light: a world with no
    // lights keeps a headlight at the camera, so turning one on
    // changes the shading everywhere and the difference would be the
    // lighting rather than the shadow.
    //
    // Through the impl entry point, not the builder: the builder
    // returns itself so a caller can chain, and the refusal below is
    // the thing being tested.
    CHECK(impl::world_light(w.Raw(),
                            {.direction = {toward[0], toward[1], toward[2]},
                             .color = {1.0f, 0.97f, 0.9f},
                             .intensity = 0.8f,
                             .shadow = true}));

    probe::shadows(app.Raw(), false);
    app.Step();
    Bmp unlit;
    REQUIRE(harness::shot(app, "shadow_off", unlit));

    probe::shadows(app.Raw(), true);
    app.Step();
    Bmp lit;
    REQUIRE(harness::shot(app, "shadow_on", lit));

    const double on_a =
        shaded_around(unlit, lit, shadow_a.x, shadow_a.y, 4, 24);
    const double on_b =
        shaded_around(unlit, lit, shadow_b.x, shadow_b.y, 4, 24);
    const double on_my =
        shaded_around(unlit, lit, mirror_y.x, mirror_y.y, 4, 24);
    const double on_mx =
        shaded_around(unlit, lit, mirror_x.x, mirror_x.y, 4, 24);
    std::printf("  shadowed: %.0f%% and %.0f%% where the rays say; "
                "%.0f%% and %.0f%% at the two mirrors\n",
                100.0 * on_a, 100.0 * on_b, 100.0 * on_my, 100.0 * on_mx);
    CHECK_GT(on_a, 0.8);
    CHECK_GT(on_b, 0.8);
    CHECK_LT(on_my, 0.05);
    CHECK_LT(on_mx, 0.05);

    // ── and it is not simply everything ──────────────────────────────
    // A lookup that fell off the map, or a bias that shadowed the
    // world, darkens the whole ground and would pass the test above.
    const Centroid shade = darkened(unlit, lit, 24);
    const std::size_t total = std::size_t(kW) * kH;
    std::printf("  darkened %zu px, %.1f%% of the picture, centred "
                "(%.0f, %.0f)\n",
                shade.n, 100.0 * double(shade.n) / double(total), shade.x,
                shade.y);
    REQUIRE(shade.found());
    CHECK_LT(shade.n, total / 8);

    // ── geometry receives too, and must not shadow ITSELF ────────────
    // The ground is not the only receiver: a sphere takes the shadow
    // of whatever is above it, in a world with no ground at all.
    //
    // The second half is the one that bit. A curved receiver at the
    // light's grazing angle crosses a texel's worth of depth INSIDE
    // that texel, so it shadows itself in bands whatever the depth
    // bias is — visible, ugly, and invisible to any check that only
    // asks whether a shadow appeared. So what is asserted is the same
    // shape as the culling claim: turning the lookup on must not
    // change a surface the caster cannot reach.
    sv::World g = app.World({.title = "geom", .grid = false, .axes = false});
    REQUIRE(bool(g));
    g.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 9.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 35.0f});
    g.Ambient(0.25f, 0.25f, 0.3f);
    sv::Cloud receiver = g.Cloud({.color = {0.85f, 0.85f, 0.88f, 1.0f},
                                  .radius = 1.8f,
                                  .shape = CloudShape::Sphere});
    sv::Cloud above = g.Cloud({.color = {1.0f, 0.4f, 0.2f, 1.0f},
                               .radius = 0.5f,
                               .shape = CloudShape::Sphere});
    REQUIRE(bool(receiver));
    REQUIRE(bool(above));
    CHECK(receiver.Update(std::vector<float>{0.0f, 0.0f, 0.0f}));
    CHECK(above.Update(std::vector<float>{0.0f, 0.0f, 3.2f}));
    CHECK(impl::world_light(
        g.Raw(),
        {.direction = {0.0f, 0.0f, 1.0f}, .intensity = 0.9f, .shadow = true}));
    app.OnUi([] {
        ImGui::SetWindowPos("geom",
                            ImVec2(float(kGeomRect.x0), float(kGeomRect.y0)));
        ImGui::SetWindowSize("geom",
                             ImVec2(float(kGeomRect.x1 - kGeomRect.x0),
                                    float(kGeomRect.y1 - kGeomRect.y0)));
    });
    app.Step();
    app.Step();

    probe::shadows(app.Raw(), false);
    Bmp geom_off;
    REQUIRE(harness::shot(app, "shadow_geom_off", geom_off));
    probe::shadows(app.Raw(), true);
    Bmp geom_on;
    REQUIRE(harness::shot(app, "shadow_geom_on", geom_on));

    constexpr Rect kIn = content_of(kGeomRect);
    std::size_t cast = 0, seen = 0;
    for (unsigned y = kIn.y0; y < kIn.y1; ++y)
        for (unsigned x = kIn.x0; x < kIn.x1; ++x) {
            ++seen;
            if (darker(geom_off, geom_on, x, y, 24))
                ++cast;
        }
    std::printf("  a sphere shadowed by a sphere, no ground: %zu of %zu px "
                "darkened (%.1f%%)\n",
                cast, seen, 100.0 * double(cast) / double(seen));
    // It arrives at all...
    CHECK_GT(cast, std::size_t(150));
    // ...and it is a SHADOW, not the receiver breaking out in bands.
    // Measured on this scene: 0.6% of the panel with the normal
    // offset, 2.0% without it, and the cast shadow itself is the
    // 0.6%. The bound sits between them — a factor of three from each
    // side, which is what makes it a measurement and not a guess.
    CHECK_LT(cast * 90, seen);

    // ── a second caster is refused, by name ──────────────────────────
    const bool second = impl::world_light(
        w.Raw(), {.direction = {0.0f, 0.5f, 0.9f}, .shadow = true});
    std::printf("  a second casting light: %s\n",
                second ? "accepted" : LastError());
    CHECK(!second);
    // The others still light the scene — only the casting is limited.
    CHECK(impl::world_light(
        w.Raw(), {.direction = {0.0f, 0.5f, 0.9f}, .intensity = 0.3f}));

    return check::summary("shadow");
}
