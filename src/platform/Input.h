#pragma once

// Internal to src/ — the input half of the platform layer.

#include "../core/Engine.h"

namespace sv {
namespace impl {

// Events posted through the automation seam, delivered exactly where
// SDL's own are: same callbacks, same order, same frame.
void deliver_posted(App *);

// SDL's queue: feed ImGui, set quit, hand keys to the sim's callbacks.
void poll(App *);

} // namespace impl
} // namespace sv
