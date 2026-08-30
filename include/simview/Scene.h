#pragma once

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

namespace impl {

struct App;

struct Particles {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

Particles particles_create(App *, const ParticlesDesc &);
bool particles_update(Particles, const float *xy, std::size_t count);
void scene_range(App *, const Range2 &);

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

}
