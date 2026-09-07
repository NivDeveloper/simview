// A click asks the world what is under the pointer, and the world
// answers with the nearest thing that can say: a particle the host has
// a copy of, or the ground.
//
// What a picture cannot prove and this must: that the ray is the one
// the reader looked along (a click at the middle finds the point at
// the focus, and a point to the right or above lands where world +Y
// and +Z do); that the NEAREST hit wins, not the first registered;
// that a radius is a radius; that a drag is not a click; that a
// double-click makes the point the pivot; and that following moves
// the focus with the element and stops when the reader takes it back.

#include "harness/Harness.h"
#include "harness/Input.h"
#include "probe/Probe.h"

#include <simview/gpud.h>
#include <simview/simview.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

// world_check's fill kernel: three floats of one value, so a device
// buffer holds one point at a known place.
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

// The summed brightness of a small block of pixels.
long brightness(const Bmp &img, unsigned x0, unsigned y0, unsigned n) {
    long sum = 0;
    for (unsigned y = y0; y < y0 + n && y < img.h; ++y)
        for (unsigned x = x0; x < x0 + n && x < img.w; ++x) {
            const auto &px = img.at(x, y);
            sum += px[0] + px[1] + px[2];
        }
    return sum;
}

sv::probe::CameraState cam(sv::App &app, const char *title = nullptr) {
    sv::probe::CameraState c{};
    CHECK(sv::probe::camera_of(app.Raw(), title, &c));
    return c;
}

bool pick(sv::App &app, float x, float y, sv::Pick &p,
          const char *title = nullptr) {
    p = sv::Pick{};
    return sv::probe::pick(app.Raw(), title, x, y, &p);
}

// One draw, so the pick has a view to look back through.
void draw(sv::App &app, const char *name) {
    Bmp img;
    CHECK(harness::shot(app, name, img));
}

