// A world you can look around: a shell of particles coloured by
// direction over a ground grid, lit from above and to one side.
// Drag inside the window to orbit, shift-drag or right-drag to pan,
// wheel to dolly.
//
// The world has no title, so it IS the window and the panel floats
// over it. Give it one and it becomes a panel among panels instead.
#include <simview/simview.h>

#include <cmath>
#include <vector>

constexpr std::size_t N = 6000;
constexpr std::size_t kHalo = 900;

// Points on a sphere, spread by the golden angle so they do not band.
void shell(std::vector<float> &pos, std::vector<float> &dir, std::size_t n,
           float radius, float squash) {
    pos.resize(n * 3);
    dir.resize(n * 3);
    const float ga = 2.39996323f;
    for (std::size_t i = 0; i < n; ++i) {
        const float t = (float(i) + 0.5f) / float(n);
        const float z = 1.0f - 2.0f * t;
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float a = ga * float(i);
        const float x = r * std::cos(a), y = r * std::sin(a);
        pos[i * 3 + 0] = radius * x;
        pos[i * 3 + 1] = radius * y;
        pos[i * 3 + 2] = radius * z * squash;
        // The outward normal: what the direction colormap reads.
        dir[i * 3 + 0] = x;
        dir[i * 3 + 1] = y;
        dir[i * 3 + 2] = z;
    }
}

int main() {
    sv::App app({.title = "simview — orbit", .size = {1024, 768}});
    if (!app)
        return 1;

    auto world = app.World();
    world.Camera({.focus = {0.0f, 0.0f, 0.0f}, .distance = 6.0f})
        .Light({.direction = {0.3f, 0.5f, 1.0f}, .intensity = 0.85f})
        .Ambient(0.18f, 0.19f, 0.22f);

    auto core = world.Cloud({.radius = 0.06f,
                             .mode = sv::CloudMode::Solid,
                             .map = sv::CloudMap::Direction});
    auto halo = world.Cloud({.color = {0.40f, 0.70f, 1.0f, 0.16f},
                             .radius = 0.075f,
                             .mode = sv::CloudMode::Alpha});

    std::vector<float> pos, dir;
    shell(pos, dir, N, 1.0f, 0.65f);
    core.Update(pos);
    core.UpdateColors(dir);
    shell(pos, dir, kHalo, 2.3f, 1.0f);
    halo.Update(pos);

    // The projection is a field on the camera, so switching it is one
    // call with everything else held still.
    static bool ortho = false;
    bool was = ortho;
    app.Panel("view").Checkbox("orthographic", ortho);
    app.OnFrame([&] {
        if (ortho == was)
            return;
        was = ortho;
        world.Camera({.focus = {0.0f, 0.0f, 0.0f},
                      .distance = 6.0f,
                      .projection = ortho ? sv::Projection::Orthographic
                                          : sv::Projection::Perspective});
    });

    app.Run();
    return 0;
}
