// One pointer, whichever device moves it: the pad's right stick steps
// the same cursor the mouse does, and its A is the mouse's left button
// — to a panel's checkbox and to the world's hover and pick alike.
// Headless, through PostEvent, which is the whole mechanism: no window
// and no warp, only the injection into ImGui's queue.

#include "harness/Harness.h"
#include "harness/Input.h"
#include "probe/Probe.h"

#include <simview/simview.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

// Where a panel's first widget sits: below the title bar and one
// window padding in.
ImVec2 first_widget(const char *title) {
    const ImGuiWindow *w = ImGui::FindWindowByName(title);
    if (!w)
        return ImVec2(-1.0f, -1.0f);
    const ImGuiStyle &s = ImGui::GetStyle();
    const float bar = ImGui::GetFontSize() + s.FramePadding.y * 2.0f;
    return ImVec2(w->Pos.x + s.WindowPadding.x + ImGui::GetFrameHeight() * 0.5f,
                  w->Pos.y + bar + s.WindowPadding.y +
                      ImGui::GetFrameHeight() * 0.5f);
}

// Hold the right stick until the pointer is within reach of a point,
// or give up after a while.
bool steer_to(sv::App &app, float tx, float ty) {
    for (int f = 0; f < 600; ++f) {
        float x, y;
        sv::probe::pointer(app.Raw(), &x, &y);
        const float dx = tx - x, dy = ty - y;
        if (std::hypot(dx, dy) < 3.0f) {
            input::stick(app, sv::Pad::RS, 0.0f, 0.0f);
            app.Step();
            return true;
        }
        const float n = std::hypot(dx, dy);
        const float k = n > 60.0f ? 1.0f : 0.35f;
        input::stick(app, sv::Pad::RS, k * dx / n, k * dy / n);
        app.Step();
    }
    return false;
}

std::vector<float> dot() { return {0.0f, 0.0f, 0.0f}; }

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.size = {800, 600}, .headless = true});
    if (!app)
        return check::skip("cursor", LastError());

    sv::World w = app.World({.grid = false, .axes = false});
    REQUIRE(bool(w));
    w.Camera({.focus = {0.0f, 0.0f, 0.0f},
              .distance = 5.0f,
              .azimuth_deg = 0.0f,
              .elevation_deg = 0.0f});
    auto cloud = w.Cloud({.radius = 0.3f, .mode = CloudMode::Solid});
    REQUIRE(bool(cloud));
    CHECK(cloud.Update(dot()));
    int picks = 0;
    w.OnPick([&](const Pick &p) {
        if (p.On(cloud))
            ++picks;
    });

    bool flag = false;
    app.Panel("switches").Checkbox("on", flag);
    app.OnUi([] {
        ImGui::SetWindowPos("switches", ImVec2(40, 40));
        ImGui::SetWindowSize("switches", ImVec2(220, 120));
    });
    for (int f = 0; f < 3; ++f)
        app.Step();
    const ImVec2 box = first_widget("switches");
    REQUIRE(box.x > 0.0f);
    // A pick goes through the picture the reader saw: headless, a shot.
    Bmp img;
    REQUIRE(harness::shot(app, "cursor_setup", img));

    // ── the stick steps the cursor at a known rate ───────────────────
    input::move(app, 400.0f, 300.0f);
    float x0, y0, x1, y1;
    probe::pointer(app.Raw(), &x0, &y0);
    input::stick(app, Pad::RS, 1.0f, 0.0f);
    app.Step();
    app.Step();
    input::stick(app, Pad::RS, 0.0f, 0.0f);
    app.Step();
    probe::pointer(app.Raw(), &x1, &y1);
    std::printf("  full stick, 2 frames: %.1f px right, %.1f down\n", x1 - x0,
                y1 - y0);
    CHECK_GT(x1 - x0, 25.0f);
    CHECK_LT(x1 - x0, 45.0f);
    CHECK_LT(std::fabs(y1 - y0), 1e-3f);
    CHECK_EQ(probe::last_device(app.Raw()), int(Device::Pad));

    // ── the pad's cursor presses a panel's checkbox with A ───────────
    REQUIRE(steer_to(app, box.x, box.y));
    CHECK(!flag);
    input::pad_tap(app, Pad::A);
    CHECK(flag);
    // And the mouse, the same box, the same result the other way.
    input::click(app, box.x, box.y);
    CHECK(!flag);
    CHECK_EQ(probe::last_device(app.Raw()), int(Device::Mouse));

    // ── the pad's cursor hovers and picks the world ──────────────────
    REQUIRE(steer_to(app, 400.0f, 300.0f));
    app.Step();
    std::int32_t index = -1;
    CHECK(probe::hovered(app.Raw(), nullptr, &index));
    CHECK_EQ(index, 0);
    input::pad_tap(app, Pad::A);
    CHECK_EQ(picks, 1);
    input::click(app, 400.0f, 300.0f);
    CHECK_EQ(picks, 2);

    return check::summary("cursor");
}
