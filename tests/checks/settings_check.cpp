// Bindings are data, so a row can be rebound from code, from a file,
// and from a settings cell that captures a press — the file over the
// code over the engine's defaults — and a check reads the same table
// the page shows. What this proves: a Rebind takes only the hand it
// names; a loaded line beats it; the text round-trips; a cell captures
// a button, a trigger, a pair for an axis, and a modifier held; Esc
// cancels and Backspace unbinds; a captured control does nothing else
// that frame; two rows on one control are a conflict; an unknown line
// survives; F1 opens the page.

#include "harness/Harness.h"
#include "harness/Input.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string text(sv::App &app) {
    char buf[4096];
    sv::probe::bindings_text(app.Raw(), buf, sizeof buf);
    return buf;
}

bool says(sv::App &app, const char *line) {
    const std::string t = text(app);
    const bool yes = t.find(line) != std::string::npos;
    if (!yes)
        std::printf("  (the file does not say \"%s\":\n%s)\n", line, t.c_str());
    return yes;
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {800, 600}, .headless = true});
    if (!app)
        return check::skip("settings", LastError());
    sv::World w = app.World();
    REQUIRE(bool(w));
    int pauses = 0, others = 0;
    app.Bind({.id = "pause",
              .label = "pause",
              .controls = {Ctl(Key::Space), Ctl(Pad::Start)}},
             [&] { ++pauses; })
        .Bind({.id = "other", .label = "other", .controls = {Ctl(Key::O)}},
              [&] { ++others; });
    app.Step();

    // ── code beats the defaults, on the hand it names ────────────────
    app.Rebind("base.pause", {Ctl(Key::P)});
    input::tap(app, Key::Space);
    CHECK_EQ(pauses, 0);
    input::tap(app, Key::P);
    CHECK_EQ(pauses, 1);
    input::pad_tap(app, Pad::Start);
    CHECK_EQ(pauses, 2);
    CHECK(text(app).find("base.pause") == std::string::npos); // no file layer

    // ── a loaded line beats the code, and round-trips ────────────────
    probe::bindings_load(app.Raw(), "base.pause = Key.Return | -\n");
    input::tap(app, Key::P);
    CHECK_EQ(pauses, 2);
    input::tap(app, Key::Return);
    CHECK_EQ(pauses, 3);
    input::pad_tap(app, Pad::Start);
    CHECK_EQ(pauses, 4);
    CHECK(says(app, "base.pause = Key.Return | -"));

    // ── a cell captures a button, and the press does nothing else ────
    probe::settings_open(app.Raw(), true);
    app.Step();
    REQUIRE(
        probe::settings_capture(app.Raw(), "base", "pause", int(Device::Pad)));
    CHECK(probe::settings_capturing(app.Raw()));
    input::pad_tap(app, Pad::Y);
    CHECK(!probe::settings_capturing(app.Raw()));
    CHECK_EQ(pauses, 4);
    CHECK(says(app, "base.pause = Key.Return | Pad.Y"));
    input::pad_tap(app, Pad::Y);
    CHECK_EQ(pauses, 5);
    input::pad_tap(app, Pad::Start);
    CHECK_EQ(pauses, 5);

    // ── a trigger on an axis row, and a pair of keys ─────────────────
    REQUIRE(probe::settings_capture(app.Raw(), "orbit", "camera.depth",
                                    int(Device::Pad)));
    input::stick(app, Pad::RT, 1.0f);
    app.Step();
    input::stick(app, Pad::RT, 0.0f);
    app.Step();
    CHECK(!probe::settings_capturing(app.Raw()));
    CHECK(says(app, "orbit.camera.depth = - | Pad.RT"));
    REQUIRE(probe::settings_capture(app.Raw(), "fly", "camera.depth",
                                    int(Device::Keyboard)));
    input::tap(app, Key::J);
    CHECK(probe::settings_capturing(app.Raw()));
    input::tap(app, Key::K);
    CHECK(!probe::settings_capturing(app.Raw()));
    CHECK(says(app, "fly.camera.depth = Axis(Key.J,Key.K) | -"));

    // ── a modifier held wraps the capture ────────────────────────────
    REQUIRE(probe::settings_capture(app.Raw(), "base", "other",
                                    int(Device::Keyboard)));
    input::key(app, Key::LeftShift, true);
    input::tap(app, Key::G);
    input::key(app, Key::LeftShift, false);
    CHECK(says(app, "base.other = Shift+Key.G | -"));
    input::tap(app, Key::G);
    CHECK_EQ(others, 0);
    input::key(app, Key::LeftShift, true);
    input::tap(app, Key::G);
    input::key(app, Key::LeftShift, false);
    CHECK_EQ(others, 1);

    // ── Esc cancels, Backspace unbinds ───────────────────────────────
    REQUIRE(probe::settings_capture(app.Raw(), "base", "other",
                                    int(Device::Keyboard)));
    input::tap(app, Key::Escape);
    CHECK(!probe::settings_capturing(app.Raw()));
    CHECK(says(app, "base.other = Shift+Key.G | -"));
    REQUIRE(probe::settings_capture(app.Raw(), "base", "other",
                                    int(Device::Keyboard)));
    input::tap(app, Key::Backspace);
    CHECK(!probe::settings_capturing(app.Raw()));
    CHECK(says(app, "base.other = none | -"));
    input::key(app, Key::LeftShift, true);
    input::tap(app, Key::G);
    input::key(app, Key::LeftShift, false);
    CHECK_EQ(others, 1);

    // ── two rows on one control clash; an unknown line survives ──────
    CHECK_EQ(probe::bindings_conflicts(app.Raw()), std::size_t(0));
    probe::bindings_load(app.Raw(), "base.other = Key.Return\n");
    CHECK_EQ(probe::bindings_conflicts(app.Raw()), std::size_t(1));
    probe::bindings_load(app.Raw(), "future.thing = Key.Z\n");
    CHECK(says(app, "future.thing = Key.Z"));
    std::printf("%s", text(app).c_str());

    // ── F1 opens the page and closes it; the page draws ──────────────
    probe::settings_open(app.Raw(), false);
    input::tap(app, Key::F1);
    CHECK(probe::settings_showing(app.Raw()));
    app.Step();
    Bmp page;
    REQUIRE(harness::shot(app, "settings_page", page));
    input::tap(app, Key::F1);
    CHECK(!probe::settings_showing(app.Raw()));

    return check::summary("settings");
}
