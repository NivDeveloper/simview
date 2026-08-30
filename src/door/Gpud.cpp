// The gpud door's one exported accessor. The pull-model constructors
// (field_from_source, particles_from_source) live with their kinds:
// a kind owns its own source semantics.

#include "../core/Engine.h"

#include <simview/gpud.h>

namespace sv {
namespace impl {

gpud::Device *app_device(App *a) { return a ? a->gdev.get() : nullptr; }

} // namespace impl
} // namespace sv
