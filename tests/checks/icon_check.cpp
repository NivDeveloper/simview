// The icon set, asked what it is made of. Two ways an icon fails
// silently: a switch arm that is missing draws NOTHING and the button
// is still there to click, and an arm copied from its neighbour draws
// the WRONG thing while every count and bound still holds. So: every
// icon marks its square, and no two mark it the same way.
#include "harness/Bmp.h"
#include "harness/Harness.h"

#include "../../src/ui/Icons.h"

#include <cstdio>
#include <vector>

namespace {

using sv::impl::Icon;

constexpr Icon kAll[] = {Icon::Home,  Icon::Fit,    Icon::Grid,
                         Icon::Axes,  Icon::Cube,   Icon::Light,
                         Icon::Eye,   Icon::Gear,   Icon::Perspective,
                         Icon::Orbit, Icon::Camera, Icon::Play,
                         Icon::Pause, Icon::Step,   Icon::Orthographic};
constexpr int kCount = int(sizeof kAll / sizeof kAll[0]);

constexpr float kSize = 40.0f;
constexpr float kPitch = 60.0f;
constexpr float kX0 = 30.0f, kY0 = 30.0f;

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.title = "icons",
             .size = {unsigned(kX0 * 2 + kPitch * kCount), 120},
             .headless = true});
    if (!app)
        return check::skip("icons", LastError());

    // app_on_ui and not OnFrame: OnFrame runs outside the ImGui frame,
    // where there is no draw list to draw into.
    impl::app_on_ui(
        app.Raw(),
        [](void *) {
            ImDrawList *dl = ImGui::GetBackgroundDrawList();
            const ImU32 fg = ImGui::GetColorU32(ImGuiCol_Text);
            for (int k = 0; k < kCount; ++k)
                impl::icon_draw(dl, kAll[k], {kX0 + kPitch * float(k), kY0},
                                kSize, fg);
        },
        nullptr);

    for (int f = 0; f < 5; ++f)
        app.Step();

    Bmp img;
    REQUIRE(harness::shot(app, "icons", img));

    // One square per icon, read back as the pixels it actually left.
    const auto cell = [&](int k) {
        std::vector<int> v;
        const auto x0 = unsigned(kX0 + kPitch * float(k));
        const auto y0 = unsigned(kY0);
        for (unsigned y = y0; y < y0 + unsigned(kSize); ++y)
            for (unsigned x = x0; x < x0 + unsigned(kSize); ++x)
                v.push_back(lit(img, x, y) ? 1 : 0);
        return v;
    };

    std::vector<std::vector<int>> marks;
    for (int k = 0; k < kCount; ++k)
        marks.push_back(cell(k));

    // Drawn at all, and drawn as a figure rather than as a filled
    // block: 1600 pixels in the square, so a solid one would be 1600.
    for (int k = 0; k < kCount; ++k) {
        std::size_t on = 0;
        for (int v : marks[std::size_t(k)])
            on += std::size_t(v);
        if (on < 60 || on > 950)
            std::printf("(icon %d lights %zu of 1600)\n", k, on);
        CHECK_GT(on, std::size_t(60));
        CHECK_GT(std::size_t(950), on);
    }

    // No two the same. A pair that matches is a copied switch arm.
    int clashes = 0;
    for (int a = 0; a < kCount; ++a)
        for (int b = a + 1; b < kCount; ++b)
            if (marks[std::size_t(a)] == marks[std::size_t(b)]) {
                std::printf("(icons %d and %d draw the same)\n", a, b);
                ++clashes;
            }
    CHECK_EQ(clashes, 0);

    return check::summary("icons");
}
