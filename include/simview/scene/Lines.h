#pragma once

#include "../Types.h"

#include <cstddef>
#include <span>

namespace sv {

struct LinesDesc {
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float width = 1.5f;
};

namespace impl {

struct Lines {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

Lines lines_create(Scene, const LinesDesc &);
bool lines_update(Lines, const float *xyxy, std::size_t count);

}

class Lines {
  public:
    Lines() = default;
    explicit Lines(impl::Lines l) : l_(l) {}

    explicit operator bool() const { return bool(l_); }
    impl::Lines Raw() const { return l_; }

    bool Update(std::span<const float> xyxy) {
        return impl::lines_update(l_, xyxy.data(), xyxy.size() / 4);
    }

  private:
    impl::Lines l_;
};

}
