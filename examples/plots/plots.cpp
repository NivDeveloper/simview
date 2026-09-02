// The plot gallery: one panel per series kind, all fifteen, so every
// kind can be LOOKED AT rather than only asserted. The data is a few
// analytic signals — the point is to see each kind, not to model
// anything. Every panel docks, tabs and tears out; the 3D ones rotate
// on drag and zoom on scroll.
//
// The first plot also carries CONTROLS of its own, which is the same
// widget vocabulary a panel uses, drawn above the canvas. And every
// plot holding points offers a "views" button: one click turns a
// scatter into the density, the distribution or the profile of itself,
// each a live view that follows the source.
#include <simview/simview.h>

#include <cmath>
#include <numbers>
#include <vector>

constexpr std::size_t N = 64;

int main() {
    sv::App app({.title = "simview — plots", .size = {1400, 900}});
    if (!app)
        return 1;

    // A shared x axis, a sine, its envelope band, and a noisy copy.
    std::vector<float> x(N), s(N), lo(N), hi(N), noisy(N), err(N), bits(N);
    for (std::size_t i = 0; i < N; ++i) {
        const float t =
            float(i) / float(N - 1) * 4.0f * std::numbers::pi_v<float>;
        x[i] = t;
        s[i] = std::sin(t);
        lo[i] = s[i] - 0.25f;
        hi[i] = s[i] + 0.25f;
        noisy[i] = s[i] + 0.15f * std::sin(t * 7.3f);
        err[i] = 0.1f + 0.05f * std::cos(t);
        bits[i] = float((i / 4) % 2);
    }
    const std::vector<float> marks = {std::numbers::pi_v<float>,
                                      2.0f * std::numbers::pi_v<float>,
                                      3.0f * std::numbers::pi_v<float>};

    // Controls live on the plot they belong to, not in a panel across
    // the window. These two do nothing here — the gallery's data is
    // fixed — and are present so the strip can be seen.
    float phase = 0.0f;
    bool show_noise = true;

    // --- the 2D kinds, one panel each ---------------------------------
    app.Plot({.title = "line + scatter",
              .x = {.label = "t"},
              .y = {.label = "sin"}})
        .Line("sin", x, s)
        .Scatter("samples", x, noisy, {.marker_size = 3.0f, .marker = 0})
        .Controls([&](sv::Panel &p) {
            p.Row([&](sv::Panel &q) {
                q.IconToggle(sv::Icon::Eye, "show the noisy copy", show_noise)
                    .Slider("phase", phase, 0.0f, 6.283f);
            });
        });

    app.Plot({.title = "stairs", .x = {.label = "t"}}).Stairs("steps", x, s);

    app.Plot({.title = "shaded + band", .x = {.label = "t"}})
        .Shaded("area", x, s, 0.0, {.fill_alpha = 0.35f})
        .Band("envelope", x, lo, hi, {.fill_alpha = 0.25f})
        .Line("sin", x, s);

    app.Plot({.title = "bars + errorbars", .x = {.label = "bin"}})
        .Bars("value", x, s, 0.15)
        .ErrorBars("value", x, s, err);

    app.Plot({.title = "stems", .x = {.label = "t"}})
        .Stems("stems", x, s, 0.0, {.marker = 0});

    app.Plot({.title = "inflines", .x = {.label = "t"}})
        .Line("sin", x, s)
        .InfLines("multiples of pi", marks);

    app.Plot({.title = "digital", .x = {.label = "t"}, .y = {.label = "level"}})
        .Digital("clock", x, bits, {.size = 24.0f});

    app.Plot({.title = "histogram",
              .x = {.label = "value"},
              .y = {.label = "count"}})
        .Histogram("distribution", noisy, 16);

    // A heatmap of sin(x)*cos(y) over a grid, with its colourbar.
    constexpr std::size_t R = 32, C = 48;
    std::vector<float> grid(R * C);
    for (std::size_t r = 0; r < R; ++r)
        for (std::size_t c = 0; c < C; ++c)
            grid[r * C + c] = std::sin(float(c) / float(C) * 6.28f) *
                              std::cos(float(r) / float(R) * 6.28f);
    app.Plot({.title = "heatmap",
              .x = {.label = "c",
                    .min = 0.0,
                    .max = double(C),
                    .fit = sv::Fit::Fixed},
              .y = {.label = "r",
                    .min = 0.0,
                    .max = double(R),
                    .fit = sv::Fit::Fixed},
              .palette = sv::Palette::Viridis})
        .Heatmap("sin*cos", grid, R, C,
                 {.scale_min = -1.0,
                  .scale_max = 1.0,
                  .x1 = double(C),
                  .y1 = double(R)});

    // --- the 3D kinds -------------------------------------------------
    // A helix: the 2D line and scatter lifted to three coordinates.
    std::vector<float> hx(N), hy(N), hz(N);
    for (std::size_t i = 0; i < N; ++i) {
        const float t =
            float(i) / float(N - 1) * 4.0f * std::numbers::pi_v<float>;
        hx[i] = std::cos(t);
        hy[i] = std::sin(t);
        hz[i] = t / (4.0f * std::numbers::pi_v<float>);
    }
    app.Plot3D({.title = "line3 + scatter3",
                .x = {.label = "x"},
                .y = {.label = "y"},
                .z = {.label = "z"}})
        .Line("helix", hx, hy, hz)
        .Scatter("points", hx, hy, hz, {.marker_size = 2.0f, .marker = 0});

    // A surface: three coordinate grids of XC*YC, the shape Surface takes.
    constexpr std::size_t XC = 40, YC = 40;
    std::vector<float> sx(XC * YC), sy(XC * YC), sz(XC * YC);
    for (std::size_t j = 0; j < YC; ++j)
        for (std::size_t i = 0; i < XC; ++i) {
            const float u = (float(i) / float(XC - 1) - 0.5f) * 6.0f;
            const float v = (float(j) / float(YC - 1) - 0.5f) * 6.0f;
            sx[j * XC + i] = u;
            sy[j * XC + i] = v;
            const float r = std::sqrt(u * u + v * v);
            sz[j * XC + i] = r > 1e-4f ? std::sin(r) / r : 1.0f;
        }
    app.Plot3D({.title = "surface",
                .x = {.label = "x"},
                .y = {.label = "y"},
                .z = {.label = "sinc"},
                .palette = sv::Palette::Plasma})
        .Surface("sinc", sx, sy, sz, XC, YC,
                 {.scale_min = -0.3, .scale_max = 1.0});

    // A mesh: a torus built in place — vertices as three arrays, and an
    // index buffer of triangles. A closed mesh shows the shading.
    constexpr unsigned MAJ = 32, MIN = 16;
    std::vector<float> mx, my, mz;
    std::vector<unsigned> mi;
    for (unsigned a = 0; a < MAJ; ++a)
        for (unsigned b = 0; b < MIN; ++b) {
            const float ta = float(a) / MAJ * 2.0f * std::numbers::pi_v<float>;
            const float tb = float(b) / MIN * 2.0f * std::numbers::pi_v<float>;
            const float ring = 1.0f + 0.4f * std::cos(tb);
            mx.push_back(ring * std::cos(ta));
            my.push_back(ring * std::sin(ta));
            mz.push_back(0.4f * std::sin(tb));
            // Two triangles per quad, wrapping in both directions.
            const unsigned a1 = (a + 1) % MAJ, b1 = (b + 1) % MIN;
            const unsigned i00 = a * MIN + b, i10 = a1 * MIN + b;
            const unsigned i01 = a * MIN + b1, i11 = a1 * MIN + b1;
            mi.insert(mi.end(), {i00, i10, i11, i00, i11, i01});
        }
    app.Plot3D({.title = "mesh",
                .x = {.label = "x"},
                .y = {.label = "y"},
                .z = {.label = "z"}})
        .Mesh("torus", mx, my, mz, mi,
              {.fill = {0.4f, 0.7f, 1.0f, 1.0f}, .fill_alpha = 0.9f});

    app.OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });
    app.Run();
}
