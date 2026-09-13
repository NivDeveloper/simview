// A stroke is a drag with the cut tool on: the pointer's path through
// the picture, handed to whoever asked with the view it was drawn in.
// What this proves: with the camera tool a drag orbits and no stroke
// fires; with the cut tool the same drag fires strokes and the camera
// holds still; a stroke crosses the segment it swept across and not
// the one beside it; the right button still orbits under the tool;
// the tool keys take the tool only where something listens, and are
// the sim's otherwise; a gamepad's move half strokes under a tool and
// its Y takes the next one; and the key bar says all of this.

#include "harness/Harness.h"
#include "harness/Input.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr float kPxPerUnit = 600.0f / (2.0f * 0.41421356f * 5.0f);
constexpr std::int16_t kFull = 32767;

sv::probe::CameraState cam(sv::App &app) {
    sv::probe::CameraState c{};
    CHECK(sv::probe::camera_of(app.Raw(), nullptr, &c));
    return c;
}

// Hold the pad's axes for some frames, then let go.
void hold(sv::App &app, std::int16_t lx, std::int16_t ly, std::int16_t rx,
          std::int16_t lt, std::int16_t rt, int frames) {
    const std::int16_t raw[6] = {lx, ly, rx, 0, lt, rt};
    sv::probe::gamepad(app.Raw(), raw);
    for (int f = 0; f < frames; ++f)
        app.Step();
    const std::int16_t rest[6] = {0, 0, 0, 0, 0, 0};
    sv::probe::gamepad(app.Raw(), rest);
    app.Step();
}

