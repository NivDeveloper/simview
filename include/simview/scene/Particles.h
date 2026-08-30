#pragma once

#include "../Types.h"

#include <cstddef>
#include <span>

namespace sv {

struct ParticlesDesc {
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float radius = 3.0f;
};

namespace impl {

struct Particles {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

Particles particles_create(Scene, const ParticlesDesc &);
Particles particles_from_host(Scene, HostSource, const ParticlesDesc &);
bool particles_update(Particles, const float *xy, std::size_t count);

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
