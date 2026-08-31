// Points drawn as GEOMETRY: that a sphere is round, a cube has flat
// faces, the whole crowd is one draw call, and the triangle budget
// drops when the crowd gets big.
//
// A billboard is shaded as a sphere and looks like one from the front.
// What real geometry buys is everything else — an edge where it meets
// a surface, a silhouette that is not a disc, faces that catch the
// light separately — so those are what this asks about.

#include "harness/Harness.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

namespace {

std::vector<float> one(float x, float y, float z) { return {x, y, z}; }

// The brightest and dimmest small patch inside a region — a flat-faced
// shape lights each face differently, a smooth one shades across.
void extremes(const Bmp &img, unsigned x0, unsigned y0, unsigned x1,
              unsigned y1, int &lo, int &hi) {
    lo = 1 << 20;
    hi = 0;
    for (unsigned y = y0 + 2; y + 2 < y1 && y + 2 < img.h; y += 3)
        for (unsigned x = x0 + 2; x + 2 < x1 && x + 2 < img.w; x += 3) {
            const auto &p = img.at(x, y);
            const int v = p[0] + p[1] + p[2];
            if (v < 90) // background, not the shape
                continue;
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
    if (lo > hi)
        lo = hi = 0;
}

// The share of a shape's pixels that sit at its single most common
// brightness. A flat face is a large patch of ONE value, so a cube
// puts a fifth of itself in one bin; a sphere's normal turns
// continuously and never does.
double flattest_share(const Bmp &img, unsigned x0, unsigned y0, unsigned x1,
                      unsigned y1) {
    std::map<int, std::size_t> bins;
    std::size_t total = 0;
    for (unsigned y = y0; y < y1 && y < img.h; ++y)
        for (unsigned x = x0; x < x1 && x < img.w; ++x) {
            const auto &p = img.at(x, y);
            const int v = p[0] + p[1] + p[2];
            if (v < 90)
                continue;
            ++bins[v / 6];
            ++total;
        }
    std::size_t best = 0;
    for (const auto &[k, n] : bins)
        best = n > best ? n : best;
    return total ? double(best) / double(total) : 0.0;
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {700, 500}, .headless = true});
    if (!app)
        return check::skip("mesh", LastError());

    sv::World w = app.World({.title = "mesh", .grid = false, .axes = false});
    REQUIRE(bool(w));
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 4.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
    w.Ambient(0.05f, 0.05f, 0.05f);
    CHECK(bool(w.Light({.direction = {0.0f, 1.0f, 0.3f}, .intensity = 1.0f})));
    app.OnUi([] {
        ImGui::SetWindowPos("mesh", ImVec2(0, 0));
        ImGui::SetWindowSize("mesh", ImVec2(420, 340));
    });

    // ── a sphere is lit like a sphere ────────────────────────────────
    sv::Cloud ball = w.Cloud({.color = {0.85f, 0.85f, 0.85f, 1.0f},
                              .radius = 1.0f,
                              .shape = CloudShape::Sphere});
    REQUIRE(bool(ball));
    CHECK(ball.Update(one(0.0f, 0.0f, 0.0f)));
    app.Step();
    app.Step();

    // Inside the panel, past its chrome: a title bar is a large patch
    // of ONE colour, which is exactly what the flat-face measure below
    // looks for and would find in the wrong place.
    constexpr unsigned kSx0 = 6, kSy0 = 30, kSx1 = 414, kSy1 = 336;
    Bmp img;
    REQUIRE(harness::shot(app, "mesh_sphere", img));
    int lo = 0, hi = 0;
    extremes(img, kSx0, kSy0, kSx1, kSy1, lo, hi);
    const double sphere_flat = flattest_share(img, kSx0, kSy0, kSx1, kSy1);
    std::printf("  sphere shading: %d dimmest, %d brightest, %.1f%% at one "
                "level\n",
                lo, hi, 100.0 * sphere_flat);
    // Lit from one side with almost no ambient, so it has a bright
    // side and a dark one — a flat disc of one colour would not.
    CHECK_GT(hi, lo + 120);
    // Many shades between them: a sphere's normal turns continuously,
    // so its shading is a gradient rather than a few steps.
    CHECK_GT(shades(img, kSx0, kSy0, kSx1, kSy1, 4), std::size_t(20));

