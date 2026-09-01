// Water in a tank: a dam-break collapse, the surge running up the far
// wall, and the sloshing after it — solved as incompressible
// free-surface flow and drawn as the particles that carry it.
//
// The method is FLIP/PIC (Brackbill & Ruppel; Zhu & Bridson), the
// standard hybrid for liquids. Particles hold the velocity and do the
// advection, because advecting on a grid smears a free surface away. A
// grid holds the PRESSURE, because incompressibility is a global
// constraint — every parcel must know at once about every other — and
// that is a linear solve, not something a particle can do with its
// neighbours.
//
// One step:
//   1. transfer particle velocity to the grid   (a scatter, ops::Fixed)
//   2. add gravity and close the tank walls
//   3. solve for the pressure that removes the divergence  (red-black SOR)
//   4. subtract its gradient
//   5. read the grid's CHANGE back onto the particles, and move them
//
// The velocity grid is STAGGERED: component c is sampled on the cell
// faces normal to axis c. That is not a detail — a collocated grid
// admits a checkerboard pressure that the discrete gradient cannot see,
// so the solve converges to a field that oscillates cell to cell and
// the water shivers.
//
// Space toggles, R restarts, Esc quits.
#include <simview/gpud.h>
#include <simview/simview.h>

#include <Tensor/Gen.h>
#include <Tensor/Gpu.h>
#include <Tensor/Math.h>
#include <Tensor/Tensor.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <utility>

using namespace tensor;
using namespace tensor::math;
using tensor::indices::clamp, tensor::indices::zero;
using tensor::indices::i, tensor::indices::j, tensor::indices::k,
    tensor::indices::l, tensor::indices::m, tensor::indices::n;
using tensor::indices::operator""_c;

using f32 = float;
using idx = size_t;

constexpr idx G = 48;     // cells per axis
constexpr idx GS = G + 1; // samples per axis: G cells, G+1 faces
constexpr f32 L = 2.0f;   // tank side, metres
constexpr f32 h = L / f32(G);
constexpr f32 margin = 0.5f * h; // keeps every stencil inside the lattice

// The dam: a column of water CX by CY by CZ cells, eight particles to a
// cell — which is also the rest density the drift correction restores.
constexpr idx CX = 24, CY = G, CZ = 15, PPC = 8;
constexpr idx N = CX * CY * CZ * PPC;
constexpr f32 rest = f32(PPC);

using Vecs = Tensor<f32, N, 3>;
using Grid = Tensor<f32, GS, GS, GS>;
using Faces = Tensor<f32, GS, GS, GS, 3>;
using Weights = Tensor<f32, N, 3, 8>;

// Deposits are exact: each lands in a 64-bit fixed-point carrier, so the
// grid does not depend on the order particles arrive in — and the device
// may use an integer atomic, where a float scatter has none and
// privatizes instead. A cell holds a few hundred particles at a few m/s.
using Sum = ops::Fixed<4096>;

