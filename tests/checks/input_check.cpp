// The camera under the pointer: what a drag, a shift-drag and a wheel
// do to it, and — the half that regressed — WHICH world a gesture
// steers.
//
// This is the only check that tests an interaction rather than a
// result. It matters because the rule it guards is not visible in any
// picture: a drag belongs to whatever was under the cursor when the
// button went down, and to nothing else. That rule broke silently the
// moment a world moved from a panel into the window, and no shot check
// could have noticed.

#include "harness/Harness.h"
#include "harness/Input.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <imgui.h>

#include <cmath>
#include <cstdio>

namespace {

sv::probe::CameraState cam(sv::App &app, const char *title = nullptr) {
    sv::probe::CameraState c{};
    CHECK(sv::probe::camera_of(app.Raw(), title, &c));
    return c;
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {800, 600}, .headless = true});
    if (!app)
        return check::skip("input", LastError());

    // The world in the WINDOW: the pointer steers it wherever no panel
    // has claimed the pointer.
    sv::World w = app.World();
    REQUIRE(bool(w));
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 5.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
    app.Step();

    // ── a drag turns the camera ──────────────────────────────────────
    const auto before = cam(app);
    input::drag(app, 400.0f, 300.0f, 460.0f, 300.0f);
    const auto after = cam(app);
    const float yaw = input::turned(before, after);
    std::printf("  drag 60 px: turned %.2f deg\n", yaw);
    CHECK_GT(yaw, 5.0f);
    // A turntable ORBITS: the subject and the range stay put.
    CHECK_LT(input::moved(before, after), 1e-4f);
    CHECK_LT(std::fabs(after.distance - before.distance), 1e-4f);
    // And it stays level — the yaw is about the world's up axis, so
    // the horizon never rolls.
    CHECK_GT(after.up[2], 0.9f);

    // Dragging the other way turns the other way: a gesture that
    // ignored the sign of the delta would pass every test above.
    const auto back = [&] {
        input::drag(app, 400.0f, 300.0f, 340.0f, 300.0f);
        return cam(app);
    }();
    std::printf("  drag back: turned %.2f deg from the start\n",
                input::turned(before, back));
    CHECK_LT(input::turned(before, back), 1.0f);

    // ── the wheel dollies ────────────────────────────────────────────
    const auto near_before = cam(app);
    input::wheel(app, 400.0f, 300.0f, 3.0f);
    const auto near_after = cam(app);
    std::printf("  wheel +3: distance %.3f -> %.3f\n", near_before.distance,
                near_after.distance);
    CHECK_LT(near_after.distance, near_before.distance);
    CHECK_LT(input::turned(near_before, near_after), 1e-3f);
    input::wheel(app, 400.0f, 300.0f, -3.0f);
    CHECK_LT(std::fabs(cam(app).distance - near_before.distance), 1e-3f);

    // ── shift-drag pans ──────────────────────────────────────────────
    const auto pan_before = cam(app);
    input::shift(app, true);
    input::drag(app, 400.0f, 300.0f, 460.0f, 340.0f);
    input::shift(app, false);
    const auto pan_after = cam(app);
    std::printf("  shift-drag: focus moved %.3f, turned %.4f deg\n",
                input::moved(pan_before, pan_after),
                input::turned(pan_before, pan_after));
    CHECK_GT(input::moved(pan_before, pan_after), 1e-3f);
    CHECK_LT(input::turned(pan_before, pan_after), 1e-3f);

    // ── a panel owns its own pointer ─────────────────────────────────
    // The rule the regression broke, in reverse: a drag that starts on
    // a panel belongs to the panel, and the world behind it must not
    // move at all.
    app.Panel("controls");
    app.OnUi([] {
        ImGui::SetWindowPos("controls", ImVec2(0, 0));
        ImGui::SetWindowSize("controls", ImVec2(200, 160));
    });
    app.Step();
    app.Step();

    const auto guarded = cam(app);
    input::drag(app, 60.0f, 60.0f, 160.0f, 120.0f);
    const auto still = cam(app);
    std::printf("  drag on a panel: turned %.4f deg (want 0)\n",
                input::turned(guarded, still));
    CHECK_LT(input::turned(guarded, still), 1e-3f);
    CHECK_LT(input::moved(guarded, still), 1e-4f);

    // And the wheel over a panel is the panel's too.
    const auto wheeled_before = cam(app);
    input::wheel(app, 60.0f, 60.0f, 3.0f);
    CHECK_LT(std::fabs(cam(app).distance - wheeled_before.distance), 1e-4f);

    // A drag that BEGAN on the scene keeps steering it even as the
    // pointer crosses the panel: a grab is held until the release.
    const auto grab_before = cam(app);
    input::move(app, 400.0f, 300.0f);
    input::press(app);
    input::move(app, 100.0f, 100.0f); // over the panel now
    input::release(app);
    std::printf("  drag across a panel: turned %.2f deg\n",
                input::turned(grab_before, cam(app)));
    CHECK_GT(input::turned(grab_before, cam(app)), 5.0f);

    // ── a world in a panel takes its own gestures ────────────────────
    sv::World p = app.World({.title = "view"});
    REQUIRE(bool(p));
    p.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 5.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
    app.OnUi([] {
        ImGui::SetWindowPos("view", ImVec2(400, 300));
        ImGui::SetWindowSize("view", ImVec2(360, 280));
    });
    app.Step();
    app.Step();

    const auto panel_before = cam(app, "view");
    const auto window_before = cam(app);
    input::drag(app, 560.0f, 440.0f, 620.0f, 440.0f);
    const auto panel_after = cam(app, "view");
    const auto window_after = cam(app);
    std::printf("  drag in a view: that world turned %.2f deg, the "
                "window's %.4f deg\n",
                input::turned(panel_before, panel_after),
                input::turned(window_before, window_after));
    CHECK_GT(input::turned(panel_before, panel_after), 5.0f);
    // The one behind it did not take the gesture: two worlds, one
    // pointer, and it went to exactly one of them. A fraction of a
    // degree can still reach the other while a newly placed window
    // settles — measured at 0.04 against 17 on one platform and 0.00
    // on another — so what is pinned is the discrimination, not a
    // zero that only some machines produce.
    CHECK_LT(input::turned(window_before, window_after), 1.0f);

    // ── a pad orbits the same camera the same way ────────────────────
    // RT held with the right stick is the drag that orbits, the left
    // stick pans, the bumpers zoom, and the right stick alone moves the
    // cursor and not the camera.
    input::move(app, 200.0f, 300.0f); // off the panels
    const auto pad_before = cam(app);
    input::stick(app, Pad::RT, 1.0f);
    app.Step();
    input::stick(app, Pad::RS, 1.0f, 0.0f);
    for (int f = 0; f < 5; ++f)
        app.Step();
    input::stick(app, Pad::RS, 0.0f, 0.0f);
    input::stick(app, Pad::RT, 0.0f);
    app.Step();
    const auto pad_after = cam(app);
    std::printf("  RT + right stick, 5 frames: turned %.2f deg, moved %.4f\n",
                input::turned(pad_before, pad_after),
                input::moved(pad_before, pad_after));
    CHECK_GT(input::turned(pad_before, pad_after), 5.0f);
    CHECK_LT(input::moved(pad_before, pad_after), 1e-4f);
    CHECK_EQ(probe::last_device(app.Raw()), int(Device::Pad));

    const auto stick_before = cam(app);
    float cx0, cy0, cx1, cy1;
    probe::pointer(app.Raw(), &cx0, &cy0);
    input::stick(app, Pad::RS, -1.0f, 0.0f);
    for (int f = 0; f < 5; ++f)
        app.Step();
    input::stick(app, Pad::RS, 0.0f, 0.0f);
    app.Step();
    probe::pointer(app.Raw(), &cx1, &cy1);
    std::printf("  right stick alone: cursor moved %.1f px, turned %.4f\n",
                cx0 - cx1, input::turned(stick_before, cam(app)));
    CHECK_GT(cx0 - cx1, 40.0f);
    CHECK_LT(input::turned(stick_before, cam(app)), 1e-3f);

    const auto ls_before = cam(app);
    input::stick(app, Pad::LS, 1.0f, 0.0f);
    for (int f = 0; f < 5; ++f)
        app.Step();
    input::stick(app, Pad::LS, 0.0f, 0.0f);
    app.Step();
    std::printf("  left stick: focus moved %.3f, turned %.4f\n",
                input::moved(ls_before, cam(app)),
                input::turned(ls_before, cam(app)));
    CHECK_GT(input::moved(ls_before, cam(app)), 1e-3f);
    CHECK_LT(input::turned(ls_before, cam(app)), 1e-3f);

    const auto rb_before = cam(app);
    input::pad(app, Pad::RB, true);
    for (int f = 0; f < 5; ++f)
        app.Step();
    input::pad(app, Pad::RB, false);
    std::printf("  right bumper: distance %.3f -> %.3f\n", rb_before.distance,
                cam(app).distance);
    CHECK_LT(cam(app).distance, rb_before.distance - 0.01f);

    // ── a sim with no world and no panel keeps its keys ──────────────
    {
        App bare({.size = {320, 200}, .headless = true});
        REQUIRE(bool(bare));
        int presses = 0;
        bare.Bind({.id = "go", .controls = {Ctl(Key::Space)}},
                  [&] { ++presses; });
        input::tap(bare, Key::Space);
        CHECK_EQ(presses, 1);
    }

    return check::summary("input");
}
