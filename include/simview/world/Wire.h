#pragma once

#include "../Types.h"
#include "../sync/Sync.h"
#include "Cloud.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sv {

struct WireDesc {
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float width = 1.5f;
    CloudMode mode = CloudMode::Solid;
};

namespace impl {

struct Wire {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

Wire wire_create(World, const std::uint32_t *edges, std::size_t count,
                 const WireDesc &);
Wire wire_from_host(World, HostSource, const std::uint32_t *edges,
                    std::size_t count, const WireDesc &);
bool wire_update(Wire, const float *xyz, std::size_t count);
bool wire_update_mask(Wire, const float *alive, std::size_t count);
bool wire_mask_from_host(Wire, HostSource);
World wire_world(Wire);

}

class Wire {
  public:
    Wire() = default;
    explicit Wire(impl::Wire w) : w_(w) {}

    explicit operator bool() const { return bool(w_); }
    impl::Wire Raw() const { return w_; }

    bool Update(std::span<const float> xyz) {
        return impl::wire_update(w_, xyz.data(), xyz.size() / 3);
    }

    bool UpdateMask(std::span<const float> alive) {
        return impl::wire_update_mask(w_, alive.data(), alive.size());
    }

    template <class P> Wire &Mask(const P &p) {
        const impl::World w = impl::wire_world(w_);
        if constexpr (impl::Synced<P>)
            impl::world_track(w, p.Gate());
        if constexpr (requires { wire_mask_from_source(w_, source_of(p)); }) {
            if constexpr (!impl::Synced<P>)
                impl::world_untracked_pull(w);
            wire_mask_from_source(w_, source_of(p));
        } else if constexpr (requires { wire_mask_from_source(w_, p); }) {
            if constexpr (!impl::Synced<P>)
                impl::world_untracked_pull(w);
            wire_mask_from_source(w_, p);
        } else if constexpr (requires {
                                 impl::wire_mask_from_host(w_, host_of(p));
                             })
            impl::wire_mask_from_host(w_, host_of(p));
        else
            static_assert(impl::no_door<P>,
                          "masking a wire from a producer that keeps its "
                          "data on the device is opt-in, and this file has "
                          "not included the door header that enables it "
                          "(docs/design.md, \"Integration with sims\"). If it "
                          "has, then this type is not one of that door's "
                          "buffer sources and declares no source_of. A Sync "
                          "over host data needs its T to be a contiguous, "
                          "sized range of float, one per edge.");
        return *this;
    }

  private:
    impl::Wire w_;
};

}
