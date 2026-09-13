#pragma once

#include "Items.h"

#include <gpud/Device.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sv {
namespace impl {

// One stream of floats an item draws from, whichever side owns it: the
// host's (a shadow, uploaded), a Sync's (pulled by generation), or a
// producer's device buffer (re-resolved and wrapped every frame).
struct Channel {
    nvrhi::BufferHandle buf;     // owned, when the host writes it
    nvrhi::BufferHandle wrapped; // the producer's, re-resolved per frame
    std::vector<float> shadow;
    std::size_t count = 0;    // elements the host last wrote
    std::size_t capacity = 0; // elements the buffer holds
    std::uint32_t width = 3;  // floats an element: xyz, or one for a mask
    bool dirty = false;
    bool external = false;
    gpud::BufferSource src{};
    HostSource host{};
    std::uint64_t host_gen = 0;
    // A device buffer's host copy comes through here, one frame late:
    // a shader reads it into `scratch`, which is copied into `staging`.
    nvrhi::BufferHandle scratch, staging;
    std::size_t staging_bytes = 0;
    std::size_t staged_count = 0;
    bool staged = false;

    bool live() const { return external || host || count; }
    nvrhi::IBuffer *bound() const {
        return external ? wrapped.Get() : buf.Get();
    }
};

bool channel_upload(Channel &, const Gpu &, const char *name, const float *,
                    std::size_t count);
// True when the host's copy changed this frame, so a summary over it
// (a centroid, a box) is stale.
bool channel_prepare(Channel &, const Gpu &, nvrhi::ICommandList *, Stats *,
                     const char *name);
void channel_resolve(Channel &, const Gpu &, const char *name);
void channel_readback(Channel &, WorldItem &, nvrhi::ICommandList *,
                      const char *name);

} // namespace impl
} // namespace sv
