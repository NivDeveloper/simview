// A 2-D hard-disc gas in plain C++, arranged both ways at once: real
// space fills the window itself — the scene drawn straight to the
// swapchain, with the panels floating over it — while the panels hold
// a second scene, a plot and the controls. The window shows WHERE the
// particles are; the phase-space panel shows HOW they move. Nothing
// here knows about a GPU: the positions are a std::vector the frame
// hands over each step.
#include <simview/simview.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <span>
#include <vector>

constexpr std::size_t N = 2000;
constexpr float BOX = 100.0f;

// The phase plot's velocity axis, and the gravity that fits inside it:
// a particle falling the height of the box reaches sqrt(2*g*BOX), so
// GMAX is the largest pull whose parabolas stay in frame.
constexpr float VMAX = 80.0f;
constexpr float GMAX = 30.0f;

// Velocity vectors for one particle in STRIDE: all 2000 would be
// noise, a sample reads as a flow. SCALE turns a velocity into a
// length in box units.
constexpr std::size_t STRIDE = 8;
constexpr float SCALE = 0.25f;

int main() {
    sv::App app({.title = "simview — gas", .size = {1100, 760}});
    if (!app)
        return 1;

    std::vector<float> xy(N * 2), vel(N * 2), speed(N), phase(N * 2);
    // The 3D view: every particle at (x, y, |v|) — real space with
    // speed as height. Three arrays, one per axis, filled each frame.
    std::vector<float> sx(N), sy(N), sz(N);
    // The binned speed distribution as mean +/- spread per bin, for
    // ErrorBars over Bars: the two kinds SHARE a name on purpose, so
    // ImPlot binds the bars and their whiskers into one legend entry.
    constexpr std::size_t NB = 16;
    std::vector<float> bin_x(NB), bin_mean(NB), bin_err(NB);
    std::vector<float> arrows((N / STRIDE) * 4);
    std::mt19937 rng(2026);
    std::uniform_real_distribution<float> place(2.0f, BOX - 2.0f);
    std::normal_distribution<float> kick(0.0f, 6.0f);
    for (std::size_t i = 0; i < N; ++i) {
        xy[2 * i] = place(rng);
        xy[2 * i + 1] = place(rng);
        vel[2 * i] = kick(rng);
        vel[2 * i + 1] = kick(rng);
        speed[i] = std::hypot(vel[2 * i], vel[2 * i + 1]);
        phase[2 * i] = xy[2 * i + 1];
        phase[2 * i + 1] = vel[2 * i + 1];
    }

    float dt = 0.004f;
    float gravity = 0.0f;
    bool running = true;
    bool vectors = true;

    // Real space, in the window itself. The box IS the coordinate
    // system: no field, so the range is named rather than inherited
    // from a lattice.
    app.SceneRange({0.0, 0.0, BOX, BOX});
    auto points =
        app.Particles({.color = {0.55f, 0.8f, 1.0f, 0.9f}, .radius = 2.0f});

    // Velocity as a segment from each sampled particle: the third
    // scene kind, composited over the cloud in the same range, so an
    // arrow starts on the disc it belongs to.
    auto velocity =
        app.Lines({.color = {1.0f, 0.85f, 0.4f, 0.8f}, .width = 1.5f});

    // The same particles in phase space, in a panel of their own:
    // height against vertical velocity, which is where gravity draws
    // its parabolas. Its scene has its own range and its own aspect.
    auto orbits =
        app.View({.title = "phase space"})
            .Range({0.0, -VMAX, BOX, VMAX})
            .Particles({.color = {1.0f, 0.72f, 0.3f, 0.85f}, .radius = 1.5f});

    app.Plot({.title = "speed",
              .x = {.label = "|v|", .fit = sv::Fit::Stream},
              .y = {.label = "count", .fit = sv::Fit::Stream}})
        .Histogram(
            "distribution", [&] { return std::span<const float>(speed); }, 32);

    // Binned: each bin's mean speed as a bar, its spread as a whisker.
    // Same series name binds the two into one legend entry.
    app.Plot({.title = "speed by bin",
              .x = {.label = "bin", .fit = sv::Fit::Stream},
              .y = {.label = "mean |v|", .fit = sv::Fit::Stream}})
        .Bars("binned", bin_x, bin_mean, 0.8)
        .ErrorBars("binned", bin_x, bin_mean, bin_err);

    // The third axis the 2D phase view could not show: the whole gas
    // in real space, lifted by speed. Drag to rotate.
    app.Plot3D(
           {.title = "gas in 3D",
            .x = {.label = "x", .min = 0.0, .max = BOX, .fit = sv::Fit::Fixed},
            .y = {.label = "y", .min = 0.0, .max = BOX, .fit = sv::Fit::Fixed},
            .z = {.label = "|v|", .fit = sv::Fit::Stream}})
        .Scatter("particles", sx, sy, sz,
                 {.color = {0.55f, 0.8f, 1.0f, 0.9f}, .marker_size = 1.5f});

    app.Panel("controls")
        .Slider("dt", dt, 0.0f, 0.01f)
        .Slider("gravity", gravity, 0.0f, GMAX)
        .Checkbox("running", running)
        .Checkbox("velocity vectors", vectors)
        .Separator()
        .Value("particles", [] { return double(N); }, "%.0f");

    app.OnFrame([&] {
        if (running)
            for (std::size_t i = 0; i < N; ++i) {
                vel[2 * i + 1] += gravity * dt;
                xy[2 * i] += vel[2 * i] * dt;
                xy[2 * i + 1] += vel[2 * i + 1] * dt;

                // Reflect off the walls; energy is conserved exactly,
                // so the speed distribution holds its shape.
                for (int c = 0; c < 2; ++c) {
                    float &p = xy[2 * i + c];
                    float &v = vel[2 * i + c];
                    if (p < 0.0f) {
                        p = -p;
                        v = -v;
                    } else if (p > BOX) {
                        p = 2.0f * BOX - p;
                        v = -v;
                    }
                }

                speed[i] = std::hypot(vel[2 * i], vel[2 * i + 1]);
                phase[2 * i] = xy[2 * i + 1];
                phase[2 * i + 1] = vel[2 * i + 1];
            }

        points.Update(xy);
        orbits.Update(phase);

        // The 3D scatter and the binned bars are re-derived from the
        // same arrays the scene draws — no second source of truth.
        float vmax = 1e-3f;
        for (std::size_t i = 0; i < N; ++i)
            vmax = std::max(vmax, speed[i]);
        std::vector<float> bin_sum(NB, 0.0f), bin_sq(NB, 0.0f);
        std::vector<int> bin_n(NB, 0);
        for (std::size_t i = 0; i < N; ++i) {
            sx[i] = xy[2 * i];
            sy[i] = xy[2 * i + 1];
            sz[i] = speed[i];
            const std::size_t b =
                std::min(NB - 1, std::size_t(speed[i] / vmax * float(NB)));
            bin_sum[b] += speed[i];
            bin_sq[b] += speed[i] * speed[i];
            ++bin_n[b];
        }
        for (std::size_t b = 0; b < NB; ++b) {
            bin_x[b] = float(b);
            const float n = float(std::max(1, bin_n[b]));
            const float mean = bin_sum[b] / n;
            bin_mean[b] = mean;
            bin_err[b] = std::sqrt(std::max(0.0f, bin_sq[b] / n - mean * mean));
        }

        // An empty set is not an error: switching vectors off is an
        // Update of nothing, and the kind draws nothing.
        if (vectors) {
            for (std::size_t k = 0; k < N / STRIDE; ++k) {
                const std::size_t i = k * STRIDE;
                arrows[4 * k + 0] = xy[2 * i];
                arrows[4 * k + 1] = xy[2 * i + 1];
                arrows[4 * k + 2] = xy[2 * i] + vel[2 * i] * SCALE;
                arrows[4 * k + 3] = xy[2 * i + 1] + vel[2 * i + 1] * SCALE;
            }
            velocity.Update(arrows);
        } else {
            velocity.Update(std::span<const float>());
        }
    });

    app.OnKey(sv::Key::Space, [&] { running = !running; })
        .OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
}
