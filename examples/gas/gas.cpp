// A 2-D hard-disc gas in plain C++, arranged both ways at once: real
// space fills the window itself — the scene drawn straight to the
// swapchain, with the panels floating over it — while the panels hold
// a second scene, a plot and the controls. The window shows WHERE the
// particles are; the phase-space panel shows HOW they move. Nothing
// here knows about a GPU: the positions are a std::vector the frame
// hands over each step.
#include <simview/simview.h>

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

int main() {
    sv::App app({.title = "simview — gas", .size = {1100, 760}});
    if (!app)
        return 1;

    std::vector<float> xy(N * 2), vel(N * 2), speed(N), phase(N * 2);
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

    // Real space, in the window itself. The box IS the coordinate
    // system: no field, so the range is named rather than inherited
    // from a lattice.
    app.SceneRange({0.0, 0.0, BOX, BOX});
    auto points =
        app.Particles({.color = {0.55f, 0.8f, 1.0f, 0.9f}, .radius = 2.0f});

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

    app.Panel("controls")
        .Slider("dt", dt, 0.0f, 0.01f)
        .Slider("gravity", gravity, 0.0f, GMAX)
        .Checkbox("running", running)
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
    });

    app.OnKey(sv::Key::Space, [&] { running = !running; })
        .OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
}
