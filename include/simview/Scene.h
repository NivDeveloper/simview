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

Particles particles_create(Scene, const ParticlesDesc &);
bool particles_update(Particles, const float *xy, std::size_t count);
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

    Scene &Range(const Range2 &r) {
        impl::scene_range(s_, r);
        return *this;
    }

    template <class P> sv::Field Field(const P &p, const FieldDesc &d) {
        if constexpr (requires { source_of(p); })
            return sv::Field{field_from_source(s_, source_of(p), d)};
        else
            return sv::Field{field_from_source(s_, p, d)};
    }

    template <class P>
    sv::Particles Particles(const P &p, const ParticlesDesc &d = {}) {
        if constexpr (requires { source_of(p); })
            return sv::Particles{particles_from_source(s_, source_of(p), d)};
        else
            return sv::Particles{particles_from_source(s_, p, d)};
    }

  private:
    impl::Scene s_;
};

}
