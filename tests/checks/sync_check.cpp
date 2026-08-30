// The sync layer's contract, device-free: the Executor state machine
// and the Sync gate's role, generation and tearing guarantees. The
// device rotation runs on gpud's mock — a test may speak gpud.
#include "harness/Check.h"

#include <simview/gpud.h>
#include <simview/sync/Sync.h>

#include <gpud/Mock.h>

#include <chrono>
#include <cstddef>
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
    // Now() first: it takes the Executor's mutex, which the worker
    // released AFTER its push_back — the edge that makes reading the
    // vector here a read, not a race (TSan named it, weekly run 33326780387).
    CHECK_EQ(timed.Now().n, std::uint64_t(3));
    CHECK_EQ(timed.Now().time, 1.5); // n * dt, exactly representable
    CHECK_EQ(seen.size(), std::size_t(3));
    if (seen.size() == 3) {
        CHECK_EQ(seen[0], std::uint64_t(0));
        CHECK_EQ(seen[2], std::uint64_t(2));
    }

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
    CHECK_EQ(timed.Now().n, std::uint64_t(0)); // the mutex, before the id
    CHECK_EQ(timed.Now().time, 0.0);
    CHECK(restart_thread != std::thread::id{});
    CHECK(restart_thread != std::this_thread::get_id());
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

    // The gate's roles: next is never current or shown, and shown moves
    // only at a flip, onto what current was — ten thousand random
    // interleavings of publish and flip, on one thread.
    {
        impl::SyncGate g = impl::sync_gate_create();
        std::uint32_t seed = 12345;
        bool bad = false;
        for (int i = 0; i < 10000 && !bad; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const int was_current = impl::sync_gate_current(g);
            const int was_shown = impl::sync_gate_shown(g);
            const bool flip = seed & 0x10000;
            if (flip)
                impl::sync_gate_flip(g);
            else
                impl::sync_gate_publish(g);
            const int n = impl::sync_gate_next(g);
            const int c = impl::sync_gate_current(g);
            const int sh = impl::sync_gate_shown(g);
            if (n == c || n == sh)
                bad = true;
            if (flip ? (sh != was_shown && sh != was_current) : sh != was_shown)
                bad = true;
        }
        CHECK(!bad);
        impl::sync_gate_destroy(g);
    }

    // Tearing: the writer fills Next() with one value per generation at
    // full speed and publishes; the reader flips, as a frame does, and
    // Shown() is never half-written, generations never go backwards.
    // Loop until a hundred generations under a deadline — a fixed count
    // would finish before the worker's first tick.
    constexpr std::size_t N = 4096;
    Sync<std::vector<float>> ch{std::vector<float>(N)};
    Executor writer([&] {
        static float v = 0;
        v += 1.0f;
        for (auto &x : ch.Next())
            x = v;
        ch.Publish();
    });
    writer.Play();
    auto deadline = std::chrono::steady_clock::now() + 5s;
    std::uint64_t last = 0;
    bool torn = false;
    while (last < 100 && std::chrono::steady_clock::now() < deadline) {
        impl::sync_gate_flip(ch.Gate());
        const std::uint64_t g = ch.Generation();
        if (g < last)
            torn = true;
        last = g;
        if (g) {
            const auto &s = ch.Shown();
            for (float x : s)
                if (x != s[0]) {
                    torn = true;
                    break;
                }
        }
        if (torn)
            break;
    }
    writer.Pause();
    CHECK(!torn);
    CHECK_GT(last, std::uint64_t(99));

    // The rotation the race lived in: a producer that parks a FRESH
    // device buffer every tick (what a tensor eval does) while the frame
    // reads the shown slot. With roles, the slot being written is never
    // the slot being read — under TSan this block is the proof.
    gpud::mock::Device dev;
    Sync<gpud::Buffer> buf;
    Executor parker([&] {
        static std::size_t n = 0;
        n = n % 7 + 1;
        buf.Publish(dev.alloc(4 * n));
    });
    parker.Play();
    deadline = std::chrono::steady_clock::now() + 5s;
    std::uint64_t rotations = 0;
    bool wrong = false;
    while (rotations < 100 && std::chrono::steady_clock::now() < deadline) {
        impl::sync_gate_flip(buf.Gate());
        rotations = buf.Generation();
        const gpud::Buffer *b = source_of(buf).current(); // the door's read
        if (rotations ? (!b || b->bytes() % 4 != 0 || b->bytes() > 28)
                      : b != nullptr)
            wrong = true;
        if (wrong)
            break;
    }
    parker.Pause();
    CHECK(!wrong);
    CHECK_GT(rotations, std::uint64_t(99));

    // A Sync destroyed while a registry still holds its gate: the flip
    // becomes a no-op and the last release frees it — a dead gate, never
    // a dangling one.
    impl::SyncGate orphan;
    {
        Sync<int> scoped;
        orphan = scoped.Gate();
        impl::sync_gate_retain(orphan);
        scoped.Publish();
    }
    impl::sync_gate_flip(orphan);
    CHECK_EQ(impl::sync_gate_generation(orphan), std::uint64_t(0));
    impl::sync_gate_release(orphan);

    std::printf("(%llu host generations, %llu device rotations)\n",
                (unsigned long long)last, (unsigned long long)rotations);
    return check::summary("sync");
}
