// Plots and panels without a display: they are ImGui windows drawing
// into draw lists, so everything but colour and feel is provable here.
// Tests may speak ImGui — they are not consumers.
#include "harness/Harness.h"

#include "probe/Probe.h"

#include <imgui.h>
#include <implot.h>

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

    // Heatmap is the one new DATA SHAPE — a grid, not arrays — and it
    // is proven by pixels rather than by vertex count: a grid whose
    // left column is cold and right column is hot must render a
    // cold-to-hot gradient across the shot. Swapping rows and cols
    // would break it, which is the point.
    {
        App hm({.headless = true, .size = {400, 300}});
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
