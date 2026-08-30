#pragma once

#include "Field.h"
#include "Types.h"

#include <cstddef>
#include <span>

namespace sv {

struct ParticlesDesc {
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float radius = 3.0f;
};

struct LinesDesc {
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float width = 1.5f;
};

struct Range2 {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
};

struct ViewDesc {
    const char *title = "view";
};

namespace impl {

struct Particles {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

template <class> inline constexpr bool no_door = false;

struct Lines {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

Particles particles_create(Scene, const ParticlesDesc &);
bool particles_update(Particles, const float *xy, std::size_t count);
Lines lines_create(Scene, const LinesDesc &);
bool lines_update(Lines, const float *xyxy, std::size_t count);
void scene_range(Scene, const Range2 &);
Scene app_scene(App *);
Scene view_create(App *, const ViewDesc &);

}

class Particles {
  public:
    Particles() = default;
    explicit Particles(impl::Particles p) : p_(p) {}

    explicit operator bool() const { return bool(p_); }
    impl::Particles Raw() const { return p_; }

    bool Update(std::span<const float> xy) {
        return impl::particles_update(p_, xy.data(), xy.size() / 2);
    }

  private:
    impl::Particles p_;
};

class Lines {
  public:
    Lines() = default;
    explicit Lines(impl::Lines l) : l_(l) {}

    explicit operator bool() const { return bool(l_); }
    impl::Lines Raw() const { return l_; }

    bool Update(std::span<const float> xyxy) {
        return impl::lines_update(l_, xyxy.data(), xyxy.size() / 4);
    }

  private:
    impl::Lines l_;
};

class Scene {
  public:
    Scene() = default;
    explicit Scene(impl::Scene s) : s_(s) {}

    explicit operator bool() const { return bool(s_); }
    impl::Scene Raw() const { return s_; }

    sv::Field Field(const FieldDesc &d) {
        return sv::Field{impl::field_create(s_, d)};
    }

    sv::Particles Particles(const ParticlesDesc &d = {}) {
        return sv::Particles{impl::particles_create(s_, d)};
    }

    sv::Lines Lines(const LinesDesc &d = {}) {
        return sv::Lines{impl::lines_create(s_, d)};
    }

    Scene &Range(const Range2 &r) {
        impl::scene_range(s_, r);
        return *this;
    }

    template <class P> sv::Field Field(const P &p, const FieldDesc &d) {
        if constexpr (requires { field_from_source(s_, source_of(p), d); })
            return sv::Field{field_from_source(s_, source_of(p), d)};
        else if constexpr (requires { field_from_source(s_, p, d); })
            return sv::Field{field_from_source(s_, p, d)};
        else
            static_assert(impl::no_door<P>,
                          "drawing a producer that keeps its data on the "
                          "device is opt-in, and this file has not included "
                          "the door header that enables it (docs/design.md, "
                          "\"Integration with sims\"). If it has, then this "
                          "type is not one of that door's buffer sources and "
                          "declares no source_of.");
    }

    template <class P> sv::Lines Lines(const P &p, const LinesDesc &d = {}) {
        if constexpr (requires { lines_from_source(s_, source_of(p), d); })
            return sv::Lines{lines_from_source(s_, source_of(p), d)};
        else if constexpr (requires { lines_from_source(s_, p, d); })
            return sv::Lines{lines_from_source(s_, p, d)};
        else
            static_assert(impl::no_door<P>,
                          "drawing a producer that keeps its data on the "
                          "device is opt-in, and this file has not included "
                          "the door header that enables it (docs/design.md, "
                          "\"Integration with sims\"). If it has, then this "
                          "type is not one of that door's buffer sources and "
                          "declares no source_of.");
    }

    template <class P>
    sv::Particles Particles(const P &p, const ParticlesDesc &d = {}) {
        if constexpr (requires { particles_from_source(s_, source_of(p), d); })
            return sv::Particles{particles_from_source(s_, source_of(p), d)};
        else if constexpr (requires { particles_from_source(s_, p, d); })
            return sv::Particles{particles_from_source(s_, p, d)};
        else
            static_assert(impl::no_door<P>,
                          "drawing a producer that keeps its data on the "
                          "device is opt-in, and this file has not included "
                          "the door header that enables it (docs/design.md, "
                          "\"Integration with sims\"). If it has, then this "
                          "type is not one of that door's buffer sources and "
                          "declares no source_of.");
    }

  private:
    impl::Scene s_;
};

}
