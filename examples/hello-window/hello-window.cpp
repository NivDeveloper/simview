// A window on a running simulation, smallest possible: an animated
// plasma computed on the CPU, pushed through the field every frame.
#include <simview/simview.h>

#include <cmath>
#include <vector>

constexpr unsigned W = 256, H = 192;

int main() {
    auto app = simview::init({.title = "simview — hello-window",
                              .size = {W * 3, H * 3}});
    if (!app) return 1;
    auto field = app.field({.extent = {W, H},
                            .map = simview::Colormap::Viridis});

    std::vector<float> v(W * H);
    float t = 0;
    app.on_frame([&] {
        t += 0.02f;
        for (unsigned y = 0; y < H; ++y)
            for (unsigned x = 0; x < W; ++x) {
                const float fx = float(x) / W, fy = float(y) / H;
                v[y * W + x] =
                    0.5f + 0.25f * std::sin(8.0f * fx + t) +
                    0.25f * std::sin(7.0f * fy + 1.3f * t) *
                        std::cos(5.0f * (fx + fy) - 0.7f * t);
            }
        field.update(v);
    });
    app.on_key(simview::Key::Escape, [&] { app.request_quit(); });
    app.run();
}
