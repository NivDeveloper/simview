#pragma once

#include <cstddef>
#include <cstdint>

namespace sv {

struct Extent2 {
    std::uint32_t w = 0, h = 0;
};

enum class DType : std::uint8_t { f32, f64, i32, u32, u8 };

enum class Scale : std::uint8_t { Linear, Log };

enum class PlotChrome : std::uint8_t { Bar, Rail, Menu };

enum class Icon : std::uint8_t {
    Home,
    Fit,
    Grid,
    Axes,
    Cube,
    Perspective,
    Orthographic,
    Camera,
    Light,
    Eye,
    Gear,
    Chart,
    Legend,
    Histogram,
    Density,
    Profile,
    Restart,
    Forward,
    Play,
    Pause,
    Step
};

struct Config {
    const char *title = "simview";
    Extent2 size = {1200, 800};
    bool headless = false;
};

struct HostSource {
    const void *(*fn)(const void *, std::size_t *, std::uint64_t *) = nullptr;
    const void *user = nullptr;
    explicit operator bool() const { return fn != nullptr; }
};

namespace impl {

struct App;

struct Scene {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

const char *version();
const char *last_error();
}

inline const char *Version() { return impl::version(); }
inline const char *LastError() { return impl::last_error(); }

}
