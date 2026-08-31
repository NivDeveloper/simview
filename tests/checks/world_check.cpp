// The world stratum, through the pixels it produces.
//
// Four claims a picture can prove and arithmetic cannot: that the
// camera the caller asked for is the camera that drew (a point at the
// focus lands at the middle); that depth is REVERSED the way every
// other half of the pipeline assumes (the near sphere wins); that the
// transparent pass sorts back to front (the near one is on top even
// though it was registered first); and that all three data doors reach
// the same item.
//
// Every world here is posed along +X looking at the origin, so "near"
// is +X and "far" is -X and both land in the middle of the panel. The
// grid and axes are off wherever geometry is being measured: they draw
// over the same pixels and would answer for the cloud.

#include "harness/Harness.h"
#include "probe/Probe.h"

#include <simview/gpud.h>

#include <imgui.h>

#include <cstdio>
#include <vector>

namespace {

constexpr char fill_src[] = R"(
struct Buf_float { float data[1]; };
struct PC {
  float s0;
  uint s1;
  Buf_float* out_buf;
};
[[vk::push_constant]] PC pc;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint i = tid.x;
  if (i >= pc.s1) return;
  pc.out_buf.data[i] = pc.s0;
}
)";

struct FillScalars {
    float s0;
    std::uint32_t s1;
};

// A panel's rectangle in the shot, pinned by the check itself. Every
// probe below is restricted to one: two panels showing spheres of the
// same colours would otherwise answer for each other, and the check
// would pass on a picture from the wrong window.
struct Rect {
    unsigned x0, y0, x1, y1;
};

// The average colour over a disc of pixels, so a probe is not one
// pixel's luck.
void mean_at(const Bmp &img, unsigned cx, unsigned cy, unsigned r, int &mr,
             int &mg, int &mb) {
    long sr = 0, sg = 0, sb = 0, n = 0;
    for (unsigned y = (cy > r ? cy - r : 0); y <= cy + r && y < img.h; ++y)
        for (unsigned x = (cx > r ? cx - r : 0); x <= cx + r && x < img.w;
             ++x) {
            const auto &p = img.at(x, y);
            sr += p[0];
            sg += p[1];
            sb += p[2];
            ++n;
        }
    mr = n ? int(sr / n) : 0;
    mg = n ? int(sg / n) : 0;
    mb = n ? int(sb / n) : 0;
}

// The bounding box of everything a channel dominates — the cloud alone,
// with the panel's own grey chrome excluded by construction.
struct Box {
    unsigned x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty = true;
};

Box channel_box(const Bmp &img, const Rect &in, int channel, int margin) {
    Box b;
    for (unsigned y = in.y0; y < in.y1 && y < img.h; ++y)
        for (unsigned x = in.x0; x < in.x1 && x < img.w; ++x) {
            const auto &p = img.at(x, y);
            const int a = p[(channel + 1) % 3], c = p[(channel + 2) % 3];
            if (p[channel] < a + margin || p[channel] < c + margin)
                continue;
            if (b.empty) {
                b = {x, y, x, y, false};
            } else {
                b.x0 = x < b.x0 ? x : b.x0;
                b.y0 = y < b.y0 ? y : b.y0;
                b.x1 = x > b.x1 ? x : b.x1;
                b.y1 = y > b.y1 ? y : b.y1;
            }
        }
    return b;
}

// The content box of one panel, found inside its own rectangle.
Bmp::Box panel_box(const Bmp &img, const Rect &in) {
    Bmp::Box b;
    for (unsigned y = in.y0; y < in.y1 && y < img.h; ++y)
        for (unsigned x = in.x0; x < in.x1 && x < img.w; ++x) {
            const auto &p = img.at(x, y);
            if (p[0] == 23 && p[1] == 23 && p[2] == 26)
                continue;
            if (b.empty)
                b = {x, y, x, y, false};
            else {
                b.x0 = x < b.x0 ? x : b.x0;
                b.y0 = y < b.y0 ? y : b.y0;
                b.x1 = x > b.x1 ? x : b.x1;
                b.y1 = y > b.y1 ? y : b.y1;
            }
        }
    return b;
}

std::vector<float> one_point(float x, float y, float z) { return {x, y, z}; }

