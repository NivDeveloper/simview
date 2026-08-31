#pragma once

#include "Types.h"
#include "sync/Sync.h"
#include "world/Cloud.h"

#include <concepts>

namespace sv {

struct WorldDesc {
    const char *title = "world";
    bool grid = true;
    bool axes = true;
};

struct CameraDesc {
    float focus[3] = {0.0f, 0.0f, 0.0f};
    float distance = 5.0f;
    float azimuth_deg = -45.0f;
    float elevation_deg = 30.0f;
    float fov_deg = 45.0f;
};

namespace impl {

World world_create(App *, const WorldDesc &);
void world_camera(World, const CameraDesc &);
void world_track(World, SyncGate);
void world_untracked_pull(World);

}

class World {
  public:
    World() = default;
    explicit World(impl::World w) : w_(w) {}

    explicit operator bool() const { return bool(w_); }
    impl::World Raw() const { return w_; }

    World &Camera(const CameraDesc &d) {
        impl::world_camera(w_, d);
        return *this;
    }

    sv::Cloud Cloud(const CloudDesc &d = {}) {
        return sv::Cloud{impl::cloud_create(w_, d)};
    }

    template <class P> sv::Cloud Cloud(const P &p, const CloudDesc &d = {}) {
        if constexpr (impl::Synced<P>)
            impl::world_track(w_, p.Gate());
        if constexpr (requires { cloud_from_source(w_, source_of(p), d); }) {
            if constexpr (!impl::Synced<P>)
                impl::world_untracked_pull(w_);
            return sv::Cloud{cloud_from_source(w_, source_of(p), d)};
        } else if constexpr (requires { cloud_from_source(w_, p, d); }) {
            if constexpr (!impl::Synced<P>)
                impl::world_untracked_pull(w_);
            return sv::Cloud{cloud_from_source(w_, p, d)};
        } else if constexpr (requires { cloud_from_host(w_, host_of(p), d); })
            return sv::Cloud{cloud_from_host(w_, host_of(p), d)};
        else
            static_assert(impl::no_door<P>,
                          "drawing a producer that keeps its data on the "
                          "device is opt-in, and this file has not included "
                          "the door header that enables it (docs/design.md, "
                          "\"Integration with sims\"). If it has, then this "
                          "type is not one of that door's buffer sources and "
                          "declares no source_of. A Sync over a "
                          "device-resident type needs that header too; a "
                          "Sync over host data needs its T to be a "
                          "contiguous, sized range of float.");
    }

  private:
    impl::World w_;
};

}
