#pragma once

#include "Panel.h"
#include "Scene.h"
#include "Types.h"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace sv {

enum class Fit : std::uint8_t { Data, Start, Fixed, Stream };

enum class AxisScale : std::uint8_t { Linear, Log10, SymLog };

struct AxisDesc {
    const char *label = nullptr;
    double min = 0.0, max = 0.0;
    Fit fit = Fit::Data;
    AxisScale scale = AxisScale::Linear;
    bool invert = false;
};

enum class Palette : std::int8_t {
    Auto = -1,
    Deep,
    Dark,
    Pastel,
    Paired,
    Viridis,
    Plasma,
    Hot,
    Cool,
    Pink,
    Jet,
    Twilight,
    RdBu,
    BrBG,
    PiYG,
    Spectral,
    Greys
};

enum class Derived : std::uint8_t { Histogram, Density, Profile };

struct PlotDesc {
    const char *title = "plot";
    AxisDesc x{};
    AxisDesc y{};
    Palette palette = Palette::Auto;
    bool derive = true;
};

struct GridDesc {
    double scale_min = 0.0, scale_max = 0.0;
    double x0 = 0.0, y0 = 0.0, x1 = 1.0, y1 = 1.0;
};

struct SeriesStyle {
    float color[4] = {0.0f, 0.0f, 0.0f, -1.0f};
    float fill[4] = {0.0f, 0.0f, 0.0f, -1.0f};
    float fill_alpha = -1.0f;
    float weight = -1.0f;
    float marker_size = -1.0f;
    float size = 4.0f;
    int marker = -2;
    bool horizontal = false;
};

template <class T> struct Points {
    std::span<const T> x, y;
    using value_type = T;
};

template <class T> struct Points3 {
    std::span<const T> x, y, z;
    using value_type = T;
};

struct Plot3DDesc {
    const char *title = "plot";
    AxisDesc x{};
    AxisDesc y{};
    AxisDesc z{};
    Palette palette = Palette::Auto;
};

namespace impl {

struct App;

enum class Family : std::int32_t { Plot2D, Plot3D };

enum class SeriesKind : std::int32_t {
    Line,
    Scatter,
    Histogram,
    Stairs,
    Shaded,
    Bars,
    Stems,
    InfLines,
    Digital,
    ErrorBars,
    Heatmap,
    Line3,
    Scatter3,
    Surface,
    Mesh
};

struct SeriesData {
    const void *a = nullptr;
    const void *b = nullptr;
    const void *c = nullptr;
    const void *d = nullptr;
    const unsigned *idx = nullptr;
    std::size_t count = 0;
    std::size_t count2 = 0;
};

struct Plot {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

struct SeriesDesc {
    const char *name = nullptr;
    SeriesKind kind = SeriesKind::Line;
    DType dtype = DType::f32;
    double param[4] = {-2.0, 0.0, 0.0, 0.0};
    SeriesData data{};
    SeriesData (*src)(void *) = nullptr;
    void *user = nullptr;
    void (*free)(void *) = nullptr;
    Range2 bounds{0.0, 0.0, 1.0, 1.0};
    SeriesStyle style{};
};

Plot plot_create(App *, const PlotDesc &);
Plot plot3d_create(App *, const Plot3DDesc &);
bool plot_series(Plot, const SeriesDesc &);
Panel plot_controls(Plot);
Plot plot_derive(Plot, Derived, const char *series);

}

template <class T> constexpr DType series_dtype() {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>,
                  "a series holds float or double values");
    return std::is_same_v<T, float> ? DType::f32 : DType::f64;
}

class Plot {
  public:
    Plot() = default;
    explicit Plot(impl::Plot p) : p_(p) {}

    explicit operator bool() const { return bool(p_); }
    impl::Plot Raw() const { return p_; }

    template <class F> Plot &Controls(F body) {
        sv::Panel bar{impl::plot_controls(p_)};
        if (bar)
            body(bar);
        return *this;
    }

    Plot Derive(Derived kind, const char *series = nullptr) {
        return Plot{impl::plot_derive(p_, kind, series)};
    }

