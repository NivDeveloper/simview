#pragma once

// A small vector icon set, drawn with ImDrawList primitives rather
// than rasterized from an icon font.
//
// A font would mean a vendored dependency, a glyph range, a second
// atlas merge and a size baked at load. These are a few lines and
// arcs in a unit square: crisp at any size because they are stroked
// at the size asked for, and themed for free because the caller hands
// in a colour — normally the one ImGui is already using for text.
//
// The Icon names themselves are PUBLIC vocabulary (Types.h): a caller
// asks for one by name and never learns that a draw list drew it.

#include <simview/Types.h>

#include <imgui.h>

namespace sv {
namespace impl {

// Fill a square of `size` at `at`. Stroke weight follows the size, so
// one icon reads the same at 16px and at 40.
void icon_draw(ImDrawList *dl, Icon ic, ImVec2 at, float size, ImU32 col);

// A square button carrying one icon. `id` is what ImGui identifies it
// by, `tip` what a hover says — an icon-only control that cannot say
// its own name in words is a guessing game, so the tooltip is not
// optional. `on` draws it held down, which is what makes the same
// button serve as a toggle.
bool icon_button(Icon ic, const char *id, const char *tip, bool on = false);

} // namespace impl
} // namespace sv
