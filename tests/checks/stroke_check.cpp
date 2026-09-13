// A stroke is a drag with the cut tool on: the pointer's path through
// the picture, handed to whoever asked with the view it was drawn in.
// What this proves: with the camera tool a drag orbits and no stroke
// fires; with the cut tool the same drag fires strokes and the camera
// holds still; a stroke crosses the segment it swept across and not
// the one beside it; and the right button still orbits under the tool.

#include "harness/Harness.h"
#include "harness/Input.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr float kPxPerUnit = 600.0f / (2.0f * 0.41421356f * 5.0f);

sv::probe::CameraState cam(sv::App &app) {
    sv::probe::CameraState c{};
    CHECK(sv::probe::camera_of(app.Raw(), nullptr, &c));
    return c;
}

constexpr sv::CameraDesc kLevel{.focus = {0.0f, 0.0f, 0.0f},
                                .distance = 5.0f,
                                .azimuth_deg = 0.0f,
                                .elevation_deg = 0.0f};

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {800, 600}, .headless = true});
    if (!app)
        return check::skip("stroke", LastError());

    sv::World w = app.World({.grid = false, .axes = false});
    REQUIRE(bool(w));
    w.Camera(kLevel);

    // Two edges through the focus: one along Y (screen x), one along Z.
    const std::vector<float> pts{0.0f, -1.0f, 0.0f,  0.0f, 1.0f, 0.0f,
                                 0.0f, 0.0f,  -1.0f, 0.0f, 0.0f, 1.0f};
    const std::vector<std::uint32_t> edges{0, 1, 2, 3};
    auto wire = w.Wire(edges, {.width = 4.0f});
    REQUIRE(bool(wire));
    CHECK(wire.Update(pts));
    std::vector<Stroke> strokes;
    w.OnStroke([&](const Stroke &s) { strokes.push_back(s); });
    // A stroke goes through the picture the reader saw, so one must
    // have been drawn: headless, that is a shot.
    Bmp img;
    REQUIRE(harness::shot(app, "stroke_setup", img));

    const float cx = 400.0f, cy = 300.0f, off = kPxPerUnit * 0.5f;

    // ── the camera tool: a drag orbits, and nothing is a stroke ──────
    CHECK(w.Tool() == Tool::Camera);
    const auto before = cam(app);
    input::drag(app, cx + off, cy - 60.0f, cx + off, cy + 60.0f);
    const auto turned = cam(app);
    std::printf("  camera tool: turned %.1f deg, %zu strokes\n",
                input::turned(before, turned), strokes.size());
    CHECK_GT(input::turned(before, turned), 1.0f);
    CHECK_EQ(strokes.size(), std::size_t(0));
    w.Camera(kLevel);
    REQUIRE(harness::shot(app, "stroke_level", img));

    // ── the cut tool: the same drag is a stroke, and the camera holds ─
    w.Tool(Tool::Cut);
    CHECK(w.Tool() == Tool::Cut);
    const auto held = cam(app);
    input::drag(app, cx + off, cy - 60.0f, cx + off, cy + 60.0f);
    const auto after = cam(app);
    std::printf("  cut tool: turned %.3f deg, %zu strokes\n",
                input::turned(held, after), strokes.size());
    CHECK_LT(input::turned(held, after), 1e-3f);
    REQUIRE(strokes.size() >= 1);
    std::printf("  stroke from (%.3f %.3f) to (%.3f %.3f)\n",
                strokes.front().from[0], strokes.front().from[1],
                strokes.front().to[0], strokes.front().to[1]);
    CHECK_LT(std::fabs(strokes.front().from[0] - (cx + off) / 800.0f), 0.01f);

    // A vertical stroke half a unit right of the focus sweeps across
    // the Y edge and misses the Z edge beside it.
    const float y0[3] = {0.0f, -1.0f, 0.0f}, y1[3] = {0.0f, 1.0f, 0.0f};
    const float z0[3] = {0.0f, 0.0f, -1.0f}, z1[3] = {0.0f, 0.0f, 1.0f};
    bool crossed_y = false, crossed_z = false;
    for (const Stroke &s : strokes) {
        crossed_y = crossed_y || s.Crosses(y0, y1);
        crossed_z = crossed_z || s.Crosses(z0, z1);
    }
    CHECK(crossed_y);
    CHECK(!crossed_z);
    // It began on nothing: neither edge is under (cx + off, cy - 60).
    CHECK(!strokes.front().On(wire));

    // ── a stroke knows what it began on, and carries a point along ───
    // Pressed on the Z edge, dragged 40 px right: the edge by index, and
    // a point at the focus carried 40 px worth of world +Y at that depth.
    strokes.clear();
    input::drag(app, cx, cy - 60.0f, cx + 40.0f, cy - 60.0f);
    REQUIRE(strokes.size() >= 1);
    CHECK(strokes.front().On(wire));
    CHECK_EQ(strokes.front().index, 1);
    const float at[3] = {0.0f, 0.0f, 0.0f};
    float carried[3] = {0.0f, 0.0f, 0.0f};
    REQUIRE(strokes.front().Carry(at, carried));
    std::printf("  carried the focus to (%.3f %.3f %.3f); 40 px is %.3f\n",
                carried[0], carried[1], carried[2], 40.0f / kPxPerUnit);
    CHECK_LT(std::fabs(carried[1] - 40.0f / kPxPerUnit), 0.02f);
    CHECK_LT(std::fabs(carried[0]), 0.01f);
    CHECK_LT(std::fabs(carried[2]), 0.01f);

    // ── the right button orbits under the cut tool ───────────────────
    strokes.clear();
    const auto still = cam(app);
    input::drag(app, cx, cy, cx + 80.0f, cy, input::Right);
    const auto orbited = cam(app);
    std::printf("  right drag: turned %.1f deg, %zu strokes\n",
                input::turned(still, orbited), strokes.size());
    CHECK_GT(input::turned(still, orbited), 1.0f);
    CHECK_EQ(strokes.size(), std::size_t(0));

    return check::summary("stroke");
}