namespace {

gpud::Device *device = nullptr;
auto go(const auto &e) { return eval(*device, e); }

// Everything about the tank that never changes, and the small constant
// tables the per-step expressions read.
//
// These are NOT file-scope objects, deliberately. Every one of them is a
// leaf of a device expression, so each parks a buffer on the Device; a
// namespace-scope tensor is destroyed after main returns, by which time
// the Device is gone, and the process dies in the destructor rather than
// in anything a stack trace points at. Built after the Device, destroyed
// before it.
struct Tank {
    Tensor<f32, 8, 3> corner; // bit d of corner m, for the trilinear weight
    Tensor<f32, 3, 3> axis;   // the Kronecker delta
    Grid inside;              // 1 on the G^3 real cells
    Faces open;               // 1 on faces between two real cells
    Grid neighbours;          // non-solid neighbours per cell
    Grid red, black;          // the checkerboard the relaxation alternates
};

// `inside` is the whole tank: the walls are where it stops being 1. The
// padding slice at index G lets the cell arrays and the face arrays
// share one shape, and reads as solid for free.
Tank tank_of() {
    Tensor<f32, 8, 3> corner([](idx c, idx d) { return f32((c >> d) & 1u); });
    Tensor<f32, 3, 3> axis([](idx a, idx b) { return f32(a == b); });
    Grid inside(
        [](idx a, idx b, idx c) { return f32(a < G && b < G && c < G); });
    Grid red([](idx a, idx b, idx c) { return f32((a + b + c) % 2 == 0); });
    Grid black([](idx a, idx b, idx c) { return f32((a + b + c) % 2 == 1); });

    Faces open =
        go(inside[i, j, k] * (inside[zero(i - 1_c), j, k] * axis[0_c, n] +
                              inside[i, zero(j - 1_c), k] * axis[1_c, n] +
                              inside[i, j, zero(k - 1_c)] * axis[2_c, n]));
    Grid neighbours =
        go(inside[zero(i + 1_c), j, k] + inside[zero(i - 1_c), j, k] +
           inside[i, zero(j + 1_c), k] + inside[i, zero(j - 1_c), k] +
           inside[i, j, zero(k + 1_c)] + inside[i, j, zero(k - 1_c)]);

    return {std::move(corner), std::move(axis),       std::move(inside),
            std::move(open),   std::move(neighbours), std::move(red),
            std::move(black)};
}

// Where each particle sits in the staggered lattice of every component.
// Component c samples axis c at a face and the other two at a centre, so
// both coordinates are computed once and `axis` picks between them.
// Laid out [particle][axis][component].
struct Stencil {
    Tensor<f32, N, 3, 3> base, frac;
};

Stencil stencil_of(const Tank &t, const Vecs &X) {
    auto face = go(Floor(X[i, n] * (1.0f / h)));
    auto cell = go(Floor(X[i, n] * (1.0f / h) - 0.5f));
    return {
        go(face[i, l] * t.axis[l, n] + cell[i, l] * (1.0f - t.axis[l, n])),
        go((X[i, l] * (1.0f / h) - face[i, l]) * t.axis[l, n] +
           (X[i, l] * (1.0f / h) - 0.5f - cell[i, l]) * (1.0f - t.axis[l, n]))};
}

// The node a corner reaches, and the weight it carries there. Both are
// expressions over (particle i, component n, corner m). They are spelled
// inline everywhere rather than materialized: each is a dozen flops, and
// a tensor of them would be three more passes over N*3*8.
auto node_at(const Tank &t, const Stencil &s, auto d) {
    return s.base[i, d, n] + t.corner[m, d];
}

// |f + b - 1| is (1-f) at b = 0 and f at b = 1 — the trilinear weight
// with ONE mention of the fraction. Two would cost an ABI slot the
// device's push-constant range cannot spare.
auto weight_at(const Tank &t, const Stencil &s, auto d) {
    return Abs(s.frac[i, d, n] + t.corner[m, d] - 1.0f);
}
auto weight_of(const Tank &t, const Stencil &s) {
    return weight_at(t, s, 0_c) * weight_at(t, s, 1_c) * weight_at(t, s, 2_c);
}

// Particle to grid: every particle splats `value` onto the eight nodes
// around it, in all three staggerings at once — the component index is
// the one free index the scatter does not consume, so it survives as the
// result's last axis and one dispatch does the whole field.
Faces deposit(const Tank &t, const Stencil &s, const Weights &w,
              const auto &value) {
    return go(scatter<Sum, i, m>(
        clamp<GS>(node_at(t, s, 0_c)), clamp<GS>(node_at(t, s, 1_c)),
        clamp<GS>(node_at(t, s, 2_c)), w[i, n, m] * value));
}

// Grid to particle, the same eight weights read the other way round.
auto sample(const Tank &t, const Stencil &s, const Weights &w, const Faces &F) {
    return fold<m>(
        w[i, n, m] *
        F[clamp<GS>(node_at(t, s, 0_c)), clamp<GS>(node_at(t, s, 1_c)),
          clamp<GS>(node_at(t, s, 2_c)), n]);
}

// Particles per cell, cell-centred rather than staggered: this says
// which cells hold water, and by how much they are over-full.
Grid count_of(const Vecs &X) {
    auto at = [&](auto d) { return clamp<GS>(Floor(X[i, d] * (1.0f / h))); };
    return go(scatter<Sum, i>(at(0_c), at(1_c), at(2_c), 1.0f));
}

// The divergence the pressure has to remove, corrected for drift.
//
// FLIP loses and gains volume: nothing in the transfer conserves the
// particle count per cell, so water slowly clumps. Telling the solve
// that an over-full cell is compressing more than it measures pushes the
// excess back out over `rest * dt / drift` seconds. Under-full cells are
// left alone — pulling water INTO a thin region would fight the free
// surface, which is exactly where cells are legitimately half empty.
Grid rhs_of(const Faces &U, const Grid &fluid, const Grid &cnt, f32 dt,
            f32 drift) {
    auto div = (U[clamp(i + 1_c), j, k, 0_c] - U[i, j, k, 0_c] +
                U[i, clamp(j + 1_c), k, 1_c] - U[i, j, k, 1_c] +
                U[i, j, clamp(k + 1_c), 2_c] - U[i, j, k, 2_c]) *
               (1.0f / h);
    return go(fluid[i, j, k] * (h * h / dt) *
              (div - (drift / (dt * rest)) * Fmax(cnt[i, j, k] - rest, 0.0f)));
}

// The pressure Poisson equation, by red-black successive over-relaxation
// and warm-started from last step's answer.
//
// Air cells hold zero and are counted as neighbours, which is exactly
// the free-surface condition p = 0; solid cells are counted by neither
// term, which is the wall's zero-gradient condition.
//
// SOR and not plain Jacobi because the pressure here is a nearly
// hydrostatic column, and that is the slowest mode there is: Jacobi
// carries information one cell per sweep, so the forty sweeps a frame
// can afford reach a quarter of the answer and the water floats.
// Sweeping the two colours in turn lets each use the values just
// written, and over-relaxing past them turns the O(cells^2) sweep count
// into O(cells).
Grid pressure_of(Grid p, const Tank &t, const Grid &fluid, const Grid &rhs,
                 const Grid &winv, int sweeps, f32 omega) {
    // A cell that has just drained must restart from zero, or
    // over-relaxing an air cell towards zero overshoots and grows.
    p = go(fluid[i, j, k] * p[i, j, k]);

    for (int s = 0; s < 2 * sweeps; ++s) {
        const Grid &half = (s % 2) ? t.black : t.red;
        p = go(p[i, j, k] +
               half[i, j, k] * omega *
                   ((p[zero(i + 1_c), j, k] + p[zero(i - 1_c), j, k] +
                     p[i, zero(j + 1_c), k] + p[i, zero(j - 1_c), k] +
                     p[i, j, zero(k + 1_c)] + p[i, j, zero(k - 1_c)] -
                     rhs[i, j, k]) *
                        winv[i, j, k] -
                    p[i, j, k]));
    }
    return p;
}

Faces projected(const Tank &t, const Faces &U, const Grid &p, f32 dt) {
    auto grad = (p[i, j, k] - p[zero(i - 1_c), j, k]) * t.axis[0_c, n] +
                (p[i, j, k] - p[i, zero(j - 1_c), k]) * t.axis[1_c, n] +
                (p[i, j, k] - p[i, j, zero(k - 1_c)]) * t.axis[2_c, n];
    return go(t.open[i, j, k, n] * (U[i, j, k, n] - (dt / h) * grad));
}

struct Params {
    f32 dt, gravity, sway, phase, flip, drift, omega;
    int sweeps;
};

struct State {
    Vecs X, V;
    Grid p;
};

// One step. The FLIP and PIC readings are combined before they are
// gathered, not after: v = flip*(v + dU) + (1-flip)*U1 is the same as
// flip*v + interp(U1 - flip*U0), which is one fold instead of two.
void advance(State &s, const Tank &tank, const Params &prm) {
    const Tensor<f32, 3> accel{prm.sway * Sin(prm.phase), 0.0f, -prm.gravity};
    const Stencil st = stencil_of(tank, s.X);
    const Weights w = go(weight_of(tank, st));

    const Faces wsum = deposit(tank, st, w, 1.0f);
    const Faces mom = deposit(tank, st, w, s.V[i, n]);
    const Grid cnt = count_of(s.X);
    const Grid fluid =
        go(tank.inside[i, j, k] * (1.0f * (cnt[i, j, k] > 0.5f)));

    // U0 is the transfer ALONE. Gravity belongs to the grid's CHANGE and
    // not to its starting point: fold it in here and the FLIP delta no
    // longer spans it, so only the (1 - flip) PIC share of gravity ever
    // reaches a particle — at flip = 0.95, a twentieth of g.
    const Faces U0 = go(tank.open[i, j, k, n] * mom[i, j, k, n] /
                        Fmax(wsum[i, j, k, n], 1e-8f));
    const Faces Ustar =
        go(tank.open[i, j, k, n] * (U0[i, j, k, n] + prm.dt * accel[n]));

    const Grid winv = go(fluid[i, j, k] / Fmax(tank.neighbours[i, j, k], 1.0f));
    s.p = pressure_of(std::move(s.p), tank, fluid,
                      rhs_of(Ustar, fluid, cnt, prm.dt, prm.drift), winv,
                      prm.sweeps, prm.omega);
    const Faces U1 = projected(tank, Ustar, s.p, prm.dt);

    const Faces blend = go(U1[i, j, k, n] - prm.flip * U0[i, j, k, n]);
    s.V = go(prm.flip * s.V[i, n] + sample(tank, st, w, blend));

    // The wall stops the particle and kills only the component that hit
    // it, so water slides along a wall instead of sticking to it.
    const Vecs moved = go(s.X[i, n] + prm.dt * s.V[i, n]);
    s.X = go(Fmin(Fmax(moved[i, n], margin), L - margin));
    s.V = go(s.V[i, n] * (1.0f - 1.0f * (moved[i, n] < margin) -
                          1.0f * (moved[i, n] > L - margin)));
}

// What the cloud is coloured by. The turbo ramp starts near black, and
// still water rendered black is water you cannot see, so speed is mapped
// onto [0.17, 1] instead: a resting pool sits at the blue end and a
// breaking crest at the red one. Magnitude is all the cloud reads, so
// any vector of the right length will do.
Vecs speed_tone(const Vecs &V) {
    const Tensor<f32, 3> unit{1.0f, 0.0f, 0.0f};
    const Tensor<f32, N> speed = go(Sqrt(fold<1>(V * V)));
    return go((0.17f + 0.14f * speed[i]) * unit[n]);
}

// Eight particles per cell on a half-cell lattice, then jittered — a
// uniform random fill would start with 35% density noise per cell, and
// the first steps would be spent expelling it instead of collapsing.
Vecs dam() {
    const Vecs lattice([](idx p, idx d) {
        const idx c = p / PPC, s = p % PPC;
        const idx cell[3] = {c / (CY * CZ), (c / CZ) % CY, c % CZ};
        return h * (f32(cell[d]) + 0.25f + 0.5f * f32((s >> d) & 1u));
    });
    return eval(
        Fmin(Fmax(lattice[i, n] +
                      (0.15f * h) * (rng::Uniform<f32, N, 3>()[i, n] - 0.5f),
                  margin),
             L - margin));
}

} // namespace

