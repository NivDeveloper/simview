// The sync layer's contract, device-free: the Executor state machine
// and the Channel's generation/tearing guarantees.
#include <simview/Sync.h>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace sv;
using namespace std::chrono_literals;

int main() {
    // Unbuffered: a test killed by a timeout must still have
    // said how far it got.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // Starts Paused: no ticks arrive unbidden.
    Executor ex([] {});
    std::this_thread::sleep_for(50ms);
    if (ex.Ticks() != 0)
        return std::printf("FAIL: ticked while paused\n"), 1;

    // Step is exactly one tick, then Paused again.
    ex.Step();
    std::this_thread::sleep_for(50ms);
    if (ex.Ticks() != 1)
        return std::printf("FAIL: step != 1 tick\n"), 1;
    if (ex.Playing())
        return std::printf("FAIL: playing after step\n"), 1;

    // Play advances; pause stops.
    ex.Play();
    std::this_thread::sleep_for(50ms);
    ex.Pause();
    const auto t1 = ex.Ticks();
    if (t1 < 2)
        return std::printf("FAIL: play did not advance\n"), 1;
    std::this_thread::sleep_for(50ms);
    if (ex.Ticks() != t1)
        return std::printf("FAIL: ticked after pause\n"), 1;

    // The channel: generations strictly increase; no torn reads — the
    // writer fills the whole slab with one value per generation at
    // full speed, the reader must only ever see uniform slabs.
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
    if (torn)
        return std::printf("FAIL: torn read / gen regression\n"), 1;
    if (worst_gen < 100)
        return std::printf("FAIL: writer too slow (%llu gens)\n",
                           (unsigned long long)worst_gen),
               1;

    std::printf("PASS: sync checks (%llu generations, no tears)\n",
                (unsigned long long)worst_gen);
    return 0;
}