    template <std::ranges::contiguous_range R>
    Plot &Line(const char *name, const R &y, const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Line, name, nullptr,
                        std::ranges::data(y), std::ranges::size(y),
                        series_dtype<std::ranges::range_value_t<R>>(), s);
    }

    template <std::ranges::contiguous_range RX,
              std::ranges::contiguous_range RY>
    Plot &Line(const char *name, const RX &x, const RY &y,
               const SeriesStyle &s = {}) {
        const std::size_t n =
            std::min(std::ranges::size(x), std::ranges::size(y));
        return borrowed(impl::SeriesKind::Line, name, std::ranges::data(x),
                        std::ranges::data(y), n,
                        series_dtype<std::ranges::range_value_t<RY>>(), s);
    }

    template <std::invocable F>
    Plot &Line(const char *name, F pull, const SeriesStyle &s = {}) {
        return pulled(impl::SeriesKind::Line, name, std::move(pull), s);
    }

    template <std::ranges::contiguous_range R>
    Plot &Scatter(const char *name, const R &y, const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Scatter, name, nullptr,
                        std::ranges::data(y), std::ranges::size(y),
                        series_dtype<std::ranges::range_value_t<R>>(), s);
    }

    template <std::ranges::contiguous_range RX,
              std::ranges::contiguous_range RY>
    Plot &Scatter(const char *name, const RX &x, const RY &y,
                  const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Scatter, name, std::ranges::data(x),
                        std::ranges::data(y),
                        std::min(std::ranges::size(x), std::ranges::size(y)),
                        series_dtype<std::ranges::range_value_t<RY>>(), s);
    }

    template <std::invocable F>
    Plot &Scatter(const char *name, F pull, const SeriesStyle &s = {}) {
        return pulled(impl::SeriesKind::Scatter, name, std::move(pull), s);
    }

    template <std::ranges::contiguous_range R>
    Plot &Histogram(const char *name, const R &v, int bins = -2,
                    const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Histogram, name, nullptr,
                        std::ranges::data(v), std::ranges::size(v),
                        series_dtype<std::ranges::range_value_t<R>>(), s, bins);
    }

    template <std::invocable F>
    Plot &Histogram(const char *name, F pull, int bins = -2,
                    const SeriesStyle &s = {}) {
        return pulled(impl::SeriesKind::Histogram, name, std::move(pull), s,
                      bins);
    }

    template <std::ranges::contiguous_range R>
    Plot &Stairs(const char *name, const R &y, const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Stairs, name, nullptr,
                        std::ranges::data(y), std::ranges::size(y),
                        series_dtype<std::ranges::range_value_t<R>>(), s);
    }

    template <std::ranges::contiguous_range RX,
              std::ranges::contiguous_range RY>
    Plot &Stairs(const char *name, const RX &x, const RY &y,
                 const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Stairs, name, std::ranges::data(x),
                        std::ranges::data(y),
                        std::min(std::ranges::size(x), std::ranges::size(y)),
                        series_dtype<std::ranges::range_value_t<RY>>(), s);
    }

    template <std::invocable F>
    Plot &Stairs(const char *name, F pull, const SeriesStyle &s = {}) {
        return pulled(impl::SeriesKind::Stairs, name, std::move(pull), s);
    }

    template <std::ranges::contiguous_range R>
    Plot &Shaded(const char *name, const R &y, double yref = 0.0,
                 const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Shaded, name, nullptr,
                        std::ranges::data(y), std::ranges::size(y),
                        series_dtype<std::ranges::range_value_t<R>>(), s, yref);
    }

    template <std::ranges::contiguous_range RX,
              std::ranges::contiguous_range RY>
    Plot &Shaded(const char *name, const RX &x, const RY &y, double yref = 0.0,
                 const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Shaded, name, std::ranges::data(x),
                        std::ranges::data(y),
                        std::min(std::ranges::size(x), std::ranges::size(y)),
                        series_dtype<std::ranges::range_value_t<RY>>(), s,
                        yref);
    }

    template <std::ranges::contiguous_range RX,
              std::ranges::contiguous_range R1,
              std::ranges::contiguous_range R2>
    Plot &Band(const char *name, const RX &x, const R1 &lo, const R2 &hi,
               const SeriesStyle &s = {}) {
        const std::size_t n =
            std::min({std::ranges::size(x), std::ranges::size(lo),
                      std::ranges::size(hi)});
        return borrowed3(impl::SeriesKind::Shaded, name, std::ranges::data(x),
                         std::ranges::data(lo), std::ranges::data(hi), n,
                         series_dtype<std::ranges::range_value_t<R1>>(), s);
    }

    template <std::ranges::contiguous_range R>
    Plot &Bars(const char *name, const R &y, double width = 0.67,
               const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Bars, name, nullptr,
                        std::ranges::data(y), std::ranges::size(y),
                        series_dtype<std::ranges::range_value_t<R>>(), s,
                        width);
    }

    template <std::ranges::contiguous_range RX,
              std::ranges::contiguous_range RY>
    Plot &Bars(const char *name, const RX &x, const RY &y, double width = 0.67,
               const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Bars, name, std::ranges::data(x),
                        std::ranges::data(y),
                        std::min(std::ranges::size(x), std::ranges::size(y)),
                        series_dtype<std::ranges::range_value_t<RY>>(), s,
                        width);
    }

    template <std::invocable F>
    Plot &Bars(const char *name, F pull, double width = 0.67,
               const SeriesStyle &s = {}) {
        return pulled(impl::SeriesKind::Bars, name, std::move(pull), s, width);
    }

    template <std::ranges::contiguous_range R>
    Plot &Stems(const char *name, const R &y, double ref = 0.0,
                const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Stems, name, nullptr,
                        std::ranges::data(y), std::ranges::size(y),
                        series_dtype<std::ranges::range_value_t<R>>(), s, ref);
    }

    template <std::ranges::contiguous_range RX,
              std::ranges::contiguous_range RY>
    Plot &Stems(const char *name, const RX &x, const RY &y, double ref = 0.0,
                const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Stems, name, std::ranges::data(x),
                        std::ranges::data(y),
                        std::min(std::ranges::size(x), std::ranges::size(y)),
                        series_dtype<std::ranges::range_value_t<RY>>(), s, ref);
    }

    template <std::ranges::contiguous_range R>
    Plot &InfLines(const char *name, const R &v, const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::InfLines, name, nullptr,
                        std::ranges::data(v), std::ranges::size(v),
                        series_dtype<std::ranges::range_value_t<R>>(), s);
    }

    template <std::ranges::contiguous_range RX,
              std::ranges::contiguous_range RY>
    Plot &Digital(const char *name, const RX &x, const RY &y,
                  const SeriesStyle &s = {}) {
        return borrowed(impl::SeriesKind::Digital, name, std::ranges::data(x),
                        std::ranges::data(y),
                        std::min(std::ranges::size(x), std::ranges::size(y)),
                        series_dtype<std::ranges::range_value_t<RY>>(), s);
    }

    template <std::ranges::contiguous_range RX,
              std::ranges::contiguous_range RY,
              std::ranges::contiguous_range RE>
    Plot &ErrorBars(const char *name, const RX &x, const RY &y, const RE &err,
                    const SeriesStyle &s = {}) {
        const std::size_t n =
            std::min({std::ranges::size(x), std::ranges::size(y),
                      std::ranges::size(err)});
        return borrowed3(impl::SeriesKind::ErrorBars, name,
                         std::ranges::data(x), std::ranges::data(y),
                         std::ranges::data(err), n,
                         series_dtype<std::ranges::range_value_t<RY>>(), s);
    }

    template <
        std::ranges::contiguous_range RX, std::ranges::contiguous_range RY,
        std::ranges::contiguous_range RN, std::ranges::contiguous_range RP>
    Plot &ErrorBars(const char *name, const RX &x, const RY &y, const RN &neg,
                    const RP &pos, const SeriesStyle &s = {}) {
        const std::size_t n =
            std::min({std::ranges::size(x), std::ranges::size(y),
                      std::ranges::size(neg), std::ranges::size(pos)});
        impl::plot_series(
            p_, impl::SeriesDesc{
                    .name = name,
                    .kind = impl::SeriesKind::ErrorBars,
                    .dtype = series_dtype<std::ranges::range_value_t<RY>>(),
                    .data = {.a = std::ranges::data(x),
                             .b = std::ranges::data(y),
                             .c = std::ranges::data(neg),
                             .d = std::ranges::data(pos),
                             .count = n},
                    .style = s});
        return *this;
    }

    template <std::ranges::contiguous_range R>
    Plot &Heatmap(const char *name, const R &values, std::size_t rows,
                  std::size_t cols, const GridDesc &g = {},
                  const SeriesStyle &s = {}) {
        impl::plot_series(
            p_, impl::SeriesDesc{
                    .name = name,
                    .kind = impl::SeriesKind::Heatmap,
                    .dtype = series_dtype<std::ranges::range_value_t<R>>(),
                    .param = {g.scale_min, g.scale_max, 0.0, 0.0},
                    .data = {.b = std::ranges::data(values),
                             .count = rows,
                             .count2 = cols},
                    .bounds = {g.x0, g.y0, g.x1, g.y1},
                    .style = s});
        return *this;
    }

    template <std::invocable F>
    Plot &Heatmap(const char *name, F pull, std::size_t rows, std::size_t cols,
                  const GridDesc &g = {}, const SeriesStyle &s = {}) {
        using Ret = std::remove_cvref_t<std::invoke_result_t<F>>;
        struct Cell {
            F f;
            std::size_t rows, cols;
        };
        impl::plot_series(
            p_, impl::SeriesDesc{
                    .name = name,
                    .kind = impl::SeriesKind::Heatmap,
                    .dtype = series_dtype<std::ranges::range_value_t<Ret>>(),
                    .param = {g.scale_min, g.scale_max, 0.0, 0.0},
                    .src =
                        [](void *u) {
                            auto *c = static_cast<Cell *>(u);
                            decltype(auto) r = c->f();
                            return impl::SeriesData{.b = std::ranges::data(r),
                                                    .count = c->rows,
                                                    .count2 = c->cols};
                        },
                    .user = new Cell{std::move(pull), rows, cols},
                    .free = [](void *u) { delete static_cast<Cell *>(u); },
                    .bounds = {g.x0, g.y0, g.x1, g.y1},
                    .style = s});
        return *this;
    }

  private:
    Plot &borrowed3(impl::SeriesKind k, const char *name, const void *a,
                    const void *b, const void *c, std::size_t n, DType dt,
                    const SeriesStyle &s) {
        impl::plot_series(
            p_, impl::SeriesDesc{.name = name,
                                 .kind = k,
                                 .dtype = dt,
                                 .data = {.a = a, .b = b, .c = c, .count = n},
                                 .style = s});
        return *this;
    }

    Plot &borrowed(impl::SeriesKind k, const char *name, const void *x,
                   const void *y, std::size_t n, DType dt, const SeriesStyle &s,
                   double p0 = -2.0) {
        impl::plot_series(p_,
                          impl::SeriesDesc{.name = name,
                                           .kind = k,
                                           .dtype = dt,
                                           .param = {p0, 0.0, 0.0, 0.0},
                                           .data = {.a = x, .b = y, .count = n},
                                           .style = s});
        return *this;
    }

    template <class F>
    Plot &pulled(impl::SeriesKind k, const char *name, F pull,
                 const SeriesStyle &s, double p0 = -2.0) {
        using Ret = std::remove_cvref_t<std::invoke_result_t<F>>;
        if constexpr (std::ranges::contiguous_range<Ret>) {
            impl::plot_series(
                p_,
                impl::SeriesDesc{
                    .name = name,
                    .kind = k,
                    .dtype = series_dtype<std::ranges::range_value_t<Ret>>(),
                    .param = {p0, 0.0, 0.0, 0.0},
                    .src =
                        [](void *u) {
                            decltype(auto) r = (*static_cast<F *>(u))();
                            return impl::SeriesData{.b = std::ranges::data(r),
                                                    .count =
                                                        std::ranges::size(r)};
                        },
                    .user = new F(std::move(pull)),
                    .free = [](void *u) { delete static_cast<F *>(u); },
                    .style = s});
        } else {
            impl::plot_series(
                p_, impl::SeriesDesc{
                        .name = name,
                        .kind = k,
                        .dtype = series_dtype<typename Ret::value_type>(),
                        .param = {p0, 0.0, 0.0, 0.0},
                        .src =
                            [](void *u) {
                                decltype(auto) r = (*static_cast<F *>(u))();
                                return impl::SeriesData{
                                    .a = r.x.data(),
                                    .b = r.y.data(),
                                    .count = std::min(r.x.size(), r.y.size())};
                            },
                        .user = new F(std::move(pull)),
                        .free = [](void *u) { delete static_cast<F *>(u); },
                        .style = s});
        }
        return *this;
    }

    impl::Plot p_;
};

