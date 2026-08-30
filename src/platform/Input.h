#pragma once

// Internal to src/ — the input half of the platform layer: SDL's
// events and the posted ones, delivered to the same callbacks in the
// same order, which is what makes a posted event a faithful stand-in
// for a keypress.

#include "../core/Callbacks.h"

#include <simview/Event.h>

#include <forward_list>
#include <vector>

namespace sv {
namespace impl {

struct App;

struct Input {
    std::forward_list<Ecb> event_cbs;
    // Events posted through the automation seam, delivered by the
    // next Step or loop iteration exactly like SDL's own.
    std::vector<Event> posted;
};

// Deliver the posted events, exactly where SDL's own land.
void deliver_posted(App *);

// SDL's queue: feed ImGui, set quit, hand keys to the sim's callbacks.
void poll(App *);

} // namespace impl
} // namespace sv
