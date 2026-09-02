#pragma once

// A small vector icon set, drawn with ImDrawList primitives rather
// than rasterized from an icon font.
//
// A font would mean a vendored dependency, a glyph range, a second
// atlas merge and a size baked at load. These are a few lines and
// arcs in a unit square: crisp at any size because they are stroked
// at the size asked for, and themed for free because the caller hands
// in a colour — normally the one ImGui is already using for text.

#include <imgui.h>

namespace sv {
namespace impl {

enum class Icon {
    Home,
    Fit,
    Grid,
    Axes,
    Cube,
    Perspective,
    Orthographic,
    Camera,
    Light,
    Eye,
    Gear,
    Orbit,
    Play,
    Pause,
    Step,
};

// Fill a square of `size` at `at`. Stroke weight follows the size, so
// one icon reads the same at 16px and at 40.
void icon_draw(ImDrawList *dl, Icon ic, ImVec2 at, float size, ImU32 col);

} // namespace impl
} // namespace sv
