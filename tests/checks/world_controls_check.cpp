// The scene's own controls: one button in a corner of the picture, and
// everything behind it.
//
// Three things can go wrong here and only one of them is visible from
// the code. The button can be drawn over a rect that already owns
// every click in its area, so it hovers and shows its tooltip and
// never opens — which is what happened. The presets can be written out
// one per entry until two of them share a pose. And a control drawn ON
// a picture can eat the gesture the picture is for.
#include "harness/Bmp.h"
#include "harness/Harness.h"
#include "harness/Input.h"

#include "probe/Probe.h"

#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>

namespace {

std::vector<float> ring() {
    std::vector<float> p;
    for (int i = 0; i < 48; ++i) {
        const float t = float(i) * 0.42f;
        p.push_back(std::cos(t) * 0.7f);
        p.push_back(std::sin(t) * 0.7f);
        p.push_back(-0.3f + 0.012f * float(i));
    }
    return p;
}

bool popup_open() {
    return ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                           ImGuiPopupFlags_AnyPopupLevel);
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {760, 540}, .headless = true});
    if (!app)
        return check::skip("world_controls", LastError());

    auto w = app.World({.title = "scene"});
    w.Camera({.distance = 4.0f, .azimuth_deg = -55.0f, .elevation_deg = 24.0f})
        .Light({.direction = {0.4f, 0.5f, 0.9f}, .intensity = 0.85f});
    auto c = w.Cloud({.radius = 0.14f, .mode = CloudMode::Solid});
    REQUIRE(bool(c));
    CHECK(c.Update(ring()));

    auto plain = app.World({.title = "plain", .controls = false});
    plain.Camera({.distance = 4.0f});

    ImGui::LoadIniSettingsFromMemory(
        "[Window][scene]\nPos=20,20\nSize=420,380\n\n"
        "[Window][plain]\nPos=460,20\nSize=280,240\n\n");
    for (int f = 0; f < 5; ++f)
        app.Step();

    // And the same corner of a world that asked for no controls starts
    // a drag instead — which is the picture still being the picture.
    probe::CameraState before{}, after{};
    REQUIRE(probe::camera_of(app.Raw(), "plain", &before));
    const ImGuiWindow *bare = ImGui::FindWindowByName("plain");
    REQUIRE(bare != nullptr);
    input::drag(app, bare->Pos.x + 32.0f, bare->Pos.y + 52.0f,
                bare->Pos.x + 150.0f, bare->Pos.y + 90.0f);
    REQUIRE(probe::camera_of(app.Raw(), "plain", &after));
    const float moved = std::abs(before.forward[0] - after.forward[0]) +
                        std::abs(before.forward[1] - after.forward[1]) +
                        std::abs(before.forward[2] - after.forward[2]);
    std::printf("(bare corner drag moved the camera by %.3f)\n", double(moved));
    CHECK_GT(moved, 0.05f);

    // The button is ON the picture, and the picture owns every click in
    // its area for the orbit gesture. Clicking the corner must open the
    // menu rather than start a drag.
    const ImGuiWindow *scene = ImGui::FindWindowByName("scene");
    REQUIRE(scene != nullptr);
    CHECK(!popup_open());
    input::move(app, scene->Pos.x + 32.0f, scene->Pos.y + 52.0f);
    input::press(app);
    input::release(app);
    CHECK(popup_open());

    // Every preset a DIFFERENT view. Applied from the table the menu
    // loops, so two entries that ended up the same fail here.
    const std::size_t n = probe::world_preset_count();
    CHECK_GT(n, std::size_t(3));
    std::vector<probe::CameraState> poses;
    for (std::size_t i = 0; i < n; ++i) {
        REQUIRE(probe::world_preset(app.Raw(), "scene", int(i)));
        probe::CameraState s{};
        REQUIRE(probe::camera_of(app.Raw(), "scene", &s));
        poses.push_back(s);
    }
    int clashes = 0;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j) {
            const float d =
                std::abs(poses[i].forward[0] - poses[j].forward[0]) +
                std::abs(poses[i].forward[1] - poses[j].forward[1]) +
                std::abs(poses[i].forward[2] - poses[j].forward[2]);
            if (d < 0.05f) {
                std::printf("(presets %zu and %zu look the same way)\n", i, j);
                ++clashes;
            }
        }
    CHECK_EQ(clashes, 0);

    // One of them looks straight down, which is what a "top" is; every
    // named direction otherwise would pass the distinctness test above
    // while pointing nowhere in particular.
    int down = 0;
    for (const auto &s : poses)
        if (std::abs(s.forward[2]) > 0.9f)
            ++down;
    CHECK_EQ(down, 1);

    // Hiding the grid takes it out of the picture rather than out of
    // the list: an item skipped at submit still exists to switch back.
    for (int f = 0; f < 3; ++f)
        app.Step();
    Bmp lit, dark;
    REQUIRE(harness::shot(app, "world_grid_on", lit));
    const auto x0 = unsigned(scene->Pos.x + 10.0f);
    const auto y0 = unsigned(scene->Pos.y + 40.0f);
    const auto x1 = unsigned(scene->Pos.x + scene->Size.x - 10.0f);
    const auto y1 = unsigned(scene->Pos.y + scene->Size.y - 10.0f);
    const std::size_t on = lit_count(lit, x0, y0, x1, y1, 26);

    REQUIRE(probe::world_show(app.Raw(), "scene", 0, false));
    REQUIRE(probe::world_show(app.Raw(), "scene", 1, false));
    for (int f = 0; f < 3; ++f)
        app.Step();
    REQUIRE(harness::shot(app, "world_grid_off", dark));
    const std::size_t off = lit_count(dark, x0, y0, x1, y1, 26);
    std::printf("(lit with grid and axes %zu, without %zu)\n", on, off);
    CHECK_GT(on, off + 500);

    REQUIRE(probe::world_show(app.Raw(), "scene", 0, true));
    for (int f = 0; f < 3; ++f)
        app.Step();
    Bmp back;
    REQUIRE(harness::shot(app, "world_grid_back", back));
    CHECK_GT(lit_count(back, x0, y0, x1, y1, 26), off + 400);

    return check::summary("world_controls");
}
