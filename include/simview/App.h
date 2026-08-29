#pragma once

#include "Event.h"
#include "Field.h"
#include "Types.h"

#include <forward_list>
#include <functional>
#include <utility>

namespace sv {

namespace impl {

App *app_init(const Config &);
void app_quit(App *);
void app_on_frame(App *, void (*fn)(void *), void *user);
void app_on_event(App *, void (*fn)(const Event &, void *), void *user);
void app_request_quit(App *);
void app_run(App *);
void app_step(App *);
bool app_shot(App *, const char *bmp_path);

}

class App {
  public:
    explicit App(const Config &c = {}) : a_(impl::app_init(c)) {}
    explicit App(impl::App *a) : a_(a) {}
    App(App &&o) noexcept : a_(std::exchange(o.a_, nullptr)) {}

    App &operator=(App &&o) noexcept {
        if (this != &o) {
            reset();
            a_ = std::exchange(o.a_, nullptr);
        }
        return *this;
    }

    ~App() { reset(); }

    explicit operator bool() const { return a_ != nullptr; }
    impl::App *Raw() const { return a_; }

    void OnFrame(std::function<void()> fn) {
        cbs_.push_front(std::move(fn));
        impl::app_on_frame(
            a_, [](void *u) { (*static_cast<std::function<void()> *>(u))(); },
            &cbs_.front());
    }

    void OnEvent(std::function<void(const Event &)> fn) {
        ecbs_.push_front(std::move(fn));
        impl::app_on_event(
            a_,
            [](const Event &e, void *u) {
                (*static_cast<std::function<void(const Event &)> *>(u))(e);
            },
            &ecbs_.front());
    }

    void OnKey(Key k, std::function<void()> fn) {
        OnEvent([k, fn = std::move(fn)](const Event &e) {
            if (e.type == Event::Type::KeyDown && !e.repeat && Is(e, k)) fn();
        });
    }

    void RequestQuit() { impl::app_request_quit(a_); }
    void Run() { impl::app_run(a_); }
    void Step() { impl::app_step(a_); }
    bool Shot(const char *path) { return impl::app_shot(a_, path); }

    sv::Field Field(const FieldDesc &d) {
        return sv::Field{impl::field_create(a_, d)};
    }

  private:
    void reset() {
        if (a_) impl::app_quit(std::exchange(a_, nullptr));
    }

    impl::App *a_ = nullptr;
    std::forward_list<std::function<void()>> cbs_;
    std::forward_list<std::function<void(const Event &)>> ecbs_;
};

}
