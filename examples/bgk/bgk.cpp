#include <simview/gpud.h>
#include <simview/simview.h>

#include <Tensor/Gen.h>
#include <Tensor/Gpu.h>
#include <Tensor/Math.h>
#include <Tensor/Stats.h>
#include <Tensor/Tensor.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <utility>

using namespace tensor;
using namespace tensor::math;
using tensor::indices::clamp;
using tensor::indices::i, tensor::indices::j, tensor::indices::m,
    tensor::indices::n;
using tensor::indices::operator""_c;

using f32 = float;
using idx = size_t;

constexpr idx N = 100000;     // particles
constexpr idx C = 64;         // cells per axis
constexpr idx CC = C * C * C; // cells in the grid
constexpr idx B = 24;         // momentum bins per cell, per component
constexpr f32 vmax = 2.5f;
constexpr f32 dv = 2.0f * vmax / f32(B);
constexpr f32 tpi = 6.283185307179586f;

constexpr idx round_up_pow2(idx v) {
    idx p = 1;
    while (p < v)
        p <<= 1;
    return p;
}
constexpr idx kDeposit = round_up_pow2(N * (idx(3.0f * vmax * vmax) + 1));
using Sum = ops::Fixed<kDeposit>;

using Vecs = Tensor<f32, N, 3>;
using Cells = Tensor<f32, N>; // each particle's cell, as one number
using Grid = Tensor<f32, CC>;
using GridV = Tensor<f32, CC, 3>;

