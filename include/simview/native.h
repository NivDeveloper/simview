#pragma once

// The ONE public header that names SDL types — as forward
// declarations, never includes. Everything here is opt-in interop for
// consumers that ALSO use SDL (or something wrapping it, like a
// compute runtime): including this file is declaring "SDL is my
// dependency too". The core surface never needs it, and the umbrella
// never includes it.

#include "App.h"

struct SDL_GPUDevice;

namespace simview {

// The device behind an App — share it with a compute runtime (which
// ADOPTS it), claim further windows on it, and so on. Non-owning;
// valid while the App lives.
SDL_GPUDevice *native_device(App *);

} // namespace simview
