#pragma once

// The ONE public header that names SDL types — as forward
// declarations, never includes. Everything here is opt-in interop for
// consumers that ALSO use SDL (or something wrapping it, like a
// compute runtime): including this file is declaring "SDL is my
// dependency too". The core surface never needs it, and the umbrella
// never includes it.

#include "App.h"
#include "Field.h"

struct SDL_GPUBuffer;
struct SDL_GPUDevice;

namespace simview {

// The device behind an App — share it with a compute runtime (which
// ADOPTS it), claim further windows on it, and so on. Non-owning;
// valid while the App lives.
SDL_GPUDevice *native_device(App *);

// A field that reads a caller-owned GPU buffer directly — zero-copy.
// The buffer lives on THIS App's device, was created with
// GRAPHICS_STORAGE_READ (a compute runtime's exports carry it), and
// holds w*h f32 values. Rebinding per frame is the expected idiom:
// resident sim buffers ping-pong. The buffer must outlive its use in
// the frame in flight; update() on such a field is a named refusal.
Field field_from_buffer(App *, SDL_GPUBuffer *, const FieldDesc &);
bool field_rebind(Field, SDL_GPUBuffer *);

} // namespace simview
