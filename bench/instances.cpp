// What a crowd of instanced geometry costs, and what the two things
// that decide how much of it to draw are worth: the triangle tier and
// the view cull. Measured rather than assumed — both constants in
// Cloud.cpp's choose_tier come from this file.
//
// The number is the GPU time of the world's "scene" section, read off
// the device clock through the same timing path a user gets with
// SIMVIEW_TIMINGS=1. It is not a frame rate: nothing here presents,
// and a headless frame has no vsync to wait for.

#include "harness/Harness.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

std::vector<float> disc(std::size_t n, float radius) {
    std::vector<float> p(n * 3);
    for (std::size_t i = 0; i < n; ++i) {
        const float t = float(i) * 2.39996323f;
        const float r = radius * std::sqrt(float(i + 1) / float(n));
        p[i * 3 + 0] = r * std::cos(t);
        p[i * 3 + 1] = r * std::sin(t);
        p[i * 3 + 2] = 0.25f * radius * std::sin(0.013f * float(i));
    }
    return p;
}

// The BEST of a few frames' scene sections, in milliseconds. The best
// and not the mean or the median: every other frame is this one plus
// something that interfered, and on a shared integrated GPU the
// interference is larger than the thing being measured — three runs
// of the median disagreed by threefold, the minimum by a few percent.
double scene_ms(sv::App &app, int frames) {
    std::vector<double> seen;
    for (int i = 0; i < frames; ++i) {
        Bmp img;
        if (!harness::shot(app, "bench", img))
            return -1.0;
        sv::probe::GpuSection s[16];
        const std::size_t n = sv::probe::gpu_sections(app.Raw(), s, 16);
        // A world stamps a section per PASS, so the world's cost is
        // their sum. "scene" is the 2D path's name and is not here.
        double total = 0.0;
        bool any = false;
        for (std::size_t k = 0; k < n; ++k) {
            const std::string name(s[k].name);
            if (name == "shadow" || name == "opaque" || name == "ground" ||
                name == "transparent" || name == "overlay") {
                total += double(s[k].end_ns - s[k].begin_ns) / 1e6;
                any = true;
            }
        }
        if (any)
            seen.push_back(total);
    }
    if (seen.empty())
        return -1.0;
    std::sort(seen.begin(), seen.end());
    return seen.front();
}

struct Row {
    std::size_t count;
    double billboard, sphere;
    unsigned triangles;
};

// One world filled with `n` points of the given shape, posed so the
// whole disc is in frame at `distance`.
struct Scene {
    sv::App app;
    sv::World world;
    sv::Cloud cloud;
};

