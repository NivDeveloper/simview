#pragma once

#include "App.h"
#include "sync/Sync.h"

#include <gpud/Device.h>

#include <concepts>

namespace sv {

namespace impl {
gpud::Device *app_device(App *);
Field field_from_source(Scene, gpud::BufferSource, const FieldDesc &);
Particles particles_from_source(Scene, gpud::BufferSource,
                                const ParticlesDesc &);
Lines lines_from_source(Scene, gpud::BufferSource, const LinesDesc &);
}

inline gpud::Device &Device(const App &a) { return *impl::app_device(a.Raw()); }

inline gpud::BufferSource source_of(const Sync<gpud::Buffer> &s) {
    return {+[](void *u) -> gpud::Buffer * {
                const gpud::Buffer &b =
                    static_cast<const Sync<gpud::Buffer> *>(u)->Shown();
                return b ? const_cast<gpud::Buffer *>(&b) : nullptr;
            },
            const_cast<Sync<gpud::Buffer> *>(&s)};
}

template <typename P>
    requires requires(const P &p) {
        { source_of(p) } -> std::same_as<gpud::BufferSource>;
    }
gpud::BufferSource source_of(const Sync<P> &s) {
    return {+[](void *u) -> gpud::Buffer * {
                return source_of(static_cast<const Sync<P> *>(u)->Shown())
                    .current();
            },
            const_cast<Sync<P> *>(&s)};
}

}
