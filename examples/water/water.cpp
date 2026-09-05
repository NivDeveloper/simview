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

// Every declaration below whose type is Dest<…> evaluates on the ambient
// device (use_device, set once in main). One alias names that, so an
// expression and where it lands stay separate things.
template <typename T> using Dest = Gpu<T>;

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
    Dest<Faces> open;         // 1 on faces between two real cells
    Dest<Grid> neighbours;    // non-solid neighbours per cell
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

    Dest<Faces> open =
        inside[i, j, k] * (inside[zero(i - 1_c), j, k] * axis[0_c, n] +
                           inside[i, zero(j - 1_c), k] * axis[1_c, n] +
                           inside[i, j, zero(k - 1_c)] * axis[2_c, n]);
    Dest<Grid> neighbours =
        inside[zero(i + 1_c), j, k] + inside[zero(i - 1_c), j, k] +
        inside[i, zero(j + 1_c), k] + inside[i, zero(j - 1_c), k] +
        inside[i, j, zero(k + 1_c)] + inside[i, j, zero(k - 1_c)];

    return {std::move(corner), std::move(axis),       std::move(inside),
            std::move(open),   std::move(neighbours), std::move(red),
            std::move(black)};
}

// Where each particle sits in the staggered lattice of every component.
// Component c samples axis c at a face and the other two at a centre, so
// both coordinates are computed once and `axis` picks between them.
// Laid out [particle][axis][component].
struct Stencil {
    Dest<Tensor<f32, N, 3, 3>> base, frac;
};

