#pragma once

// The app shell: one App owns the SDL side (subsystem, GPU device,
// later the window and frame loop) and everything dies with it.
// Move 1 scope: init/quit and the frame-callback registration; run()
// drives no frames yet (the window arrives in Move 2).

#include "Types.h"

#include <forward_list>
#include <functional>
#include <utility>

namespace simview {

struct App;

// ── seam ────────────────────────────────────────────────────────────
// Null handle + last_error() on failure. The App is not copyable
// through the seam; ownership is released exactly once via app_quit.
App *app_init(const Config &);
void app_quit(App *);
// Register a per-frame callback (fires each loop iteration once run()
// drives frames — Move 2). Callbacks run in registration order.
void app_on_frame(App *, void (*fn)(void *), void *user);
// Block until quit. Move 1: returns immediately (no window yet).
void app_run(App *);

// ── sugar ───────────────────────────────────────────────────────────
// The owning wrapper a consumer holds; every method lowers onto the
// seam calls above.
class AppHandle {
  public:
    AppHandle() = default;
    explicit AppHandle(App *a) : a_(a) {}
    AppHandle(AppHandle &&o) noexcept : a_(std::exchange(o.a_, nullptr)) {}
    AppHandle &operator=(AppHandle &&o) noexcept {
        if (this != &o) {
            reset();
            a_ = std::exchange(o.a_, nullptr);
        }
        return *this;
    }
    ~AppHandle() { reset(); }

    explicit operator bool() const { return a_ != nullptr; }
    App *get() const { return a_; }

    // std::function is sugar-legal: it is flattened to fn-ptr + void*
    // before crossing the seam, and its storage lives with the handle.
    void on_frame(std::function<void()> fn) {
        cbs_.push_front(std::move(fn));
        app_on_frame(a_, [](void *u) { (*static_cast<std::function<void()> *>(u))(); },
                     &cbs_.front());
    }
    void run() { app_run(a_); }

  private:
    void reset() {
        if (a_) app_quit(std::exchange(a_, nullptr));
    }
    App *a_ = nullptr;
    // forward_list: stable addresses for the void* the seam holds.
    std::forward_list<std::function<void()>> cbs_;
};

inline AppHandle init(const Config &c = {}) { return AppHandle{app_init(c)}; }

} // namespace simview
