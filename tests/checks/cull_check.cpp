// What the view decides not to draw, and what it must never decide
// that about.
//
// Culling is the one optimization in the world stratum whose failures
// are invisible in the frame it is right about and catastrophic in the
// frame it is wrong about: a box that is a little too generous costs a
// draw nobody sees, and a box that is a little too tight DELETES
// geometry. So the assertion here is one-directional and it is the
// dangerous direction — whenever a single pixel of the subject is on
// screen, the subject was not culled. The other direction is not
// claimed: a shape can be off screen and still be drawn, and that is
// the safe way round.
//
// The near plane is here for the same reason. It used to follow the
// orbit distance alone, which put it a whole world unit from the eye
// at a range of a thousand — and anything closer than that simply was
// not there.

#include "harness/Harness.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr unsigned kW = 700, kH = 500;

// Where the panel worlds are pinned, and the image inside one: the
// title bar's antialiased white text is bright and would answer for
// the geometry under it.
struct Rect {
    unsigned x0, y0, x1, y1;
};
constexpr Rect kFarRect{20, 20, 360, 340};
constexpr Rect content_of(const Rect &r) {
    return {r.x0 + 4, r.y0 + 26, r.x1 - 4, r.y1 - 4};
}

// One point, so the subject is a single shape with a known extent and
// every lit pixel belongs to it.
std::vector<float> point(float x, float y, float z) { return {x, y, z}; }

