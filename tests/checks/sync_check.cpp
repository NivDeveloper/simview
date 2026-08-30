// The sync layer's contract, device-free: the Executor state machine
// and the Channel's generation and tearing guarantees.
#include "harness/Check.h"

#include <simview/Sync.h>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace sv;
using namespace std::chrono_literals;

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // Starts Paused: no ticks arrive unbidden.
    Executor ex([] {});
    std::this_thread::sleep_for(50ms);
    CHECK_EQ(ex.Ticks(), std::uint64_t(0));

    // Step is exactly one tick, then Paused again.
    ex.Step();
    std::this_thread::sleep_for(50ms);
    CHECK_EQ(ex.Ticks(), std::uint64_t(1));
    CHECK(!ex.Playing());

    // Play advances; Pause stops. Pause cannot abort a tick already
    // running — a user's callback is not interruptible — so ONE more
    // may land after it returns. What the contract promises is that no
    // NEW tick starts, which is what settling first measures. An
    // uncapped executor reaches millions of ticks a second, so that
    // window is wide on a loaded machine.
    ex.Play();
    std::this_thread::sleep_for(50ms);
    ex.Pause();
    std::this_thread::sleep_for(20ms);
    const auto t1 = ex.Ticks();
    CHECK_GT(t1, std::uint64_t(1));
    std::this_thread::sleep_for(50ms);
    CHECK_EQ(ex.Ticks(), t1);

    // The channel: generations never go backwards and a reader never
    // sees a half-written slab — the writer fills the whole slab with
    // one value per generation at full speed.
    constexpr std::size_t N = 4096;
    Channel<float> ch(N);
    std::uint64_t worst_gen = 0;
    bool torn = false;
    Executor writer([&] {
        static float v = 0;
        v += 1.0f;
        auto s = ch.State();
        for (auto &x : s)
            x = v;
        ch.Publish();
    });
    writer.Play();
    const auto until = std::chrono::steady_clock::now() + 1s;
    std::uint64_t last = 0;
    while (std::chrono::steady_clock::now() < until) {
        std::uint64_t g = 0;
        auto s = ch.Latest(g);
        if (!s.empty()) {
            if (g < last) {
                torn = true;
                break;
            }
            last = g;
            for (auto x : s)
                if (x != s[0]) {
                    torn = true;
                    break;
                }
        }
        worst_gen = last;
        if (torn)
            break;
    }
    writer.Pause();
    CHECK(!torn);
    CHECK_GT(worst_gen, std::uint64_t(100));

    std::printf("(%llu generations)\n", (unsigned long long)worst_gen);
    return check::summary("sync");
}
