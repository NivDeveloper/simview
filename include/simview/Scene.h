#pragma once

#include "Types.h"
#include "scene/Field.h"
#include "scene/Lines.h"
#include "scene/Particles.h"

namespace sv {

struct Range2 {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
};

struct ViewDesc {
    const char *title = "view";
};

namespace impl {

template <class> inline constexpr bool no_door = false;

void scene_range(Scene, const Range2 &);
Scene app_scene(App *);
Scene view_create(App *, const ViewDesc &);

}

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