unsigned per_instance(sv::App &app, const char *title, std::size_t n) {
    std::uint64_t t[4] = {0, 0, 0, 0};
    if (sv::probe::item_triangles(app.Raw(), title, t, 4) == 0 || !n)
        return 0;
    return unsigned(t[0] / std::uint64_t(n));
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    // ── what a crowd costs, by shape ─────────────────────────────────
    const std::size_t counts[] = {1000, 4000, 20000, 100000, 500000};
    std::vector<Row> rows;

    for (std::size_t n : counts) {
        Row row{n, -1.0, -1.0, 0};
        const std::vector<float> pts = disc(n, 40.0f);

        for (int pass = 0; pass < 2; ++pass) {
            App app({.size = {960, 720}, .headless = true});
            if (!app) {
                std::printf("bench: no device (%s)\n", LastError());
                return 0;
            }
            sv::World w = app.World({.grid = false, .axes = false});
            if (!w)
                return 1;
            w.Camera({.focus = {0.0f, 0.0f, 0.0f},
                      .distance = 90.0f,
                      .azimuth_deg = -40.0f,
                      .elevation_deg = 25.0f});
            sv::Cloud c = w.Cloud(
                {.color = {0.7f, 0.8f, 1.0f, 1.0f},
                 .radius = 0.25f,
                 .shape = pass ? CloudShape::Sphere : CloudShape::Billboard});
            if (!c || !c.Update(pts))
                return 1;
            app.Step();
            app.Step();
            const double ms = scene_ms(app, 25);
            if (pass) {
                row.sphere = ms;
                row.triangles = per_instance(app, nullptr, n);
            } else {
                row.billboard = ms;
            }
        }
        rows.push_back(row);
    }

    std::printf("\ninstanced draw cost, one world, 960x720, headless, "
                "4x multisampled\n");
    std::printf("the number is the GPU time of the world's passes, "
                "best of 25 frames\n");
    std::printf("every sphere here is 2.4 px of radius on screen, so the "
                "tier is the cheap one\n\n");
    std::printf("  %-10s %-16s %-16s %s\n", "instances", "billboard",
                "sphere mesh", "triangles/sphere");
    for (const Row &r : rows) {
        if (r.billboard < 0 || r.sphere < 0) {
            std::printf("  %-10zu (no timings — SIMVIEW_TIMINGS unset?)\n",
                        r.count);
            continue;
        }
        char a[24], b[24];
        std::snprintf(a, sizeof a, "%.3f ms", r.billboard);
        std::snprintf(b, sizeof b, "%.3f ms", r.sphere);
        std::printf("  %-10zu %-16s %-16s %u\n", r.count, a, b, r.triangles);
    }
    // No billboard-against-sphere column: the billboard numbers are
    // small enough to sit near this measurement's own noise (three
    // runs of the same build disagreed threefold at the larger
    // counts), and a ratio between a steady number and an unsteady one
    // says less than either of them alone.

    // ── what the tier is worth, at a fixed crowd ─────────────────────
    // The same twenty thousand spheres, flown in. Nothing about the
    // crowd changes — only how big each one is on screen, which is the
    // signal the tier is chosen from. Fill rises with the distance
    // too, so the column to read is triangles against milliseconds,
    // not the milliseconds alone.
    std::printf("\nthe tier, at a fixed 20000 spheres: what changes is the "
                "camera\n\n");
    std::printf("  %-12s %-10s %-18s %-16s %s\n", "distance", "radius px",
                "triangles/sphere", "triangles", "pass time");
    struct TierRow {
        float distance;
        unsigned tri;
        double ms;
    };
    std::vector<TierRow> tiers;
    constexpr std::size_t kTierN = 20000;
    for (float distance : {200.0f, 90.0f, 40.0f, 18.0f, 8.0f}) {
        App app({.size = {960, 720}, .headless = true});
        if (!app)
            return 0;
        sv::World w = app.World({.grid = false, .axes = false});
        if (!w)
            return 1;
        w.Camera({.focus = {0.0f, 0.0f, 0.0f}, .distance = distance});
        sv::Cloud c = w.Cloud({.color = {0.7f, 0.8f, 1.0f, 1.0f},
                               .radius = 0.25f,
                               .shape = CloudShape::Sphere});
        if (!c || !c.Update(disc(kTierN, 40.0f)))
            return 1;
        app.Step();
        app.Step();
        const double ms = scene_ms(app, 25);
        const unsigned tri = per_instance(app, nullptr, kTierN);
        tiers.push_back({distance, tri, ms});
        char px[24], tot[24], t[24];
        std::snprintf(px, sizeof px, "%.1f px",
                      double(0.25f * (720.0f / (2.0f * 0.41421f)) / distance));
        std::snprintf(tot, sizeof tot, "%.2f M",
                      double(tri) * double(kTierN) / 1e6);
        std::snprintf(t, sizeof t, "%.3f ms", ms);
        char d[24];
        std::snprintf(d, sizeof d, "%.0f units", double(distance));
        std::printf("  %-12s %-10s %-18u %-16s %s\n", d, px, tri, tot, t);
    }

    // The budget in choose_tier is a triangle count, so this is the
    // number that sets it: what a frame's worth of milliseconds buys.
    for (const TierRow &r : tiers)
        if (r.tri > 500 && r.ms > 0.0) {
            const double per_m = r.ms / (double(r.tri) * double(kTierN) / 1e6);
            std::printf("\nat the fine tier: %.3f ms per million triangles, "
                        "so a 16 ms frame is about\n%.0f million — the "
                        "budget in choose_tier is set well under that, "
                        "because\nthe frame has a scene to draw as well as "
                        "this one crowd.\n",
                        per_m, 16.0 / per_m);
            break;
        }

    // ── what culling is worth ────────────────────────────────────────
    // One camera, one scene, the test switched off and on. Nothing
    // else differs, so the difference IS the saving — where a second
    // camera pointed somewhere emptier would change the fill as well
    // and measure the two together.
    {
        App app({.size = {960, 720}, .headless = true});
        if (!app)
            return 0;
        sv::World w = app.World({.grid = false, .axes = false});
        if (!w)
            return 1;
        // Sixty-four crowds spread over a wide field, and a camera on
        // one corner of it.
        constexpr int kClouds = 64;
        std::vector<sv::Cloud> keep;
        for (int i = 0; i < kClouds; ++i) {
            const float x = float(i % 8) * 120.0f - 420.0f;
            const float y = float(i / 8) * 120.0f - 420.0f;
            sv::Cloud c = w.Cloud({.color = {0.7f, 0.8f, 1.0f, 1.0f},
                                   .radius = 0.6f,
                                   .shape = CloudShape::Sphere});
            if (!c)
                return 1;
            std::vector<float> pts = disc(4000, 40.0f);
            for (std::size_t k = 0; k < pts.size(); k += 3) {
                pts[k] += x;
                pts[k + 1] += y;
            }
            if (!c.Update(pts))
                return 1;
            keep.push_back(std::move(c));
        }
        w.Camera({.focus = {-420.0f, -420.0f, 0.0f}, .distance = 120.0f});
        app.Step();
        app.Step();

        probe::culling(app.Raw(), false);
        const double off = scene_ms(app, 25);
        const std::uint64_t draws_off = app.Stats().draws;
        probe::culling(app.Raw(), true);
        const double on = scene_ms(app, 25);
        const std::uint64_t culled_before = app.Stats().culled;
        Bmp last;
        (void)harness::shot(app, "bench_cull", last);
        const std::uint64_t culled = app.Stats().culled - culled_before;
        (void)draws_off;

        std::printf("\nthe view cull: %d crowds of 4000 spread over 960 "
                    "world units, camera on\none corner of the field, "
                    "%llu of them off screen\n\n",
                    kClouds, static_cast<unsigned long long>(culled));
        std::printf("  %-22s %s\n", "culling off", "culling on");
        char a[24], b[24];
        std::snprintf(a, sizeof a, "%.3f ms", off);
        std::snprintf(b, sizeof b, "%.3f ms", on);
        std::printf("  %-22s %s", a, b);
        if (off > 0.0 && on > 0.0)
            std::printf("   (%.1fx faster with it on)", off / on);
        std::printf("\n");
    }

    std::printf("\nwhat moves all of these: the device, the window size "
                "(fill is most of the\nfirst table), the sample count, and "
                "for the tier rows the camera distance,\nwhich is what "
                "chooses the mesh.\n\n");
    return 0;
}
