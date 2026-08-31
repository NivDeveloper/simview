#pragma once

#include "../Types.h"

#include <cstddef>
#include <span>

namespace sv {

enum class CloudMode : int {
    Solid = 0,
    Additive = 1,
    Alpha = 2,
};

struct CloudDesc {
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float radius = 0.05f;
    CloudMode mode = CloudMode::Solid;
};

namespace impl {

struct World {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

struct Cloud {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

Cloud cloud_create(World, const CloudDesc &);
Cloud cloud_from_host(World, HostSource, const CloudDesc &);
bool cloud_update(Cloud, const float *xyz, std::size_t count);

}

class Cloud {
  public:
    Cloud() = default;
    explicit Cloud(impl::Cloud c) : c_(c) {}

    explicit operator bool() const { return bool(c_); }
    impl::Cloud Raw() const { return c_; }

    bool Update(std::span<const float> xyz) {
        return impl::cloud_update(c_, xyz.data(), xyz.size() / 3);
    }

  private:
    impl::Cloud c_;
};

}
