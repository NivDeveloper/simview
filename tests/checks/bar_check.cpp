// The bar on the picture says what the device in hand does under the
// camera, the pointer style and the mode that are on — and only that.
// One line per state, read as text, and a shot of each so the drawing
// can be looked at: orbit under the cursor, in a mode, under a
// crosshair, in flight, and each with a pad in hand.

#include "harness/Harness.h"
#include "harness/Input.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string bar(sv::App &app) {
    char line[512];
    sv::probe::world_legend(app.Raw(), nullptr, line, sizeof line);
    return line;
}

bool says(sv::App &app, const char *what) {
    const std::string b = bar(app);
    const bool yes = b.find(what) != std::string::npos;
    if (!yes)
        std::printf("  (no \"%s\" in: %s)\n", what, b.c_str());
    return yes;
}

bool silent(sv::App &app, const char *what) {
    const std::string b = bar(app);
    const bool no = b.find(what) == std::string::npos;
    if (!no)
        std::printf("  (\"%s\" in: %s)\n", what, b.c_str());
    return no;
}

void shot(sv::App &app, const char *name) {
    app.Step();
    Bmp img;
    CHECK(harness::shot(app, name, img));
    std::printf("  %-16s %s\n", name, bar(app).c_str());
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {900, 200}, .headless = true});
    if (!app)
        return check::skip("bar", LastError());
    sv::World w = app.World({.grid = false, .axes = false});
    REQUIRE(bool(w));
    app.Bind({.id = "pause",
              .label = "pause",
              .controls = {Ctl(Key::Space), Ctl(Pad::Start)}},
             [] {});
    sv::Mode cut =
        app.Mode({.name = "cut", .enter = {Ctl(Key::N2), Ctl(Pad::X)}});
    cut.OnStroke([](const Stroke &) {});
    app.Step();

    // ── orbit under the cursor: the drag orbits, nothing about picking
    shot(app, "bar_orbit");
    CHECK(says(app, "2 cut"));
    CHECK(says(app, "Space pause"));
    CHECK(says(app, "Tab fly"));
    CHECK(says(app, "F1 settings"));
    CHECK(says(app, "  drag orbit"));
    CHECK(says(app, "wheel zoom"));
    CHECK(silent(app, "pick"));
    CHECK(silent(app, "frame"));
    CHECK(silent(app, "right-drag"));

    // ── the pad in hand: RT with the stick orbits, the bumpers zoom ──
    input::stick(app, Pad::RS, 0.3f, 0.0f);
    app.Step();
    input::stick(app, Pad::RS, 0.0f, 0.0f);
    shot(app, "bar_orbit_pad");
    CHECK(says(app, "RT + RS orbit"));
    CHECK(says(app, "LS pan"));
    CHECK(says(app, "LB RB zoom"));
    CHECK(says(app, "Start pause"));
    CHECK(says(app, "X cut"));
    CHECK(silent(app, "A "));

    // ── in a mode: the stroke first, the other button orbits ─────────
    input::tap(app, Key::N2);
    shot(app, "bar_cut");
    CHECK(says(app, "[2 cut]"));
    CHECK(says(app, "  drag cut"));
    CHECK(says(app, "right-drag orbit"));
    CHECK(says(app, "Esc exit"));
    CHECK(silent(app, "  drag orbit"));
    input::stick(app, Pad::RS, 0.3f, 0.0f);
    app.Step();
    input::stick(app, Pad::RS, 0.0f, 0.0f);
    shot(app, "bar_cut_pad");
    CHECK(says(app, "RT + RS cut"));
    CHECK(says(app, "LT + RS orbit"));
    CHECK(says(app, "B exit"));
    input::tap(app, Key::Escape);

    // ── under a crosshair: the mouse looks, Esc is the way out ───────
    input::key(app, Key::LeftCtrl, true);
    input::tap(app, Key::Tab);
    input::key(app, Key::LeftCtrl, false);
    shot(app, "bar_crosshair");
    CHECK(says(app, "mouse orbit"));
    CHECK(says(app, "W A S D pan"));
    CHECK(says(app, "Ctrl Tab cursor"));
    CHECK(silent(app, "drag"));

    // ── in flight ────────────────────────────────────────────────────
    input::tap(app, Key::Tab);
    shot(app, "bar_fly");
    CHECK(says(app, "mouse look"));
    CHECK(says(app, "W A S D move"));
    CHECK(says(app, "Q E down / up"));
    CHECK(says(app, "Shift faster"));
    CHECK(says(app, "wheel speed"));
    CHECK(says(app, "Tab orbit"));
    input::stick(app, Pad::RS, 0.3f, 0.0f);
    app.Step();
    input::stick(app, Pad::RS, 0.0f, 0.0f);
    shot(app, "bar_fly_pad");
    CHECK(says(app, "RS look"));
    CHECK(says(app, "LS move"));
    CHECK(says(app, "LB RB down / up"));
    CHECK(says(app, "L3 faster"));
    CHECK(says(app, "D-pad speed"));

    return check::summary("bar");
}