class Plot3D {
  public:
    Plot3D() = default;
    explicit Plot3D(impl::Plot p) : p_(p) {}

    explicit operator bool() const { return bool(p_); }
    impl::Plot Raw() const { return p_; }

    template <std::ranges::contiguous_range RX,
              std::ranges::contiguous_range RY,
              std::ranges::contiguous_range RZ>
    Plot3D &Line(const char *name, const RX &x, const RY &y, const RZ &z,
                 const SeriesStyle &s = {}) {
        return triple(impl::SeriesKind::Line3, name, x, y, z, s);
    }

    template <std::invocable F>
    Plot3D &Line(const char *name, F pull, const SeriesStyle &s = {}) {
        return pulled3(impl::SeriesKind::Line3, name, std::move(pull), s);
    }

    template <std::ranges::contiguous_range RX,
              std::ranges::contiguous_range RY,
              std::ranges::contiguous_range RZ>
    Plot3D &Scatter(const char *name, const RX &x, const RY &y, const RZ &z,
                    const SeriesStyle &s = {}) {
        return triple(impl::SeriesKind::Scatter3, name, x, y, z, s);
    }

    template <std::invocable F>
    Plot3D &Scatter(const char *name, F pull, const SeriesStyle &s = {}) {
        return pulled3(impl::SeriesKind::Scatter3, name, std::move(pull), s);
    }

