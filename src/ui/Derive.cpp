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

// Marching squares. For each cell of the binned field and each level,
// the four corners above or below the level pick one of sixteen cases,
// and the crossings are found by linear interpolation along the edges
// they lie on — so a contour follows the DATA rather than the cell
// boundaries and does not staircase.
//
// The two saddle cases are resolved by the cell's own average, which
// is the cheapest rule that is at least self-consistent: whichever way
// it decides, the two crossings it joins are the two the average says
// are on the same side.
//
// Output is pairs of points, which is what a line drawn as SEGMENTS
// takes — one item for every level rather than one per segment.
void contours(const std::vector<double> &f, int n, Span sx, Span sy, double top,
              std::vector<double> &xs, std::vector<double> &ys) {
    xs.clear();
    ys.clear();
    if (n < 2 || top <= 0.0)
        return;

    const double dx = (sx.hi - sx.lo) / double(n);
    const double dy = (sy.hi - sy.lo) / double(n);
    const auto px = [&](double i) { return sx.lo + (i + 0.5) * dx; };
    const auto py = [&](double j) { return sy.lo + (j + 0.5) * dy; };
    const auto at = [&](int i, int j) {
        return f[std::size_t(j) * std::size_t(n) + std::size_t(i)];
    };

    // Four levels, evenly through the field's range. Enough to read a
    // shape; more at 32 bins is noise dressed as detail.
    for (int k = 1; k <= 4; ++k) {
        const double level = top * double(k) / 5.0;
        for (int j = 0; j + 1 < n; ++j)
            for (int i = 0; i + 1 < n; ++i) {
                const double v[4] = {at(i, j), at(i + 1, j), at(i + 1, j + 1),
                                     at(i, j + 1)};
                int code = 0;
                for (int c = 0; c < 4; ++c)
                    if (v[c] > level)
                        code |= 1 << c;
                if (code == 0 || code == 15)
                    continue;

                // The crossing on each edge, as a point.
                const auto lerp = [&](double a, double b) {
                    const double t = (level - a) / (b - a);
                    return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                };
                const double bottom_t = lerp(v[0], v[1]);
                const double right_t = lerp(v[1], v[2]);
                const double top_t = lerp(v[3], v[2]);
                const double left_t = lerp(v[0], v[3]);
                const double ex[4] = {px(double(i) + bottom_t),
                                      px(double(i + 1)), px(double(i) + top_t),
                                      px(double(i))};
                const double ey[4] = {py(double(j)), py(double(j) + right_t),
                                      py(double(j + 1)),
                                      py(double(j) + left_t)};

                const auto emit = [&](int e0, int e1) {
                    xs.push_back(ex[e0]);
                    ys.push_back(ey[e0]);
                    xs.push_back(ex[e1]);
                    ys.push_back(ey[e1]);
                };
                switch (code) {
                case 1:
                case 14:
                    emit(3, 0);
                    break;
                case 2:
                case 13:
                    emit(0, 1);
                    break;
                case 3:
                case 12:
                    emit(3, 1);
                    break;
                case 4:
                case 11:
                    emit(1, 2);
                    break;
                case 6:
                case 9:
                    emit(0, 2);
                    break;
                case 7:
                case 8:
                    emit(3, 2);
                    break;
                case 5:
                case 10: {
                    const double mid = (v[0] + v[1] + v[2] + v[3]) * 0.25;
                    const bool joined = (mid > level) == (code == 5);
                    if (joined) {
                        emit(3, 0);
                        emit(1, 2);
                    } else {
                        emit(0, 1);
                        emit(3, 2);
                    }
                    break;
                }
                default:
                    break;
                }
            }
    }
}

// The joint view: the field, the distribution along each axis, and the
// contours over the field. One reduction rather than four, because
// they share the binning and a reader asking for one of them is
// usually asking the question all four answer together.
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
        out.push_back({&s, Derived::Joint, "joint view of " + s.name});
    }
    return out;
}

} // namespace sv