Stencil stencil_of(const Tank &t, const Vecs &X) {
    const Dest<Vecs> face = Floor(X[i, n] * (1.0f / h));
    const Dest<Vecs> cell = Floor(X[i, n] * (1.0f / h) - 0.5f);
    Dest<Tensor<f32, N, 3, 3>> base =
        face[i, l] * t.axis[l, n] + cell[i, l] * (1.0f - t.axis[l, n]);
    Dest<Tensor<f32, N, 3, 3>> frac =
        (X[i, l] * (1.0f / h) - face[i, l]) * t.axis[l, n] +
        (X[i, l] * (1.0f / h) - 0.5f - cell[i, l]) * (1.0f - t.axis[l, n]);
    return {std::move(base), std::move(frac)};
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
Dest<Faces> deposit(const Tank &t, const Stencil &s, const Weights &w,
                    const auto &value) {
    Dest<Faces> out = scatter<Sum, i, m>(
        clamp<GS>(node_at(t, s, 0_c)), clamp<GS>(node_at(t, s, 1_c)),
        clamp<GS>(node_at(t, s, 2_c)), w[i, n, m] * value);
    return out;
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
Dest<Grid> count_of(const Vecs &X) {
    auto at = [&](auto d) { return clamp<GS>(Floor(X[i, d] * (1.0f / h))); };
    Dest<Grid> out = scatter<Sum, i>(at(0_c), at(1_c), at(2_c), 1.0f);
    return out;
}

// The divergence the pressure has to remove, corrected for drift.
//
// FLIP loses and gains volume: nothing in the transfer conserves the
// particle count per cell, so water slowly clumps. Telling the solve
// that an over-full cell is compressing more than it measures pushes the
// excess back out over `rest * dt / drift` seconds. Under-full cells are
// left alone — pulling water INTO a thin region would fight the free
// surface, which is exactly where cells are legitimately half empty.
Dest<Grid> rhs_of(const Faces &U, const Grid &fluid, const Grid &cnt, f32 dt,
                  f32 drift) {
    auto div = (U[clamp(i + 1_c), j, k, 0_c] - U[i, j, k, 0_c] +
                U[i, clamp(j + 1_c), k, 1_c] - U[i, j, k, 1_c] +
                U[i, j, clamp(k + 1_c), 2_c] - U[i, j, k, 2_c]) *
               (1.0f / h);
    Dest<Grid> out =
        fluid[i, j, k] * (h * h / dt) *
        (div - (drift / (dt * rest)) * Fmax(cnt[i, j, k] - rest, 0.0f));
    return out;
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
Dest<Grid> pressure_of(Dest<Grid> p, const Tank &t, const Grid &fluid,
                       const Grid &rhs, const Grid &winv, int sweeps,
                       f32 omega) {
    // A cell that has just drained must restart from zero, or
    // over-relaxing an air cell towards zero overshoots and grows.
    p = fluid[i, j, k] * p[i, j, k];

    for (int s = 0; s < 2 * sweeps; ++s) {
        const Grid &half = (s % 2) ? t.black : t.red;
        p = p[i, j, k] + half[i, j, k] * omega *
                             ((p[zero(i + 1_c), j, k] + p[zero(i - 1_c), j, k] +
                               p[i, zero(j + 1_c), k] + p[i, zero(j - 1_c), k] +
                               p[i, j, zero(k + 1_c)] + p[i, j, zero(k - 1_c)] -
                               rhs[i, j, k]) *
                                  winv[i, j, k] -
                              p[i, j, k]);
    }
    return p;
}

Dest<Faces> projected(const Tank &t, const Faces &U, const Grid &p, f32 dt) {
    auto grad = (p[i, j, k] - p[zero(i - 1_c), j, k]) * t.axis[0_c, n] +
                (p[i, j, k] - p[i, zero(j - 1_c), k]) * t.axis[1_c, n] +
                (p[i, j, k] - p[i, j, zero(k - 1_c)]) * t.axis[2_c, n];
    Dest<Faces> out = t.open[i, j, k, n] * (U[i, j, k, n] - (dt / h) * grad);
    return out;
}

struct Params {
    f32 dt;
    f32 gravity[3];
    f32 sway, phase, flip, drift, omega;
    int sweeps;
};

struct State {
    Dest<Vecs> X, V;
    Dest<Grid> p;
};

// One step. The FLIP and PIC readings are combined before they are
// gathered, not after: v = flip*(v + dU) + (1-flip)*U1 is the same as
// flip*v + interp(U1 - flip*U0), which is one fold instead of two.
void advance(State &s, const Tank &tank, const Params &prm) {
    const Tensor<f32, 3> accel{prm.gravity[0] + prm.sway * Sin(prm.phase),
                               prm.gravity[1], prm.gravity[2]};
    const Stencil st = stencil_of(tank, s.X);
    const Dest<Weights> w = weight_of(tank, st);

    const Dest<Faces> wsum = deposit(tank, st, w, 1.0f);
    const Dest<Faces> mom = deposit(tank, st, w, s.V[i, n]);
    const Dest<Grid> cnt = count_of(s.X);
    const Dest<Grid> fluid =
        tank.inside[i, j, k] * (1.0f * (cnt[i, j, k] > 0.5f));

    // U0 is the transfer ALONE. Gravity belongs to the grid's CHANGE and
    // not to its starting point: fold it in here and the FLIP delta no
    // longer spans it, so only the (1 - flip) PIC share of gravity ever
    // reaches a particle — at flip = 0.95, a twentieth of g.
    const Dest<Faces> U0 =
        tank.open[i, j, k, n] * mom[i, j, k, n] / Fmax(wsum[i, j, k, n], 1e-8f);
    const Dest<Faces> Ustar =
        tank.open[i, j, k, n] * (U0[i, j, k, n] + prm.dt * accel[n]);

    const Dest<Grid> winv =
        fluid[i, j, k] / Fmax(tank.neighbours[i, j, k], 1.0f);
    s.p = pressure_of(std::move(s.p), tank, fluid,
                      rhs_of(Ustar, fluid, cnt, prm.dt, prm.drift), winv,
                      prm.sweeps, prm.omega);
    const Dest<Faces> U1 = projected(tank, Ustar, s.p, prm.dt);

    const Dest<Faces> blend = U1[i, j, k, n] - prm.flip * U0[i, j, k, n];
    s.V = prm.flip * s.V[i, n] + sample(tank, st, w, blend);

    // The wall stops the particle and kills only the component that hit
    // it, so water slides along a wall instead of sticking to it.
    const Dest<Vecs> moved = s.X[i, n] + prm.dt * s.V[i, n];
    s.X = Fmin(Fmax(moved[i, n], margin), L - margin);
    s.V = s.V[i, n] * (1.0f - 1.0f * (moved[i, n] < margin) -
                       1.0f * (moved[i, n] > L - margin));
}

// A start state is one or two boxes of water, each an exact number of
// CELLS so that eight particles a cell comes out even — a fractional
// last layer would start the run with a density the solver has to
// expel before anything physical happens.
struct Box {
    idx nx, ny, nz, ox, oy, oz;
};

// Every entry holds the same 17280 cells, which is what lets one fixed
// particle count serve all four.
constexpr Box kStarts[8] = {
    {24, 48, 15, 0, 0, 0},
    {0, 0, 0, 0, 0, 0}, // a dam against one wall
    {12, 48, 15, 0, 0, 0},
    {12, 48, 15, 36, 0, 0}, // two, meeting in the middle
    {24, 24, 30, 12, 12, 14},
    {0, 0, 0, 0, 0, 0}, // a block dropped from height
    {45, 48, 8, 0, 0, 0},
    {0, 0, 0, 0, 0, 0}, // a pool already at rest
};

// Eight particles per cell on a half-cell lattice, then jittered — a
// uniform random fill would start with 35% density noise per cell, and
// the first steps would be spent expelling it instead of collapsing.
Dest<Vecs> seed(int start) {
    const Box a = kStarts[2 * start], b = kStarts[2 * start + 1];
    const bool split = b.nx != 0;
    const Vecs lattice([a, b, split](idx p, idx d) {
        const idx half = N / 2;
        const Box &box = (split && p >= half) ? b : a;
        const idx q = (split && p >= half) ? p - half : p;
        const idx c = q / PPC, s = q % PPC;
        const idx cell[3] = {c / (box.ny * box.nz), (c / box.nz) % box.ny,
                             c % box.nz};
        const idx origin[3] = {box.ox, box.oy, box.oz};
        return h *
               (f32(origin[d] + cell[d]) + 0.25f + 0.5f * f32((s >> d) & 1u));
    });
    Dest<Vecs> out =
        Fmin(Fmax(lattice[i, n] +
                      (0.15f * h) * (rng::Uniform<f32, N, 3>()[i, n] - 0.5f),
                  margin),
             L - margin);
    return out;
}

// A poke: an upward jet under the middle of the tank, or a swirl about
// its axis. Both fall off over a disc about a third of the tank wide,
// so what they do is visible without being a teleport.
Dest<Vecs> poked(const Vecs &V, const Vecs &X, f32 kick, bool swirl) {
    const Tensor<f32, 3> ex{1.0f, 0.0f, 0.0f}, ey{0.0f, 1.0f, 0.0f},
        ez{0.0f, 0.0f, 1.0f};
    auto dx = X[i, 0_c] - 0.5f * L;
    auto dy = X[i, 1_c] - 0.5f * L;
    auto fall = Exp(-8.0f * (dx * dx + dy * dy) / (L * L));
    if (swirl) {
        Dest<Vecs> out = V[i, n] + (kick * fall) * (ey[n] * dx - ex[n] * dy);
        return out;
    }
    Dest<Vecs> out = V[i, n] + (kick * fall) * ez[n];
    return out;
}

// What the cloud is coloured by. The turbo ramp starts near black, and
// still water rendered black is water you cannot see, so every source
// is mapped onto [0.17, 1] instead: the low end is a water blue and the
// high end red. Magnitude is all the cloud reads, so any vector of the
// right length will do.
Dest<Vecs> tone_of(const Vecs &X, const Vecs &V, const Vecs &start, int source,
                   f32 top) {
    const Tensor<f32, 3> unit{1.0f, 0.0f, 0.0f};
    if (source == 1) {
        Dest<Vecs> out = (0.17f + (0.83f / L) * X[i, 2_c]) * unit[n];
        return out;
    }
    if (source == 2) {
        Dest<Vecs> out = (0.17f + (0.83f / L) * start[i, 0_c]) * unit[n];
        return out;
    }
    const Dest<Tensor<f32, N>> speed = Sqrt(fold<1>(V * V));
    Dest<Vecs> out =
        (0.17f + 0.83f * Fmin(speed[i] * (1.0f / top), 1.0f)) * unit[n];
    return out;
}

} // namespace

int main() {
    sv::App app({.title = "simview — water (FLIP, tensor on gpud)",
                 .size = {1280, 860}});
    if (!app)
        return 1;
    device = &sv::Device(app);
    // The ambient device is per THREAD, so every thread that evaluates a
    // Dest<…> arms itself. This one builds the tank and the start state;
    // the executor's thread does the same below. Every Dest<…> must be
    // destroyed before the app that owns the device is.
    use_device(*device);
    rng::Seed(20260901);

    const Tank tank = tank_of();
    int start = 0;
    State state{seed(start), Dest<Vecs>(gen::Fill(0.0f)),
                Dest<Grid>(gen::Fill(0.0f))};
    Dest<Vecs> origin = state.X[i, n];

    // The tank is modelled in [0, L]^3 and drawn centred on the grid
    // origin, so the floor of the tank is the floor of the world.
    const Tensor<f32, 3> centre{-0.5f * L, -0.5f * L, 0.0f};
    int colour_by = 0;
    std::atomic<int> colour_now{colour_by};
    std::atomic<float> top_now{6.0f};
    std::atomic<float> energy{0.0f};

    sv::Sync<Vecs> pos, tone;
    const auto publish = [&] {
        pos.Next() = eval(*device, state.X[i, n] + centre[n]);
        pos.Publish();
        tone.Next() = tone_of(state.X, state.V, origin,
                              colour_now.load(std::memory_order_relaxed),
                              top_now.load(std::memory_order_relaxed));
        tone.Publish();
    };
    publish();

    // One const read to sync: the compute backend batches eagerly, so
    // without it the first frames draw an empty tank.
    (void)pos.Current()[0, 0];
    (void)tone.Current()[0, 0];

    // Named, because the view button puts it back: an opening pose
    // spelled twice is one that drifts.
    constexpr sv::CameraDesc kOpening{.focus = {0.0f, 0.0f, 0.34f},
                                      .distance = 4.5f,
                                      .azimuth_deg = -58.0f,
                                      .elevation_deg = 21.0f};

    auto world = app.World({});
    world.Camera(kOpening)
        .Light({.direction = {0.35f, 0.45f, 0.82f}, .intensity = 0.8f})
        .Ambient(0.40f, 0.44f, 0.52f);

    auto drops = world.Cloud(
        pos,
        {.radius = 0.0135f, .map = sv::CloudMap::Magnitude, .map_scale = 1.0f});
    if (!drops)
        return 1;
    drops.Colors(tone);

    float gravity[3] = {0.0f, 0.0f, -9.81f};
    float sway = 0.6f, sway_hz = 0.44f, flip = 0.90f, drift = 0.04f,
          omega = 1.85f, dt = 0.005f, kick = 6.0f, top = 6.0f;
    float ambient[3] = {0.40f, 0.44f, 0.52f};
    int sweeps = 40, substeps = 2;
    bool driven = false;

    std::atomic<float> gx_now{0.0f}, gy_now{0.0f}, gz_now{-9.81f}, s_now{0.0f},
        hz_now{sway_hz}, f_now{flip}, d_now{drift}, o_now{omega}, dt_now{dt},
        kick_now{kick};
    std::atomic<int> sw_now{sweeps}, sub_now{substeps};
    std::atomic<bool> want_jet{false}, want_swirl{false};
    float clock = 0.0f;

    sv::Executor sim([&](const sv::Tick &) {
        // The executor runs on its own thread, and the ambient device is
        // per-thread: arm it once, on the first tick this thread runs.
        static thread_local const bool armed = (use_device(*device), true);
        (void)armed;

        const f32 step = dt_now.load(std::memory_order_relaxed);
        if (want_jet.exchange(false, std::memory_order_relaxed))
            state.V = poked(state.V, state.X,
                            kick_now.load(std::memory_order_relaxed), false);
        if (want_swirl.exchange(false, std::memory_order_relaxed))
            state.V = poked(state.V, state.X,
                            kick_now.load(std::memory_order_relaxed), true);

        const int sub =
            std::clamp(sub_now.load(std::memory_order_relaxed), 1, 4);
        for (int t = 0; t < sub; ++t) {
            clock += step;
            advance(state, tank,
                    {.dt = step,
                     .gravity = {gx_now.load(std::memory_order_relaxed),
                                 gy_now.load(std::memory_order_relaxed),
                                 gz_now.load(std::memory_order_relaxed)},
                     .sway = s_now.load(std::memory_order_relaxed),
                     .phase = 6.2831853f *
                              hz_now.load(std::memory_order_relaxed) * clock,
                     .flip = f_now.load(std::memory_order_relaxed),
                     .drift = d_now.load(std::memory_order_relaxed),
                     .omega = o_now.load(std::memory_order_relaxed),
                     .sweeps = sw_now.load(std::memory_order_relaxed)});
        }
        const Dest<Tensor<f32, 3>> ke = fold<0>(state.V * state.V);
        energy.store((float(ke[0]) + float(ke[1]) + float(ke[2])) / float(N),
                     std::memory_order_relaxed);
        publish();
    });
    sim.OnRestart([&] {
        clock = 0.0f;
        state = {seed(start), Dest<Vecs>(gen::Fill(0.0f)),
                 Dest<Grid>(gen::Fill(0.0f))};
        origin = state.X[i, n];
        publish();
    });
    sim.SetDt(double(dt * substeps));
    sim.Play();

    app.Panel("water")
        .Transport(sim)
        .Tabs([&](sv::Panel &p) {
            p.Tab(
                 "scene",
                 [&](sv::Panel &q) {
                     q.Choice("start from", start,
                              {"dam break", "two dams", "dropped block",
                               "pool at rest"})
                         .Help(
                             "Each holds the same volume of water, so the same "
                             "particles serve all four. Changing it restarts.")
                         .Row([&](sv::Panel &r) {
                             r.Button("jet", [&] {
                                  want_jet.store(true,
                                                 std::memory_order_relaxed);
                              }).Button("swirl", [&] {
                                 want_swirl.store(true,
                                                  std::memory_order_relaxed);
                             });
                         })
                         .Drag("poke m/s", kick, 0.2f, 0.0f, 0.0f)
                         .Help(
                             "The impulse the two buttons add, under a disc at "
                             "the middle of the tank. Unbounded on purpose: "
                             "there is no principled largest poke.")
                         .Separator("forces")
                         .Slider("gravity m/s2", gravity, -20.0f, 20.0f)
                         .Help(
                             "Tilt it and the free surface tilts with it. All "
                             "three axes, because gravity is one vector and "
                             "not "
                             "three numbers.")
                         .Checkbox("drive the tank", driven)
                         .Help(
                             "A horizontal acceleration, oscillating. This is "
                             "what makes the water keep sloshing.")
                         .Enabled(driven, [&](sv::Panel &r) {
                             r.Slider("sway m/s2", sway, 0.0f, 4.0f)
                                 .Slider("sway Hz", sway_hz, 0.15f, 1.5f)
                                 .Help(
                                     "0.44 Hz is this tank's own sloshing "
                                     "mode, "
                                     "2L/sqrt(g*depth). Drive it there and the "
                                     "wave grows every cycle until it reaches "
                                     "the top of the domain.");
                         });
                 })
                .Tab(
                    "solver",
                    [&](sv::Panel &q) {
                        q.Slider("pressure sweeps", sweeps, 4, 80)
                            .Help(
                                "Red-black SOR sweeps per step. The converged "
                                "answer needs about 40 here; below 20 the "
                                "pressure lags gravity and the water floats.")
                            .Slider("over-relax", omega, 1.0f, 1.97f)
                            .Help("1 is plain Gauss-Seidel. Past it each sweep "
                                  "overshoots on purpose, which is what turns "
                                  "the "
                                  "sweep count from O(cells^2) into O(cells).")
                            .Separator("transfer")
                            .Slider("FLIP blend", flip, 0.0f, 1.0f)
                            .Help(
                                "1 keeps everything the grid does not take "
                                "back "
                                "— lively and noisy. 0 re-averages every "
                                "particle from the grid every step — treacle. "
                                "This is the only dissipation dial here.")
                            .Slider("drift fix", drift, 0.001f, 1.0f,
                                    sv::Scale::Log)
                            .Help(
                                "How hard an over-full cell is pushed back "
                                "towards eight particles. Logarithmic because "
                                "it is useful across three decades.")
                            .Separator("stepping")
                            .Slider("substeps", substeps, 1, 4)
                            .Input("dt (s)", dt)
                            .Help("Typed, not dragged: a slider cannot say "
                                  "exactly 0.005, and a run you want to repeat "
                                  "needs it to.");
                    })
                .Tab("view", [&](sv::Panel &q) {
                    q.IconButton(sv::Icon::Home, "back to the opening view",
                                 [&] { world.Camera(kOpening); })
                        .Choice("colour by", colour_by,
                                {"speed", "height", "where it started"})
                        .Help("Speed reads the flow; where it started reads "
                              "the mixing, because a particle keeps its tag.")
                        .Slider("ramp tops out at m/s", top, 0.3f, 20.0f,
                                sv::Scale::Log)
                        .Help("Where the colour ramp saturates. Logarithmic, "
                              "because a settled pool and a breaking crest "
                              "are two decades apart.")
                        .Color("ambient light", ambient)
                        .Help("The world's fill light. Everything else about "
                              "the shading is fixed.");
                });
        })
        .Separator()
        .Progress(
            "kinetic energy / particle",
            [&] { return energy.load(std::memory_order_relaxed); }, 0.0f, 1.5f)
        .Value(
            "particles", [] { return float(N); }, "%.0f")
        .Value("grid", [] { return float(G); }, "%.0f cells/axis");

    app.OnFrame([&] {
        gx_now.store(gravity[0], std::memory_order_relaxed);
        gy_now.store(gravity[1], std::memory_order_relaxed);
        gz_now.store(gravity[2], std::memory_order_relaxed);
        s_now.store(driven ? sway : 0.0f, std::memory_order_relaxed);
        hz_now.store(sway_hz, std::memory_order_relaxed);
        f_now.store(flip, std::memory_order_relaxed);
        d_now.store(drift, std::memory_order_relaxed);
        o_now.store(omega, std::memory_order_relaxed);
        dt_now.store(dt, std::memory_order_relaxed);
        kick_now.store(kick, std::memory_order_relaxed);
        sw_now.store(sweeps, std::memory_order_relaxed);
        sub_now.store(substeps, std::memory_order_relaxed);
        colour_now.store(colour_by, std::memory_order_relaxed);
        sim.SetDt(double(dt * substeps));

        top_now.store(top, std::memory_order_relaxed);
        world.Ambient(ambient[0], ambient[1], ambient[2]);

        // The start state is a restart, not a live parameter: changing
        // it here is the only place that knows it changed.
        static int shown = -1;
        if (shown != start && shown >= 0)
            sim.Restart();
        shown = start;
    });

    app.OnKey(sv::Key::Space, [&] { sim.Toggle(); })
        .OnKey(sv::Key::R, [&] { sim.Restart(); })
        .OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
}
