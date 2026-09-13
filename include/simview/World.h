#pragma once

#include "Types.h"
#include "sync/Sync.h"
#include "world/Cloud.h"
#include "world/Wire.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace sv {

struct WorldDesc {
    const char *title = nullptr;
    bool grid = true;
    bool axes = true;
    bool controls = true;
};

enum class Projection : int {
    Perspective = 0,
    Orthographic = 1,
};

struct CameraDesc {
    float focus[3] = {0.0f, 0.0f, 0.0f};
    float distance = 5.0f;
    float azimuth_deg = -45.0f;
    float elevation_deg = 30.0f;
    float fov_deg = 45.0f;
    sv::Projection projection = sv::Projection::Perspective;
};

struct LightDesc {
    float direction[3] = {0.0f, 0.0f, 1.0f};
    float color[3] = {1.0f, 1.0f, 1.0f};
    float intensity = 0.7f;
};

struct Pick {
    float point[3] = {0.0f, 0.0f, 0.0f};
    float distance = 0.0f;
    std::int32_t index = -1;
    impl::Cloud cloud;
    bool Ground() const { return cloud.p == nullptr; }
    bool On(const sv::Cloud &c) const {
        return cloud.p != nullptr && cloud.p == c.Raw().p;
    }
    bool On(const sv::Wire &w) const {
        return cloud.p != nullptr && cloud.p == w.Raw().p;
    }
};

enum class Tool : int {
    Camera = 0,
    Cut = 1,
};

struct Stroke;

namespace impl {
bool stroke_crosses(const Stroke &, const float p[3], const float q[3]);
bool stroke_carry(const Stroke &, const float at[3], float to[3]);
}

struct Stroke {
    float from[2] = {0.0f, 0.0f};
    float to[2] = {0.0f, 0.0f};
    float clip[16] = {};
    impl::Cloud item;
    std::int32_t index = -1;
    bool Crosses(const float p[3], const float q[3]) const {
        return impl::stroke_crosses(*this, p, q);
    }
    bool Carry(const float at[3], float to[3]) const {
        return impl::stroke_carry(*this, at, to);
    }
    bool On(const sv::Cloud &c) const {
        return item.p != nullptr && item.p == c.Raw().p;
    }
    bool On(const sv::Wire &w) const {
        return item.p != nullptr && item.p == w.Raw().p;
    }
};

namespace impl {

World world_create(App *, const WorldDesc &);
void world_camera(World, const CameraDesc &);
bool world_light(World, const LightDesc &);
void world_ambient(World, const float rgb[3]);
void world_track(World, SyncGate);
void world_untracked_pull(World);
void world_on_pick(World, void (*fn)(const Pick &, void *), void *user,
                   void (*free)(void *));
void world_follow(World, Cloud, std::int32_t index);
void world_on_stroke(World, void (*fn)(const Stroke &, void *), void *user,
                     void (*free)(void *));
void world_tool(World, int);
int world_tool(World);

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

    World &Light(const LightDesc &d) {
        impl::world_light(w_, d);
        return *this;
    }

    World &Ambient(float r, float g, float b) {
        const float rgb[3] = {r, g, b};
        impl::world_ambient(w_, rgb);
        return *this;
    }

    template <class F> World &OnPick(F fn) {
        impl::world_on_pick(
            w_, [](const Pick &p, void *u) { (*static_cast<F *>(u))(p); },
            new F(std::move(fn)), [](void *u) { delete static_cast<F *>(u); });
        return *this;
    }

    World &Follow(const Pick &p) {
        impl::world_follow(w_, p.cloud, p.index);
        return *this;
    }

    World &Unfollow() {
        impl::world_follow(w_, impl::Cloud{}, -1);
        return *this;
    }

    template <class F> World &OnStroke(F fn) {
        impl::world_on_stroke(
            w_, [](const Stroke &s, void *u) { (*static_cast<F *>(u))(s); },
            new F(std::move(fn)), [](void *u) { delete static_cast<F *>(u); });
        return *this;
    }

    World &Tool(sv::Tool t) {
        impl::world_tool(w_, int(t));
        return *this;
    }

    sv::Tool Tool() const { return sv::Tool(impl::world_tool(w_)); }

    sv::Wire Wire(std::span<const std::uint32_t> edges,
                  const WireDesc &d = {}) {
        return sv::Wire{
            impl::wire_create(w_, edges.data(), edges.size() / 2, d)};
    }

    template <class P>
    sv::Wire Wire(const P &p, std::span<const std::uint32_t> edges,
                  const WireDesc &d = {}) {
        const std::uint32_t *e = edges.data();
        const std::size_t n = edges.size() / 2;
        if constexpr (impl::Synced<P>)
            impl::world_track(w_, p.Gate());
        if constexpr (requires {
                          wire_from_source(w_, source_of(p), e, n, d);
                      }) {
            if constexpr (!impl::Synced<P>)
                impl::world_untracked_pull(w_);
            return sv::Wire{wire_from_source(w_, source_of(p), e, n, d)};
        } else if constexpr (requires { wire_from_source(w_, p, e, n, d); }) {
            if constexpr (!impl::Synced<P>)
                impl::world_untracked_pull(w_);
            return sv::Wire{wire_from_source(w_, p, e, n, d)};
        } else if constexpr (requires {
                                 impl::wire_from_host(w_, host_of(p), e, n, d);
                             })
            return sv::Wire{impl::wire_from_host(w_, host_of(p), e, n, d)};
        else
            static_assert(impl::no_door<P>,
                          "drawing a wire over a producer that keeps its "
                          "data on the device is opt-in, and this file has "
                          "not included the door header that enables it "
                          "(docs/design.md, \"Integration with sims\"). If "
                          "it has, then this type is not one of that door's "
                          "buffer sources and declares no source_of. A Sync "
                          "over host data needs its T to be a contiguous, "
                          "sized range of float.");
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
