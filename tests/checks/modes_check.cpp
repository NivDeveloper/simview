// A mode is the app's: entered by a key or a pad button, left by Esc
// or B, its strokes and carries the pointer's whichever device moves
// it. What this proves: 2 and X enter cut, 3 and Y drag, Esc leaves and
// the sim's own Esc waits its turn; under cut a left drag strokes and
// does not orbit while the right button still does; a stroke crosses
// the segment it swept and knows what it began on; a carry follows the
// pointer and the wheel pushes it; A plus the right stick strokes the
// same way; under a crosshair a turn sweeps the stroke; the panel's
// choice and the keys agree; a mode-local key fires only inside; and
// the bar says all of it.

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

sv::probe::CameraState cam(sv::App &app) {
    sv::probe::CameraState c{};
    CHECK(sv::probe::camera_of(app.Raw(), nullptr, &c));
    return c;
}

constexpr sv::CameraDesc kLevel{.focus = {0.0f, 0.0f, 0.0f},
                                .distance = 5.0f,
                                .azimuth_deg = 0.0f,
                                .elevation_deg = 0.0f};

bool bar_says(sv::App &app, const char *what) {
    char line[512];
    sv::probe::world_legend(app.Raw(), nullptr, line, sizeof line);
    return std::strstr(line, what) != nullptr;
}

