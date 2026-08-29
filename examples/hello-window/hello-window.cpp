// The walking skeleton's proof: an animated CPU plasma pushed through
// the field every frame. Windowed: vsync, Esc quits, title shows fps.
// Headless (CI, eyeless verification): --frames N out.bmp.
#include <simview/simview.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr unsigned W = 256, H = 192;

void plasma(std::vector<float> &v, float t) {
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x) {
            const float fx = float(x) / W, fy = float(y) / H;
            v[y * W + x] =
                0.5f + 0.25f * std::sin(8.0f * fx + t) +
                0.25f * std::sin(7.0f * fy + 1.3f * t) *
                    std::cos(5.0f * (fx + fy) - 0.7f * t);
        }
}

} // namespace

int main(int argc, char **argv) {
    const bool headless = argc >= 3 && std::string(argv[1]) == "--frames";
    auto app = simview::init({.title = "simview — hello-window",
                              .size = {W * 3, H * 3},
                              .headless = headless});
    if (!app)
        return std::printf("no GPU device on this machine: %s\n",
                           simview::last_error()),
               0; // a reported outcome, not a failure (CI runners)

    auto field = app.field({.extent = {W, H}, .map = simview::Colormap::Viridis});
    if (!field) return std::printf("field: %s\n", simview::last_error()), 1;

    std::vector<float> v(W * H);
    float t = 0;
    app.on_frame([&] {
        plasma(v, t += 0.02f);
        field.update(v);
    });

    if (headless) {
        const int n = std::atoi(argv[2]);
        for (int k = 0; k < n; ++k)
            app.step();
        const char *out = argc >= 4 ? argv[3] : "hello-window.bmp";
        if (!app.shot(out))
            return std::printf("shot: %s\n", simview::last_error()), 1;
        return std::printf("%d frames, shot: %s\n", n, out), 0;
    }

    app.on_key(simview::Key::Escape, [&] { app.request_quit(); });
    app.run();
    return 0;
}
