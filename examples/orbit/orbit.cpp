// A world you can look around: two shells of particles over a ground
// grid, with the camera on a turntable. Drag inside the panel to
// orbit, shift-drag or right-drag to pan, wheel to dolly.
//
// The inner shell is solid — lit spheres that occlude one another —
// and the outer one is translucent, so the two together show what the
// depth buffer and the blend order are doing.
#include <simview/simview.h>

#include <cmath>
#include <vector>

constexpr std::size_t N = 4000;

// Points on a sphere, spread by the golden angle so they do not band.
std::vector<float> shell(float radius, float squash) {
    std::vector<float> p(N * 3);
    const float ga = 2.39996323f;
    for (std::size_t i = 0; i < N; ++i) {
        const float t = (float(i) + 0.5f) / float(N);
        const float z = 1.0f - 2.0f * t;
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float a = ga * float(i);
        p[i * 3 + 0] = radius * r * std::cos(a);
        p[i * 3 + 1] = radius * r * std::sin(a);
        p[i * 3 + 2] = radius * z * squash;
    }
    return p;
}

int main() {
    sv::App app({.title = "simview — orbit", .size = {1024, 768}});
    if (!app)
        return 1;

    auto world = app.World({.title = "orbit"});
    world.Camera({.focus = {0.0f, 0.0f, 0.0f}, .distance = 6.0f});

    auto core = world.Cloud({.color = {1.0f, 0.72f, 0.30f, 1.0f},
                             .radius = 0.05f,
                             .mode = sv::CloudMode::Solid});
    auto halo = world.Cloud({.color = {0.35f, 0.65f, 1.0f, 0.25f},
                             .radius = 0.09f,
                             .mode = sv::CloudMode::Alpha});
    core.Update(shell(1.0f, 0.6f));
    halo.Update(shell(2.2f, 1.0f));

    app.Run();
    return 0;
}
