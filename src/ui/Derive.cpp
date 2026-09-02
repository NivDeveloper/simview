#include "PlotState.h"

#include "../core/Math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace sv {
namespace {

using impl::bin_of;
using impl::contours;
using impl::Span;
using impl::span_of;

// The source's current points, as doubles. x is the index when the
// series carries none, which is what a y-only series means by it.
void gather(const impl::SeriesState &s, std::vector<double> &xs,
            std::vector<double> &ys) {
    const impl::SeriesData d = s.src ? s.src(s.user) : s.data;
    const std::size_t n = d.b ? d.count : 0;
    xs.resize(n);
    ys.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (s.dtype == DType::f64) {
            ys[i] = static_cast<const double *>(d.b)[i];
            xs[i] = d.a ? static_cast<const double *>(d.a)[i] : double(i);
        } else {
            ys[i] = double(static_cast<const float *>(d.b)[i]);
            xs[i] =
                d.a ? double(static_cast<const float *>(d.a)[i]) : double(i);
        }
    }
}

// A 2-D count, row 0 at the TOP so the picture stands the way the
// scatter does.
void density(impl::Derivation &d, const std::vector<double> &xs,
             const std::vector<double> &ys, impl::SeriesState &out) {
    const int n = std::max(4, d.bins);
    const Span sx = span_of(xs), sy = span_of(ys);
    d.a.assign(std::size_t(n) * std::size_t(n), 0.0);
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const int cx = bin_of(xs[i], sx, n);
        const int cy = bin_of(ys[i], sy, n);
        d.a[std::size_t((n - 1 - cy) * n + cx)] += 1.0;
    }
    const double top =
        d.a.empty() ? 1.0 : *std::max_element(d.a.begin(), d.a.end());
    out.dtype = DType::f64;
    out.data = {
        .b = d.a.data(), .count = std::size_t(n), .count2 = std::size_t(n)};
    out.bounds = {sx.lo, sy.lo, sx.hi, sy.hi};
    out.param[0] = 0.0;
    out.param[1] = top > 0.0 ? top : 1.0;
}

// The mean of y in bins of x, with the standard error of that mean —
// which is the number a scatter is usually being squinted at for.
// Empty bins are dropped rather than drawn as zero.
void profile(impl::Derivation &d, const std::vector<double> &xs,
             const std::vector<double> &ys, impl::SeriesState &line,
             impl::SeriesState &bars) {
    const int n = std::max(2, d.bins);
    const Span sx = span_of(xs);
    std::vector<double> sum(std::size_t(n), 0.0), sq(std::size_t(n), 0.0);
    std::vector<double> count(std::size_t(n), 0.0);
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const std::size_t c = std::size_t(bin_of(xs[i], sx, n));
        sum[c] += ys[i];
        sq[c] += ys[i] * ys[i];
        count[c] += 1.0;
    }

    d.a.clear();
    d.b.clear();
    d.c.clear();
    const double w = (sx.hi - sx.lo) / double(n);
    for (int c = 0; c < n; ++c) {
        const double k = count[std::size_t(c)];
        if (k < 1.0)
            continue;
        const double mean = sum[std::size_t(c)] / k;
        const double var = std::max(0.0, sq[std::size_t(c)] / k - mean * mean);
        d.a.push_back(sx.lo + (double(c) + 0.5) * w);
        d.b.push_back(mean);
        d.c.push_back(k > 1.0 ? std::sqrt(var / (k - 1.0)) : 0.0);
    }

    line.dtype = DType::f64;
    line.data = {.a = d.a.data(), .b = d.b.data(), .count = d.a.size()};
    bars.dtype = DType::f64;
    bars.data = {
        .a = d.a.data(), .b = d.b.data(), .c = d.c.data(), .count = d.a.size()};
}

