// The camera modes and the pointer styles are app state, never the
// device's: Tab flies for the keyboard and the pad alike, the crosshair
// comes with a flight, and Esc peels one layer at a time. What this
// proves headless: in flight W moves eye and focus along forward and
// never reaches the sim's own W, while Space does; E rises along world
// Z; a look and the right stick turn about the eye alike; Shift and the
// wheel scale the step; a cursor cannot fly; the panel world under the
// pointer is the one aimed; a key held into the switch-back moves
// nothing after.

#include "harness/Bmp.h"
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

void eye(const sv::probe::CameraState &c, float out[3]) {
    for (int i = 0; i < 3; ++i)
        out[i] = c.focus[i] - c.forward[i] * c.distance;
}

float dist(const float a[3], const float b[3]) {
    float s = 0.0f;
    for (int i = 0; i < 3; ++i)
        s += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(s);
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
        return check::skip("crosshair", LastError());

    sv::World w = app.World();
    REQUIRE(bool(w));
    w.Camera(kLevel);
    int ws = 0, spaces = 0, quits = 0;
    app.Bind({.id = "w", .controls = {Ctl(Key::W)}}, [&] { ++ws; })
        .Bind({.id = "pause", .controls = {Ctl(Key::Space)}}, [&] { ++spaces; })
        .Bind({.id = "quit", .controls = {Ctl(Key::Escape)}}, [&] { ++quits; });
    app.Step();

    // ── Tab flies, a flight is aimed, and the crosshair is drawn ─────
    CHECK(app.Modes().Camera() == CameraMode::Orbit);
    CHECK(app.Modes().Pointer() == PointerStyle::Cursor);
    Bmp bare, aimed_shot;
    REQUIRE(harness::shot(app, "crosshair_off", bare));
    input::tap(app, Key::Tab);
    CHECK(app.Modes().Camera() == CameraMode::Fly);
    CHECK(app.Modes().Pointer() == PointerStyle::Crosshair);
    CHECK(probe::aimed(app.Raw(), nullptr));
    CHECK_EQ(probe::pointer(app.Raw(), nullptr, nullptr),
             int(PointerStyle::Crosshair));
    app.Step();
    REQUIRE(harness::shot(app, "crosshair_on", aimed_shot));
    const std::size_t centre_off = lit_count(bare, 386, 286, 414, 314, 40);
    const std::size_t centre_on = lit_count(aimed_shot, 386, 286, 414, 314, 40);
    std::printf("  the centre lit: %zu without the crosshair, %zu with\n",
                centre_off, centre_on);
    CHECK_GT(centre_on, centre_off + 20);

    // ── W moves eye and focus along forward, and is the flight's ─────
    const auto start = cam(app);
    input::key(app, Key::W, true);
    for (int f = 0; f < 9; ++f)
        app.Step();
    input::key(app, Key::W, false);
    const auto ahead = cam(app);
    float e0[3], e1[3];
    eye(start, e0);
    eye(ahead, e1);
    float along = 0.0f;
    for (int i = 0; i < 3; ++i)
        along += (ahead.focus[i] - start.focus[i]) * start.forward[i];
    std::printf("  W for 10 frames: %.3f ahead, eye moved %.3f, sim saw %d\n",
                along, dist(e0, e1), ws);
    CHECK_GT(along, 0.1f);
    CHECK_LT(std::fabs(dist(e0, e1) - along), 1e-3f);
    CHECK_EQ(ws, 0);
    input::tap(app, Key::Space);
    CHECK_EQ(spaces, 1);

    // ── E rises along WORLD up ───────────────────────────────────────
    const auto low = cam(app);
    input::key(app, Key::E, true);
    for (int f = 0; f < 4; ++f)
        app.Step();
    input::key(app, Key::E, false);
    const auto high = cam(app);
    CHECK_GT(high.focus[2] - low.focus[2], 0.05f);
    CHECK_LT(
        std::hypot(high.focus[0] - low.focus[0], high.focus[1] - low.focus[1]),
        1e-4f);

    // ── a look and the right stick turn about the EYE alike ──────────
    const auto before = cam(app);
    input::look(app, 60.0f, 0.0f);
    const auto looked = cam(app);
    eye(before, e0);
    eye(looked, e1);
    std::printf("  look 60 px: turned %.2f deg, eye moved %.5f\n",
                input::turned(before, looked), dist(e0, e1));
    CHECK_GT(input::turned(before, looked), 5.0f);
    CHECK_LT(dist(e0, e1), 1e-3f);
    CHECK_GT(looked.up[2], 0.9f);
    const auto steady = cam(app);
    input::stick(app, Pad::RS, 1.0f, 0.0f);
    for (int f = 0; f < 5; ++f)
        app.Step();
    input::stick(app, Pad::RS, 0.0f, 0.0f);
    app.Step();
    eye(steady, e0);
    eye(cam(app), e1);
    std::printf("  right stick 5 frames: turned %.2f deg, eye moved %.5f\n",
                input::turned(steady, cam(app)), dist(e0, e1));
    CHECK_GT(input::turned(steady, cam(app)), 5.0f);
    CHECK_LT(dist(e0, e1), 1e-3f);

    // ── Shift and the wheel scale the step ───────────────────────────
    const auto walk = cam(app);
    input::key(app, Key::W, true);
    for (int f = 0; f < 4; ++f)
        app.Step();
    input::key(app, Key::W, false);
    const float plain = input::moved(walk, cam(app));
    const auto sprint = cam(app);
    input::key(app, Key::LeftShift, true);
    input::key(app, Key::W, true);
    for (int f = 0; f < 4; ++f)
        app.Step();
    input::key(app, Key::W, false);
    input::key(app, Key::LeftShift, false);
    const float fast = input::moved(sprint, cam(app));
    std::printf("  5 frames of W: %.3f plain, %.3f with Shift\n", plain, fast);
    CHECK_GT(fast, plain * 2.0f);
    app.PostEvent(MouseWheel(2.0f));
    app.Step();
    const auto quick = cam(app);
    input::key(app, Key::W, true);
    for (int f = 0; f < 4; ++f)
        app.Step();
    input::key(app, Key::W, false);
    CHECK_GT(input::moved(quick, cam(app)), plain * 1.2f);

    // ── Esc leaves the crosshair, and with it the flight; the sim's
    // own Esc did not fire ───────────────────────────────────────────
    input::tap(app, Key::Escape);
    CHECK(app.Modes().Pointer() == PointerStyle::Cursor);
    CHECK(app.Modes().Camera() == CameraMode::Orbit);
    CHECK(!probe::aimed(app.Raw(), nullptr));
    CHECK_EQ(quits, 0);
    input::tap(app, Key::Escape);
    CHECK_EQ(quits, 1);

    // ── a crosshair without a flight orbits from the centre; Tab from
    // there flies and Tab again orbits, the crosshair kept ───────────
    input::key(app, Key::LeftCtrl, true);
    input::tap(app, Key::Tab);
    input::key(app, Key::LeftCtrl, false);
    CHECK(app.Modes().Pointer() == PointerStyle::Crosshair);
    CHECK(app.Modes().Camera() == CameraMode::Orbit);
    const auto pivot = cam(app);
    input::look(app, 60.0f, 0.0f);
    CHECK_GT(input::turned(pivot, cam(app)), 5.0f);
    CHECK_LT(input::moved(pivot, cam(app)), 1e-4f); // about the focus
    input::tap(app, Key::Tab);
    CHECK(app.Modes().Camera() == CameraMode::Fly);
    input::tap(app, Key::Tab);
    CHECK(app.Modes().Camera() == CameraMode::Orbit);
    CHECK(app.Modes().Pointer() == PointerStyle::Crosshair);
    input::pad_tap(app, Pad::B);
    CHECK(app.Modes().Pointer() == PointerStyle::Cursor);

    // ── a cursor cannot fly: asking for one lands in orbit ───────────
    app.Modes().Camera(CameraMode::Fly);
    CHECK(app.Modes().Pointer() == PointerStyle::Crosshair);
    app.Modes().Pointer(PointerStyle::Cursor);
    CHECK(app.Modes().Camera() == CameraMode::Orbit);

    // ── a key held into the switch-back moves nothing after ──────────
    input::tap(app, Key::Tab);
    input::key(app, Key::W, true);
    app.Step();
    input::tap(app, Key::Escape);
    const auto landed = cam(app);
    for (int f = 0; f < 5; ++f)
        app.Step();
    CHECK_LT(input::moved(landed, cam(app)), 1e-6f);
    input::key(app, Key::W, false);
    w.Camera(kLevel);

    // ── the panel world under the pointer is the one aimed ───────────
    sv::World side = app.World({.title = "side"});
    REQUIRE(bool(side));
    side.Camera(
        {.distance = 4.0f, .azimuth_deg = 90.0f, .elevation_deg = 10.0f});
    app.OnUi([] {
        ImGui::SetWindowPos("side", ImVec2(500, 380));
        ImGui::SetWindowSize("side", ImVec2(280, 200));
    });
    app.Step();
    app.Step();
    input::move(app, 640.0f, 480.0f);
    input::tap(app, Key::Tab);
    CHECK(probe::aimed(app.Raw(), "side"));
    CHECK(!probe::aimed(app.Raw(), nullptr));
    const auto side_before = cam(app, "side");
    const auto main_before = cam(app);
    input::look(app, 60.0f, 0.0f);
    std::printf("  aimed at the panel: it turned %.2f deg, the window's "
                "%.4f\n",
                input::turned(side_before, cam(app, "side")),
                input::turned(main_before, cam(app)));
    CHECK_GT(input::turned(side_before, cam(app, "side")), 5.0f);
    CHECK_LT(input::turned(main_before, cam(app)), 1e-4f);
    input::tap(app, Key::Escape);

    return check::summary("crosshair");
}
