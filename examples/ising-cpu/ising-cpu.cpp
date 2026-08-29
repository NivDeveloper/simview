// The 2-D Ising model in plain C++ — no tensor library, no GPU
// compute — simulated on simview's Executor thread and handed to the
// window through a Channel, with its magnetisation traced beside it
// and a panel of controls. Space pauses, Up/Down move temperature, R
// reseeds, Esc quits — the same variables the panel writes.
#include <simview/simview.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <random>
#include <span>
#include <vector>

constexpr unsigned L = 256;

int main() {
    sv::App app({.title = "simview — ising", .size = {768, 768}});
    if (!app)
        return 1;
    auto field = app.Field({.extent = {L, L}, .lo = -1.0f, .hi = 1.0f});

    std::vector<float> s(L * L, 1.0f);
    // The slider moves a plain float on the main thread; the frame
    // publishes it to the atomic the sim thread reads.
    float temperature = 2.269f; // the critical point
    bool running = true;
    std::atomic<float> T{temperature};
    std::mt19937 rng(2026);

    auto reseed = [&] {
        std::bernoulli_distribution half;
        for (auto &x : s)
            x = half(rng) ? 1.0f : -1.0f;
    };
    reseed();

    sv::Channel<float> chan(L * L);
    sv::Executor sim([&] {
        // One Metropolis sweep at the current temperature.
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

        auto out = chan.State();
        std::copy(s.begin(), s.end(), out.begin());
        chan.Publish();
    });

    sim.Play();

    // The trace grows, so it REALLOCATES — which is exactly why the
    // series pulls instead of borrowing a span. It is filled here, on
    // the main thread, so the render thread's pull never races the
    // Executor.
    std::vector<float> mag;

    auto trace = app.Plot(
        {.title = "magnetisation",
         .x = {.label = "sample", .fit = sv::Fit::Stream},
         .y = {.label = "m", .min = -1.0, .max = 1.0, .fit = sv::Fit::Fixed}});
    trace.Line("m", [&] { return std::span<const float>(mag); });

    bool reseed_wanted = false;
    app.Panel("controls")
        .Slider("temperature", temperature, 0.05f, 4.5f)
        .Checkbox("running", running)
        .Separator()
        .Button("reseed", [&] { reseed_wanted = true; });

    std::uint64_t gen = 0;
    app.OnFrame([&] {
        T.store(temperature, std::memory_order_relaxed);
        if (running != sim.Playing())
            running ? sim.Play() : sim.Pause();
        if (reseed_wanted) {
            reseed_wanted = false;
            sim.Pause();
            reseed();
            mag.clear();
            if (running)
                sim.Play();
        }

        if (auto latest = chan.Latest(gen); !latest.empty()) {
            field.Update(latest);
            float sum = 0.0f;
            for (float s_ : latest)
                sum += s_;
            mag.push_back(sum / float(L * L));
            if (mag.size() > 4096)
                mag.erase(mag.begin());
        }
    });

    // The keys move the same variables the panel does, so the two
    // never disagree about what the sim is doing.
    app.OnKey(sv::Key::Space, [&] { running = !running; });
    app.OnKey(sv::Key::Up,
              [&] { temperature = std::min(4.5f, temperature + 0.05f); });
    app.OnKey(sv::Key::Down,
              [&] { temperature = std::max(0.05f, temperature - 0.05f); });
    app.OnKey(sv::Key::R, [&] { reseed_wanted = true; });
    app.OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
}
