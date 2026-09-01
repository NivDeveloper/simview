// Derived views: a plot computed from another plot's series, recomputed
// every frame from whatever that series currently holds.
//
// The point is that the caller writes nothing. A scatter of particles
// already hands the engine its positions every frame, so the engine can
// answer "where are they concentrated" or "how are they distributed"
// without being told how — which is the difference between a question
// you can ask in one click and one you have to go and code.
//
// Every reduction here is O(n) over the source and allocates only on a
// change of size, because it runs inside the frame that draws it.

#include "PlotState.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace sv {
namespace {

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

struct Span {
    double lo = 0.0, hi = 1.0;
};

// A range that always has width: a degenerate one makes every bin index
// the same bin, and the picture is a single spike that says nothing.
Span span_of(const std::vector<double> &v) {
    if (v.empty())
        return {};
    auto [a, b] = std::minmax_element(v.begin(), v.end());
    Span s{*a, *b};
    if (!(s.hi > s.lo))
        s.hi = s.lo + 1.0;
    return s;
}

int bin_of(double v, Span s, int bins) {
    const double t = (v - s.lo) / (s.hi - s.lo);
    return std::clamp(int(t * double(bins)), 0, bins - 1);
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
    if (d.kind == Derived::Density) {
        density(d, xs, ys, *it);
        return;
    }

    impl::SeriesState &line = *it;
    profile(d, xs, ys, line, *(++it));
}

std::vector<DeriveOption> derive_options(const impl::PlotState &p) {
    std::vector<DeriveOption> out;
    if (!p.derive || p.derivation || p.family != impl::Family::Plot2D)
        return out;

    for (const impl::SeriesState &s : p.series) {
        const bool points = s.kind == impl::SeriesKind::Line ||
                            s.kind == impl::SeriesKind::Scatter ||
                            s.kind == impl::SeriesKind::Stairs ||
                            s.kind == impl::SeriesKind::Stems ||
                            s.kind == impl::SeriesKind::Bars;
        if (!points)
            continue;
        out.push_back({&s, Derived::Histogram, "histogram of " + s.name});
        out.push_back({&s, Derived::Density, "density of " + s.name});
        out.push_back({&s, Derived::Profile, "profile of " + s.name});
    }
    return out;
}

} // namespace sv
