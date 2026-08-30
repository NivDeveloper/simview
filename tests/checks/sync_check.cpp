// The sync layer's contract, device-free: the Executor state machine
// and the Channel's generation and tearing guarantees.
#include "harness/Check.h"

#include <simview/sync/Sync.h>

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

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

    // The Executor keeps the CLOCK. A timed body reads n and time; it
    // never fabricates its own counter. Three Advance(1)s see n = 0,
    // 1, 2 — the value BEFORE each tick.
    std::vector<std::uint64_t> seen;
    Executor timed([&](const Tick &t) { seen.push_back(t.n); });
    timed.SetDt(0.5);
    for (int i = 0; i < 3; ++i) {
        timed.Advance(1);
        std::this_thread::sleep_for(30ms);
    }
    CHECK_EQ(seen.size(), std::size_t(3));
    if (seen.size() == 3) {
        CHECK_EQ(seen[0], std::uint64_t(0));
        CHECK_EQ(seen[2], std::uint64_t(2));
    }
    CHECK_EQ(timed.Now().n, std::uint64_t(3));
    CHECK_EQ(timed.Now().time, 1.5); // n * dt, exactly representable

    // Advance(N) from Paused ends Paused with EXACTLY N more ticks: the
    // worker pauses itself at the target. An uncapped body would
    // overshoot by thousands if the check were "when the caller
    // notices", which is the bug this pins.
    const auto before = timed.Now().n;
    timed.Advance(50);
    std::this_thread::sleep_for(100ms);
    CHECK_EQ(timed.Now().n, before + 50);
    CHECK(!timed.Playing());

    // Restart runs its callback on the WORKER thread, between ticks,
    // and zeros the clock. Restarted() reports it once, then not.
    std::thread::id restart_thread;
    timed.OnRestart([&] { restart_thread = std::this_thread::get_id(); });
    timed.Restart();
    std::this_thread::sleep_for(30ms);
    CHECK(restart_thread != std::thread::id{});
    CHECK(restart_thread != std::this_thread::get_id());
    CHECK_EQ(timed.Now().n, std::uint64_t(0));
    CHECK_EQ(timed.Now().time, 0.0);
    CHECK(timed.Restarted());
    CHECK(!timed.Restarted());

    // Rate is achieved ticks per second, MEASURED. What can be pinned
    // on any machine: a paced executor reads positive and never above
    // the theoretical ceiling of 1/delay (sleep_for is a minimum, so a
    // loaded CI runner may oversleep badly — a first version asked for
    // ~100/s under a 10 ms delay and a macOS runner delivered 23). And
    // once paused, the window drains to zero.
    Executor paced([] {});
    paced.SetDelayNs(10'000'000); // 10 ms -> at most 100/s
    paced.Play();
    std::this_thread::sleep_for(600ms);
    const double r = paced.Rate();
    paced.Pause();
    CHECK_GT(r, 0.0);
    CHECK_LT(r, 110.0);
    std::this_thread::sleep_for(1100ms); // past the one-second window
    CHECK_EQ(paced.Rate(), 0.0);

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
