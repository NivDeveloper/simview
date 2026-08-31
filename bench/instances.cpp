// What a crowd of instanced geometry costs, measured rather than
// assumed — the reason the sphere has two triangle tiers at all.
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
        for (std::size_t k = 0; k < n; ++k)
            if (std::string(s[k].name) == "scene")
                seen.push_back(double(s[k].end_ns - s[k].begin_ns) / 1e6);
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

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    // 4096 is where the tier flips, so the two counts either side of
    // it sit next to each other in the table: that pair IS the
    // argument for having two tiers.
    const std::size_t counts[] = {1000, 4000, 5000, 20000, 100000, 500000};
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
            if (pass)
                row.sphere = ms;
            else
                row.billboard = ms;
            if (pass) {
                unsigned tri[4] = {0, 0, 0, 0};
                if (probe::mesh_tiers(app.Raw(), nullptr, tri, 4) > 0)
                    row.triangles = tri[0];
            }
        }
        rows.push_back(row);
    }

    std::printf("\ninstanced draw cost, one world, 960x720, headless, "
                "4x multisampled\n");
    std::printf("the number is the GPU time of the frame's scene section, "
                "best of 25\n\n");
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
    // The pair across the threshold, per instance, which is the only
    // way to compare two counts honestly.
    const Row *fine = nullptr, *coarse = nullptr;
    for (const Row &r : rows) {
        if (r.count == 4000)
            fine = &r;
        if (r.count == 5000)
            coarse = &r;
    }
    if (fine && coarse && fine->sphere > 0 && coarse->sphere > 0)
        std::printf("\nacross the tier boundary, per instance: %.3f us at "
                    "4000 (972 triangles),\n%.3f us at 5000 (108) — a "
                    "factor of %.1f. Measured at 1.7, 2.2 and 2.2 over\n"
                    "three runs of one build, so the tier is worth about "
                    "twice, not a precise number.\n",
                    1000.0 * fine->sphere / double(fine->count),
                    1000.0 * coarse->sphere / double(coarse->count),
                    (fine->sphere / double(fine->count)) /
                        (coarse->sphere / double(coarse->count)));

    std::printf("\nwhat moves these: the device, the window size (fill is "
                "most of it),\nthe sample count, and the instance count's "
                "own tier — a sphere is 972\ntriangles below 4096 instances "
                "and 108 above.\n\n");
    return 0;
}
