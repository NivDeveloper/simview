// Where a window lands when nobody has said. The cascade puts every
// new one slightly down and right of the last, which keeps title bars
// reachable and everything else buried; the grid tiles them.
//
// The grid's whole claim is two things a picture would have to be
// squinted at for and the rects say outright: no two windows overlap,
// and every one of them is on screen. The second is not hypothetical —
// the first version passed the plot's PREFERRED height as a floor, and
// three rows of 360 in a 720-tall viewport put the last row under the
// bottom edge.
#include "harness/Bmp.h"
#include "harness/Harness.h"

#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>

namespace {

constexpr const char *kNames[] = {"alpha", "beta", "gamma", "delta", "epsilon",
                                  "zeta",  "eta",  "theta", "iota"};
constexpr int kN = int(sizeof kNames / sizeof kNames[0]);

bool overlap(const ImGuiWindow *a, const ImGuiWindow *b) {
    return a->Pos.x < b->Pos.x + b->Size.x && b->Pos.x < a->Pos.x + a->Size.x &&
           a->Pos.y < b->Pos.y + b->Size.y && b->Pos.y < a->Pos.y + a->Size.y;
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    std::vector<float> x(48);
    for (std::size_t i = 0; i < 48; ++i)
        x[i] = float(i) * 0.1f;

    int overlaps[2] = {0, 0};
    int off_screen[2] = {0, 0};
    for (int mode = 0; mode < 2; ++mode) {
        App app({.size = {1100, 720}, .headless = true});
        if (!app)
            return check::skip("layout", LastError());
        app.Layout(mode ? Layout::Grid : Layout::Cascade);

        std::vector<std::vector<float>> ys(std::size_t(kN),
                                           std::vector<float>(48));
        for (int k = 0; k < kN; ++k) {
            for (std::size_t i = 0; i < 48; ++i)
                ys[std::size_t(k)][i] =
                    std::sin(x[i] * (0.5f + 0.2f * float(k)));
            app.Plot({.title = kNames[k]}).Line("s", x, ys[std::size_t(k)]);
        }
        for (int f = 0; f < 5; ++f)
            app.Step();

        std::vector<ImGuiWindow *> w;
        for (int k = 0; k < kN; ++k) {
            ImGuiWindow *win = ImGui::FindWindowByName(kNames[k]);
            REQUIRE(win != nullptr);
            w.push_back(win);
        }

        const ImGuiViewport *vp = ImGui::GetMainViewport();
        for (int i = 0; i < kN; ++i) {
            if (w[std::size_t(i)]->Pos.x < vp->WorkPos.x - 1.0f ||
                w[std::size_t(i)]->Pos.y < vp->WorkPos.y - 1.0f ||
                w[std::size_t(i)]->Pos.x + w[std::size_t(i)]->Size.x >
                    vp->WorkPos.x + vp->WorkSize.x + 1.0f ||
                w[std::size_t(i)]->Pos.y + w[std::size_t(i)]->Size.y >
                    vp->WorkPos.y + vp->WorkSize.y + 1.0f)
                ++off_screen[mode];
            for (int j = i + 1; j < kN; ++j)
                if (overlap(w[std::size_t(i)], w[std::size_t(j)]))
                    ++overlaps[mode];
        }
    }

    std::printf("(cascade: %d overlapping pairs, %d off screen)\n", overlaps[0],
                off_screen[0]);
    std::printf("(grid:    %d overlapping pairs, %d off screen)\n", overlaps[1],
                off_screen[1]);

    // The grid's two promises.
    CHECK_EQ(overlaps[1], 0);
    CHECK_EQ(off_screen[1], 0);

    // And the cascade is a different thing rather than the same thing
    // renamed: it stacks, which is exactly what the grid is for.
    CHECK_GT(overlaps[0], 0);

    return check::summary("layout");
}
