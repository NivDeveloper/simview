#pragma once

#include "App.h"
#include "Field.h"

struct SDL_GPUBuffer;
struct SDL_GPUDevice;

namespace sv {

namespace seam {
SDL_GPUDevice *native_device(App *);
Field field_from_buffer(App *, SDL_GPUBuffer *, const FieldDesc &);
bool field_rebind(Field, SDL_GPUBuffer *);
}

inline SDL_GPUDevice *NativeDevice(const App &a) {
    return seam::native_device(a.Raw());
}

inline Field FieldFromBuffer(const App &a, SDL_GPUBuffer *buf,
                             const FieldDesc &d) {
    return Field{seam::field_from_buffer(a.Raw(), buf, d)};
}

inline bool FieldRebind(const Field &f, SDL_GPUBuffer *buf) {
    return seam::field_rebind(f.Raw(), buf);
}

}
