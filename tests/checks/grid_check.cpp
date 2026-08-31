// The ground grid: that it covers the whole plane, in both
// directions, at any zoom, and that it survives being flown around.
//
// It gets a check of its own because the ways a grid goes wrong are
// not the ways anything else does. It had a whole quadrant missing and
// two more with half their lines, and every colour probe in the suite
// passed the entire time: there WERE grey pixels, the content box WAS
// full, the picture DID change when the camera moved. What none of
// them asked was whether the picture had the same structure
// everywhere, which is the only question a grid really answers.
//
// The camera is overhead for the structural part, so the four
// quadrants of the plane are the four quadrants of the image and the
// origin is the middle pixel.

#include "harness/Harness.h"
#include "harness/Input.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <cmath>
#include <cstdio>

namespace {

// The image is the whole shot: this app has no panels, so the world
// fills the window and the grid fills the world.
constexpr unsigned kW = 600, kH = 600;

void report(const char *what, const Quadrants &q) {
    std::printf("  %-22s tl %5zu  tr %5zu  bl %5zu  br %5zu\n", what, q.tl,
                q.tr, q.bl, q.br);
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {kW, kH}, .headless = true});
    if (!app)
        return check::skip("grid", LastError());

    sv::World w = app.World({.axes = false});
    REQUIRE(bool(w));
    const auto look = [&](float distance, float azimuth, float elevation) {
        w.Camera({.focus = {0.0f, 0.0f, 0.0f},
                  .distance = distance,
                  .azimuth_deg = azimuth,
                  .elevation_deg = elevation});
    };

    // ── every quadrant of the plane is drawn ─────────────────────────
    // Straight down on the origin. The remainder that places a line
    // used to carry the sign of the coordinate, so the whole negative
    // half of each axis had no lines: one quadrant blank, two with one
    // family each, one correct.
    look(8.0f, 0.0f, 89.0f);
    app.Step();
    app.Step();
    Bmp top;
    REQUIRE(harness::shot(app, "grid_top", top));

    const Quadrants q = quadrants(top, 0, 0, kW, kH);
    report("overhead", q);
    CHECK_GT(q.lowest(), std::size_t(200));
    // Symmetric about the origin, so no quadrant may be a fraction of
    // another: the plane has no special corner.
    CHECK_LT(q.highest(), q.lowest() * 2);

    // ── and in BOTH directions, in every one of them ─────────────────
    // A quadrant can be lit and still be wrong: with only one family
    // of lines it is covered in stripes. Scanning a row counts the
    // lines running down the picture, a column the ones running
    // across, and a grid owes both to every quadrant.
    const unsigned qx[2] = {kW / 4, 3 * kW / 4};
    const unsigned qy[2] = {kH / 4, 3 * kH / 4};
    for (int iy = 0; iy < 2; ++iy)
        for (int ix = 0; ix < 2; ++ix) {
            const unsigned x0 = ix ? kW / 2 : 0, x1 = ix ? kW : kW / 2;
            const unsigned y0 = iy ? kH / 2 : 0, y1 = iy ? kH : kH / 2;
            const std::size_t down = runs_in_row(top, qy[iy], x0, x1);
            const std::size_t across = runs_in_col(top, qx[ix], y0, y1);
            std::printf("  quadrant %s%s: %zu lines down, %zu across\n",
                        iy ? "bottom-" : "top-", ix ? "right" : "left", down,
                        across);
            CHECK_GT(down, std::size_t(0));
            CHECK_GT(across, std::size_t(0));
        }

    // ── the lines are drawn, not stamped ─────────────────────────────
    // Anti-aliasing is what keeps a grid from crawling when the camera
    // moves, and it shows as brightnesses between the background and
    // the line.
    CHECK_GT(shades(top, 0, 0, kW, kH), std::size_t(6));

    // ── the picture repeats every decade of zoom ─────────────────────
    // The strongest thing that can be said about a grid whose extent
    // follows the camera and whose cell is a decade of world units:
    // ten times further away is the SAME PICTURE, drawn with the next
    // decade of cells. Not similar — the same, to a percent.
    const auto coverage = [&](float distance) {
        look(distance, 0.0f, 89.0f);
        app.Step();
        Bmp z;
        CHECK(harness::shot(app, "grid_zoom", z));
        const Quadrants zq = quadrants(z, 0, 0, kW, kH);
        CHECK_GT(zq.lowest(), std::size_t(100));
        return lit_count(z, 0, 0, kW, kH);
    };
    const std::size_t decade[4] = {coverage(0.5f), coverage(5.0f),
                                   coverage(50.0f), coverage(500.0f)};
    std::printf("  a decade apart: %zu, %zu, %zu, %zu lit\n", decade[0],
                decade[1], decade[2], decade[3]);
    for (int i = 1; i < 4; ++i) {
        const std::size_t a = decade[0], b = decade[i];
        const std::size_t diff = a > b ? a - b : b - a;
        CHECK_LT(diff * 100, a); // within one percent
    }

    // Between the decades the picture BREATHES, and should: the fine
    // tier fades out as the coarse one arrives, which is what a
    // level-of-detail grid does instead of shimmering. What must hold
    // at every zoom is only that all four quadrants are drawn.
    for (int i = 0; i < 8; ++i) {
        const float d = 1.0f * std::pow(10.0f, float(i) / 8.0f);
        look(d, 0.0f, 89.0f);
        app.Step();
        Bmp z;
        REQUIRE(harness::shot(app, "grid_step", z));
        CHECK_GT(quadrants(z, 0, 0, kW, kH).lowest(), std::size_t(100));
    }

    // ── and while the camera is flown around ─────────────────────────
    // Through the pointer, not the API: this is what a person does to
    // it, and a grid that only survives a camera set once is not the
    // thing being claimed.
    look(8.0f, 0.0f, 60.0f);
    app.Step();
    app.Step();

    input::wheel(app, kW / 2.0f, kH / 2.0f, 5.0f);
    input::wheel(app, kW / 2.0f, kH / 2.0f, -8.0f);
    input::drag(app, 300.0f, 300.0f, 420.0f, 260.0f);
    input::shift(app, true);
    input::drag(app, 300.0f, 300.0f, 360.0f, 340.0f);
    input::shift(app, false);
    app.Step();

    Bmp flown;
    REQUIRE(harness::shot(app, "grid_flown", flown));
    const Quadrants fq = quadrants(flown, 0, 0, kW, kH);
    report("after flying around", fq);
    CHECK_GT(fq.lowest(), std::size_t(50));
    CHECK_GT(shades(flown, 0, 0, kW, kH), std::size_t(6));

    // The camera really did move, or the four assertions above were
    // about a picture nobody touched.
    sv::probe::CameraState c{};
    CHECK(sv::probe::camera_of(app.Raw(), nullptr, &c));
    std::printf("  camera after: distance %.2f, focus (%.2f, %.2f, %.2f)\n",
                c.distance, c.focus[0], c.focus[1], c.focus[2]);
    CHECK_GT(std::fabs(c.distance - 8.0f), 0.1f);
    CHECK_GT(std::fabs(c.focus[0]) + std::fabs(c.focus[1]), 0.01f);

    // ── seen from the side, the far field fades ──────────────────────
    // A grid that keeps drawing to the horizon shimmers there, because
    // a cell is thinner than a pixel long before it reaches it.
    look(8.0f, 0.0f, 8.0f);
    app.Step();
    app.Step();
    Bmp low;
    REQUIRE(harness::shot(app, "grid_low", low));
    const std::size_t near_half = lit_count(low, 0, kH / 2, kW, kH);
    const std::size_t far_half = lit_count(low, 0, 0, kW, kH / 2);
    std::printf("  grazing view: %zu lit near, %zu far\n", near_half, far_half);
    CHECK_GT(near_half, far_half);

    return check::summary("grid");
}