    // ── one draw for the whole crowd ─────────────────────────────────
    // Instancing is the point of a mesh item: the cost of a thousand
    // is one call, not a thousand.
    sv::World many = app.World({.title = "many", .grid = false, .axes = false});
    REQUIRE(bool(many));
    many.Camera({.focus = {0.0f, 0.0f, 0.0f}, .distance = 30.0f});
    app.OnUi([] {
        ImGui::SetWindowPos("many", ImVec2(430, 0));
        ImGui::SetWindowSize("many", ImVec2(260, 340));
    });
    sv::Cloud crowd = many.Cloud({.color = {0.6f, 0.75f, 1.0f, 1.0f},
                                  .radius = 0.09f,
                                  .shape = CloudShape::Sphere});
    REQUIRE(bool(crowd));
    std::vector<float> pts;
    constexpr int kN = 8000;
    for (int i = 0; i < kN; ++i) {
        const float t = float(i) * 2.39996323f;
        const float r = 6.0f * std::sqrt(float(i) / float(kN));
        pts.push_back(r * std::cos(t));
        pts.push_back(r * std::sin(t));
        pts.push_back(2.0f * std::sin(0.01f * float(i)));
    }
    CHECK(crowd.Update(pts));

    app.Step();
    const std::uint64_t before = app.Stats().draws;
    Bmp shot;
    REQUIRE(harness::shot(app, "mesh_many", shot));
    const std::uint64_t drew = app.Stats().draws - before;
    std::printf("  %d spheres in %llu draw calls\n", kN,
                static_cast<unsigned long long>(drew));
    // Two worlds are on screen, so two draws — one each, not 8001.
    CHECK_LT(drew, std::uint64_t(4));

    // ── the triangle budget follows the crowd ────────────────────────
    // The single sphere gets the round one; eight thousand get the
    // cheap one. vklib learned this at fifty thousand instances of a
    // three-and-a-half-thousand-triangle sphere, and the frame went
    // with it.
    unsigned few[4] = {0, 0, 0, 0}, lots[4] = {0, 0, 0, 0};
    const std::size_t nf = probe::mesh_tiers(app.Raw(), "mesh", few, 4);
    const std::size_t nl = probe::mesh_tiers(app.Raw(), "many", lots, 4);
    REQUIRE(nf > 0);
    REQUIRE(nl > 0);
    std::printf("  triangles: %u for one sphere, %u for %d\n", few[0], lots[0],
                kN);
    CHECK_LT(lots[0], few[0] / 4);
    CHECK_GT(few[0], 400u);
    CHECK_LT(lots[0], 200u);

    // ── a cube is made of flat faces ─────────────────────────────────
    // Three faces are visible from a corner and each takes the light
    // differently, so a cube's shading is a few steps where a sphere's
    // is a gradient. Its own world, so nothing else answers.
    sv::World cw = app.World({.title = "cube", .grid = false, .axes = false});
    REQUIRE(bool(cw));
    cw.Camera({.focus = {0.0f, 0.0f, 0.0f},
               .distance = 4.0f,
               .azimuth_deg = -35.0f,
               .elevation_deg = 25.0f});
    cw.Ambient(0.05f, 0.05f, 0.05f);
    CHECK(bool(cw.Light({.direction = {0.4f, 0.7f, 0.6f}, .intensity = 1.0f})));
    app.OnUi([] {
        ImGui::SetWindowPos("cube", ImVec2(0, 350));
        ImGui::SetWindowSize("cube", ImVec2(420, 140));
    });
    sv::Cloud box = cw.Cloud({.color = {0.85f, 0.85f, 0.85f, 1.0f},
                              .radius = 0.9f,
                              .shape = CloudShape::Cube});
    REQUIRE(bool(box));
    CHECK(box.Update(one(0.0f, 0.0f, 0.0f)));
    app.Step();
    app.Step();

    Bmp cube;
    REQUIRE(harness::shot(app, "mesh_cube", cube));
    const double cube_flat = flattest_share(cube, 6, 380, 414, 486);
    std::printf("  cube shading: %.1f%% at one level (sphere %.1f%%)\n",
                100.0 * cube_flat, 100.0 * sphere_flat);
    // A face is a large patch of ONE brightness, so a cube puts much
    // of itself in a single level; a sphere's normal turns
    // continuously and cannot. That is the difference between flat
    // faces and a smooth surface, in one number.
    CHECK_GT(cube_flat, 0.15);
    CHECK_LT(sphere_flat, 0.10);
    CHECK_GT(cube_flat, sphere_flat * 3.0);

