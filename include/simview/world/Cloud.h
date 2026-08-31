#pragma once

#include "../Types.h"
#include "../sync/Sync.h"

#include <cstddef>
#include <span>

namespace sv {

enum class CloudMode : int {
    Solid = 0,
    Additive = 1,
    Alpha = 2,
};

enum class CloudMap : int {
    Uniform = 0,
    Magnitude = 1,
    Direction = 2,
    Components = 3,
};

struct CloudDesc {
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float radius = 0.05f;
    CloudMode mode = CloudMode::Solid;
    CloudMap map = CloudMap::Uniform;
    float map_scale = 1.0f;
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
bool cloud_update_values(Cloud, const float *xyz, std::size_t count);
bool cloud_values_from_host(Cloud, HostSource);
World cloud_world(Cloud);
void world_track(World, SyncGate);
void world_untracked_pull(World);

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

    bool UpdateColors(std::span<const float> xyz) {
        return impl::cloud_update_values(c_, xyz.data(), xyz.size() / 3);
    }

    template <class P> Cloud &Colors(const P &p) {
        const impl::World w = impl::cloud_world(c_);
        if constexpr (impl::Synced<P>)
            impl::world_track(w, p.Gate());
        if constexpr (requires {
                          cloud_values_from_source(c_, source_of(p));
                      }) {
            if constexpr (!impl::Synced<P>)
                impl::world_untracked_pull(w);
            cloud_values_from_source(c_, source_of(p));
        } else if constexpr (requires { cloud_values_from_source(c_, p); }) {
            if constexpr (!impl::Synced<P>)
                impl::world_untracked_pull(w);
            cloud_values_from_source(c_, p);
        } else if constexpr (requires {
                                 impl::cloud_values_from_host(c_, host_of(p));
                             })
            impl::cloud_values_from_host(c_, host_of(p));
        else
            static_assert(impl::no_door<P>,
                          "colouring a cloud from a producer that keeps its "
                          "data on the device is opt-in, and this file has "
                          "not included the door header that enables it "
                          "(docs/design.md, \"Integration with sims\"). If it "
                          "has, then this type is not one of that door's "
                          "buffer sources and declares no source_of. A Sync "
                          "over host data needs its T to be a contiguous, "
                          "sized range of float.");
        return *this;
    }

  private:
    impl::Cloud c_;
};

}