void print_bar(sv::App &app) {
    char line[512];
    sv::probe::world_legend(app.Raw(), nullptr, line, sizeof line);
    std::printf("  the bar: %s\n", line);
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {800, 600}, .headless = true});
    if (!app)
        return check::skip("modes", LastError());

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

    int quits = 0, undos = 0;
    app.Bind({.id = "pause", .label = "pause", .controls = {Ctl(Key::Space)}},
             [] {})
        .Bind({.id = "quit", .label = "quit", .controls = {Ctl(Key::Escape)}},
              [&] { ++quits; });
    sv::Mode cut =
        app.Mode({.name = "cut", .enter = {Ctl(Key::N2), Ctl(Pad::X)}});
    sv::Mode drag =
        app.Mode({.name = "drag", .enter = {Ctl(Key::N3), Ctl(Pad::Y)}});
    REQUIRE(bool(cut));
    REQUIRE(bool(drag));
    std::vector<Stroke> strokes, carries;
    cut.OnStroke([&](const Stroke &s) { strokes.push_back(s); });
    cut.Bind({.id = "undo", .label = "undo", .controls = {Ctl(Key::Z)}},
             [&] { ++undos; });
    drag.OnCarry([&](const Stroke &s) { carries.push_back(s); });
    // A stroke goes through the picture the reader saw, so one must
    // have been drawn: headless, that is a shot — and a frame, so the
    // rows just registered have been resolved for the bar.
    app.Step();
    Bmp img;
    REQUIRE(harness::shot(app, "modes_setup", img));

    // ── the keys enter and leave, and the bar lights the one on ──────
    CHECK(app.Modes().Active() == nullptr);
    print_bar(app);
    CHECK(bar_says(app, "2 cut"));
    CHECK(bar_says(app, "3 drag"));
    CHECK(bar_says(app, "Space pause"));
    CHECK(bar_says(app, "Tab fly"));
    CHECK(bar_says(app, "  drag orbit"));
    CHECK(!bar_says(app, "[2 cut]"));
    input::tap(app, Key::N2);
    REQUIRE(app.Modes().Active() != nullptr);
    CHECK_EQ(std::string(app.Modes().Active()), std::string("cut"));
    print_bar(app);
    CHECK(bar_says(app, "[2 cut]"));
    CHECK(bar_says(app, "  drag cut"));
    CHECK(bar_says(app, "right-drag orbit"));
    CHECK(!bar_says(app, "  drag orbit"));
    CHECK(bar_says(app, "Esc exit"));
    input::tap(app, Key::Z);
    CHECK_EQ(undos, 1);
    input::tap(app, Key::Escape);
    CHECK(app.Modes().Active() == nullptr);
    CHECK_EQ(quits, 0);
    input::tap(app, Key::Z);
    CHECK_EQ(undos, 1); // a mode's key is nothing outside it
    input::tap(app, Key::Escape);
    CHECK_EQ(quits, 1);
    input::pad_tap(app, Pad::Y);
    CHECK_EQ(std::string(app.Modes().Active()), std::string("drag"));
    input::pad_tap(app, Pad::B);
    CHECK(app.Modes().Active() == nullptr);
    input::tap(app, Key::N3);
    input::tap(app, Key::N3); // the same key again leaves
    CHECK(app.Modes().Active() == nullptr);
    app.Modes().Enter("cut");
    CHECK_EQ(std::string(app.Modes().Active()), std::string("cut"));

    const float cx = 400.0f, cy = 300.0f, off = kPxPerUnit * 0.5f;

    // ── under cut a left drag strokes and the camera holds ───────────
    const auto held = cam(app);
    input::drag(app, cx + off, cy - 60.0f, cx + off, cy + 60.0f);
    const auto after = cam(app);
    std::printf("  cut: turned %.3f deg, %zu strokes\n",
                input::turned(held, after), strokes.size());
    CHECK_LT(input::turned(held, after), 1e-3f);
    REQUIRE(strokes.size() >= 1);
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
    CHECK(!strokes.front().On(wire)); // it began on nothing

    // ── the right button still orbits under a mode ───────────────────
    const auto still = cam(app);
    input::drag(app, cx, cy, cx + 80.0f, cy, input::Right);
    CHECK_GT(input::turned(still, cam(app)), 1.0f);
    w.Camera(kLevel);
    REQUIRE(harness::shot(app, "modes_level", img));

    // ── a stroke knows what it began on ──────────────────────────────
    strokes.clear();
    input::drag(app, cx, cy - 60.0f, cx + 40.0f, cy - 60.0f);
    REQUIRE(strokes.size() >= 1);
    CHECK(strokes.front().On(wire));
    CHECK_EQ(strokes.front().index, 1);
    CHECK_EQ(strokes.front().depth, 0.0f);

    // ── a carry follows the pointer, and the wheel pushes it ─────────
    app.Modes().Enter("drag");
    carries.clear();
    input::move(app, cx, cy);
    input::press(app);
    input::move(app, cx + 40.0f, cy);
    REQUIRE(carries.size() >= 1);
    const float at[3] = {0.0f, 0.0f, 0.0f};
    float carried[3] = {0.0f, 0.0f, 0.0f};
    REQUIRE(carries.back().Carry(at, carried));
    std::printf("  carried the focus to (%.3f %.3f %.3f); 40 px is %.3f\n",
                carried[0], carried[1], carried[2], 40.0f / kPxPerUnit);
    CHECK_LT(std::fabs(carried[1] - 40.0f / kPxPerUnit), 0.02f);
    CHECK_LT(std::fabs(carried[0]), 0.01f);
    carries.clear();
    app.PostEvent(MouseWheel(2.0f));
    app.Step();
    REQUIRE(carries.size() >= 1);
    CHECK_GT(carries.back().depth, 0.0f);
    REQUIRE(carries.back().Carry(at, carried));
    const auto view = cam(app);
    float along = 0.0f;
    for (int i = 0; i < 3; ++i)
        along += (carried[i] - at[i]) * view.forward[i];
    std::printf("  two notches pushed the focus %.3f along the sight\n", along);
    CHECK_GT(along, 0.5f);
    input::release(app);
    // The wheel outside a carry is the camera's zoom again.
    const auto zoom_before = cam(app);
    input::wheel(app, cx, cy, 2.0f);
    CHECK_LT(cam(app).distance, zoom_before.distance);
    w.Camera(kLevel);

    // ── RT held with the right stick is the same stroke ──────────────
    app.Modes().Enter("cut");
    strokes.clear();
    input::move(app, cx + off, cy - 60.0f);
    const auto put = cam(app);
    input::stick(app, Pad::RT, 1.0f);
    app.Step();
    input::stick(app, Pad::RS, 0.0f, 1.0f);
    for (int f = 0; f < 8; ++f)
        app.Step();
    input::stick(app, Pad::RS, 0.0f, 0.0f);
    input::stick(app, Pad::RT, 0.0f);
    app.Step();
    std::printf("  RT + right stick under cut: %zu strokes, turned %.4f\n",
                strokes.size(), input::turned(put, cam(app)));
    REQUIRE(strokes.size() >= 5);
    CHECK_LT(input::turned(put, cam(app)), 1e-3f);
    crossed_y = false;
    for (const Stroke &s : strokes)
        crossed_y = crossed_y || s.Crosses(y0, y1);
    CHECK(crossed_y);
    CHECK(bar_says(app, "RT + RS cut"));

    // ── under a crosshair a turn sweeps the stroke ───────────────────
    // In flight: a look turns about the eye, so what the crosshair was
    // on moves in the picture. (An orbit keeps its focus centred, so a
    // sweep there needs a point off the focus.)
    input::tap(app, Key::Tab);
    REQUIRE(probe::aimed(app.Raw(), nullptr));
    strokes.clear();
    input::press(app);
    input::look(app, 40.0f, 0.0f);
    input::look(app, 40.0f, 0.0f);
    input::release(app);
    REQUIRE(strokes.size() >= 1);
    const Stroke &sweep = strokes.back();
    std::printf("  crosshair sweep: from (%.3f %.3f) to (%.3f %.3f)\n",
                sweep.from[0], sweep.from[1], sweep.to[0], sweep.to[1]);
    CHECK_GT(std::fabs(sweep.from[0] - sweep.to[0]), 0.01f);
    CHECK_LT(std::fabs(sweep.to[0] - 0.5f), 1e-3f);
    input::tap(app, Key::Escape); // leaves the mode first
    CHECK(app.Modes().Active() == nullptr);
    CHECK(probe::aimed(app.Raw(), nullptr));
    input::tap(app, Key::Escape); // then the crosshair
    CHECK(!probe::aimed(app.Raw(), nullptr));
    CHECK_EQ(quits, 1);

    return check::summary("modes");
}
