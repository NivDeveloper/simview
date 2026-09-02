#include "PlotState.h"

#include "../core/App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace sv {
namespace {

// Wraps every sixth window, so every title bar stays reachable.
void place_cascade(int slot, float width, float height) {
    const int col = slot / 6, row = slot % 6;
    // The column step follows the window's own width, or the second
    // column lands on the first and the cascade has bought nothing.
    ImGui::SetNextWindowPos(
        ImVec2(26.0f + float(col) * (width + 60.0f) + float(row) * 22.0f,
               26.0f + float(row) * 30.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
}

// The column count is chosen so a tile comes out near square, then
// narrowed while it costs no row: nine tiles want 3x3, not 4x3.
void place_grid(int slot, int total, float min_h) {
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    const float pad = 12.0f;
    const float w = vp->WorkSize.x - pad, h = vp->WorkSize.y - pad;
    if (total < 1 || w < 100.0f || h < 100.0f)
        return;

    int cols = int(std::ceil(std::sqrt(double(total) * double(w) / double(h))));
    cols = std::clamp(cols, 1, total);
    int rows = (total + cols - 1) / cols;
    // Then narrowed while it costs no row: nine tiles want 3x3 and not
    // 4x3 with a column of one, and the aspect that chose 4 does not
    // know that a ragged row is the thing a reader notices.
    while (cols > 1 && (total + cols - 2) / (cols - 1) == rows)
        --cols;

    const float tw = w / float(cols) - pad;
    const float th = std::max(min_h, h / float(rows) - pad);
    const int col = slot % cols, row = slot / cols;

    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + pad + float(col) * (tw + pad),
               vp->WorkPos.y + pad + float(row) * (th + pad)),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(tw, th), ImGuiCond_FirstUseEver);
}

} // namespace

// ImGui puts every new window in the same place. Only ever a
// FIRST-USE default: a dragged layout is saved and wins.
void place_window(impl::App *app, int slot, float width, float height) {
    if (app && app->layout == Layout::Grid)
        // A FLOOR, not the window's preferred size: passing 360 for a
        // plot forces three rows of 360 into a 720-tall viewport and
        // the last row lands off screen.
        place_grid(slot, app->windows, 140.0f);
    else
        place_cascade(slot, width, height);
}

} // namespace sv
