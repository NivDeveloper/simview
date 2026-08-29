#pragma once

#include <cstddef>
#include <cstdint>

namespace sv {

struct Extent2 {
    std::uint32_t w = 0, h = 0;
};

enum class DType : std::uint8_t { f32, f64, i32, u32, u8 };

struct Config {
    const char *title = "simview";
    Extent2 size = {1200, 800};
    bool headless = false;
};

namespace impl {
const char *version();
const char *last_error();
}

inline const char *Version() { return impl::version(); }
inline const char *LastError() { return impl::last_error(); }

}
