#pragma once

#include <simview/Types.h>

#include <imgui.h>

namespace sv {
namespace impl {

// Fill a square of `size` at `at`. Stroke weight follows the size, so
// one icon reads the same at 16px and at 40.
void icon_draw(ImDrawList *dl, Icon ic, ImVec2 at, float size, ImU32 col);

// The tooltip is not optional: an icon-only control that cannot say
// its own name is a guessing game. `on` draws it held down.
bool icon_button(Icon ic, const char *id, const char *tip, bool on = false);

} // namespace impl
} // namespace sv
