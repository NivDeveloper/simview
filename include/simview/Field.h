#pragma once

#include "Types.h"

#include <cstddef>
#include <span>

namespace sv {

enum class Colormap : std::int32_t { Gray = 0, Hue = 1, Viridis = 2 };

struct FieldDesc {
    Extent2 extent;
    DType dtype = DType::f32;
    Colormap map = Colormap::Gray;
    float lo = 0.0f, hi = 1.0f;
};

namespace seam {

struct App;

struct Field {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

Field field_create(App *, const FieldDesc &);
bool field_update(Field, const void *data, DType, std::size_t count);

}

class Field {
  public:
    Field() = default;
    explicit Field(seam::Field f) : f_(f) {}
    explicit operator bool() const { return bool(f_); }
    seam::Field Raw() const { return f_; }

    bool Update(std::span<const float> v) {
        return seam::field_update(f_, v.data(), DType::f32, v.size());
    }

  private:
    seam::Field f_;
};

}
