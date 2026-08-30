// The 2-D Ising model in plain C++ — no tensor library, no GPU
// compute — simulated on simview's Executor thread and handed to the
// window through sv::Sync: the same lattice, the same keys and the
// same transport panel as examples/ising, with std::vector where that
// one has a device-resident tensor. Space toggles, Up/Down move
// temperature, R restarts, Esc quits — the keys call the Executor, as
// the panel does, so nothing is reconciled.
#include <simview/simview.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <vector>

constexpr unsigned L = 256;

int main() {
    sv::App app({.title = "simview — ising (cpu)", .size = {768, 768}});
    if (!app)
        return 1;

    // The state: three lattices the sim and the frame share by role.
    // The sim reads Current(), writes Next() and publishes; the field
    // draws Shown(), which the frame flips once and the sim never
    // writes. Publish the initial condition before anyone reads.
    sv::Sync<std::vector<float>> spins{std::vector<float>(L * L)};
    std::mt19937 rng(2026);
    auto reseed = [&](std::vector<float> &s) {
        std::bernoulli_distribution half;
        for (auto &x : s)
            x = half(rng) ? 1.0f : -1.0f;
    };
    reseed(spins.Next());
    spins.Publish();

    auto field = app.Field(spins, {.extent = {L, L}, .lo = -1.0f, .hi = 1.0f});
    if (!field)
        return 1;

    // The slider moves a plain float on the main thread; the frame
    // publishes it to the atomic the sim thread reads.
    float temperature = 2.269f; // the critical point
    std::atomic<float> T{temperature};

    // The Executor keeps the clock. One tick is one Metropolis sweep,
    // IN PLACE — so the next lattice starts as a copy of the current
    // one: the memcpy an in-place sim pays, and the only copy anywhere.
    sv::Executor sim([&](const sv::Tick &) {
        std::vector<float> &s = spins.Next();
        s = spins.Current();

        std::uniform_real_distribution<float> u;
        std::uniform_int_distribution<unsigned> cell(0, L * L - 1);
        const float t = T.load(std::memory_order_relaxed);
        for (unsigned n = 0; n < L * L; ++n) {
            const unsigned k = cell(rng);
            const unsigned x = k % L, y = k / L;
            const float nn =
                s[y * L + (x + 1) % L] + s[y * L + (x + L - 1) % L] +
                s[((y + 1) % L) * L + x] + s[((y + L - 1) % L) * L + x];

            const float dE = 2.0f * s[k] * nn;
            if (dE <= 0.0f || u(rng) < std::exp(-dE / t))
                s[k] = -s[k];
        }
        spins.Publish();
    });
    // Restart re-randomises on the worker, between ticks — it can never
    // overlap a sweep.
    sim.OnRestart([&] {
        reseed(spins.Next());
        spins.Publish();
    });
    sim.Play();

    // Play, pause, advance one sweep, advance N, restart, the clock and
    // the rate — one line, reading the Executor. Sliders chain on.
    app.Controls(sim).Slider("temperature", temperature, 0.05f, 4.5f);

    app.OnFrame([&] { T.store(temperature, std::memory_order_relaxed); });

    app.OnKey(sv::Key::Space, [&] { sim.Toggle(); })
        .OnKey(sv::Key::Up,
               [&] { temperature = std::min(4.5f, temperature + 0.05f); })
        .OnKey(sv::Key::Down,
               [&] { temperature = std::max(0.05f, temperature - 0.05f); })
        .OnKey(sv::Key::R, [&] { sim.Restart(); })
        .OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
}
