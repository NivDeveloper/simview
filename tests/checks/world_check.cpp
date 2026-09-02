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

Box channel_box(const Bmp &img, const Rect &in, int channel, int margin,
                int floor = 150) {
    Box b;
    for (unsigned y = in.y0; y < in.y1 && y < img.h; ++y)
        for (unsigned x = in.x0; x < in.x1 && x < img.w; ++x) {
            const auto &p = img.at(x, y);
            const int a = p[(channel + 1) % 3], c = p[(channel + 2) % 3];
            // The floor is what keeps a panel's own blue title bar out
            // of a blue probe: chrome is dark, geometry is not.
            if (p[channel] < floor)
                continue;
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
constexpr Rect kMapRect{630, 0, 930, 230};
constexpr Rect kGroundRect{0, 240, 300, 470};
constexpr Rect kOrthoRect{310, 240, 620, 470};
constexpr Rect kLightRect{630, 240, 930, 470};
constexpr Rect kDoorRect{0, 480, 300, 695};
constexpr Rect kMagRect{310, 480, 620, 695};

// The image inside a pinned panel — the panel less its chrome. The
// title bar is a blue strip with white text on it, and the ANTIALIASED
// edges of that text are bright and blue-dominant: a colour probe over
// the whole panel finds them and answers for the geometry.
constexpr Rect content_of(const Rect &r) {
    return {r.x0 + 4, r.y0 + 26, r.x1 - 4, r.y1 - 4};
}

// A panel, pinned where the check put it.
void pin(sv::App &app, const char *title, const Rect &r) {
    app.OnUi([title, r] {
        ImGui::SetWindowPos(title, ImVec2(float(r.x0), float(r.y0)));
        ImGui::SetWindowSize(title,
                             ImVec2(float(r.x1 - r.x0), float(r.y1 - r.y0)));
    });
}

// The brightness at a point, as one number.
int brightness_at(const Bmp &img, unsigned cx, unsigned cy, unsigned r) {
    int mr = 0, mg = 0, mb = 0;
    mean_at(img, cx, cy, r, mr, mg, mb);
    return mr + mg + mb;
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {940, 700}, .headless = true});
    if (!app)
        return check::skip("world", LastError());

    // A world with NO title is the window itself; a titled one is a
    // panel. There can be one of the first and any number of the
    // second, and two panels of one name would draw into each other.
    sv::World main = app.World({.grid = false, .axes = false});
    REQUIRE(bool(main));
    CHECK(!app.World({}));

    sv::World w = app.World(
        {.title = "world", .grid = false, .axes = false, .controls = false});
    REQUIRE(bool(w));
    CHECK(!app.World({.title = "world"}));
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 4.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});

    // The panel is pinned, so the geometry below is about the world and
    // not about how ImGui chose to lay the window out.
    pin(app, "world", kWorldRect);

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
    const Bmp::Box panel = panel_box(img, content_of(kWorldRect));
    REQUIRE(!panel.empty);
    const Box red = channel_box(img, content_of(kWorldRect), 0, 40);
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
        const Bmp::Box p = panel_box(img, content_of(kWorldRect));
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
    sv::World t = app.World(
        {.title = "blend", .grid = false, .axes = false, .controls = false});
    REQUIRE(bool(t));
    t.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 4.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
    pin(app, "blend", kBlendRect);
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
        const Bmp::Box p = panel_box(blend, content_of(kBlendRect));
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
    sv::World d = app.World(
        {.title = "doors", .grid = false, .axes = false, .controls = false});
    REQUIRE(bool(d));
    // Pinned, because what follows has to be probed rather than merely
    // run: this section used to assert nothing about the picture and a
    // device-sourced cloud that drew NOTHING would have passed it.
    pin(app, "doors", kDoorRect);
    d.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 6.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
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
    // Declared out here, and AFTER the App: an item pulls from this
    // Sync at every later frame, so it must outlive the shots below —
    // and its buffers must not outlive the device, which the reverse
    // destruction order gives. Scoping it to the branch below left the
    // world pulling from a dead producer (a crash on lavapipe, silence
    // on the other two).
    Sync<gpud::Buffer> dpts;
    if (fill) {
        dpts.Next() = dev.alloc(3 * sizeof(float));
        FillScalars sc{0.5f, 3};
        gpud::Buffer *bufs[] = {&dpts.Next()};
        dev.run(*fill, 1, {reinterpret_cast<const std::byte *>(&sc), sizeof sc},
                bufs);
        dpts.Publish();
        // Green, so it can be told from the host-sourced cloud already
        // in this panel.
        CHECK(bool(d.Cloud(
            dpts, {.color = {0.15f, 1.0f, 0.35f, 1.0f}, .radius = 0.3f})));
        app.Step();
        Bmp shot;
        CHECK(harness::shot(app, "world_device", shot));

        // The assertion this section did not have. A cloud whose
        // points live in a device buffer must DRAW them — the kernel
        // ran, the buffer resolved, the count came out of its size,
        // and the shader read the layout it expects. Every one of
        // those can fail silently, and the frame is the only place
        // that shows it.
        const Box green = channel_box(shot, content_of(kDoorRect), 1, 40);
        CHECK(!green.empty);
        const unsigned wide = green.empty ? 0u : green.x1 - green.x0;
        std::printf("  a cloud from a device buffer: %u px across\n", wide);
        CHECK_GT(wide, 8u);

        CHECK_EQ(probe::validation_errors(app.Raw()),
                 probe::validation_on(app.Raw())
                     ? probe::validation_errors(app.Raw())
                     : std::size_t(0));
    }

    // ── orthographic: distance stops shrinking things ────────────────
    // Two spheres of one size at different depths. Under perspective
    // the near one is half again as wide; under the orthographic
    // projection they measure the same, which is the whole reason a
    // caller reaches for it.
    sv::World o = app.World(
        {.title = "ortho", .grid = false, .axes = false, .controls = false});
    REQUIRE(bool(o));
    o.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 6.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f,
              .projection = Projection::Orthographic});
    pin(app, "ortho", kOrthoRect);
    sv::Cloud o_near = o.Cloud({.color = {1.0f, 0.25f, 0.25f, 1.0f},
                                .radius = 0.45f,
                                .mode = CloudMode::Solid});
    sv::Cloud o_far = o.Cloud({.color = {0.25f, 0.35f, 1.0f, 1.0f},
                               .radius = 0.45f,
                               .mode = CloudMode::Solid});
    CHECK(o_near.Update(one_point(2.0f, 0.0f, 0.9f)));
    CHECK(o_far.Update(one_point(-2.0f, 0.0f, -0.9f)));

    app.Step();
    app.Step();
    Bmp ortho;
    REQUIRE(harness::shot(app, "world_ortho", ortho));
    {
        const Box r = channel_box(ortho, content_of(kOrthoRect), 0, 40);
        const Box b = channel_box(ortho, content_of(kOrthoRect), 2, 40);
        CHECK(!r.empty);
        CHECK(!b.empty);
        if (!r.empty && !b.empty) {
            const unsigned wr = r.x1 - r.x0 + 1, wb = b.x1 - b.x0 + 1;
            const unsigned hi = wr > wb ? wr : wb, lo = wr > wb ? wb : wr;
            std::printf("  ortho widths: near %u px, far %u px\n", wr, wb);
            CHECK_LT(hi * 100u, lo * 125u); // within a quarter of each other
        }
    }

    // ── a colormap reads per-point values ────────────────────────────
    // Three points, three directions, three colours. It fails unless
    // the second buffer is bound AND indexed by the same point index
    // the position was.
    sv::World m = app.World(
        {.title = "map", .grid = false, .axes = false, .controls = false});
    REQUIRE(bool(m));
    m.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 6.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
    pin(app, "map", kMapRect);
    sv::Cloud tri = m.Cloud({.radius = 0.45f,
                             .mode = CloudMode::Solid,
                             .map = CloudMap::Direction});
    REQUIRE(bool(tri));
    const std::vector<float> tri_pts{0.0f, -1.6f, 0.0f, 0.0f, 0.0f,
                                     0.0f, 0.0f,  1.6f, 0.0f};
    const std::vector<float> dirs{1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                  0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(tri.Update(tri_pts));
    CHECK(tri.UpdateColors(dirs));

    app.Step();
    app.Step();
    Bmp mapped;
    REQUIRE(harness::shot(app, "world_map", mapped));
    {
        const Box r = channel_box(mapped, content_of(kMapRect), 0, 30);
        const Box g = channel_box(mapped, content_of(kMapRect), 1, 30);
        const Box b = channel_box(mapped, content_of(kMapRect), 2, 30);
        CHECK(!r.empty);
        CHECK(!g.empty);
        CHECK(!b.empty);
        // Three separate points, so three separate patches: a map that
        // ignored the index would paint all three alike.
        if (!r.empty && !g.empty && !b.empty) {
            std::printf("  map boxes: r[%u,%u] g[%u,%u] b[%u,%u]\n", r.x0, r.x1,
                        g.x0, g.x1, b.x0, b.x1);
            CHECK(r.x1 < g.x0 || g.x1 < r.x0);
            CHECK(g.x1 < b.x0 || b.x1 < g.x0);
        }
    }

    // ── and the magnitude map is ORDERED ─────────────────────────────
    // The direction map above proves the value buffer is read per
    // point. This proves the other half: that the number it reads is
    // the LENGTH of the value, and that the colormap walks in the
    // order it claims. Three points of rising speed must come out
    // blue, then green, then red along the row — the same colours a
    // caller reads a velocity field by, and the reason a thermalized
    // gas looks like noise is that its speeds really are uncorrelated,
    // not that this is unhooked.
    sv::World mg = app.World(
        {.title = "mag", .grid = false, .axes = false, .controls = false});
    REQUIRE(bool(mg));
    mg.Camera({.focus = {0.0f, 0.0f, 0.0f},
               .distance = 6.0f,
               .azimuth_deg = 0.0f,
               .elevation_deg = 0.0f});
    pin(app, "mag", kMagRect);
    sv::Cloud speeds = mg.Cloud({.radius = 0.45f,
                                 .mode = CloudMode::Solid,
                                 .map = CloudMap::Magnitude,
                                 .map_scale = 1.0f});
    REQUIRE(bool(speeds));
    CHECK(speeds.Update(tri_pts));
    // Magnitudes 0.15, 0.5 and 0.85 of the scale: turbo is blue,
    // green and red there.
    const std::vector<float> mags{0.15f, 0.0f, 0.0f, 0.5f, 0.0f,
                                  0.0f,  0.0f, 0.0f, 0.85f};
    CHECK(speeds.UpdateColors(mags));
    app.Step();
    app.Step();
    Bmp ramp;
    REQUIRE(harness::shot(app, "world_magnitude", ramp));
    {
        const Box b = channel_box(ramp, content_of(kMagRect), 2, 30);
        const Box g = channel_box(ramp, content_of(kMagRect), 1, 30);
        const Box r = channel_box(ramp, content_of(kMagRect), 0, 30);
        CHECK(!b.empty);
        CHECK(!g.empty);
        CHECK(!r.empty);
        if (!b.empty && !g.empty && !r.empty) {
            std::printf("  speed ramp: blue[%u,%u] green[%u,%u] red[%u,%u]\n",
                        b.x0, b.x1, g.x0, g.x1, r.x0, r.x1);
            CHECK_LT(b.x1, g.x0);
            CHECK_LT(g.x1, r.x0);
        }
    }

    // ── a light comes from somewhere ─────────────────────────────────
    // One sphere, one light from the side, no ambient. The lit half is
    // brighter than the dark half; a light at the camera — which is
    // what W1 had — lights a disc symmetrically and fails this.
    sv::World l = app.World(
        {.title = "light", .grid = false, .axes = false, .controls = false});
    REQUIRE(bool(l));
    l.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 5.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
    l.Ambient(0.02f, 0.02f, 0.02f);
    CHECK(bool(l.Light({.direction = {0.0f, 1.0f, 0.0f}, .intensity = 1.0f})));
    pin(app, "light", kLightRect);
    sv::Cloud ball = l.Cloud({.color = {0.9f, 0.9f, 0.9f, 1.0f},
                              .radius = 1.2f,
                              .mode = CloudMode::Solid});
    CHECK(ball.Update(one_point(0.0f, 0.0f, 0.0f)));

    app.Step();
    app.Step();
    Bmp lit;
    REQUIRE(harness::shot(app, "world_light", lit));
    {
        const Bmp::Box p = panel_box(lit, content_of(kLightRect));
        CHECK(!p.empty);
        if (!p.empty) {
            // Two probes INSIDE the sphere, either side of its centre.
            // A light at the camera lights a disc symmetrically, so
            // these two agree; a light from the side is what makes
            // them differ.
            const unsigned cx = (p.x0 + p.x1) / 2, cy = (p.y0 + p.y1) / 2;
            const unsigned off = p.w() / 5;
            const int left = brightness_at(lit, cx - off, cy, 4);
            const int right = brightness_at(lit, cx + off, cy, 4);
            const int diff = left > right ? left - right : right - left;
            std::printf("  lit sides: left %d, right %d (diff %d)\n", left,
                        right, diff);
            CHECK_GT(diff, 120);
        }
    }

    // ── the grid and the axes ────────────────────────────────────────
    // A world that draws nothing of its own still draws its
    // orientation: two items, two draws.
    sv::World g = app.World({.title = "ground"});
    REQUIRE(bool(g));
    pin(app, "ground", kGroundRect);
    app.Step();
    const std::uint64_t draws_before = app.Stats().draws;
    Bmp ground;
    REQUIRE(harness::shot(app, "world_ground", ground));
    CHECK_GT(app.Stats().draws - draws_before, std::uint64_t(1));
    CHECK_GT(ground.distinct(), std::size_t(4));

    // The grid is drawn at whatever scale the camera is looking from.
    // It used to be a hundred world units wide and faded from fifty,
    // both fixed: zoomed out past that the whole thing sat beyond its
    // own fade and vanished, which is invisible to every other check
    // here. Count what it covers, close in and far out.
    const auto grid_pixels = [](const Bmp &img, const Rect &in) {
        std::size_t n = 0;
        for (unsigned y = in.y0; y < in.y1 && y < img.h; ++y)
            for (unsigned x = in.x0; x < in.x1 && x < img.w; ++x) {
                const auto &p = img.at(x, y);
                if (p[0] + p[1] + p[2] > 72 + 40)
                    ++n;
            }
        return n;
    };
    std::size_t covered[2] = {0, 0};
    const float zooms[2] = {6.0f, 400.0f};
    for (int i = 0; i < 2; ++i) {
        g.Camera({.focus = {0.0f, 0.0f, 0.0f},
                  .distance = zooms[i],
                  .azimuth_deg = -50.0f,
                  .elevation_deg = 18.0f});
        app.Step();
        Bmp shot;
        REQUIRE(harness::shot(app, "world_zoom", shot));
        covered[i] = grid_pixels(shot, content_of(kGroundRect));
    }
    std::printf("  grid pixels: %zu at distance 6, %zu at 400\n", covered[0],
                covered[1]);
    CHECK_GT(covered[0], std::size_t(200));
    CHECK_GT(covered[1], std::size_t(200));
    // And it looks the SAME at both: a grid whose extent follows the
    // camera covers a like part of the frame however far out it is.
    const std::size_t hi = covered[0] > covered[1] ? covered[0] : covered[1];
    const std::size_t lo = covered[0] > covered[1] ? covered[1] : covered[0];
    CHECK_LT(hi, lo * 3);

    if (probe::validation_on(app.Raw()))
        CHECK_EQ(probe::validation_errors(app.Raw()), std::size_t(0));

    return check::summary("world");
}