    template <std::ranges::contiguous_range RX,
              std::ranges::contiguous_range RY,
              std::ranges::contiguous_range RZ>
    Plot3D &Surface(const char *name, const RX &x, const RY &y, const RZ &z,
                    std::size_t x_count, std::size_t y_count,
                    const GridDesc &g = {}, const SeriesStyle &s = {}) {
        impl::plot_series(
            p_, impl::SeriesDesc{
                    .name = name,
                    .kind = impl::SeriesKind::Surface,
                    .dtype = series_dtype<std::ranges::range_value_t<RZ>>(),
                    .param = {g.scale_min, g.scale_max, 0.0, 0.0},
                    .data = {.a = std::ranges::data(x),
                             .b = std::ranges::data(y),
                             .c = std::ranges::data(z),
                             .count = x_count,
                             .count2 = y_count},
                    .style = s});
        return *this;
    }

    template <
        std::ranges::contiguous_range RX, std::ranges::contiguous_range RY,
        std::ranges::contiguous_range RZ, std::ranges::contiguous_range RI>
    Plot3D &Mesh(const char *name, const RX &x, const RY &y, const RZ &z,
                 const RI &indices, const SeriesStyle &s = {}) {
        static_assert(
            std::is_same_v<std::ranges::range_value_t<RI>, unsigned>,
            "mesh indices are unsigned ints, one per triangle corner");
        const std::size_t n = std::min(
            {std::ranges::size(x), std::ranges::size(y), std::ranges::size(z)});
        impl::plot_series(
            p_, impl::SeriesDesc{
                    .name = name,
                    .kind = impl::SeriesKind::Mesh,
                    .dtype = series_dtype<std::ranges::range_value_t<RZ>>(),
                    .data = {.a = std::ranges::data(x),
                             .b = std::ranges::data(y),
                             .c = std::ranges::data(z),
                             .idx = std::ranges::data(indices),
                             .count = n,
                             .count2 = std::ranges::size(indices)},
                    .style = s});
        return *this;
    }

