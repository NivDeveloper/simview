// A gamepad steers a world without a flight: it has no hotkeys to
// collide with and no pointer to hide, so nothing has to be captured
// for it. Left stick walks, right stick looks, triggers lift.
//
// Headless, and through the dead zone a device goes through: a check
// that wrote normalized axes would pass with the zone missing, and a
// stick at rest never reads exactly zero.

#include "harness/Harness.h"
#include "harness/Input.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <imgui.h>

#include <cmath>
#include <cstdint>
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

// Hold the axes for some frames, then let go of everything.
void hold(sv::App &app, std::int16_t lx, std::int16_t ly, std::int16_t rx,
          std::int16_t ry, std::int16_t lt, std::int16_t rt, int frames) {
    const std::int16_t raw[6] = {lx, ly, rx, ry, lt, rt};
    sv::probe::gamepad(app.Raw(), raw);
    for (int f = 0; f < frames; ++f)
        app.Step();
    const std::int16_t rest[6] = {0, 0, 0, 0, 0, 0};
    sv::probe::gamepad(app.Raw(), rest);
    app.Step();
}

constexpr std::int16_t kFull = 32767;

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {800, 600}, .headless = true});
    if (!app)
        return check::skip("pad", LastError());

    sv::World w = app.World();
    REQUIRE(bool(w));
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 5.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
    app.Step();

    // ── the left stick walks, and no flight is begun for it ──────────
    const auto start = cam(app);
    hold(app, 0, -kFull, 0, 0, 0, 0, 10);
    const auto ahead = cam(app);
    float e0[3], e1[3];
    eye(start, e0);
    eye(ahead, e1);
    float along = 0.0f;
    for (int i = 0; i < 3; ++i)
        along += (ahead.focus[i] - start.focus[i]) * start.forward[i];
    std::printf("  stick forward, 10 frames: %.3f ahead, eye moved %.3f, "
                "turned %.4f\n",
                along, dist(e0, e1), input::turned(start, ahead));
    CHECK_GT(along, 0.1f);
    CHECK_LT(std::fabs(dist(e0, e1) - along), 1e-3f);
    CHECK_LT(std::fabs(ahead.distance - start.distance), 1e-4f);
    CHECK_LT(input::turned(start, ahead), 1e-3f);
    CHECK(!probe::flying(app.Raw(), nullptr));

    // ── inside the dead zone nothing moves ───────────────────────────
    const auto rest = cam(app);
    hold(app, 3000, -5000, 2500, -4000, 0, 0, 10);
    std::printf("  inside the dead zone: moved %.6f, turned %.6f\n",
                input::moved(rest, cam(app)), input::turned(rest, cam(app)));
    CHECK_LT(input::moved(rest, cam(app)), 1e-6f);
    CHECK_LT(input::turned(rest, cam(app)), 1e-6f);

    // ── the right stick looks about the EYE, and stays level ─────────
    const auto before = cam(app);
    hold(app, 0, 0, kFull, 0, 0, 0, 5);
    const auto yawed = cam(app);
    eye(before, e0);
    eye(yawed, e1);
    std::printf("  stick right, 5 frames: turned %.2f deg, eye moved %.5f\n",
                input::turned(before, yawed), dist(e0, e1));
    CHECK_GT(input::turned(before, yawed), 5.0f);
    CHECK_LT(dist(e0, e1), 1e-3f);
    CHECK_LT(std::fabs(yawed.forward[2] - before.forward[2]), 1e-4f);
    CHECK_GT(yawed.up[2], 0.9f);
    hold(app, 0, 0, 0, kFull, 0, 0, 5);
    CHECK_GT(std::fabs(cam(app).forward[2] - yawed.forward[2]), 0.05f);

    // ── the triggers lift along WORLD up ─────────────────────────────
    const auto low = cam(app);
    hold(app, 0, 0, 0, 0, 0, kFull, 5);
    const auto high = cam(app);
    const float drift =
        std::hypot(high.focus[0] - low.focus[0], high.focus[1] - low.focus[1]);
    std::printf("  right trigger, 5 frames: rose %.3f, drifted %.5f in xy\n",
                high.focus[2] - low.focus[2], drift);
    CHECK_GT(high.focus[2] - low.focus[2], 0.05f);
    CHECK_LT(drift, 1e-4f);
    hold(app, 0, 0, 0, 0, kFull, 0, 5);
    CHECK_LT(cam(app).focus[2], high.focus[2] - 0.05f);

    // ── released, nothing drifts ─────────────────────────────────────
    const auto still = cam(app);
    for (int f = 0; f < 5; ++f)
        app.Step();
    CHECK_LT(input::moved(still, cam(app)), 1e-6f);
    CHECK_LT(input::turned(still, cam(app)), 1e-6f);

    // ── in flight the pad still steers, and a key on top of a stick
    // is still one full deflection ───────────────────────────────────
    input::tap(app, Key::Tab);
    REQUIRE(probe::flying(app.Raw(), nullptr));
    const auto flown = cam(app);
    hold(app, 0, -kFull, 0, 0, 0, 0, 5);
    const float stick_only = input::moved(flown, cam(app));
    // Measured over frames where BOTH are held, and only those.
    input::key(app, Key::W, true);
    const std::int16_t fwd[6] = {0, -kFull, 0, 0, 0, 0};
    probe::gamepad(app.Raw(), fwd);
    const auto both_start = cam(app);
    for (int f = 0; f < 5; ++f)
        app.Step();
    const float both = input::moved(both_start, cam(app));
    const std::int16_t off[6] = {0, 0, 0, 0, 0, 0};
    probe::gamepad(app.Raw(), off);
    input::key(app, Key::W, false);
    std::printf("  stick forward in flight: %.3f alone, %.3f with W held\n",
                stick_only, both);
    CHECK_GT(stick_only, 0.1f);
    CHECK_LT(std::fabs(both - stick_only), 1e-3f);

    // ── the left stick pressed is the pad's Shift ────────────────────
    const auto walk = cam(app);
    probe::gamepad_buttons(app.Raw(), true, false);
    hold(app, 0, -kFull, 0, 0, 0, 0, 5);
    probe::gamepad_buttons(app.Raw(), false, false);
    const float sprint = input::moved(walk, cam(app));
    std::printf("  with L3 held: %.3f\n", sprint);
    CHECK_GT(sprint, stick_only * 2.0f);

    // ── B is the pad's Escape: its EDGE ends the flight, holding it
    // does not keep ending things ────────────────────────────────────
    probe::gamepad_buttons(app.Raw(), false, true);
    app.Step();
    CHECK(!probe::flying(app.Raw(), nullptr));
    input::tap(app, Key::Tab);
    // A device reports a held button every frame; so does this.
    probe::gamepad_buttons(app.Raw(), false, true);
    app.Step();
    CHECK(probe::flying(app.Raw(), nullptr)); // B still down, no edge
    probe::gamepad_buttons(app.Raw(), false, false);
    input::tap(app, Key::Escape);

    // ── a panel world under the pointer takes the pad ────────────────
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
    const auto side_before = cam(app, "side");
    const auto main_before = cam(app);
    hold(app, 0, -kFull, 0, 0, 0, 0, 5);
    std::printf("  stick over the panel: it moved %.3f, the window's %.5f\n",
                input::moved(side_before, cam(app, "side")),
                input::moved(main_before, cam(app)));
    CHECK_GT(input::moved(side_before, cam(app, "side")), 0.1f);
    CHECK_LT(input::moved(main_before, cam(app)), 1e-4f);

    return check::summary("pad");
}
