// The sync layer's contract, device-free: the Executor state machine
// and the Channel's generation/tearing guarantees.
#include <simview/Sync.h>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace simview;
using namespace std::chrono_literals;

int main() {
    // Starts Paused: no ticks arrive unbidden.
    ExecutorHandle ex([] {});
    std::this_thread::sleep_for(50ms);
    if (ex.ticks() != 0) return std::printf("FAIL: ticked while paused\n"), 1;

    // Step is exactly one tick, then Paused again.
    ex.step();
    std::this_thread::sleep_for(50ms);
    if (ex.ticks() != 1) return std::printf("FAIL: step != 1 tick\n"), 1;
    if (ex.playing()) return std::printf("FAIL: playing after step\n"), 1;

    // Play advances; pause stops.
    ex.play();
    std::this_thread::sleep_for(50ms);
    ex.pause();
    const auto t1 = ex.ticks();
    if (t1 < 2) return std::printf("FAIL: play did not advance\n"), 1;
    std::this_thread::sleep_for(50ms);
    if (ex.ticks() != t1) return std::printf("FAIL: ticked after pause\n"), 1;

    // The channel: generations strictly increase; no torn reads — the
    // writer fills the whole slab with one value per generation at
    // full speed, the reader must only ever see uniform slabs.
    constexpr std::size_t N = 4096;
    HostChannel<float> ch(N);
    std::uint64_t worst_gen = 0;
    bool torn = false;
    ExecutorHandle writer([&] {
        static float v = 0;
        v += 1.0f;
        auto s = ch.state();
        for (auto &x : s) x = v;
        ch.publish();
    });
    writer.play();
    const auto until = std::chrono::steady_clock::now() + 1s;
    std::uint64_t last = 0;
    while (std::chrono::steady_clock::now() < until) {
        std::uint64_t g = 0;
        auto s = ch.latest(g);
        if (!s.empty()) {
            if (g < last) { torn = true; break; }
            last = g;
            for (auto x : s)
                if (x != s[0]) { torn = true; break; }
        }
        worst_gen = last;
        if (torn) break;
    }
    writer.pause();
    if (torn) return std::printf("FAIL: torn read / gen regression\n"), 1;
    if (worst_gen < 100)
        return std::printf("FAIL: writer too slow (%llu gens)\n",
                           (unsigned long long)worst_gen), 1;

    std::printf("PASS: sync checks (%llu generations, no tears)\n",
                (unsigned long long)worst_gen);
    return 0;
}