  private:
    template <class RX, class RY, class RZ>
    Plot3D &triple(impl::SeriesKind k, const char *name, const RX &x,
                   const RY &y, const RZ &z, const SeriesStyle &s) {
        const std::size_t n = std::min(
            {std::ranges::size(x), std::ranges::size(y), std::ranges::size(z)});
        impl::plot_series(
            p_, impl::SeriesDesc{
                    .name = name,
                    .kind = k,
                    .dtype = series_dtype<std::ranges::range_value_t<RZ>>(),
                    .data = {.a = std::ranges::data(x),
                             .b = std::ranges::data(y),
                             .c = std::ranges::data(z),
                             .count = n},
                    .style = s});
        return *this;
    }

    template <class F>
    Plot3D &pulled3(impl::SeriesKind k, const char *name, F pull,
                    const SeriesStyle &s) {
        using Ret = std::remove_cvref_t<std::invoke_result_t<F>>;
        impl::plot_series(
            p_, impl::SeriesDesc{
                    .name = name,
                    .kind = k,
                    .dtype = series_dtype<typename Ret::value_type>(),
                    .src =
                        [](void *u) {
                            decltype(auto) r = (*static_cast<F *>(u))();
                            return impl::SeriesData{
                                .a = r.x.data(),
                                .b = r.y.data(),
                                .c = r.z.data(),
                                .count = std::min(
                                    {r.x.size(), r.y.size(), r.z.size()})};
                        },
                    .user = new F(std::move(pull)),
                    .free = [](void *u) { delete static_cast<F *>(u); },
                    .style = s});
        return *this;
    }

    impl::Plot p_;
};

}
