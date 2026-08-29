#pragma once

// The app shell: one App owns the SDL side (subsystem, GPU device,
// later the window and frame loop) and everything dies with it.
// Move 1 scope: init/quit and the frame-callback registration; run()
// drives no frames yet (the window arrives in Move 2).

#include "Event.h"
#include "Field.h"
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
// Register a per-frame callback: fires once per loop iteration, in
// registration order, before the frame renders.
void app_on_frame(App *, void (*fn)(void *), void *user);
// Register an input callback: fires for every key event the library
// did not consume itself, during run()'s poll phase.
void app_on_event(App *, void (*fn)(const Event &, void *), void *user);
// Ask the loop to stop after the current iteration; safe from inside
// any callback.
void app_request_quit(App *);
// Block driving the window's frame loop until quit is requested or the
// window closes. A headless App returns immediately — drive it with
// step()/shot() instead.
void app_run(App *);
// One headless "frame": fires the frame callbacks (the sim advances)
// and applies pending field uploads, without a window. run()'s loop
// body is the windowed twin.
void app_step(App *);
// Render the CURRENT field state offscreen through the same pass the
// window uses and write a BMP — the eyeless-verification primitive.
bool app_shot(App *, const char *bmp_path);

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
    void on_event(std::function<void(const Event &)> fn) {
        ecbs_.push_front(std::move(fn));
        app_on_event(a_,
                     [](const Event &e, void *u) {
                         (*static_cast<std::function<void(const Event &)> *>(u))(e);
                     },
                     &ecbs_.front());
    }
    // The common case: one key, one action, on key-down.
    void on_key(Key k, std::function<void()> fn) {
        on_event([k, fn = std::move(fn)](const Event &e) {
            if (e.type == Event::Type::KeyDown && !e.repeat && is(e, k)) fn();
        });
    }
    void request_quit() { app_request_quit(a_); }
    void run() { app_run(a_); }
    void step() { app_step(a_); }
    bool shot(const char *path) { return app_shot(a_, path); }
    FieldHandle field(const FieldDesc &d) {
        return FieldHandle{field_create(a_, d)};
    }

  private:
    void reset() {
        if (a_) app_quit(std::exchange(a_, nullptr));
    }
    App *a_ = nullptr;
    // forward_list: stable addresses for the void* the seam holds.
    std::forward_list<std::function<void()>> cbs_;
    std::forward_list<std::function<void(const Event &)>> ecbs_;
};

inline AppHandle init(const Config &c = {}) { return AppHandle{app_init(c)}; }

} // namespace simview
