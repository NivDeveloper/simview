// The headless loop contract: step() fires frame callbacks in
// registration order; quit is idempotent; no-device exits as a SKIP.
#include <simview/simview.h>

#include <cstdio>
#include <vector>

int main() {
    using namespace sv;
    App app({.headless = true});
    if (!app)
        return std::printf("SKIP: no GPU device (%s)\n", LastError()), 0;

    std::vector<int> order;
    app.OnFrame([&] { order.push_back(1); });
    app.OnFrame([&] { order.push_back(2); });
    app.Step();
    app.Step();
    if (order != std::vector<int>{1, 2, 1, 2})
        return std::printf("FAIL: callback order\n"), 1;

    app.RequestQuit();
    app.RequestQuit(); // idempotent
    app.Run();         // headless: returns immediately, logged

    std::printf("PASS: loop checks\n");
    return 0;
}
