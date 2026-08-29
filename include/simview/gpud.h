#pragma once

#include "App.h"
#include "Field.h"

#include <gpud/Device.h>

namespace sv {

namespace impl {
gpud::Device *app_device(App *);
Field field_from_source(App *, gpud::BufferSource, const FieldDesc &);
}

inline gpud::Device &Device(const App &a) { return *impl::app_device(a.Raw()); }

}