// One reduction rather than four: they share the binning, and a
// reader asking for one is usually asking what all four answer.
void joint(impl::Derivation &d, const std::vector<double> &xs,
           const std::vector<double> &ys, impl::SeriesState &field,
           impl::SeriesState &line, impl::SeriesState &mx,
           impl::SeriesState &my) {
    const int n = std::max(4, d.bins);
    const Span sx = span_of(xs), sy = span_of(ys);
    const std::size_t cells = std::size_t(n) * std::size_t(n);

    // Counted with j increasing UPWARD, which is the orientation the
    // contours are computed in; the heatmap's own copy is flipped
    // below, because ImPlot fills a heatmap from the top row down.
    std::vector<double> up(cells, 0.0);
    d.mx.assign(std::size_t(n), 0.0);
    d.my.assign(std::size_t(n), 0.0);
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const int ci = bin_of(xs[i], sx, n);
        const int cj = bin_of(ys[i], sy, n);
        up[std::size_t(cj) * std::size_t(n) + std::size_t(ci)] += 1.0;
        d.mx[std::size_t(ci)] += 1.0;
        d.my[std::size_t(cj)] += 1.0;
    }

    d.a.assign(cells, 0.0);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            d.a[std::size_t(n - 1 - j) * std::size_t(n) + std::size_t(i)] =
                up[std::size_t(j) * std::size_t(n) + std::size_t(i)];

    const double top =
        up.empty() ? 1.0 : *std::max_element(up.begin(), up.end());
    field.dtype = DType::f64;
    field.data = {
        .b = d.a.data(), .count = std::size_t(n), .count2 = std::size_t(n)};
    field.bounds = {sx.lo, sy.lo, sx.hi, sy.hi};
    field.param[0] = 0.0;
    field.param[1] = top > 0.0 ? top : 1.0;

    contours(up, n, sx, sy, top, d.seg_x, d.seg_y);
    line.dtype = DType::f64;
    line.data = {
        .a = d.seg_x.data(), .b = d.seg_y.data(), .count = d.seg_x.size()};

    d.cx.assign(std::size_t(n), 0.0);
    d.cy.assign(std::size_t(n), 0.0);
    for (int k = 0; k < n; ++k) {
        d.cx[std::size_t(k)] = sx.lo + (double(k) + 0.5) * (sx.hi - sx.lo) / n;
        d.cy[std::size_t(k)] = sy.lo + (double(k) + 0.5) * (sy.hi - sy.lo) / n;
    }
    mx.dtype = DType::f64;
    mx.data = {.a = d.cx.data(), .b = d.mx.data(), .count = std::size_t(n)};
    mx.param[0] = (sx.hi - sx.lo) / double(n) * 0.9;
    // Values first for the sideways one: a horizontal bar takes its
    // LENGTH from xs and its position from ys, which is the pair the
    // vertical one hands over the other way round.
    my.dtype = DType::f64;
    my.data = {.a = d.my.data(), .b = d.cy.data(), .count = std::size_t(n)};
    my.param[0] = (sy.hi - sy.lo) / double(n) * 0.9;
}

} // namespace

void derive_update(impl::PlotState &p) {
    impl::Derivation &d = *p.derivation;
    if (!d.series || p.series.empty())
        return;

    std::vector<double> xs, ys;
    gather(*d.series, xs, ys);
    auto it = p.series.begin();

    if (d.kind == Derived::Histogram) {
        // The values themselves, binned by ImPlot: the reduction is the
        // bin COUNT, and handing it the raw sample keeps the axis honest
        // when the source's range moves.
        d.a = ys;
        it->dtype = DType::f64;
        it->data = {.b = d.a.data(), .count = d.a.size()};
        it->param[0] = double(std::max(2, d.bins));
        return;
    }
    if (d.kind == Derived::Joint) {
        impl::SeriesState &field = *it;
        impl::SeriesState &line = *(++it);
        impl::SeriesState &mx = *(++it);
        impl::SeriesState &my = *(++it);
        joint(d, xs, ys, field, line, mx, my);
        return;
    }
    if (d.kind == Derived::Density) {
        density(d, xs, ys, *it);
        return;
    }

    impl::SeriesState &line = *it;
    profile(d, xs, ys, line, *(++it));
}

// The CAPABILITY, which plot_derive asks — separate from what the
// menu offers.
bool reducible(impl::SeriesKind k) {
    return k == impl::SeriesKind::Line || k == impl::SeriesKind::Scatter ||
           k == impl::SeriesKind::Stairs || k == impl::SeriesKind::Stems ||
           k == impl::SeriesKind::Bars;
}

// What the menu OFFERS, and deliberately nothing: the set is curated
// from empty. Adding one back costs a line here and nothing else.
std::vector<DeriveOption> derive_options(const impl::PlotState &p) {
    (void)p;
    return {};
}

} // namespace sv