sv::probe::CameraState cam(sv::App &app) {
    sv::probe::CameraState c{};
    CHECK(sv::probe::camera_of(app.Raw(), nullptr, &c));
    return c;
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {kW, kH}, .headless = true});
    if (!app)
        return check::skip("cull", LastError());

    // No grid, no axes: they report no bounds and are never culled, so
    // their pixels would answer for the subject's.
    sv::World w = app.World({.grid = false, .axes = false});
    REQUIRE(bool(w));
    sv::Cloud c = w.Cloud({.color = {1.0f, 0.35f, 0.25f, 1.0f},
                           .radius = 0.5f,
                           .shape = CloudShape::Sphere});
    REQUIRE(bool(c));
    // OFF the focus, which is the only place a turntable can sweep
    // something out of frame: a subject at the focus stays centred
    // however far the camera is turned, and a sweep around one would
    // report that culling never fires while proving nothing.
    CHECK(c.Update(point(3.0f, 0.0f, 0.0f)));

    // Panning the focus DIAGONALLY away, which walks the subject out
    // through a corner of the frame. A corner is where a plane-by-
    // plane test is weakest — a box can be outside the frustum while
    // being in front of all five planes taken one at a time — and it
    // is also the only exit an orbit cannot produce, because a
    // turntable keeps its focus centred however far it is turned.
    const auto sweep = [&](const char *name, float t) {
        w.Camera({.focus = {0.0f, t, 0.6f * t},
                  .distance = 6.0f,
                  .azimuth_deg = 0.0f,
                  .elevation_deg = 0.0f});
        app.Step();
        const std::uint64_t before = app.Stats().culled;
        Bmp shot;
        CHECK(harness::shot(app, name, shot));
        return std::pair<bool, std::size_t>{app.Stats().culled != before,
                                            lit_count(shot, 0, 0, kW, kH)};
    };

    // ── it fires at all ──────────────────────────────────────────────
    // Pointed at the sphere and pointed away from it. Without both
    // halves the sweep below passes on a world where nothing is ever
    // culled and nothing is ever drawn.
    const auto at = sweep("cull_at", 0.0f);
    std::printf("  the subject in frame: culled %s, %zu lit\n",
                at.first ? "yes" : "no", at.second);
    CHECK(!at.first);
    CHECK_GT(at.second, std::size_t(100));
    // Visible, not filling the frame: a subject that covers the whole
    // picture cannot leave it, and the sweep below needs it to.
    CHECK_LT(at.second, std::size_t(kW) * kH / 4);

    // The camera standing inside the subject's own box. A plane test
    // gets this wrong by testing corners one plane at a time, and the
    // frame it gets wrong is the one the user is closest to.
    w.Camera({.focus = {3.0f, 0.0f, 0.0f}, .distance = 0.0001f});
    const std::uint64_t b0 = app.Stats().culled;
    Bmp inside_shot;
    REQUIRE(harness::shot(app, "cull_inside", inside_shot));
    const bool culled_inside = app.Stats().culled != b0;
    std::printf("  camera inside the bounds: culled %s, %zu lit\n",
                culled_inside ? "yes" : "no",
                lit_count(inside_shot, 0, 0, kW, kH));
    // The degenerate case a plane test gets wrong: standing inside the
    // box, every corner is behind some plane taken one at a time.
    CHECK(!culled_inside);

    // ── across the boundary, a step at a time ────────────────────────
    // The claim is not "a culled item draws nothing" — that is a
    // tautology, since culling is what removed the pixels. It is that
    // culling CHANGES NOTHING: the same camera, rendered with the test
    // on and with it off, must produce the same picture. An item the
    // view was too quick to reject shows up here as a difference and
    // nowhere else.
    //
    // The pan walks the subject out through a corner, one fifth of a
    // unit at a time, so the boundary is crossed in steps smaller than
    // the subject.
    std::size_t culls = 0, drawn = 0, worst_diff = 0;
    for (int i = 0; i < 40; ++i) {
        const float t = float(i) * 0.2f;

        probe::culling(app.Raw(), false);
        const auto uncut = sweep("cull_uncut", t);
        Bmp reference;
        REQUIRE(harness::shot(app, "cull_uncut", reference));

        probe::culling(app.Raw(), true);
        const auto cut = sweep("cull_sweep", t);
        Bmp culled_shot;
        REQUIRE(harness::shot(app, "cull_sweep", culled_shot));

        CHECK(!uncut.first); // with the test off, nothing may be rejected
        if (cut.first)
            ++culls;
        else if (cut.second > 0)
            ++drawn;

        std::size_t diff = 0;
        for (unsigned y = 0; y < kH && y < culled_shot.h; ++y)
            for (unsigned x = 0; x < kW && x < culled_shot.w; ++x) {
                const auto &a0 = reference.at(x, y);
                const auto &b0 = culled_shot.at(x, y);
                if (a0[0] != b0[0] || a0[1] != b0[1] || a0[2] != b0[2])
                    ++diff;
            }
        if (diff > worst_diff)
            worst_diff = diff;
    }
    probe::culling(app.Raw(), true);
    std::printf("  panning it out of frame: %zu culled, %zu drawn, worst "
                "pixel difference against an uncut render %zu\n",
                culls, drawn, worst_diff);
    CHECK_GT(culls, std::size_t(0));
    CHECK_GT(drawn, std::size_t(0));
    // The whole point. One differing pixel is geometry the view threw
    // away while it was still on screen.
    CHECK_EQ(worst_diff, std::size_t(0));

    // ── a cloud the host cannot see is never culled ──────────────────
    // Bounds are an answer, not a guess: an item that reports none is
    // drawn. A cloud whose points were never handed to the host has
    // nothing to walk, and inventing a box for it would cull data
    // nobody can look at.
    sv::World d = app.World({.title = "device", .grid = false, .axes = false});
    REQUIRE(bool(d));
    sv::Cloud unknown = d.Cloud({.color = {0.3f, 0.9f, 0.4f, 1.0f}});
    REQUIRE(bool(unknown));
    d.Camera({.focus = {500.0f, 500.0f, 500.0f}, .distance = 1.0f});
    // Stats are the APP's, and the sweep left the window's subject off
    // frame — where it would be culled and answer for this one. Put it
    // back in view so the only item that could raise the count is the
    // cloud with no bounds.
    w.Camera({.focus = {3.0f, 0.0f, 0.0f}, .distance = 6.0f});
    app.Step();
    const std::uint64_t b1 = app.Stats().culled;
    Bmp away;
    REQUIRE(harness::shot(app, "cull_unknown", away));
    std::printf("  a cloud with no host data, camera 800 units off: "
                "culled %llu\n",
                static_cast<unsigned long long>(app.Stats().culled - b1));
    CHECK_EQ(app.Stats().culled, b1);

    // ── the near plane follows the geometry ──────────────────────────
    // Orbiting a thousand units out, the old rule put the near plane a
    // full unit from the eye. Anything nearer than that was gone, and
    // "gone" is the same picture as "not drawn yet".
    sv::World f = app.World({.title = "far", .grid = false, .axes = false});
    REQUIRE(bool(f));
    // Two clouds, because the radius is the CLOUD's: half a unit from
    // the eye and a thousand units away want wildly different ones,
    // and a single radius would make one of them invisible.
    sv::Cloud backdrop = f.Cloud({.color = {0.25f, 0.4f, 1.0f, 1.0f},
                                  .radius = 60.0f,
                                  .shape = CloudShape::Sphere});
    sv::Cloud close = f.Cloud({.color = {1.0f, 0.9f, 0.2f, 1.0f},
                               .radius = 0.05f,
                               .shape = CloudShape::Sphere});
    REQUIRE(bool(backdrop));
    REQUIRE(bool(close));
    f.Camera({.focus = {0.0f, 0.0f, 0.0f}, .distance = 1000.0f});
    CHECK(backdrop.Update(point(0.0f, 0.0f, 0.0f)));
    app.OnUi([] {
        ImGui::SetWindowPos("far",
                            ImVec2(float(kFarRect.x0), float(kFarRect.y0)));
        ImGui::SetWindowSize("far", ImVec2(float(kFarRect.x1 - kFarRect.x0),
                                           float(kFarRect.y1 - kFarRect.y0)));
    });
    app.Step();
    app.Step();

    constexpr Rect kIn = content_of(kFarRect);
    Bmp alone;
    REQUIRE(harness::shot(app, "cull_far_only", alone));
    const std::size_t far_lit =
        lit_count(alone, kIn.x0, kIn.y0, kIn.x1, kIn.y1);
    CHECK_GT(far_lit, std::size_t(100));

    // Where the eye ended up, and a point half a unit in front of it.
    sv::probe::CameraState fc{};
    REQUIRE(sv::probe::camera_of(app.Raw(), "far", &fc));
    const float eye[3] = {fc.focus[0] - fc.forward[0] * fc.distance,
                          fc.focus[1] - fc.forward[1] * fc.distance,
                          fc.focus[2] - fc.forward[2] * fc.distance};
    CHECK(close.Update(point(eye[0] + fc.forward[0] * 0.5f,
                             eye[1] + fc.forward[1] * 0.5f,
                             eye[2] + fc.forward[2] * 0.5f)));

    Bmp near_shot;
    REQUIRE(harness::shot(app, "cull_near", near_shot));
    const std::size_t both_lit =
        lit_count(near_shot, kIn.x0, kIn.y0, kIn.x1, kIn.y1);
    const auto centre =
        near_shot.at((kIn.x0 + kIn.x1) / 2, (kIn.y0 + kIn.y1) / 2);
    std::printf("  eye orbiting 1000, a sphere 0.5 from it: %zu lit alone, "
                "%zu with it, centre rgb %u %u %u\n",
                far_lit, both_lit, centre[0], centre[1], centre[2]);
    // It is drawn at all — under the old rule the near plane sat a
    // whole unit out and this sphere was simply not in the picture.
    CHECK_GT(both_lit, far_lit);
    // And it is IN FRONT: the near plane moved without giving up the
    // depth ordering at the far end of a thousand-unit range.
    CHECK_GT(centre[0], centre[2] + 40);

    (void)cam;
    return check::summary("cull");
}
