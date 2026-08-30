// Plots and panels without a display: they are ImGui windows drawing
// into draw lists, so everything but colour and feel is provable here.
// Tests may speak ImGui — they are not consumers.
#include "harness/Harness.h"

#include "probe/Probe.h"

#include <imgui.h>
#include <implot.h>
#include <implot3d.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

int total_vertices() {
    const ImDrawData *dd = ImGui::GetDrawData();
    return dd ? dd->TotalVtxCount : 0;
}

bool any_vtx_offset() {
    const ImDrawData *dd = ImGui::GetDrawData();
    for (int i = 0; dd && i < dd->CmdListsCount; ++i)
        for (const ImDrawCmd &c : dd->CmdLists[i]->CmdBuffer)
            if (c.VtxOffset != 0)
                return true;
    return false;
}

// A newly created ImGui window renders NOTHING on its first frame:
// it is hidden while its content is measured. Anything that measures
// geometry must step past that frame.
void settle(sv::App &app) {
    app.Step();
    app.Step();
}

bool refused(const char *needle) {
    return std::string(sv::LastError()).find(needle) != std::string::npos;
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.headless = true});
    if (!app)
        return check::skip("plot", LastError());
    CHECK(ImPlot::GetCurrentContext() == sv::probe::plot_context(app.Raw()));

    // A plot alone is a panel: it registers its own callback, so the
    // UI runs with no OnUi anywhere.
    auto empty = app.Plot({.title = "empty"});
    CHECK(bool(empty));
    app.Step();
    CHECK_EQ(ImGui::GetFrameCount(), 1);
    settle(app);
    const int axes_only = total_vertices();
    CHECK_GT(axes_only, 0);

    // A series adds real geometry, and the pull is asked exactly once
    // per frame — the guarantee the field's source already makes.
    std::vector<float> ys(64);
    for (std::size_t i = 0; i < ys.size(); ++i)
        ys[i] = std::sin(float(i) * 0.2f);
    int pulls = 0;
    auto p =
        app.Plot({.title = "trace",
                  .x = {.label = "i"},
                  .y = {.label = "v", .min = -1, .max = 1, .fit = Fit::Fixed}});
    CHECK(bool(p));
    p.Line("pulled", [&] {
        ++pulls;
        return std::span<const float>(ys);
    });
    app.Step();
    CHECK_EQ(pulls, 1);
    app.Step();
    CHECK_EQ(pulls, 2);
    settle(app);
    CHECK_GT(total_vertices(), axes_only);

    // Both element types draw, and a borrowed span needs no closure.
    std::vector<double> yd(32, 0.25);
    std::vector<float> xs(64);
    for (std::size_t i = 0; i < xs.size(); ++i)
        xs[i] = float(i);
    const int before_both = total_vertices();
    p.Line("f64", yd);
    p.Line("xy", xs, ys);
    settle(app);
    CHECK_GT(total_vertices(), before_both);

    // The other kinds, each costing one enum value, one emitter case
    // and one forwarder per data shape.
    const int before_kinds = total_vertices();
    p.Scatter("points", xs, ys);
    p.Histogram("dist", ys, 16);
    settle(app);
    CHECK_GT(total_vertices(), before_kinds);

    // The eight array-shaped kinds, one per block, each proven the
    // same way: it puts geometry on the frame the plot did not have
    // before. Each block is the cost of its kind's test.
    std::vector<float> err(xs.size(), 0.1f), lo(xs.size()), hi(xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        lo[i] = ys[i] - 0.2f;
        hi[i] = ys[i] + 0.2f;
    }
    const std::vector<float> marks = {2.0f, 5.0f};
    std::vector<float> bits(xs.size());
    for (std::size_t i = 0; i < bits.size(); ++i)
        bits[i] = float(i % 2);

    int before = total_vertices();
    p.Stairs("steps", xs, ys);
    settle(app);
    CHECK_GT(total_vertices(), before);

    before = total_vertices();
    p.Shaded("area", xs, ys, 0.0);
    settle(app);
    CHECK_GT(total_vertices(), before);

    before = total_vertices();
    p.Band("band", xs, lo, hi);
    settle(app);
    CHECK_GT(total_vertices(), before);

    before = total_vertices();
    p.Bars("bars", xs, ys, 0.5);
    settle(app);
    CHECK_GT(total_vertices(), before);

    before = total_vertices();
    p.Stems("stems", xs, ys, 0.0);
    settle(app);
    CHECK_GT(total_vertices(), before);

    before = total_vertices();
    p.InfLines("marks", marks);
    settle(app);
    CHECK_GT(total_vertices(), before);

    before = total_vertices();
    p.Digital("bits", xs, bits);
    settle(app);
    CHECK_GT(total_vertices(), before);

    before = total_vertices();
    p.ErrorBars("err", xs, ys, err);
    settle(app);
    CHECK_GT(total_vertices(), before);

    before = total_vertices();
    p.ErrorBars("err2", xs, ys, err, err);
    settle(app);
    CHECK_GT(total_vertices(), before);

    // ErrorBars DECORATE: ImPlot binds whiskers to their host by a
    // shared name, so that one reuse is allowed — and only that one.
    // Two plain kinds on a name still merge, and still refuse.
    // Asserted on ACCEPTANCE, not on vertex growth: a refused series
    // still leaves the host drawing, so the frame grows either way and
    // a vertex count would pass with the exception removed. Measured.
    CHECK(bool(p.Bars("host", xs, ys, 0.5)));
    CHECK(impl::plot_series(
        p.Raw(),
        impl::SeriesDesc{.name = "host",
                         .kind = impl::SeriesKind::ErrorBars,
                         .data = {.a = xs.data(),
                                  .b = ys.data(),
                                  .c = err.data(),
                                  .count = xs.size()}})); // decorates: allowed
    CHECK(!impl::plot_series(
        p.Raw(),
        impl::SeriesDesc{.name = "host",
                         .kind = impl::SeriesKind::Bars,
                         .data = {.b = ys.data(),
                                  .count = ys.size()}})); // a second Bars: not
    CHECK(refused("already has a series named"));

    // Heatmap is the one new DATA SHAPE — a grid, not arrays — and it
    // is proven by pixels rather than by vertex count: a grid whose
    // left column is cold and right column is hot must render a
    // cold-to-hot gradient across the shot. Swapping rows and cols
    // would break it, which is the point.
    {
        App hm({.size = {400, 300}, .headless = true});
        constexpr std::size_t R = 4, C = 8;
        std::vector<float> grid(R * C);
        for (std::size_t r = 0; r < R; ++r)
            for (std::size_t c = 0; c < C; ++c)
                grid[r * C + c] = float(c) / float(C - 1);
        hm.Plot({.title = "heat", .palette = Palette::Viridis})
            .Heatmap("grid", grid, R, C, {.scale_min = 0.0, .scale_max = 1.0});
        settle(hm);
        int hv = total_vertices();
        CHECK_GT(hv, 0);
        Bmp img;
        REQUIRE(harness::shot(hm, "heat", img));
        // The shot holds the window, its axes and the COLOURBAR too —
        // a thin strip of the same palette, so its cold end looks like
        // a cell's. Tell them apart by shape: a cell in a 4x8 grid is
        // taller than it is wide; the bar is a 60px-wide strip. Find
        // the cold purple whose vertical run is the tallest.
        const std::array<int, 3> cold{68, 1, 84};
        auto is_cold = [&](unsigned x, unsigned y) {
            const auto &p = img.at(x, y);
            return std::abs(p[0] - cold[0]) + std::abs(p[1] - cold[1]) +
                       std::abs(p[2] - cold[2]) <
                   30;
        };
        unsigned cx0 = 0, cy0 = 0, best_h = 0;
        for (unsigned x = 0; x < img.w; ++x) {
            unsigned y = 0;
            while (y < img.h) {
                if (!is_cold(x, y)) {
                    ++y;
                    continue;
                }
                unsigned y1 = y;
                while (y1 + 1 < img.h && is_cold(x, y1 + 1))
                    ++y1;
                if (y1 - y + 1 > best_h) {
                    best_h = y1 - y + 1;
                    cx0 = x;
                    cy0 = y;
                }
                y = y1 + 1;
            }
        }
        REQUIRE(best_h > 0);
        // Two things, and the second catches a transposed grid. Across
        // a row the colour must CHANGE a lot — a gradient, not a fill.
        // And the cold cell must be TALL: 4 rows over 8 columns makes
        // each cell taller than wide, so its cold run is the whole cell
        // height; read as 8x4 the cell is wide and short.
        const unsigned y_in = cy0 + best_h / 2;
        CHECK_GT(img.diff(cx0 + 2, y_in, cx0 + best_h, y_in), 100);
        CHECK_GT(best_h, 40u);
    }

    // An empty source is not an error — a sim that has produced no
    // samples yet simply draws nothing.
    p.Line("empty", [] { return std::span<const float>(); });
    app.Step();
    CHECK(bool(p));

    // The refusals. A builder method returns itself so chains read,
    // so what a refusal changes is the reported sentence.
    p.Line("pulled", ys);
    CHECK(refused("already has a series"));
    CHECK(!app.Plot({.title = "trace"}));
    CHECK(refused("already the title"));
    CHECK(!app.Plot(
        {.title = "bad", .y = {.min = 1, .max = 1, .fit = Fit::Fixed}}));
    CHECK(refused("min is not below max"));
    CHECK(!app.Plot({.title = ""}));
    CHECK(refused("needs a title"));

    // A panel of widgets: no ImGui in the caller's hands.
    float speed = 1.0f;
    bool on = true;
    int clicks = 0;
    const int before_panel = total_vertices();
    auto panel = app.Panel("controls");
    CHECK(bool(panel));
    panel.Text("a label").Separator().Slider("speed", speed, 0.0f, 4.0f);
    panel.Checkbox("running", on).Button("reset", [&] { ++clicks; });

    // A value is bound or pulled — the same duality a series has.
    int reads = 0;
    panel.Value("speed", speed).Value("reads", [&] {
        ++reads;
        return 1.0;
    });
    settle(app);
    CHECK_GT(total_vertices(), before_panel);
    CHECK_EQ(reads, 2);            // pulled once a frame, like a series
    CHECK(!app.Panel("controls")); // same window
    CHECK(!app.Panel(""));

    // A long series must SPLIT rather than wrap its indices: headless
    // claims RendererHasVtxOffset precisely so its geometry matches
    // the window's.
    // The second plot FAMILY. Plot3D is a sibling builder over the
    // same impl: same window, same title namespace, same source pull,
    // same dtype erasure — and a different library opening the plot
    // and drawing the items. Each kind is proven the way the 2D ones
    // are: it puts geometry on the frame the plot did not have.
    {
        App a3({.headless = true});
        if (a3) {
            std::vector<float> px = {0.0f, 1.0f, 2.0f, 3.0f};
            std::vector<float> py = {0.0f, 1.0f, 0.0f, 1.0f};
            std::vector<float> pz = {0.0f, 0.5f, 1.0f, 1.5f};
            auto p3 = a3.Plot3D({.title = "orbit",
                                 .x = {.label = "x"},
                                 .y = {.label = "y"},
                                 .z = {.label = "z"}});
            REQUIRE(bool(p3));
            settle(a3);
            const int empty3 = total_vertices();
            CHECK_GT(empty3, 0); // an empty 3D plot still draws its box

            int before3 = total_vertices();
            p3.Line("path", px, py, pz);
            settle(a3);
            CHECK_GT(total_vertices(), before3);

            before3 = total_vertices();
            p3.Scatter("pts", px, py, pz);
            settle(a3);
            CHECK_GT(total_vertices(), before3);

            // Surface: three coordinate grids of x_count*y_count. Not a
            // z-grid over bounds — that is Heatmap's shape, and the two
            // are deliberately not one vocabulary.
            constexpr std::size_t XC = 6, YC = 5;
            std::vector<float> gx(XC * YC), gy(XC * YC), gz(XC * YC);
            for (std::size_t j = 0; j < YC; ++j)
                for (std::size_t i = 0; i < XC; ++i) {
                    gx[j * XC + i] = float(i);
                    gy[j * XC + i] = float(j);
                    gz[j * XC + i] = std::sin(float(i)) * std::cos(float(j));
                }
            before3 = total_vertices();
            p3.Surface("wave", gx, gy, gz, XC, YC);
            settle(a3);
            CHECK_GT(total_vertices(), before3);

            // Mesh: ImPlot3D's own cube, de-interleaved into the three
            // arrays a sim would hold, plus its index buffer.
            std::vector<float> mx, my, mz;
            for (int i = 0; i < ImPlot3D::CUBE_VTX_COUNT; ++i) {
                mx.push_back(ImPlot3D::cube_vtx[i].x);
                my.push_back(ImPlot3D::cube_vtx[i].y);
                mz.push_back(ImPlot3D::cube_vtx[i].z);
            }
            std::vector<unsigned> mi(ImPlot3D::cube_idx,
                                     ImPlot3D::cube_idx +
                                         ImPlot3D::CUBE_IDX_COUNT);
            before3 = total_vertices();
            p3.Mesh("cube", mx, my, mz, mi);
            settle(a3);
            CHECK_GT(total_vertices(), before3);

            // The family is on the PLOT and the kind is on the SERIES,
            // and a mismatch is refused BY NAME — the sentence, not the
            // false, because a bare `!x` here would pass for the wrong
            // reason once the emitter ignored the kind.
            CHECK(!impl::plot_series(
                p3.Raw(), impl::SeriesDesc{
                              .name = "wrong",
                              .kind = impl::SeriesKind::Line,
                              .data = {.b = py.data(), .count = py.size()}}));
            CHECK(refused("3D plot and that is a 2D series"));
            auto p2 = a3.Plot({.title = "flat"});
            REQUIRE(bool(p2));
            CHECK(!impl::plot_series(
                p2.Raw(), impl::SeriesDesc{.name = "wrong",
                                           .kind = impl::SeriesKind::Line3,
                                           .data = {.a = px.data(),
                                                    .b = py.data(),
                                                    .c = pz.data(),
                                                    .count = px.size()}}));
            CHECK(refused("2D plot and that is a 3D series"));

            // A pulled 3D series, asked once per frame like the 2D ones.
            int pulls3 = 0;
            p3.Line("live", [&] {
                ++pulls3;
                return Points3<float>{px, py, pz};
            });
            settle(a3);
            const int after_two = pulls3;
            a3.Step();
            CHECK_EQ(pulls3, after_two + 1);
        }
    }

    // The transport: app.Controls(sim) is a panel led by a widget that
    // READS the Executor. Its buttons are eyes-only like every button;
    // what a headless check can pin is that the panel builds geometry
    // and that it reflects state — the label it draws is computed from
    // executor_playing, so a running sim and a paused one differ.
    {
        App ac({.headless = true});
        if (ac) {
            Executor sim([] {});
            auto ctl = ac.Controls(sim);
            REQUIRE(bool(ctl));
            ctl.Slider("chained", *new float(0.0f), 0.0f, 1.0f);
            settle(ac);
            CHECK_GT(total_vertices(), 0);
            // The transport refuses to exist with nothing to drive.
            CHECK(!impl::panel_widget(
                ac.Panel("bare").Raw(),
                impl::WidgetDesc{.kind = impl::WidgetKind::Transport}));
            CHECK(refused("needs an Executor"));
        }
    }

    {
        App big({.headless = true});
        std::vector<float> many(100000);
        for (std::size_t i = 0; i < many.size(); ++i)
            many[i] = std::sin(float(i) * 0.01f);
        auto bp = big.Plot({.title = "many"});
        bp.Line("many", many);
        settle(big);
        CHECK_GT(total_vertices(), 65535);
        CHECK(any_vtx_offset());
    }

    return check::summary("plot");
}