namespace {

// simview owns the device; this is the whole of the handshake, and
// every expression below goes through it.
gpud::Device *device = nullptr;

auto go(const auto &e) { return eval(*device, e); }

// The conserved quantities per cell, and the equilibrium they imply.
struct Cell {
    Grid pop, inv, E, T, mu;
    GridV p;
};

Cells cells(const Vecs &pos) {
    auto a = Fmin(Fmax(bins<C>(pos, -0.5f, 0.5f), 0.0f), f32(C - 1));
    return go((a[i, 0_c] * f32(C) + a[i, 1_c]) * f32(C) + a[i, 2_c]);
}

Cell measure(const auto &at, const Vecs &mom) {
    auto sq = go(fold<1>(mom * mom));
    auto count = go(scatter<Sum, i>(at, 1.0f));
    auto inv = go(1.0f / Fmax(count, 1.0f));
    auto p = go(scatter<Sum, i>(at, mom[i, n]));
    auto E = go(scatter<Sum, i>(at, sq[i]));
    auto p2 = go(fold<1>(p * p));

    auto T = go(Fmax((E - p2 * inv) * inv * (1.0f / 3.0f), 1e-9f));
    auto mu = go(T * Log(Fmax(count, 1.0f) * f32(CC) / Pow(tpi * T, 1.5f)));

    return {std::move(count), std::move(inv), std::move(E),
            std::move(T),     std::move(mu),  std::move(p)};
}

Vecs resample(const auto &at, const Cell &c, const Vecs &mom,
              const Tensor<f32, B> &centre, f32 alpha) {
    // Momenta per cell
    auto hist = go(scatter<i>(at, clamp(bins<B>(mom[i, n], -vmax, vmax)), 1u));

    // The Maxwellian for these cell parameters, centred on the drift.
    // The cell index leads: free indices take first-appearance order.
    auto off = go(c.p[j, n] * -c.inv[j] + centre[m]);
    auto heat = go(Exp(off[j, n, m] * off[j, n, m] * -0.5f / c.T[j]));
    auto nrm = go(fold<2>(heat));

    // f(t+dt) = f_eq + (f - f_eq)·exp(-dt/tau)
    auto relaxed = go((1.0f - alpha) * c.pop[j] * heat[j, n, m] / nrm[j, n] +
                      hist[j, m, n] * alpha);

    // The CDF is the running sum of the relaxed histogram along its bins.
    auto cdf = go(scan<ops::Add, m>(relaxed[j, n, m]) * c.inv[j]);

    auto u1 = go(rng::Uniform<f32, N, 3>());
    auto hit = go(fold<m>(1.0f * (u1[i, n] > cdf[at, n, m])));
    return go(-vmax + (hit + rng::Uniform<f32, N, 3>()) * dv);
}

void step(Vecs &Pos, Vecs &Mom, const Tensor<f32, B> &centre, f32 dt,
          f32 alpha) {
    auto moved = go(Pos + Mom * dt);
    // periodic boundary conditions
    Pos = go(moved - Floor(moved + 0.5f));

    auto cid = cells(Pos);
    // The cell each particle deposits into and reads back from: built
    // once, and the same object serves both, since a destination is
    // also a subscript.
    auto at = clamp<CC>(cid[i]);
    auto c = measure(at, Mom);
    auto q = resample(at, c, Mom, centre, alpha);

    // Shift and scale q -> b·q + a, fixed by the cell's own totals:
    // a = (p - b·Q)/n, b = sqrt((E - |p|²/n) / (Q2 - |Q|²/n)).
    auto qsq = go(fold<1>(q * q));
    auto Q = go(scatter<Sum, i>(at, q[i, n]));
    auto Q2 = go(scatter<Sum, i>(at, qsq[i]));
    auto p2 = go(fold<1>(c.p * c.p));
    auto q2 = go(fold<1>(Q * Q));
    auto b =
        go(Sqrt(Fmax(c.E - p2 * c.inv, 0.0f) / Fmax(Q2 - q2 * c.inv, 1e-9f)));
    auto shift = go((c.p[j, n] - b[j] * Q[j, n]) * c.inv[j]);

    // Under two particles there is nothing to resample from.
    auto live = go(1.0f * (c.pop[at] >= 2.0f));
    auto next = go(b[at] * q[i, n] + shift[at, n]);
    Mom = go(Mom[i, n] + live[i] * (next[i, n] - Mom[i, n]));
}

// The bin centres the histogram and the Maxwellian share.
Tensor<f32, B> bin_centres() { return eval(stats::Centres<B>(-vmax, vmax)); }

struct State {
    Vecs Pos, Mom;
};

// What a restart is built from. `offset` is how far off-axis the
// second disc sits — head-on at zero, a glancing blow as it grows.
struct Setup {
    f32 speed = 0.80f;  // the beam's drift along x
    f32 radius = 0.15f; // each disc's radius
    f32 offset = 0.05f; // the second disc's displacement in y
    f32 spread = 0.15f; // the thermal spread each particle starts with
};

// Two discs of particles flying at each other, off-axis. Draws, so
// the caller seeds first if it wants a particular run.
State initial_state(const Setup &su) {
    constexpr size_t half = N / 2;
    auto beam = eval(1.0f * (gen::Iota<N>(0.0f) < f32(half)));
    auto sign = eval(2.0f * beam - 1.0f);
    auto rad = eval(su.radius * Sqrt(rng::Uniform<f32, N>())); // by area
    auto ang = eval(tpi * rng::Uniform<f32, N>());

    auto x = eval(-0.25f * sign + 0.002f * rng::Normal<f32, N>());
    auto y = eval(su.offset * (1.0f - beam) + rad * Cos(ang));
    auto z = eval(rad * Sin(ang));
    auto vx = eval(su.speed * sign + su.spread * rng::Normal<f32, N>());
    auto vy = eval(su.spread * rng::Normal<f32, N>());
    auto vz = eval(su.spread * rng::Normal<f32, N>());

    return {.Pos = Vecs([&](idx q, idx d) {
                switch (d) {
                case 0:
                    return x[q];
                case 1:
                    return y[q];
                default:
                    return z[q];
                }
            }),
            .Mom = Vecs([&](idx q, idx d) {
                switch (d) {
                case 0:
                    return vx[q];
                case 1:
                    return vy[q];
                default:
                    return vz[q];
                }
            })};
}

void publish(sv::Sync<Vecs> &s, const Vecs &v) {
    s.Next() = go(v[i, n]);
    s.Publish();
}

} // namespace

