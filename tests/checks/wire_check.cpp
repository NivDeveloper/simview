// Segments over a point set: that an edge is drawn between its two
// points and nowhere else, that a mask hides one edge and leaves the
// other, that a Sync-fed mask does the same, and that a click lands on
// an edge by its index — and not on a masked-out one.

#include "harness/Harness.h"
#include "harness/Input.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

// The summed brightness of a 3x3 block: a 4 px white line under it
// reads in the thousands, the background in the hundreds.
long brightness(const Bmp &img, unsigned x, unsigned y) {
    long sum = 0;
    for (unsigned yy = y - 1; yy <= y + 1 && yy < img.h; ++yy)
        for (unsigned xx = x - 1; xx <= x + 1 && xx < img.w; ++xx) {
            const auto &px = img.at(xx, yy);
            sum += px[0] + px[1] + px[2];
        }
    return sum;
}

bool bright(const Bmp &img, unsigned x, unsigned y) {
    return brightness(img, x, y) > 3000;
}

// At distance 5 and a 45 degree field of view over 600 rows.
constexpr float kPxPerUnit = 600.0f / (2.0f * 0.41421356f * 5.0f);

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {800, 600}, .headless = true});
    if (!app)
        return check::skip("wire", LastError());

    // Level at +X: screen right is world +Y, screen up is world +Z.
    sv::World w = app.World({.grid = false, .axes = false});
    REQUIRE(bool(w));
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 5.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});

    // Two edges through the focus: one along Y, one along Z.
    const std::vector<float> pts{0.0f, -1.0f, 0.0f,  0.0f, 1.0f, 0.0f,
                                 0.0f, 0.0f,  -1.0f, 0.0f, 0.0f, 1.0f};
    const std::vector<std::uint32_t> edges{0, 1, 2, 3};
    auto wire = w.Wire(edges, {.width = 4.0f});
    REQUIRE(bool(wire));
    CHECK(wire.Update(pts));
    app.Step();

    const unsigned cx = 400, cy = 300, off = unsigned(kPxPerUnit * 0.5f);
    Bmp img;
    REQUIRE(harness::shot(app, "wire_both", img));
    std::printf("  on the Y edge %ld, on the Z edge %ld, between %ld\n",
                brightness(img, cx + off, cy), brightness(img, cx, cy - off),
                brightness(img, cx + off, cy - off));
    CHECK(bright(img, cx + off, cy));
    CHECK(bright(img, cx, cy - off));
    CHECK(!bright(img, cx + off, cy - off));

    // A mask: the Z edge goes, the Y edge stays.
    CHECK(wire.UpdateMask(std::vector<float>{1.0f, 0.0f}));
    app.Step();
    REQUIRE(harness::shot(app, "wire_masked", img));
    CHECK(bright(img, cx + off, cy));
    CHECK(!bright(img, cx, cy - off));

    // A Sync-fed mask, the other way round, tracked before it publishes.
    Sync<std::vector<float>> mask;
    wire.Mask(mask);
    mask.Next() = {0.0f, 1.0f};
    mask.Publish();
    app.Step();
    app.Step();
    REQUIRE(harness::shot(app, "wire_synced", img));
    CHECK(!bright(img, cx + off, cy));
    CHECK(bright(img, cx, cy - off));

    // A click names the edge by index; the masked-out one is not there.
    Pick p;
    CHECK(!probe::pick(app.Raw(), nullptr, float(cx + off), float(cy), &p));
    REQUIRE(probe::pick(app.Raw(), nullptr, float(cx), float(cy - off), &p));
    std::printf("  pick: index %d at %.2f\n", p.index, p.distance);
    CHECK(p.On(wire));
    CHECK_EQ(p.index, 1);

    return check::summary("wire");
}
