// Input: SDL's events and the posted ones, delivered to the same
// callbacks in the same order — which is what makes a posted event a
// faithful stand-in for a keypress.

#include "../core/Engine.h"
#include "../ui/Ui.h"

#include <vector>

namespace sv {
namespace impl {

// Events posted through the automation seam, delivered exactly where
// SDL's own are: same callbacks, same order, same frame.
void deliver_posted(App *a) {
    std::vector<Event> queued;
    queued.swap(a->posted);
    for (const Event &e : queued)
        in_order(a->event_cbs, [&](const App::Ecb &c) { c.fn(e, c.user); });
}

void poll(App *a) {
    deliver_posted(a); // the automation seam is never gated by the UI
    const bool ui = ui_on(a);
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        const bool typing = ui && ui_event(a, ev);
        if (ev.type == SDL_EVENT_QUIT)
            a->quit = true;
        // Once a panel is torn out, this window is no longer the last
        // one, so closing it stops producing SDL_EVENT_QUIT — and the
        // app would run on with only a floating panel to show for it.
        if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && a->win &&
            ev.window.windowID == SDL_GetWindowID(a->win))
            a->quit = true;
        if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
            if (typing)
                continue; // a panel has the keyboard
            const Event e{ev.type == SDL_EVENT_KEY_DOWN ? Event::Type::KeyDown
                                                        : Event::Type::KeyUp,
                          std::int32_t(ev.key.scancode), ev.key.repeat};
            in_order(a->event_cbs, [&](const App::Ecb &c) { c.fn(e, c.user); });
        }
    }
}

} // namespace impl
} // namespace sv
