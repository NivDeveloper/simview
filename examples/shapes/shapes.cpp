// The three ways a point can be drawn, side by side, so the
// difference is a thing you look at rather than a paragraph.
//
//   left    cubes      — flat faces, hard edges, a lattice
//   middle  spheres    — real geometry, coloured by direction
//   right   billboards — a disc facing you, shaded as a sphere
//
// The slider changes how many spheres there are. As they shrink on
// screen the sphere quietly drops from 972 triangles to 108: the
// picture barely changes and the cost does, which is the whole reason
// the tier exists. Zoom in and it comes back.
//
// They float over the ground so the light casts their shadows onto
// it. Drag to orbit, shift-drag to pan, wheel to zoom.
#include <simview/simview.h>

#include <cmath>
#include <vector>

namespace {

// The three groups float ABOVE the ground rather than sitting on it,
// so the light throws their shadows somewhere you can see. Centred on
// the origin, a shadow lands underneath its own group and behind the
// half of it that is below the plane.
constexpr float kLift = 2.6f;

// Points on a sphere, spread by the golden angle so they do not band,
// with the outward normal beside each for the colormap to read.
void shell(std::vector<float> &pos, std::vector<float> &dir, std::size_t n,
           float radius, float cx) {
    pos.resize(n * 3);
    dir.resize(n * 3);
    for (std::size_t i = 0; i < n; ++i) {
        const float t = (float(i) + 0.5f) / float(n);
        const float z = 1.0f - 2.0f * t;
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float a = 2.39996323f * float(i);
        const float x = r * std::cos(a), y = r * std::sin(a);
        pos[i * 3 + 0] = cx + radius * x;
        pos[i * 3 + 1] = radius * y;
        pos[i * 3 + 2] = radius * z + kLift;
        dir[i * 3 + 0] = x;
        dir[i * 3 + 1] = y;
        dir[i * 3 + 2] = z;
    }
}

std::vector<float> lattice(int side, float spacing, float cx) {
    std::vector<float> p;
    const float half = 0.5f * spacing * float(side - 1);
    for (int k = 0; k < side; ++k)
        for (int j = 0; j < side; ++j)
            for (int i = 0; i < side; ++i) {
                p.push_back(cx + spacing * float(i) - half);
                p.push_back(spacing * float(j) - half);
                p.push_back(spacing * float(k) - half + kLift);
            }
    return p;
}

} // namespace

int main() {
    sv::App app({.title = "simview — shapes", .size = {1200, 760}});
    if (!app)
        return 1;

    auto world = app.World();
    world.Camera({.focus = {0.0f, 0.0f, 1.6f}, .distance = 13.0f})
        .Light({.direction = {0.4f, 0.5f, 0.9f},
                .intensity = 0.9f,
                .shadow = true})
        .Ambient(0.16f, 0.17f, 0.21f);

    auto cubes = world.Cloud({.color = {0.95f, 0.72f, 0.32f, 1.0f},
                              .radius = 0.22f,
                              .shape = sv::CloudShape::Cube});
    cubes.Update(lattice(4, 0.9f, -4.6f));

    auto spheres = world.Cloud({.radius = 0.075f,
                                .shape = sv::CloudShape::Sphere,
                                .map = sv::CloudMap::Direction});
    auto discs =
        world.Cloud({.color = {0.55f, 0.8f, 1.0f, 1.0f}, .radius = 0.075f});

    std::vector<float> pos, dir;
    shell(pos, dir, 900, 1.8f, 4.6f);
    discs.Update(pos);

    // The one that changes: everything else is here to compare it to.
    float wanted = 900.0f;
    std::size_t built = 0;
    const auto rebuild = [&] {
        const std::size_t n = std::size_t(wanted);
        if (n == built)
            return;
        built = n;
        shell(pos, dir, n, 1.8f, 0.0f);
        spheres.Update(pos);
        spheres.UpdateColors(dir);
    };
    rebuild();

    bool ortho = false;
    bool was_ortho = false;
    app.Panel("shapes")
        .Text("cubes, spheres, billboards")
        .Separator()
        .Slider("spheres", wanted, 100.0f, 40000.0f)
        .Value(
            "drawn", [&] { return float(built); }, "%.0f")
        .Text("972 triangles each, or 108 when they are small on screen")
        .Separator()
        .Checkbox("orthographic", ortho);

    app.OnFrame([&] {
        rebuild();
        if (ortho != was_ortho) {
            was_ortho = ortho;
            world.Camera({.focus = {0.0f, 0.0f, 1.6f},
                          .distance = 13.0f,
                          .projection = ortho ? sv::Projection::Orthographic
                                              : sv::Projection::Perspective});
        }
    });

    app.Run();
    return 0;
}
