#pragma once

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

struct PlotDesc {
    const char *title = "plot";
    AxisDesc x{};
    AxisDesc y{};
};

struct SeriesStyle {
    float color[4] = {0.0f, 0.0f, 0.0f, -1.0f};
    float fill[4] = {0.0f, 0.0f, 0.0f, -1.0f};
    float fill_alpha = 1.0f;
    float weight = 1.0f;
    float marker_size = 4.0f;
    float size = 4.0f;
    int marker = -2;
    bool horizontal = false;
};

template <class T> struct Points {
    std::span<const T> x, y;
    using value_type = T;
};

namespace impl {

struct App;

enum class SeriesKind : std::int32_t { Line, Scatter, Histogram };

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
    SeriesStyle style{};
};

Plot plot_create(App *, const PlotDesc &);
bool plot_series(Plot, const SeriesDesc &);

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

  private:
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

}
