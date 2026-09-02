// Which way a Surface reads its grid. Three arrays of x_count*y_count
// arrive with two counts, and the vertex total is the same whichever
// way round they are taken — so every arithmetic assertion passes on a
// transposed read, and only the picture disagrees. The counts here are
// deliberately UNEQUAL: at 32x32 a swap is a no-op and the gate would
// be green by construction.
//
// Two surfaces, identical but for which axis carries a wall. A
// correct read puts the high-x wall low in frame and the high-y wall
// high in frame, so their quadrant signatures are each other's
// mirror. Read the grid the other way and the two swap.
#include "harness/Bmp.h"
#include "harness/Harness.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <vector>

constexpr std::size_t XC = 32, YC = 12;

int main() {
    harness::begin();
    using namespace sv;

    App app({.title = "surface", .size = {1200, 500}, .headless = true});
    if (!app)
        return check::skip("surface", LastError());

    // x varies fastest: index = y_index * x_count + x_index. The two
    // walls stand on the last quarter of one axis each.
    std::vector<float> gx(XC * YC), gy(XC * YC), wall_x(XC * YC),
        wall_y(XC * YC);
    for (std::size_t b = 0; b < YC; ++b)
        for (std::size_t a = 0; a < XC; ++a) {
            gx[b * XC + a] = float(a) / float(XC - 1);
            gy[b * XC + a] = float(b) / float(YC - 1);
            wall_x[b * XC + a] = a >= XC - 4 ? 1.0f : 0.0f;
            wall_y[b * XC + a] = b >= YC - 2 ? 1.0f : 0.0f;
        }

    auto along_x = Plot3DDesc{.title = "wall at high x",
                              .x = {.label = "x"},
                              .y = {.label = "y"},
                              .z = {.label = "z"},
                              .palette = Palette::Plasma};
    auto along_y = along_x;
    along_y.title = "wall at high y";
    app.Plot3D(along_x).Surface("w", gx, gy, wall_x, XC, YC);
    app.Plot3D(along_y).Surface("w", gx, gy, wall_y, XC, YC);

    static const char *ini =
        "[Window][wall at high x]\nPos=20,20\nSize=520,440\n\n"
        "[Window][wall at high y]\nPos=560,20\nSize=520,440\n\n";
    ImGui::LoadIniSettingsFromMemory(ini);
    for (int f = 0; f < 6; ++f)
        app.Step();

    Bmp img;
    REQUIRE(harness::shot(app, "surface", img));

    const ImGuiWindow *wx = ImGui::FindWindowByName("wall at high x");
    const ImGuiWindow *wy = ImGui::FindWindowByName("wall at high y");
    REQUIRE(wx != nullptr);
    REQUIRE(wy != nullptr);

    const auto lit_quadrants = [&img](const ImGuiWindow *w, const char *title) {
        const auto q =
            quadrants(img, unsigned(w->Pos.x + 40), unsigned(w->Pos.y + 50),
                      unsigned(w->Pos.x + w->Size.x - 40),
                      unsigned(w->Pos.y + w->Size.y - 40), 110);
        std::printf("(%s: tl %zu tr %zu bl %zu br %zu)\n", title, q.tl, q.tr,
                    q.bl, q.br);
        return q;
    };

    const auto x_wall = lit_quadrants(wx, "wall at high x");
    const auto y_wall = lit_quadrants(wy, "wall at high y");

    // The whole gate in two lines: each wall is where its own axis
    // puts it, and a transposed read is exactly the two exchanged.
    CHECK_GT(x_wall.bl, x_wall.tl);
    CHECK_GT(y_wall.tl, y_wall.bl);

    // Both draw something, so a blank canvas cannot satisfy the pair.
    CHECK_GT(x_wall.tl + x_wall.tr + x_wall.bl + x_wall.br, std::size_t(2000));
    CHECK_GT(y_wall.tl + y_wall.tr + y_wall.bl + y_wall.br, std::size_t(2000));

    return check::summary("surface");
}
