#pragma once

#include "App.h"
#include "Field.h"
#include "Scene.h"

#include <gpud/Device.h>

namespace sv {

namespace impl {
gpud::Device *app_device(App *);
Field field_from_source(App *, gpud::BufferSource, const FieldDesc &);
Particles particles_from_source(App *, gpud::BufferSource,
                                const ParticlesDesc &);
}

inline gpud::Device &Device(const App &a) { return *impl::app_device(a.Raw()); }

}
