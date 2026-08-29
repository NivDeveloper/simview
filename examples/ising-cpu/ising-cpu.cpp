// The 2-D Ising model in plain C++ — no tensor library, no GPU
// compute — simulated on simview's Executor thread and handed to the
// window through a Channel. Space pauses, Up/Down move temperature,
// R reseeds, Esc quits.
#include <simview/simview.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <random>
#include <vector>

constexpr unsigned L = 256;

int main() {
    auto app = simview::init({.title = "simview — ising", .size = {768, 768}});
    if (!app) return 1;
    auto field = app.field({.extent = {L, L}, .lo = -1.0f, .hi = 1.0f});

    std::vector<float> s(L * L, 1.0f);
    std::atomic<float> T{2.269f}; // start at the critical point
    std::mt19937 rng(2026);
    auto reseed = [&] {
        std::bernoulli_distribution half;
        for (auto &x : s) x = half(rng) ? 1.0f : -1.0f;
    };
    reseed();

    simview::HostChannel<float> chan(L * L);
    simview::ExecutorHandle sim([&] {
        // One Metropolis sweep at the current temperature.
        std::uniform_real_distribution<float> u;
        std::uniform_int_distribution<unsigned> cell(0, L * L - 1);
        const float t = T.load(std::memory_order_relaxed);
        for (unsigned n = 0; n < L * L; ++n) {
            const unsigned k = cell(rng);
            const unsigned x = k % L, y = k / L;
            const float nn = s[y * L + (x + 1) % L] + s[y * L + (x + L - 1) % L] +
                             s[((y + 1) % L) * L + x] + s[((y + L - 1) % L) * L + x];
            const float dE = 2.0f * s[k] * nn;
            if (dE <= 0.0f || u(rng) < std::exp(-dE / t)) s[k] = -s[k];
        }
        auto out = chan.state();
        std::copy(s.begin(), s.end(), out.begin());
        chan.publish();
    });
    sim.play();

    std::uint64_t gen = 0;
    app.on_frame([&] {
        if (auto latest = chan.latest(gen); !latest.empty())
            field.update(latest);
    });
    app.on_key(simview::Key::Space,
               [&] { sim.playing() ? sim.pause() : sim.play(); });
    app.on_key(simview::Key::Up, [&] { T = T.load() + 0.05f; });
    app.on_key(simview::Key::Down,
               [&] { T = std::max(0.05f, T.load() - 0.05f); });
    app.on_key(simview::Key::R, [&] { sim.pause(); reseed(); sim.play(); });
    app.on_key(simview::Key::Escape, [&] { app.request_quit(); });
    app.run();
}
