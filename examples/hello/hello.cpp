// The founding consumer: core surface only, headless, CI-runnable
// anywhere. "No device" on a driverless runner is a REPORTED outcome,
// not a failure — the build and the surface are what this proves.
#include <simview/simview.h>

#include <cstdio>

int main() {
    std::printf("simview %s\n", simview::version());
    auto app = simview::init({.title = "hello", .headless = true});
    if (!app) {
        std::printf("no GPU device on this machine: %s\n",
                    simview::last_error());
        return 0;
    }
    bool fired = false;
    app.on_frame([&] { fired = true; });
    app.run(); // Move 1: drives zero frames
    std::printf("device open, run() returned, frame fired: %s\n",
                fired ? "yes" : "not yet (Move 2)");
    return 0;
}