bool bar_says(sv::App &app, const char *what) {
    char line[512];
    sv::probe::world_legend(app.Raw(), nullptr, line, sizeof line);
    return std::strstr(line, what) != nullptr;
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
    app.Step();

    // ── with nobody listening there are no tools: 2 is the sim's key,
    // and the bar lists what the sim labelled and no tool ────────────
    int twos = 0;
    app.OnKey(Key::N2, [&] { ++twos; });
    app.OnKey(Key::Space, "pause", [] {});
    input::tap(app, Key::N2);
    CHECK_EQ(twos, 1);
    CHECK(w.Tool() == Tool::Camera);
    CHECK(bar_says(app, "Space pause"));
    CHECK(bar_says(app, "Tab fly"));
    CHECK(bar_says(app, "drag orbit"));
    CHECK(!bar_says(app, "camera"));

    std::vector<Stroke> strokes;
    w.OnStroke([&](const Stroke &s) { strokes.push_back(s); });
    // A stroke goes through the picture the reader saw, so one must
    // have been drawn: headless, that is a shot.
    Bmp img;
    REQUIRE(harness::shot(app, "stroke_setup", img));

    // ── with a listener the keys 1 2 3 are the tools', never the sim's,
    // and the bar lights the one that is on ──────────────────────────
    input::tap(app, Key::N2);
    CHECK(w.Tool() == Tool::Cut);
    CHECK_EQ(twos, 1);
    CHECK(bar_says(app, "[2 cut]"));
    CHECK(bar_says(app, "drag cut"));
    input::tap(app, Key::N3);
    CHECK(w.Tool() == Tool::Drag);
    CHECK(bar_says(app, "[3 drag]"));
    CHECK(bar_says(app, "drag move"));
    input::tap(app, Key::N1);
    CHECK(w.Tool() == Tool::Camera);
    CHECK(bar_says(app, "[1 camera]"));
    {
        char line[512];
        probe::world_legend(app.Raw(), nullptr, line, sizeof line);
        std::printf("  the bar: %s\n", line);
    }

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

    // ── the drag tool is a stroke tool too, and a stroke says which ──
    for (const Stroke &s : strokes)
        CHECK(s.tool == Tool::Cut);
    strokes.clear();
    w.Tool(Tool::Drag);
    const auto steady = cam(app);
    input::drag(app, cx, cy - 60.0f, cx + 40.0f, cy - 60.0f);
    CHECK_LT(input::turned(steady, cam(app)), 1e-3f);
    REQUIRE(strokes.size() >= 1);
    CHECK(strokes.front().tool == Tool::Drag);
    CHECK(strokes.front().On(wire));
    CHECK(!strokes.front().pad);
    CHECK_EQ(strokes.front().push, 0.0f);
    // Two presses at one spot inside the double-click time: the second
    // framed the picked point, as a double-click does. Level again, and
    // draw the picture the pad's strokes go through.
    w.Camera(kLevel);
    REQUIRE(harness::shot(app, "stroke_level_pad", img));

    // ── under a stroke tool the pad's left stick strokes instead of
    // walking: one a frame, from the pad, begun on nothing, and the
    // camera stays put; a carried point goes the stick's way ─────────
    strokes.clear();
    const auto put = cam(app);
    hold(app, kFull, 0, 0, 0, 0, 5);
    std::printf("  stick right under drag: %zu strokes, camera moved %.5f\n",
                strokes.size(), input::moved(put, cam(app)));
    REQUIRE(strokes.size() >= 5);
    CHECK_LT(input::moved(put, cam(app)), 1e-5f);
    CHECK_LT(input::turned(put, cam(app)), 1e-4f);
    for (const Stroke &s : strokes) {
        CHECK(s.pad);
        CHECK(s.tool == Tool::Drag);
        CHECK(!s.On(wire));
        CHECK_EQ(s.push, 0.0f);
    }
    REQUIRE(strokes.front().Carry(at, carried));
    std::printf("  one frame of stick carried the focus to (%.3f %.3f %.3f)\n",
                carried[0], carried[1], carried[2]);
    CHECK_GT(carried[1], 0.02f);
    CHECK_LT(std::fabs(carried[0]), 1e-3f);
    CHECK_LT(std::fabs(carried[2]), 1e-3f);

    // ── the bar follows the device in hand: the pad's names while the
    // pad spoke last, the pointer's again after a key ────────────────
    CHECK(bar_says(app, "LS move"));
    CHECK(bar_says(app, "LT RT pull / push"));
    CHECK(bar_says(app, "Y tool"));
    CHECK(!bar_says(app, "drag move"));
    REQUIRE(harness::shot(app, "stroke_pad_bar", img));
    input::tap(app, Key::N3);
    CHECK(bar_says(app, "drag move"));
    CHECK(!bar_says(app, "LS move"));

    // ── the triggers push along the line of sight: away on the right,
    // toward on the left, and the point keeps its place in the picture
    strokes.clear();
    hold(app, 0, 0, 0, 0, kFull, 3);
    REQUIRE(strokes.size() >= 1);
    CHECK_GT(strokes.front().push, 0.0f);
    REQUIRE(strokes.front().Carry(at, carried));
    const auto view = cam(app);
    float along = 0.0f, across = 0.0f;
    for (int i = 0; i < 3; ++i)
        along += (carried[i] - at[i]) * view.forward[i];
    for (int i = 0; i < 3; ++i) {
        const float side = (carried[i] - at[i]) - along * view.forward[i];
        across += side * side;
    }
    std::printf("  right trigger pushed the focus %.3f along, %.5f across\n",
                along, std::sqrt(across));
    CHECK_GT(along, 0.03f);
    CHECK_LT(std::sqrt(across), 1e-3f);
    strokes.clear();
    hold(app, 0, 0, 0, kFull, 0, 3);
    REQUIRE(strokes.size() >= 1);
    CHECK_LT(strokes.front().push, 0.0f);

    // ── the right stick is still the camera's under a tool ───────────
    const auto level = cam(app);
    hold(app, 0, 0, kFull, 0, 0, 5);
    std::printf("  right stick under drag: turned %.2f deg\n",
                input::turned(level, cam(app)));
    CHECK_GT(input::turned(level, cam(app)), 1.0f);
    w.Camera(kLevel);
    REQUIRE(harness::shot(app, "stroke_level_again", img));

    // ── Y takes the next tool round, on its edge ─────────────────────
    probe::gamepad_buttons(app.Raw(), false, false, true);
    app.Step();
    CHECK(w.Tool() == Tool::Camera); // after drag comes camera
    app.Step();
    CHECK(w.Tool() == Tool::Camera); // still held: no edge
    probe::gamepad_buttons(app.Raw(), false, false, false);
    app.Step();
    probe::gamepad_buttons(app.Raw(), false, false, true);
    app.Step();
    CHECK(w.Tool() == Tool::Cut);
    probe::gamepad_buttons(app.Raw(), false, false, false);
    app.Step();

    // ── and under the camera tool the whole pad is the camera's ──────
    w.Tool(Tool::Camera);
    strokes.clear();
    const auto walk = cam(app);
    hold(app, kFull, 0, 0, 0, 0, 5);
    CHECK_GT(input::moved(walk, cam(app)), 0.05f);
    CHECK_EQ(strokes.size(), std::size_t(0));
    w.Camera(kLevel);
    w.Tool(Tool::Cut);

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