int main() {
    sv::App app(
        {.title = "simview — bgk (tensor on gpud)", .size = {1200, 820}});
    if (!app)
        return 1;

    device = &sv::Device(app);

    rng::Seed(20260814);
    auto centre = bin_centres();

    // The two the sim reads EVERY step, and the four a restart is
    // built from. All atomics: the worker reads them on its own
    // thread, the panel writes them on the frame's.
    std::atomic<float> tau_now{0.01f}, dt_now{0.004f};
    std::atomic<float> speed_now{0.80f}, radius_now{0.15f};
    std::atomic<float> offset_now{0.05f}, spread_now{0.15f};
    const auto setup_now = [&] {
        return Setup{.speed = speed_now.load(std::memory_order_relaxed),
                     .radius = radius_now.load(std::memory_order_relaxed),
                     .offset = offset_now.load(std::memory_order_relaxed),
                     .spread = spread_now.load(std::memory_order_relaxed)};
    };

    auto start = initial_state(setup_now());
    Vecs Pos = std::move(start.Pos), Mom = std::move(start.Mom);

    // Positions place the particles; momenta colour them, so the gas
    // reads as fast and slow rather than as a shape. Both resident.
    sv::Sync<Vecs> pos, mom;
    publish(pos, Pos);
    publish(mom, Mom);

    (void)pos.Current()[0, 0];
    (void)mom.Current()[0, 0];

    auto world = app.World();
    world
        .Camera({.focus = {0.0f, 0.0f, 0.0f},
                 .distance = 2.4f,
                 .azimuth_deg = -55.0f,
                 .elevation_deg = 22.0f})
        .Light({.direction = {0.35f, 0.5f, 0.9f}, .intensity = 0.85f})
        .Ambient(0.20f, 0.21f, 0.25f);

    // The domain is the unit box, so the grid's decade of cells IS the
    // simulation's own scale.
    auto gas = world.Cloud(pos, {.radius = 0.001f,
                                 .shape = sv::CloudShape::Sphere,
                                 .map = sv::CloudMap::Magnitude,
                                 .map_scale = 1.1f});
    if (!gas)
        return 1;
    gas.Colors(mom);

    std::atomic<float> sig_x{0.0f}, sig_y{0.0f}, sig_z{0.0f};
    std::uint64_t since = 0;

    sv::Executor sim([&](const sv::Tick &) {
        const f32 tau = tau_now.load(std::memory_order_relaxed);
        const f32 h = dt_now.load(std::memory_order_relaxed);
        step(Pos, Mom, centre, h, std::exp(-h / tau));
        publish(pos, Pos);
        publish(mom, Mom);

        if (++since >= 20) {
            since = 0;
            auto sp = go(fold<ops::Welford, 0>(Mom));
            sig_x.store(std::sqrt(sp[0].var), std::memory_order_relaxed);
            sig_y.store(std::sqrt(sp[1].var), std::memory_order_relaxed);
            sig_z.store(std::sqrt(sp[2].var), std::memory_order_relaxed);
        }
    });

    sim.OnRestart([&] {
        auto s = initial_state(setup_now());
        Pos = std::move(s.Pos);
        Mom = std::move(s.Mom);
        publish(pos, Pos);
        publish(mom, Mom);
    });

    sim.SetDt(double(dt_now.load()));
    sim.SetDelayNs(5'000'000);
    sim.Play();

    app.Controls(sim);

    static char fixed_line[96];
    std::snprintf(fixed_line, sizeof fixed_line,
                  "fixed: %zu particles, %zu^3 cells (%zu per cell), "
                  "%zu bins/axis",
                  size_t(N), size_t(C), size_t(N / CC), size_t(B));

    float relax = 0.01f, h = 0.004f;
    float speed = 0.80f, radius = 0.15f, offset = 0.05f, spread = 0.15f;
    app.Panel("bgk")
        .Text("live")
        .Slider("relaxation time", relax, 0.002f, 0.5f)
        .Slider("time step", h, 0.0005f, 0.02f)
        .Separator()
        .Text("initial condition - press R")
        .Slider("beam speed", speed, 0.1f, 2.0f)
        .Slider("disc radius", radius, 0.02f, 0.35f)
        .Slider("impact offset", offset, 0.0f, 0.30f)
        .Slider("thermal spread", spread, 0.0f, 0.6f)
        .Separator()
        .Text("momentum spread per axis")
        .Value(
            "along the beam", [&] { return sig_x.load(); }, "%.3f")
        .Value(
            "across it (y)", [&] { return sig_y.load(); }, "%.3f")
        .Value(
            "across it (z)", [&] { return sig_z.load(); }, "%.3f")
        .Text("equal on all three = thermalized")
        .Separator()
        .Text(fixed_line);

    app.OnFrame([&] {
        tau_now.store(relax, std::memory_order_relaxed);
        dt_now.store(h, std::memory_order_relaxed);
        speed_now.store(speed, std::memory_order_relaxed);
        radius_now.store(radius, std::memory_order_relaxed);
        offset_now.store(offset, std::memory_order_relaxed);
        spread_now.store(spread, std::memory_order_relaxed);
        sim.SetDt(double(h));
    });

    app.OnKey(sv::Key::Space, [&] { sim.Toggle(); })
        .OnKey(sv::Key::Up, [&] { relax = std::min(0.5f, relax * 1.3f); })
        .OnKey(sv::Key::Down, [&] { relax = std::max(0.002f, relax / 1.3f); })
        .OnKey(sv::Key::R, [&] { sim.Restart(); })
        .OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
}
