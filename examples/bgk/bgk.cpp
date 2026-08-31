// The flagship: a tensor sim stepped on the GPU and drawn in 3-D
// without the particles ever reaching the host.
//
// Test-particle Boltzmann relaxation. Two discs of particles collide
// off-axis in a periodic box and thermalize. Per step — stream,
// measure n/p/E per cell, histogram the momenta, relax toward the
// Maxwellian by the relaxation-time approximation, resample, then
// shift and scale so the cell's momentum and energy come out
// unchanged. Thirty-odd fused expressions, every one of them
// evaluated on the device.
//
// The physics below is tensor's own examples/bgk, reproduced here so
// this example reads as one file and builds from one. It is a COPY,
// with the copy's one cost: tensor's version can change without this
// one noticing. What it buys is that a reader sees the whole thing —
// the expressions, the transport and the drawing — in the order they
// happen, which is what a flagship is for.
//
// What it is actually showing is the seam. simview owns the device,
// tensor evaluates on it, and a frame copies nothing: the positions
// the vertex shader reads are the buffers tensor wrote. The sim runs
// on the Executor's thread and the frame on the main one; they share
// the state through sv::Sync, which is what makes "no copy" also mean
// "no lock and no torn read".
//
// Space toggles, Up/Down move the relaxation time, R restarts,
// Esc quits.
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
#include <utility>

using namespace tensor;
using namespace tensor::math;
using tensor::indices::clamp;
using tensor::indices::i, tensor::indices::j, tensor::indices::m,
    tensor::indices::n;
using tensor::indices::operator""_c;

using f32 = float;
using idx = size_t;

constexpr idx N = 8192;       // particles
constexpr idx C = 4;          // cells per axis
constexpr idx CC = C * C * C; // cells in the grid
constexpr idx B = 24;         // momentum bins per cell, per component
constexpr f32 vmax = 2.5f;
constexpr f32 dv = 2.0f * vmax / f32(B);
constexpr f32 dt = 0.004f;
constexpr f32 tpi = 6.283185307179586f;

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

// Each particle's cell as ONE number: bins per axis, clamped per
// AXIS, then combined row-major. The clamp must precede the combine —
// an axis reaching C aliases into the NEXT axis's slot, a valid id no
// write policy can catch.
//
// The combined id is a DATA-LAYOUT choice, measured twice in tensor:
// the rank-N spelling is proven and byte-identical and its fold side
// is faster, but the per-particle deposit and gather loops resolve
// three stored values where this id resolves one, and that priced the
// step at 1.5x.
Cells cells(const Vecs &pos) {
    auto a = Fmin(Fmax(bins<C>(pos, -0.5f, 0.5f), 0.0f), f32(C - 1));
    return go((a[i, 0_c] * f32(C) + a[i, 1_c]) * f32(C) + a[i, 2_c]);
}

Cell measure(const auto &at, const Vecs &mom) {
    auto sq = go(fold<1>(mom * mom));
    auto count = go(scatter<i>(at, 1.0f));
    auto inv = go(1.0f / Fmax(count, 1.0f));
    auto p = go(scatter<i>(at, mom[i, n]));
    auto E = go(scatter<i>(at, sq[i]));
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

void step(Vecs &Pos, Vecs &Mom, const Tensor<f32, B> &centre, f32 alpha) {
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
    auto Q = go(scatter<i>(at, q[i, n]));
    auto Q2 = go(scatter<i>(at, qsq[i]));
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

// Two discs of particles flying at each other, off-axis. Draws, so
// the caller seeds first if it wants a particular run.
State initial_state() {
    constexpr size_t half = N / 2;
    auto beam = eval(1.0f * (gen::Iota<N>(0.0f) < f32(half)));
    auto sign = eval(2.0f * beam - 1.0f);
    auto rad = eval(0.15f * Sqrt(rng::Uniform<f32, N>())); // uniform by area
    auto ang = eval(tpi * rng::Uniform<f32, N>());

    auto x = eval(-0.25f * sign + 0.002f * rng::Normal<f32, N>());
    auto y = eval(0.05f * (1.0f - beam) + rad * Cos(ang));
    auto z = eval(rad * Sin(ang));
    auto vx = eval(0.80f * sign + 0.15f * rng::Normal<f32, N>());
    auto vy = eval(0.15f * rng::Normal<f32, N>());
    auto vz = eval(0.15f * rng::Normal<f32, N>());

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

// The view's copy of a state tensor, made ON THE DEVICE: one identity
// kernel over 24K floats, not a round trip through the host.
//
// It exists because step() owns its state by reference and hands back
// the same two tensors every step, while a Sync slot wants a value it
// can keep while the sim moves on.
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
    auto start = initial_state();
    Vecs Pos = std::move(start.Pos), Mom = std::move(start.Mom);

    // Positions place the particles; momenta colour them, so the gas
    // reads as fast and slow rather than as a shape. Both resident.
    sv::Sync<Vecs> pos, mom;
    publish(pos, Pos);
    publish(mom, Mom);

    // One const element read, and it is not a debug leftover: it SYNCS
    // the device. Without it the initial state's kernel sits in an
    // unsubmitted batch — the compute backend batches eagerly and a
    // couple of dispatches do not fill one — so the window opens on an
    // empty box and stays that way for the seconds the first step
    // spends compiling thirty kernels. A const read keeps the parking,
    // so this costs one download and nothing after it.
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
    auto gas = world.Cloud(pos, {.radius = 0.006f,
                                 .shape = sv::CloudShape::Sphere,
                                 .map = sv::CloudMap::Magnitude,
                                 .map_scale = 1.1f});
    if (!gas)
        return 1;
    gas.Colors(mom);

    // The one live knob, and a physical one: the relaxation time is
    // how long a cell takes to forget its distribution. Large and the
    // discs pass through each other; small and they thermalize on
    // contact.
    float relax = 0.01f;
    std::atomic<float> tau_now{relax};

    sv::Executor sim([&](const sv::Tick &) {
        const f32 t = tau_now.load(std::memory_order_relaxed);
        step(Pos, Mom, centre, std::exp(-dt / t));
        publish(pos, Pos);
        publish(mom, Mom);
    });
    sim.OnRestart([&] {
        auto s = initial_state();
        Pos = std::move(s.Pos);
        Mom = std::move(s.Mom);
        publish(pos, Pos);
        publish(mom, Mom);
    });
    sim.Play();

    app.Controls(sim).Slider("relaxation time", relax, 0.002f, 0.5f);
    app.OnFrame([&] { tau_now.store(relax, std::memory_order_relaxed); });

    app.OnKey(sv::Key::Space, [&] { sim.Toggle(); })
        .OnKey(sv::Key::Up, [&] { relax = std::min(0.5f, relax * 1.3f); })
        .OnKey(sv::Key::Down, [&] { relax = std::max(0.002f, relax / 1.3f); })
        .OnKey(sv::Key::R, [&] { sim.Restart(); })
        .OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
    // Teardown is the lifetime rule: the Executor (which writes the
    // Syncs) dies before them, and they die before app — the device's
    // owner — in reverse declaration order.
}
