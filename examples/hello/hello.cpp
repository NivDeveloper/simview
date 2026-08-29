// The founding consumer: core surface only, no window, buildable
// anywhere. On a driverless machine the App is simply false — the
// library has already said why on its own log.
#include <simview/simview.h>

int main() {
    sv::App app({.title = "hello", .headless = true});
    if (!app)
        return 0;

    app.OnFrame([] {});
    app.Step();
    return 0;
}