int main() {
    sv::App app({.title = "simview — water (FLIP, tensor on gpud)",
                 .size = {1280, 860}});
    if (!app)
        return 1;
    device = &sv::Device(app);
    rng::Seed(20260901);

    const Tank tank = tank_of();
    State state{dam(), Vecs(gen::Fill(0.0f)), Grid(gen::Fill(0.0f))};

    // The tank is modelled in [0, L]^3 and drawn centred on the grid
    // origin, so the floor of the tank is the floor of the world.
    const Tensor<f32, 3> centre{-0.5f * L, -0.5f * L, 0.0f};
    sv::Sync<Vecs> pos, tone;
    const auto publish = [&] {
        pos.Next() = go(state.X[i, n] + centre[n]);
        pos.Publish();
        tone.Next() = speed_tone(state.V);
        tone.Publish();
    };
    publish();

    // One const read to sync: the compute backend batches eagerly, so
    // without it the first frames draw an empty tank.
    (void)pos.Current()[0, 0];
    (void)tone.Current()[0, 0];

    auto world = app.World({});
    world
        .Camera({.focus = {0.0f, 0.0f, 0.34f},
                 .distance = 4.1f,
                 .azimuth_deg = -58.0f,
                 .elevation_deg = 21.0f})
        .Light({.direction = {0.35f, 0.45f, 0.82f}, .intensity = 0.8f})
        .Ambient(0.40f, 0.44f, 0.52f);

    auto drops = world.Cloud(
        pos,
        {.radius = 0.011f, .map = sv::CloudMap::Magnitude, .map_scale = 1.0f});
    if (!drops)
        return 1;
    drops.Colors(tone);

    float gravity = 9.81f, sway = 0.6f, sway_hz = 0.44f, flip = 0.95f,
          drift = 0.04f, omega = 1.85f, sweeps = 40.0f, substeps = 2.0f;
    constexpr f32 dt = 0.005f;
    std::atomic<float> g_now{gravity}, s_now{sway}, hz_now{sway_hz},
        f_now{flip}, d_now{drift}, o_now{omega};
    std::atomic<int> sw_now{int(sweeps)}, sub_now{int(substeps)};
    float clock = 0.0f;

    sv::Executor sim([&](const sv::Tick &) {
        const int sub =
            std::clamp(sub_now.load(std::memory_order_relaxed), 1, 4);
        for (int t = 0; t < sub; ++t) {
            clock += dt;
            advance(state, tank,
                    {.dt = dt,
                     .gravity = g_now.load(std::memory_order_relaxed),
                     .sway = s_now.load(std::memory_order_relaxed),
                     .phase = 6.2831853f *
                              hz_now.load(std::memory_order_relaxed) * clock,
                     .flip = f_now.load(std::memory_order_relaxed),
                     .drift = d_now.load(std::memory_order_relaxed),
                     .omega = o_now.load(std::memory_order_relaxed),
                     .sweeps = sw_now.load(std::memory_order_relaxed)});
        }
        publish();
    });
    sim.OnRestart([&] {
        clock = 0.0f;
        state = {dam(), Vecs(gen::Fill(0.0f)), Grid(gen::Fill(0.0f))};
        publish();
    });
    sim.SetDt(double(dt * substeps));
    sim.Play();

    app.Controls(sim);
    app.Panel("water")
        .Text("FLIP/PIC, 48^3 staggered grid")
        .Separator()
        .Slider("gravity m/s2", gravity, 0.0f, 20.0f)
        .Slider("sway m/s2", sway, 0.0f, 4.0f)
        .Slider("sway Hz", sway_hz, 0.15f, 1.5f)
        .Separator()
        .Slider("FLIP blend", flip, 0.0f, 1.0f)
        .Slider("drift fix", drift, 0.0f, 0.2f)
        .Slider("over-relax", omega, 1.0f, 1.97f)
        .Slider("pressure sweeps", sweeps, 4.0f, 80.0f)
        .Slider("substeps", substeps, 1.0f, 4.0f)
        .Separator()
        .Text("138240 particles, 8 per cell");

    app.OnFrame([&] {
        g_now.store(gravity, std::memory_order_relaxed);
        s_now.store(sway, std::memory_order_relaxed);
        hz_now.store(sway_hz, std::memory_order_relaxed);
        f_now.store(flip, std::memory_order_relaxed);
        d_now.store(drift, std::memory_order_relaxed);
        o_now.store(omega, std::memory_order_relaxed);
        sw_now.store(int(sweeps), std::memory_order_relaxed);
        sub_now.store(int(substeps), std::memory_order_relaxed);
        sim.SetDt(double(dt * substeps));
    });

    app.OnKey(sv::Key::Space, [&] { sim.Toggle(); })
        .OnKey(sv::Key::R, [&] { sim.Restart(); })
        .OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
}