    // ── the silhouette is antialiased ────────────────────────────────
    // A shape with ONE brightness, so any pixel between it and the
    // background is a pixel the edge only partly covers. Without
    // multisampling an edge is a staircase and there are none of
    // those; with it there is a rim of them all the way round.
    sv::World aw = app.World({.title = "edge", .grid = false, .axes = false});
    REQUIRE(bool(aw));
    aw.Camera({.focus = {0.0f, 0.0f, 0.0f},
               .distance = 4.0f,
               .azimuth_deg = -20.0f,
               .elevation_deg = 12.0f});
    aw.Ambient(1.0f, 1.0f, 1.0f); // no lights: every face alike
    app.OnUi([] {
        ImGui::SetWindowPos("edge", ImVec2(430, 350));
        ImGui::SetWindowSize("edge", ImVec2(260, 140));
    });
    sv::Cloud flat = aw.Cloud({.color = {0.75f, 0.75f, 0.75f, 1.0f},
                               .radius = 0.8f,
                               .shape = CloudShape::Cube});
    REQUIRE(bool(flat));
    CHECK(flat.Update(one(0.0f, 0.0f, 0.0f)));
    app.Step();
    app.Step();

    Bmp edge;
    REQUIRE(harness::shot(app, "mesh_edge", edge));
    std::size_t solid = 0, partial = 0;
    for (unsigned y = 380; y < 486 && y < edge.h; ++y)
        for (unsigned x = 436; x < 684 && x < edge.w; ++x) {
            const auto &q = edge.at(x, y);
            const int v = q[0] + q[1] + q[2];
            if (v > 500)
                ++solid;
            else if (v > 100)
                ++partial;
        }
    std::printf("  cube edge: %zu solid px, %zu partly covered\n", solid,
                partial);
    CHECK_GT(solid, std::size_t(500));
    CHECK_GT(partial, std::size_t(40));

    // ── two shapes in ONE world ──────────────────────────────────────
    // The registry hands an item the address of the shape it resolved
    // and the item keeps it. A second shape appearing in the same
    // world used to move the first one, and the next frame drew from
    // freed memory — every world in this check until now held exactly
    // one shape, so none of them could have found it. An example did.
    sv::World both = app.World({.title = "both", .grid = false, .axes = false});
    REQUIRE(bool(both));
    both.Camera({.focus = {0.0f, 0.0f, 0.0f},
                 .distance = 6.0f,
                 .azimuth_deg = -30.0f,
                 .elevation_deg = 15.0f});
    app.OnUi([] {
        ImGui::SetWindowPos("both", ImVec2(430, 350));
        ImGui::SetWindowSize("both", ImVec2(260, 140));
    });
    sv::Cloud b_sphere = both.Cloud({.color = {0.95f, 0.4f, 0.35f, 1.0f},
                                     .radius = 0.55f,
                                     .shape = CloudShape::Sphere});
    sv::Cloud b_cube = both.Cloud({.color = {0.4f, 0.9f, 0.5f, 1.0f},
                                   .radius = 0.5f,
                                   .shape = CloudShape::Cube});
    REQUIRE(bool(b_sphere));
    REQUIRE(bool(b_cube));
    CHECK(b_sphere.Update(one(-1.1f, 0.0f, 0.0f)));
    CHECK(b_cube.Update(one(1.1f, 0.0f, 0.0f)));

    // Several frames: the crash was on the frame AFTER the second
    // shape arrived, not the one that added it.
    for (int i = 0; i < 4; ++i)
        app.Step();
    Bmp pair;
    REQUIRE(harness::shot(app, "mesh_pair", pair));
    unsigned tri2[4] = {0, 0, 0, 0};
    const std::size_t n2 = probe::mesh_tiers(app.Raw(), "both", tri2, 4);
    std::printf("  two shapes in one world: %zu meshes resident\n", n2);
    CHECK_EQ(n2, std::size_t(2));
    // And both are still drawn — a dangling shape draws nothing or
    // draws the other one.
    std::size_t red = 0, green = 0;
    for (unsigned y = 380; y < 486 && y < pair.h; ++y)
        for (unsigned x = 436; x < 684 && x < pair.w; ++x) {
            const auto &q = pair.at(x, y);
            if (q[0] > q[1] + 40 && q[0] > 90)
                ++red;
            if (q[1] > q[0] + 40 && q[1] > 90)
                ++green;
        }
    std::printf("  sphere %zu px, cube %zu px\n", red, green);
    CHECK_GT(red, std::size_t(200));
    CHECK_GT(green, std::size_t(200));

    if (probe::validation_on(app.Raw()))
        CHECK_EQ(probe::validation_errors(app.Raw()), std::size_t(0));

    return check::summary("mesh");
}
