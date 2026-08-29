// The headless loop contract: step() fires frame callbacks in
// registration order; quit is idempotent; no-device exits as a SKIP.
#include <simview/simview.h>

#include <cstdio>
#include <vector>

int main() {
    using namespace simview;
    auto app = init({.headless = true});
    if (!app)
        return std::printf("SKIP: no GPU device (%s)\n", last_error()), 0;

    std::vector<int> order;
    app.on_frame([&] { order.push_back(1); });
    app.on_frame([&] { order.push_back(2); });
    app.step();
    app.step();
    if (order != std::vector<int>{1, 2, 1, 2})
        return std::printf("FAIL: callback order\n"), 1;

    app.request_quit();
    app.request_quit(); // idempotent
    app.run();          // headless: returns immediately, logged

    std::printf("PASS: loop checks\n");
    return 0;
}