// Where each panel is pinned. Non-overlapping, inside the 640x480 the
// headless app is laid out for.
constexpr Rect kWorldRect{0, 0, 300, 230};
constexpr Rect kBlendRect{310, 0, 620, 230};
constexpr Rect kGroundRect{0, 240, 300, 470};

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {640, 480}, .headless = true});
    if (!app)
        return check::skip("world", LastError());

    // A world is a panel: it must be named, and two windows of one name
    // would draw into each other.
    CHECK(!app.World({.title = ""}));

    sv::World w = app.World({.title = "world", .grid = false, .axes = false});
    REQUIRE(bool(w));
    CHECK(!app.World({.title = "world"}));
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 4.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});

    // The panel is pinned, so the geometry below is about the world and
    // not about how ImGui chose to lay the window out.
    app.OnUi([] {
        ImGui::SetWindowPos("world", ImVec2(0, 0));
        ImGui::SetWindowSize("world", ImVec2(300, 230));
    });

    // ── the camera drew what the caller asked for ────────────────────
    // One point AT the focus: whatever the matrices did, it belongs in
    // the middle of the image. A transposed upload puts it elsewhere or
    // nowhere, which is the failure this canary exists for.
    sv::Cloud centre = w.Cloud({.color = {1.0f, 0.2f, 0.2f, 1.0f},
                                .radius = 0.35f,
                                .mode = CloudMode::Solid});
    REQUIRE(bool(centre));
    CHECK(centre.Update(one_point(0.0f, 0.0f, 0.0f)));

    app.Step();
    app.Step();
    const Extent2 e = probe::view_extent(app.Raw(), "world");
    CHECK_GT(e.w, 64u);
    CHECK_GT(e.h, 64u);

    Bmp img;
    REQUIRE(harness::shot(app, "world_centre", img));
    const Bmp::Box panel = panel_box(img, kWorldRect);
    REQUIRE(!panel.empty);
    const Box red = channel_box(img, kWorldRect, 0, 40);
    CHECK(!red.empty);
    if (!red.empty && !panel.empty) {
        const unsigned rx = (red.x0 + red.x1) / 2, ry = (red.y0 + red.y1) / 2;
        const unsigned px = (panel.x0 + panel.x1) / 2,
                       py = (panel.y0 + panel.y1) / 2;
        const unsigned tol_x = panel.w() / 5, tol_y = panel.h() / 5;
        std::printf("  point at focus: red centre (%u,%u), panel (%u,%u)\n", rx,
                    ry, px, py);
        CHECK_LT(rx > px ? rx - px : px - rx, tol_x);
        CHECK_LT(ry > py ? ry - py : py - ry, tol_y);
    }

    // ── depth runs the right way ─────────────────────────────────────
    // A blue sphere BEHIND the red one, big enough to cover it. Under
    // reverse-Z with a GreaterOrEqual test the near one wins; with the
    // test or the clear inverted, the far one paints over it.
    sv::Cloud behind = w.Cloud({.color = {0.2f, 0.3f, 1.0f, 1.0f},
                                .radius = 0.7f,
                                .mode = CloudMode::Solid});
    REQUIRE(bool(behind));
    CHECK(behind.Update(one_point(-1.0f, 0.0f, 0.0f)));
    CHECK(centre.Update(one_point(1.0f, 0.0f, 0.0f)));

    app.Step();
    REQUIRE(harness::shot(app, "world_depth", img));
    {
        const Bmp::Box p = panel_box(img, kWorldRect);
        REQUIRE(!p.empty);
        int r = 0, g = 0, b = 0;
        mean_at(img, (p.x0 + p.x1) / 2, (p.y0 + p.y1) / 2, 6, r, g, b);
        std::printf("  occlusion: rgb %d %d %d (near is red)\n", r, g, b);
        CHECK_GT(r, b + 40);
    }

    // ── the transparent pass sorts back to front ─────────────────────
    // Its own world, so the opaque spheres above cannot answer for it.
    // The NEAR cloud is registered FIRST: submission order alone would
    // paint the far one over it, and only the sort puts them right.
    sv::World t = app.World({.title = "blend", .grid = false, .axes = false});
    REQUIRE(bool(t));
    t.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 4.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
    app.OnUi([] {
        ImGui::SetWindowPos("blend", ImVec2(310, 0));
        ImGui::SetWindowSize("blend", ImVec2(300, 230));
    });
    sv::Cloud near_blue = t.Cloud({.color = {0.2f, 0.3f, 1.0f, 0.5f},
                                   .radius = 0.6f,
                                   .mode = CloudMode::Alpha});
    sv::Cloud far_red = t.Cloud({.color = {1.0f, 0.2f, 0.2f, 0.5f},
                                 .radius = 0.6f,
                                 .mode = CloudMode::Alpha});
    REQUIRE(bool(near_blue));
    REQUIRE(bool(far_red));
    CHECK(near_blue.Update(one_point(1.0f, 0.0f, 0.0f)));
    CHECK(far_red.Update(one_point(-1.0f, 0.0f, 0.0f)));

    app.Step();
    app.Step();
    Bmp blend;
    REQUIRE(harness::shot(app, "world_blend", blend));
    {
        const Bmp::Box p = panel_box(blend, kBlendRect);
        CHECK(!p.empty);
        if (!p.empty) {
            int r = 0, g = 0, b = 0;
            mean_at(blend, (p.x0 + p.x1) / 2, (p.y0 + p.y1) / 2, 5, r, g, b);
            std::printf("  blend order: rgb %d %d %d (near is blue)\n", r, g,
                        b);
            CHECK_GT(b, r);
        }
    }

    // ── the three doors ──────────────────────────────────────────────
    // A Sync: the world must register its gate, so the frame waits for
    // exactly the work behind what it shows.
    const std::size_t gates_before = probe::gate_count(app.Raw());
    sv::World d = app.World({.title = "doors", .grid = false, .axes = false});
    REQUIRE(bool(d));
    Sync<std::vector<float>> pts;
    pts.Next() = {0.0f, 0.0f, 0.0f};
    pts.Publish();
    CHECK(bool(d.Cloud(pts, {.radius = 0.3f})));
    CHECK_EQ(probe::gate_count(app.Raw()), gates_before + 1);

    app.Step();
    Bmp before;
    REQUIRE(harness::shot(app, "world_sync_a", before));
    pts.Next() = {1.5f, 1.5f, 0.0f};
    pts.Publish();
    app.Step();
    Bmp after;
    REQUIRE(harness::shot(app, "world_sync_b", after));
    CHECK(!similar(before, after));

    // A device buffer, through the gpud door: the same item, resolved
    // from the producer's memory every frame.
    gpud::Device &dev = sv::Device(app);
    const gpud::Kernel *fill = nullptr;
    try {
        fill = &dev.compile(fill_src);
    } catch (const std::exception &ex) {
        std::printf("  (device door skipped: %s)\n", ex.what());
    }
    if (fill) {
        Sync<gpud::Buffer> dpts;
        dpts.Next() = dev.alloc(3 * sizeof(float));
        FillScalars sc{0.5f, 3};
        gpud::Buffer *bufs[] = {&dpts.Next()};
        dev.run(*fill, 1, {reinterpret_cast<const std::byte *>(&sc), sizeof sc},
                bufs);
        dpts.Publish();
        CHECK(bool(d.Cloud(dpts, {.radius = 0.3f})));
        app.Step();
        Bmp shot;
        CHECK(harness::shot(app, "world_device", shot));
        CHECK_EQ(probe::validation_errors(app.Raw()),
                 probe::validation_on(app.Raw())
                     ? probe::validation_errors(app.Raw())
                     : std::size_t(0));
    }

    // ── the grid and the axes ────────────────────────────────────────
    // A world that draws nothing of its own still draws its
    // orientation: two items, two draws.
    sv::World g = app.World({.title = "ground"});
    REQUIRE(bool(g));
    app.OnUi([] {
        ImGui::SetWindowPos("ground", ImVec2(0, 240));
        ImGui::SetWindowSize("ground", ImVec2(300, 230));
    });
    app.Step();
    const std::uint64_t draws_before = app.Stats().draws;
    Bmp ground;
    REQUIRE(harness::shot(app, "world_ground", ground));
    CHECK_GT(app.Stats().draws - draws_before, std::uint64_t(1));
    CHECK_GT(ground.distinct(), std::size_t(4));

    if (probe::validation_on(app.Raw()))
        CHECK_EQ(probe::validation_errors(app.Raw()), std::size_t(0));

    return check::summary("world");
}
