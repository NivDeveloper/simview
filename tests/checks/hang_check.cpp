// A hang is a report. A frame waiting on a value nothing will ever
// signal — the pump deleted, a stamp past what compute will reach —
// froze the process before, and no gate can report a freeze. Under
// SIMVIEW_WAIT_MS the wait trips, the sentence names what it waited
// for and where both timelines stood, and the process exits; ctest
// passes this check on the SENTENCE (PASS_REGULAR_EXPRESSION), never
// on an exit code. With the environment unset this check hangs — the
// "before", run once by hand — except on MoltenVK, where Metal's
// watchdog turns the wait into VK_ERROR_DEVICE_LOST after ~8 s and
// the device-lost sentence is what fires.

#include "harness/Harness.h"
#include "probe/Probe.h"

#include <cstdio>

int main() {
    harness::begin();
    using namespace sv;

    App app({.headless = true});
    if (!app)
        return check::skip("hang", LastError());

    // One ordinary shot first: the stall must be the ONLY reason the
    // next one does not come back.
    Bmp img;
    CHECK(harness::shot(app, "hang_before", img));

    probe::stall_frame(app.Raw());
    std::printf("stalled: the next shot waits on a value nothing will "
                "signal\n");
    app.Shot(harness::tmp_path("hang_after.bmp").c_str());

    // Reached only when no bound tripped — either the environment was
    // unset (then this line is never reached: the shot hangs) or the
    // bound is broken.
    std::printf("FAILED: hang (the stalled shot returned)\n");
    return 1;
}