// At distance 5 and a 45 degree field of view over 600 rows, one
// world unit at the focus is this many pixels.
constexpr float kPxPerUnit = 600.0f / (2.0f * 0.41421356f * 5.0f);

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {800, 600}, .headless = true});
    if (!app)
        return check::skip("pick", LastError());

    // Posed at +X looking at the origin, level: screen right is world
    // +Y and screen up is world +Z, which the clicks below rely on.
    sv::World w = app.World({.axes = false});
    REQUIRE(bool(w));
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 5.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});

    // Registered FIRST, and directly behind the near cloud's first
    // point: a pick that kept the first answer would return it.
    auto far = w.Cloud({.radius = 0.3f});
    REQUIRE(bool(far));
    CHECK(far.Update(std::vector<float>{-2.0f, 0.0f, 0.0f}));
    // Mid-blue, not white: a white sphere is already saturated at its
    // centre and a highlight could not show on it.
    auto near = w.Cloud({.color = {0.25f, 0.45f, 0.80f, 1.0f}, .radius = 0.3f});
    REQUIRE(bool(near));
    CHECK(near.Update(std::vector<float>{0.0f, 0.0f, 0.0f,    // at the focus
                                         0.0f, 1.0f, 0.0f,    // to the right
                                         0.0f, 0.0f, 1.0f})); // above
    std::vector<Pick> picked;
    w.OnPick([&](const Pick &p) { picked.push_back(p); });
    app.Step();
    draw(app, "pick_level");

    const float cx = 400.0f, cy = 300.0f;
    Pick p;

    // ── the middle finds the point at the focus, and the NEAR one ────
    REQUIRE(pick(app, cx, cy, p));
    std::printf("  centre: cloud %s index %d at %.2f (%.3f %.3f %.3f)\n",
                p.On(near)  ? "near"
                : p.On(far) ? "far"
                            : "none",
                p.index, p.distance, p.point[0], p.point[1], p.point[2]);
    CHECK(p.On(near));
    CHECK(!p.On(far));
    CHECK(!p.Ground());
    CHECK_EQ(p.index, 0);
    CHECK_LT(std::fabs(p.distance - 4.7f), 0.05f);
    CHECK_LT(std::fabs(p.point[0] - 0.3f), 0.02f);

    // ── right is +Y, up is +Z ────────────────────────────────────────
    REQUIRE(pick(app, cx + kPxPerUnit, cy, p));
    CHECK(p.On(near));
    CHECK_EQ(p.index, 1);
    REQUIRE(pick(app, cx, cy - kPxPerUnit, p));
    CHECK(p.On(near));
    CHECK_EQ(p.index, 2);

    // ── a radius is a radius: just outside the disc is a miss ────────
    // The camera is level, so the ground is edge-on and cannot answer
    // either. 0.45 units off the point, against a radius of 0.3.
    CHECK(!pick(app, cx + kPxPerUnit * 1.45f, cy, p));
    // And inside it, a hit.
    CHECK(pick(app, cx + kPxPerUnit * 1.2f, cy, p));
    CHECK_EQ(p.index, 1);

    // ── a double-click makes the point the pivot ─────────────────────
    const auto before = cam(app);
    input::double_click(app, cx + kPxPerUnit, cy);
    const auto after = cam(app);
    std::printf("  double-click: focus (%.2f %.2f %.2f) -> (%.2f %.2f %.2f), "
                "distance %.2f -> %.2f\n",
                before.focus[0], before.focus[1], before.focus[2],
                after.focus[0], after.focus[1], after.focus[2], before.distance,
                after.distance);
    CHECK_GT(input::moved(before, after), 0.5f);
    // On the sphere's surface, so within its radius of the centre.
    CHECK_LT(std::fabs(after.focus[1] - 1.0f), 0.3f);
    CHECK_LT(std::fabs(after.distance - before.distance), 1e-4f);
    CHECK_LT(input::turned(before, after), 1e-3f);
    picked.clear();
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 5.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
    draw(app, "pick_level_again");

    // ── a point smaller than a pixel is still clickable ─────────────
    // bgk draws its gas at a radius of 0.001, under half a pixel: a
    // pick that demanded the geometry could never be hit there. Six
    // pixels of slop at the point's depth, and no more.
    auto specks = w.Cloud({.radius = 0.001f});
    REQUIRE(bool(specks));
    CHECK(specks.Update(std::vector<float>{0.0f, -1.0f, 0.0f})); // to the left
    app.Step();
    draw(app, "pick_specks");
    REQUIRE(pick(app, cx - kPxPerUnit + 3.0f, cy, p));
    std::printf("  3 px off a speck: %s index %d\n",
                p.On(specks) ? "the speck" : "something else", p.index);
    CHECK(p.On(specks));
    CHECK_EQ(p.index, 0);
    CHECK(!pick(app, cx - kPxPerUnit + 12.0f, cy, p));

    // ── hovering names the point, and the picture brightens it ───────
    input::move(app, cx + 300.0f, cy);
    CHECK(!probe::hovered(app.Raw(), nullptr, nullptr));
    Bmp plain;
    REQUIRE(harness::shot(app, "pick_unhovered", plain));
    input::move(app, cx, cy);
    std::int32_t over = -1;
    CHECK(probe::hovered(app.Raw(), nullptr, &over));
    CHECK_EQ(over, 0);
    Bmp lit;
    REQUIRE(harness::shot(app, "pick_hovered", lit));
    const long dark = brightness(plain, unsigned(cx) - 8, unsigned(cy) - 8, 16);
    const long bright = brightness(lit, unsigned(cx) - 8, unsigned(cy) - 8, 16);
    std::printf("  the sphere under the pointer: brightness %ld -> %ld\n", dark,
                bright);
    CHECK_GT(bright, dark + dark / 4);
    // The point to the right is not the one under the pointer.
    const long other =
        brightness(lit, unsigned(cx + kPxPerUnit) - 8, unsigned(cy) - 8, 16);
    CHECK_LT(std::labs(other - brightness(plain, unsigned(cx + kPxPerUnit) - 8,
                                          unsigned(cy) - 8, 16)),
             dark / 10 + 1);
    input::move(app, cx + 300.0f, cy);
    CHECK(!probe::hovered(app.Raw(), nullptr, nullptr));

    // ── a click fires the callback, a drag does not ──────────────────
    input::click(app, cx, cy);
    REQUIRE(picked.size() == 1);
    CHECK_EQ(picked[0].index, 0);
    // Ending ON the point at the focus, so that only the gate keeps
    // the release from picking it.
    input::drag(app, cx - 30.0f, cy, cx, cy);
    CHECK_EQ(picked.size(), std::size_t(1));
    std::printf("  a click picked %zu, a drag picked %zu\n", picked.size(),
                picked.size() - 1);

    // ── the ground answers where the camera can see it ───────────────
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 5.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 30.0f});
    draw(app, "pick_tilted");
    REQUIRE(pick(app, cx, cy + 120.0f, p));
    std::printf("  below the focus, tilted: %s at (%.3f %.3f %.3f)\n",
                p.Ground() ? "ground" : "a cloud", p.point[0], p.point[1],
                p.point[2]);
    CHECK(p.Ground());
    CHECK_EQ(p.index, -1);
    CHECK_LT(std::fabs(p.point[2]), 1e-4f);
    // Nearer to the eye than the focus, since the eye is above and in
    // front: the hit has positive x.
    CHECK_GT(p.point[0], 0.0f);
    // The particle at the focus still beats the ground behind it.
    REQUIRE(pick(app, cx, cy, p));
    CHECK(p.On(near));
    CHECK_EQ(p.index, 0);

    // ── following moves the focus with the element ───────────────────
    w.Follow(picked[0]);
    std::int32_t idx = -1;
    CHECK(probe::following(app.Raw(), nullptr, &idx));
    CHECK_EQ(idx, 0);
    CHECK(near.Update(std::vector<float>{0.0f, 2.0f, 0.5f, 0.0f, 1.0f, 0.0f,
                                         0.0f, 0.0f, 1.0f}));
    draw(app, "pick_followed");
    const auto followed = cam(app);
    std::printf("  followed: focus (%.2f %.2f %.2f)\n", followed.focus[0],
                followed.focus[1], followed.focus[2]);
    CHECK_LT(std::fabs(followed.focus[1] - 2.0f), 1e-4f);
    CHECK_LT(std::fabs(followed.focus[2] - 0.5f), 1e-4f);
    // A pan is the reader taking the focus back.
    input::shift(app, true);
    input::drag(app, cx, cy, cx + 40.0f, cy);
    input::shift(app, false);
    CHECK(!probe::following(app.Raw(), nullptr, nullptr));
    // And an element that has gone ends it too.
    w.Follow(picked[0]);
    CHECK(near.Update(std::vector<float>{}));
    draw(app, "pick_gone");
    CHECK(!probe::following(app.Raw(), nullptr, nullptr));

    // ── a world in a panel picks through its own rect ────────────────
    sv::World side = app.World({.title = "side", .axes = false});
    REQUIRE(bool(side));
    side.Camera({.focus = {0.0f, 0.0f, 0.0f},
                 .distance = 5.0f,
                 .azimuth_deg = 0.0f,
                 .elevation_deg = 0.0f});
    auto dot = side.Cloud({.radius = 0.3f});
    REQUIRE(bool(dot));
    CHECK(dot.Update(std::vector<float>{0.0f, 0.0f, 0.0f}));
    int side_picks = 0;
    side.OnPick([&](const Pick &q) {
        if (q.On(dot))
            ++side_picks;
    });
    app.OnUi([] {
        ImGui::SetWindowPos("side", ImVec2(500, 380));
        ImGui::SetWindowSize("side", ImVec2(280, 200));
    });
    app.Step();
    app.Step();
    draw(app, "pick_panel");
    const ImGuiWindow *win = ImGui::FindWindowByName("side");
    REQUIRE(win != nullptr);
    const float mx = win->Pos.x + win->Size.x * 0.5f;
    const float my = win->Pos.y + (win->Size.y + 20.0f) * 0.5f;
    const std::size_t main_before = picked.size();
    input::click(app, mx, my);
    std::printf("  click in the panel: the panel picked %d, the window %zu\n",
                side_picks, picked.size() - main_before);
    CHECK_EQ(side_picks, 1);
    CHECK_EQ(picked.size(), main_before);

    // ── a cloud that lives on the device answers after a readback ────
    // The host has no copy of it, so the world takes one: recorded in
    // one frame, mapped in the next, which is why it draws twice.
    gpud::Device &dev = sv::Device(app);
    const gpud::Kernel *fill = nullptr;
    try {
        fill = &dev.compile(fill_src);
    } catch (const std::exception &ex) {
        std::printf("  (device cloud skipped: %s)\n", ex.what());
    }
    Sync<gpud::Buffer> dpts;
    if (fill) {
        w.Camera({.focus = {0.0f, 0.0f, 0.0f},
                  .distance = 5.0f,
                  .azimuth_deg = 0.0f,
                  .elevation_deg = 0.0f});
        // The cloud FIRST: it installs the stamper on the Sync, and only
        // a stamped Publish makes the frame wait for the fill.
        auto resident = w.Cloud(dpts, {.radius = 0.3f});
        REQUIRE(bool(resident));
        dpts.Next() = dev.alloc(3 * sizeof(float));
        FillScalars sc{0.5f, 3};
        gpud::Buffer *bufs[] = {&dpts.Next()};
        dev.run(*fill, 1, {reinterpret_cast<const std::byte *>(&sc), sizeof sc},
                bufs);
        dpts.Publish();
        app.Step();
        draw(app, "pick_device_recorded");
        Bmp shown;
        REQUIRE(harness::shot(app, "pick_device_mapped", shown));
        // (0.5, 0.5, 0.5) from +X: half a unit right and up, at depth 4.5.
        // Drawn there, first — a pick of data that never arrived would
        // otherwise read as a readback that failed.
        const float k = kPxPerUnit * 5.0f / 4.5f;
        const long there = brightness(shown, unsigned(cx + 0.5f * k) - 4,
                                      unsigned(cy - 0.5f * k) - 4, 8);
        std::printf("  the device point drawn: brightness %ld\n", there);
        CHECK_GT(there, 8L * 8L * 3L * 100L);
        REQUIRE(pick(app, cx + 0.5f * k, cy - 0.5f * k, p));
        std::printf("  the device point: %s index %d at (%.2f %.2f %.2f)\n",
                    p.On(resident) ? "resident" : "something else", p.index,
                    p.point[0], p.point[1], p.point[2]);
        CHECK(p.On(resident));
        CHECK_EQ(p.index, 0);
        w.Follow(p);
        draw(app, "pick_device_followed");
        const auto tracked = cam(app);
        CHECK_LT(std::fabs(tracked.focus[0] - 0.5f), 1e-3f);
        CHECK_LT(std::fabs(tracked.focus[1] - 0.5f), 1e-3f);
        CHECK_LT(std::fabs(tracked.focus[2] - 0.5f), 1e-3f);
        w.Unfollow();
    }

    return check::summary("pick");
}
