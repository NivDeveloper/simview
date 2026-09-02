#pragma once

#include "Event.h"
#include "Panel.h"
#include "Plots.h"
#include "Scene.h"
#include "Theme.h"
#include "Types.h"
#include "World.h"

#include <cstdint>
#include <forward_list>
#include <functional>
#include <utility>

namespace sv {

struct Stats {
    std::uint64_t frames = 0;
    std::uint64_t uploads = 0;
    std::uint64_t pipelines = 0;
    std::uint64_t draws = 0;
    std::uint64_t culled = 0;
    std::uint64_t triangles = 0;
};

namespace impl {

App *app_init(const Config &);
void app_quit(App *);
void app_on_frame(App *, void (*fn)(void *), void *user);
void app_theme(App *, const Theme &);
void app_layout(App *, sv::Layout);
void app_on_event(App *, void (*fn)(const Event &, void *), void *user);
void app_request_quit(App *);
void app_run(App *);
void app_step(App *);
bool app_shot(App *, const char *bmp_path);
void app_on_ui(App *, void (*fn)(void *), void *user);
void app_post_event(App *, const Event &);
Stats app_stats(App *);

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

    App &Layout(sv::Layout l) {
        impl::app_layout(a_, l);
        return *this;
    }

    App &Theme(const sv::Theme &t) {
        impl::app_theme(a_, t);
        return *this;
    }

    App &OnFrame(std::function<void()> fn) {
        cbs_.push_front(std::move(fn));
        impl::app_on_frame(
            a_, [](void *u) { (*static_cast<std::function<void()> *>(u))(); },
            &cbs_.front());
        return *this;
    }

    App &OnEvent(std::function<void(const Event &)> fn) {
        ecbs_.push_front(std::move(fn));
        impl::app_on_event(
            a_,
            [](const Event &e, void *u) {
                (*static_cast<std::function<void(const Event &)> *>(u))(e);
            },
            &ecbs_.front());
        return *this;
    }

    App &OnKey(Key k, std::function<void()> fn) {
        return OnEvent([k, fn = std::move(fn)](const Event &e) {
            if (e.type == Event::Type::KeyDown && !e.repeat && Is(e, k))
                fn();
        });
    }

    void RequestQuit() { impl::app_request_quit(a_); }
    void Run() { impl::app_run(a_); }
    void Step() { impl::app_step(a_); }
    bool Shot(const char *path) { return impl::app_shot(a_, path); }

    App &OnUi(std::function<void()> fn) {
        cbs_.push_front(std::move(fn));
        impl::app_on_ui(
            a_, [](void *u) { (*static_cast<std::function<void()> *>(u))(); },
            &cbs_.front());
        return *this;
    }

    App &PostEvent(const Event &e) {
        impl::app_post_event(a_, e);
        return *this;
    }

    sv::Stats Stats() const { return impl::app_stats(a_); }

    sv::Scene Scene() { return sv::Scene{impl::app_scene(a_)}; }

    sv::Scene View(const ViewDesc &d = {}) {
        return sv::Scene{impl::view_create(a_, d)};
    }

    sv::World World(const WorldDesc &d = {}) {
        return sv::World{impl::world_create(a_, d)};
    }

    sv::Field Field(const FieldDesc &d) { return Scene().Field(d); }

    sv::Plot Plot(const PlotDesc &d) {
        return sv::Plot{impl::plot_create(a_, d)};
    }

    sv::Plot3D Plot3D(const Plot3DDesc &d) {
        return sv::Plot3D{impl::plot3d_create(a_, d)};
    }

    sv::Particles Particles(const ParticlesDesc &d = {}) {
        return Scene().Particles(d);
    }

    sv::Lines Lines(const LinesDesc &d = {}) { return Scene().Lines(d); }

    App &SceneRange(const Range2 &r) {
        Scene().Range(r);
        return *this;
    }

    sv::Panel Panel(const char *title) {
        return sv::Panel{impl::panel_create(a_, title)};
    }

    sv::Panel Controls(Executor &sim, const char *title = "controls") {
        return Panel(title).Transport(sim);
    }

    template <class P> sv::Field Field(const P &p, const FieldDesc &d) {
        return Scene().Field(p, d);
    }

    template <class P>
    sv::Particles Particles(const P &p, const ParticlesDesc &d = {}) {
        return Scene().Particles(p, d);
    }

    template <class P> sv::Lines Lines(const P &p, const LinesDesc &d = {}) {
        return Scene().Lines(p, d);
    }

  private:
    void reset() {
        if (a_)
            impl::app_quit(std::exchange(a_, nullptr));
    }

    impl::App *a_ = nullptr;
    std::forward_list<std::function<void()>> cbs_;
    std::forward_list<std::function<void(const Event &)>> ecbs_;
};

}
