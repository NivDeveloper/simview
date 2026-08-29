#pragma once

// The field view: a 2-D grid of values drawn as colormapped pixels —
// the picture of an array. The sim owns its data in whatever form it
// likes; a field looks at a pointer once per update. Move 2 scope:
// ONE field per App (the window IS the field, aspect-fit with
// letterbox bars), f32 values only — both gates named in last_error.

#include "Types.h"

#include <cstddef>
#include <span>

namespace simview {

struct App;

enum class Colormap : std::int32_t { Gray = 0, Hue = 1, Viridis = 2 };

struct FieldDesc {
    Extent2 extent;               // the grid, w x h
    DType dtype = DType::f32;     // Move 2 accepts f32 only
    Colormap map = Colormap::Gray;
    float lo = 0.0f, hi = 1.0f;   // value range -> the colormap's ends
};

// ── seam ────────────────────────────────────────────────────────────
struct Field {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

Field field_create(App *, const FieldDesc &);
// data holds count values of the DECLARED dtype, tightly packed,
// row-major; count must equal w*h. Checked, named. The upload happens
// with the next rendered frame (or shot/step).
bool field_update(Field, const void *data, DType, std::size_t count);

// ── sugar ───────────────────────────────────────────────────────────
class FieldHandle {
  public:
    FieldHandle() = default;
    explicit FieldHandle(Field f) : f_(f) {}
    explicit operator bool() const { return bool(f_); }
    bool update(std::span<const float> v) {
        return field_update(f_, v.data(), DType::f32, v.size());
    }

  private:
    Field f_;
};

} // namespace simview
