// Flight: Tab captures a world, the held keys and the pointer's motion
// steer it, Escape releases it — and the mode is ONE fact.
//
// None of what can go wrong here shows in a picture. A key held into
// the release keeps moving the camera (vklib's stuck velocity). A
// hotkey the sim bound to W fires while flying. Two worlds move at
// once. A drag that was latched before the flight orbits under it.
// Each is a line here, headless: relative mouse mode needs a window
// and is a no-op without one, and every other fact flips the same.

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

// Where the eye is. The turntable derives it, so a check does too.
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

// Hold a key for some frames, then let go.
void hold(sv::App &app, sv::Key k, int frames) {
    input::key(app, k, true);
    for (int f = 0; f < frames; ++f)
        app.Step();
    input::key(app, k, false);
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {800, 600}, .headless = true});
    if (!app)
        return check::skip("fly", LastError());

    sv::World w = app.World();
    REQUIRE(bool(w));
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 5.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
    int spaces = 0, ws = 0, escapes = 0;
    app.OnKey(Key::Space, [&] { ++spaces; })
        .OnKey(Key::W, [&] { ++ws; })
        .OnKey(Key::Escape, [&] { ++escapes; });
    app.Step();

    // ── Tab captures the window's world ──────────────────────────────
    CHECK(!probe::flying(app.Raw(), nullptr));
    input::tap(app, Key::Tab);
    CHECK(probe::flying(app.Raw(), nullptr));

    // ── W moves eye and focus together, ahead ────────────────────────
    const auto start = cam(app);
    hold(app, Key::W, 10);
    const auto ahead = cam(app);
    float e0[3], e1[3];
    eye(start, e0);
    eye(ahead, e1);
    float along = 0.0f;
    for (int i = 0; i < 3; ++i)
        along += (ahead.focus[i] - start.focus[i]) * start.forward[i];
    std::printf("  W for 10 frames: %.3f ahead, eye moved %.3f, turned %.4f\n",
                along, dist(e0, e1), input::turned(start, ahead));
    CHECK_GT(along, 0.1f);
    CHECK_LT(std::fabs(dist(e0, e1) - along), 1e-3f);
    CHECK_LT(std::fabs(ahead.distance - start.distance), 1e-4f);
    CHECK_LT(input::turned(start, ahead), 1e-3f);
    // The key was the flight's, not the sim's.
    CHECK_EQ(ws, 0);

    // ── Space still reaches the sim ──────────────────────────────────
    input::tap(app, Key::Space);
    CHECK_EQ(spaces, 1);

    // ── E rises along WORLD up ───────────────────────────────────────
    const auto low = cam(app);
    hold(app, Key::E, 5);
    const auto high = cam(app);
    const float drift =
        std::hypot(high.focus[0] - low.focus[0], high.focus[1] - low.focus[1]);
    std::printf("  E for 5 frames: rose %.3f, drifted %.5f in xy\n",
                high.focus[2] - low.focus[2], drift);
    CHECK_GT(high.focus[2] - low.focus[2], 0.05f);
    CHECK_LT(drift, 1e-4f);

    // ── the pointer turns the camera about the EYE ───────────────────
    const auto before = cam(app);
    input::look(app, 60.0f, 0.0f);
    const auto yawed = cam(app);
    eye(before, e0);
    eye(yawed, e1);
    std::printf("  look 60 px: turned %.2f deg, eye moved %.5f, focus moved "
                "%.3f\n",
                input::turned(before, yawed), dist(e0, e1),
                input::moved(before, yawed));
    CHECK_GT(input::turned(before, yawed), 5.0f);
    CHECK_LT(dist(e0, e1), 1e-3f);
    CHECK_GT(input::moved(before, yawed), 0.1f);
    // Yaw is about world up: forward keeps its height, the horizon
    // stays level.
    CHECK_LT(std::fabs(yawed.forward[2] - before.forward[2]), 1e-4f);
    CHECK_GT(yawed.up[2], 0.9f);
    input::look(app, 0.0f, 40.0f);
    const auto pitched = cam(app);
    CHECK_GT(std::fabs(pitched.forward[2] - yawed.forward[2]), 0.05f);

    // ── a drag in flight is nothing: the flight owns the pointer ─────
    // Without the gate the orbit gesture would read ImGui's delta too,
    // and the eye would swing about the focus.
    const auto held = cam(app);
    eye(held, e0);
    probe::mouse_move(app.Raw(), 400.0f, 300.0f);
    app.Step();
    probe::mouse_button(app.Raw(), 0, true);
    app.Step();
    probe::mouse_move(app.Raw(), 460.0f, 330.0f);
    app.Step();
    probe::mouse_button(app.Raw(), 0, false);
    app.Step();
    eye(cam(app), e1);
    std::printf("  drag in flight: eye moved %.5f, turned %.5f\n", dist(e0, e1),
                input::turned(held, cam(app)));
    CHECK_LT(dist(e0, e1), 1e-4f);
    CHECK_LT(input::turned(held, cam(app)), 1e-4f);

    // ── Shift is faster, and the wheel scales the speed ──────────────
    const auto s0 = cam(app);
    hold(app, Key::W, 5);
    const auto s1 = cam(app);
    input::key(app, Key::LeftShift, true);
    hold(app, Key::W, 5);
    input::key(app, Key::LeftShift, false);
    const auto s2 = cam(app);
    const float plain = input::moved(s0, s1), fast = input::moved(s1, s2);
    std::printf("  5 frames of W: %.3f plain, %.3f with shift\n", plain, fast);
    CHECK_GT(fast, plain * 2.0f);
    probe::fly_wheel(app.Raw(), 4.0f);
    hold(app, Key::W, 5);
    const auto s3 = cam(app);
    std::printf("  after wheel +4: %.3f\n", input::moved(s2, s3));
    CHECK_GT(input::moved(s2, s3), plain * 1.5f);

    // ── Escape releases; a key held INTO the release moves nothing ───
    input::key(app, Key::W, true);
    input::tap(app, Key::Escape);
    CHECK(!probe::flying(app.Raw(), nullptr));
    CHECK_EQ(escapes, 0); // the flight's Escape, not the sim's
    const auto rest = cam(app);
    for (int f = 0; f < 5; ++f)
        app.Step();
    std::printf("  after release, W still down: moved %.6f\n",
                input::moved(rest, cam(app)));
    CHECK_LT(input::moved(rest, cam(app)), 1e-6f);
    input::key(app, Key::W, false);
    CHECK_EQ(ws, 0);

    // ── out of flight the keys are the sim's again ───────────────────
    input::tap(app, Key::W);
    CHECK_EQ(ws, 1);
    input::tap(app, Key::Escape);
    CHECK_EQ(escapes, 1);

    // ── a flight forgets the keys it starts with: a release that went
    // to another window, before or during a flight, cannot move it ───
    input::key(app, Key::W, true); // out of flight: the sim's, and held
    CHECK_EQ(ws, 2);
    input::tap(app, Key::Tab); // the release never arrived
    const auto again = cam(app);
    for (int f = 0; f < 5; ++f)
        app.Step();
    std::printf("  took off with a lost release: moved %.6f\n",
                input::moved(again, cam(app)));
    CHECK_LT(input::moved(again, cam(app)), 1e-6f);
    input::key(app, Key::W, true); // in flight this time
    input::tap(app, Key::Escape);
    input::tap(app, Key::Tab);
    const auto twice = cam(app);
    for (int f = 0; f < 5; ++f)
        app.Step();
    CHECK_LT(input::moved(twice, cam(app)), 1e-6f);
    input::tap(app, Key::Escape);
    input::key(app, Key::W, false);

    // ── a panel world: Tab captures the one under the pointer ────────
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
    CHECK(probe::flying(app.Raw(), "side"));
    CHECK(!probe::flying(app.Raw(), nullptr));
    const auto side_before = cam(app, "side");
    const auto main_before = cam(app);
    input::look(app, 60.0f, 0.0f);
    std::printf("  look in the panel's flight: it turned %.2f deg, the "
                "window's %.5f\n",
                input::turned(side_before, cam(app, "side")),
                input::turned(main_before, cam(app)));
    CHECK_GT(input::turned(side_before, cam(app, "side")), 5.0f);
    CHECK_LT(input::turned(main_before, cam(app)), 1e-4f);
    input::tap(app, Key::Escape);
    CHECK(!probe::flying(app.Raw(), "side"));

    // ── a press latched before the flight does not orbit under it ────
    input::move(app, 640.0f, 480.0f);
    input::press(app);
    input::tap(app, Key::Tab);
    CHECK(probe::flying(app.Raw(), "side"));
    const auto latched = cam(app, "side");
    eye(latched, e0);
    probe::mouse_move(app.Raw(), 700.0f, 500.0f);
    app.Step();
    app.Step();
    eye(cam(app, "side"), e1);
    std::printf("  latched press, then flight, then motion: eye moved %.5f\n",
                dist(e0, e1));
    CHECK_LT(dist(e0, e1), 1e-4f);
    input::release(app);
    input::tap(app, Key::Escape);

    // ── over a plain panel, Tab falls back to the window's world ─────
    app.Panel("controls");
    app.OnUi([] {
        ImGui::SetWindowPos("controls", ImVec2(0, 0));
        ImGui::SetWindowSize("controls", ImVec2(200, 160));
    });
    app.Step();
    app.Step();
    input::move(app, 60.0f, 60.0f);
    input::tap(app, Key::Tab);
    CHECK(probe::flying(app.Raw(), nullptr));
    CHECK(!probe::flying(app.Raw(), "side"));
    input::tap(app, Key::Escape);

    return check::summary("fly");
}
