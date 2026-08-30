// A window on a running simulation, smallest possible: an animated
// plasma computed on the CPU and pushed through the field every
// frame, with a panel of controls over it. Drag the panel's title bar
// to dock it against an edge, or right out of the window into one of
// its own.
#include <simview/simview.h>

#include <cmath>
#include <vector>

constexpr unsigned W = 256, H = 192;

int main() {
    sv::App app({.title = "simview — hello-window", .size = {W * 3, H * 3}});
    if (!app)
        return 1;

    auto field = app.Field({.extent = {W, H}, .map = sv::Colormap::Viridis});

    std::vector<float> v(W * H);
    float t = 0.0f;
    float speed = 1.0f;
    bool running = true;

    app.Panel("controls")
        .Slider("speed", speed, 0.0f, 4.0f)
        .Checkbox("running", running)
        .Separator()
        .Value(
            "frames", [&] { return double(app.Stats().frames); }, "%.0f")
        .Value("uploads", [&] { return double(app.Stats().uploads); }, "%.0f");

    app.OnFrame([&] {
        if (running)
            t += 0.02f * speed;
        for (unsigned y = 0; y < H; ++y)
            for (unsigned x = 0; x < W; ++x) {
                const float fx = float(x) / W, fy = float(y) / H;
                v[y * W + x] = 0.5f + 0.25f * std::sin(8.0f * fx + t) +
                               0.25f * std::sin(7.0f * fy + 1.3f * t) *
                                   std::cos(5.0f * (fx + fy) - 0.7f * t);
            }
        field.Update(v);
    });

    app.OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
}
