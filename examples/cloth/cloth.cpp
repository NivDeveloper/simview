// A cloth hung by its top edge: position-based dynamics on a grid of
// springs, every spring family a stencil over the grid. Choose "cut" and
// drag to cut it, or drag the ball through it; a spring stretched past
// its limit tears.
//
// Space toggles, R restarts, Esc quits. Right-drag orbits while cutting.
#include <simview/simview.h>

#include <Tensor/Gen.h>
#include <Tensor/Math.h>
#include <Tensor/Tensor.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

using namespace tensor;
using namespace tensor::math;
using tensor::indices::clamp, tensor::indices::zero;
using tensor::indices::i, tensor::indices::j, tensor::indices::n;
using tensor::indices::operator""_c;

using f32 = float;
using idx = size_t;
using u32 = std::uint32_t;

constexpr idx N = 40;             // particles per side
constexpr f32 L = 1.6f;           // side, metres
constexpr f32 h = L / f32(N - 1); // a structural spring's rest length
constexpr f32 z0 = 0.15f;         // the bottom edge's height at rest
constexpr f32 ball_r = 0.3f;
constexpr f32 ball_start[3] = {0.0f, -0.35f, 0.8f}; // the camera's side

// The solver: a 1/60 s tick in eight substeps of two averaged Jacobi
// pulls, over-relaxed; the settings that hold a hanging cloth within a
// few percent at under two milliseconds a tick.
constexpr f32 kDt = 1.0f / 60.0f, kDamping = 0.02f, kRelax = 1.5f;
constexpr f32 kBend = 0.25f; // the bend springs' weight
constexpr int kIterations = 2, kSubsteps = 8;

using Vecs = Tensor<f32, N, N, 3>; // (column, row, xyz)
using Grid = Tensor<f32, N, N>;

// Six spring families, a mask each: spring (c, r) reaches the particle
// one family step away, and holds while its mask is 1.
struct Springs {
    Grid right, up, diag_a, diag_b, bend_r, bend_u;
};

struct Reach {
    int dc, dr;
};
constexpr Reach kReach[6] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}, {2, 0}, {0, 2}};

Grid &family(Springs &s, int f) {
    Grid *m[6] = {&s.right, &s.up, &s.diag_a, &s.diag_b, &s.bend_r, &s.bend_u};
    return *m[f];
}

struct State {
    Vecs x, xo; // now, and a step ago
    Springs s;
};

struct Params {
    f32 gravity = 9.81f;
    f32 tear = 1.6f; // a spring past this many rest lengths is gone
    int pin = 0;     // 0 the top edge, 1 its two corners
    bool ball = true;
    f32 ball_at[3] = {ball_start[0], ball_start[1], ball_start[2]};
};

struct Cut {
    int family, c, r;
};

// What the two threads share, under one lock: the panel's settings and
// the pointer's cuts.
struct Shared {
    std::mutex lock;
    Params params;
    std::vector<Cut> cuts;
    bool mend = false;
};

// The rest pose: a vertical sheet in the plane y = 0, columns along x,
// rows up z, every spring alive whose far end is on the grid.
State rest() {
    auto pos = [](idx c, idx r, idx d) {
        return d == 0   ? -0.5f * L + f32(c) * h
               : d == 1 ? 0.0f
                        : z0 + f32(r) * h;
    };

    auto reach = [](int dc, int dr) {
        return [dc, dr](idx c, idx r) {
            int c2 = int(c) + dc, r2 = int(r) + dr;
            return c2 < int(N) && r2 >= 0 && r2 < int(N) ? 1.0f : 0.0f;
        };
    };

    return {
        .x = Vecs(pos),
        .xo = Vecs(pos),
        .s = {Grid(reach(1, 0)), Grid(reach(0, 1)), Grid(reach(1, 1)),
              Grid(reach(1, -1)), Grid(reach(2, 0)), Grid(reach(0, 2))},
    };
}

// Inverse mass: zero holds a particle where it is.
Grid pinned(int mode) {
    return Grid([mode](idx c, idx r) {
        if (r != N - 1)
            return 1.0f;
        return mode == 0 || c == 0 || c == N - 1 ? 0.0f : 1.0f;
    });
}

// One substep: predict under gravity, pull every spring toward its rest
// length (Jacobi, averaged over the springs that pulled), collide, then
// drop the springs that stretched too far.
void substep(State &s, const Grid &W, const Params &p) {
    constexpr f32 dt = kDt / f32(kSubsteps);
    constexpr f32 damping = kDamping / f32(kSubsteps);
    Tensor<f32, 3> g{0.0f, 0.0f, -p.gravity};

    Vecs xn = s.x[i, j, n] +
              W[i, j] * ((s.x[i, j, n] - s.xo[i, j, n]) * (1.0f - damping) +
                         g[n] * (dt * dt));
    Springs &sp = s.s;

    auto pull = [&](const auto &alive, auto ni, auto nj, f32 rest_len, f32 k,
                    Vecs &d, Grid &cnt) {
        auto dx = xn[ni, nj, 0_c] - xn[i, j, 0_c];
        auto dy = xn[ni, nj, 1_c] - xn[i, j, 1_c];
        auto dz = xn[ni, nj, 2_c] - xn[i, j, 2_c];
        auto len = Fmax(Sqrt(dx * dx + dy * dy + dz * dz), 1e-6f);
        auto share = W[i, j] / (W[i, j] + W[ni, nj] + 1e-9f);
        Grid coef =
            where(alive > 0.0f, k * share * (1.0f - rest_len / len), 0.0f);
        d += coef[i, j] * (xn[ni, nj, n] - xn[i, j, n]);
        cnt += where(alive > 0.0f, k, 0.0f);
    };

    f32 hd = h * 1.41421356f, hb = 2.0f * h, kb = kBend;

    for (int it = 0; it < kIterations; ++it) {
        Vecs d(gen::Fill(0.0f));
        Grid cnt(gen::Fill(0.0f));
        pull(sp.right[i, j], clamp(i + 1_c), j, h, 1.0f, d, cnt);
        pull(sp.right[zero(i - 1_c), j], clamp(i - 1_c), j, h, 1.0f, d, cnt);
        pull(sp.up[i, j], i, clamp(j + 1_c), h, 1.0f, d, cnt);
        pull(sp.up[i, zero(j - 1_c)], i, clamp(j - 1_c), h, 1.0f, d, cnt);
        pull(sp.diag_a[i, j], clamp(i + 1_c), clamp(j + 1_c), hd, 1.0f, d, cnt);
        pull(sp.diag_a[zero(i - 1_c), zero(j - 1_c)], clamp(i - 1_c),
             clamp(j - 1_c), hd, 1.0f, d, cnt);
        pull(sp.diag_b[i, j], clamp(i + 1_c), clamp(j - 1_c), hd, 1.0f, d, cnt);
        pull(sp.diag_b[zero(i - 1_c), zero(j + 1_c)], clamp(i - 1_c),
             clamp(j + 1_c), hd, 1.0f, d, cnt);
        pull(sp.bend_r[i, j], clamp(i + 2_c), j, hb, kb, d, cnt);
        pull(sp.bend_r[zero(i - 2_c), j], clamp(i - 2_c), j, hb, kb, d, cnt);
        pull(sp.bend_u[i, j], i, clamp(j + 2_c), hb, kb, d, cnt);
        pull(sp.bend_u[i, zero(j - 2_c)], i, clamp(j - 2_c), hb, kb, d, cnt);
        xn += kRelax * d[i, j, n] / Fmax(cnt[i, j], 1.0f);
    }

    Tensor<f32, 3> keep{1.0f, 1.0f, 0.0f};
    xn = where(xn[i, j, 2_c] < 0.0f, xn[i, j, n] * keep[n], xn[i, j, n]);

    if (p.ball) {
        Tensor<f32, 3> c{p.ball_at[0], p.ball_at[1], p.ball_at[2]};
        auto dx = xn[i, j, 0_c] - p.ball_at[0];
        auto dy = xn[i, j, 1_c] - p.ball_at[1];
        auto dz = xn[i, j, 2_c] - p.ball_at[2];
        auto r = Fmax(Sqrt(dx * dx + dy * dy + dz * dz), 1e-6f);
        xn = where(r < ball_r, c[n] + (xn[i, j, n] - c[n]) * (ball_r / r),
                   xn[i, j, n]);
    }

    s.xo = std::move(s.x);
    s.x = std::move(xn);

    Vecs &x = s.x;
    auto holds = [&](auto ni, auto nj, f32 rest_len) {
        auto dx = x[ni, nj, 0_c] - x[i, j, 0_c];
        auto dy = x[ni, nj, 1_c] - x[i, j, 1_c];
        auto dz = x[ni, nj, 2_c] - x[i, j, 2_c];
        f32 limit = p.tear * rest_len;
        return 1.0f * (dx * dx + dy * dy + dz * dz < limit * limit);
    };

    s.s.right *= holds(clamp(i + 1_c), j, h);
    s.s.up *= holds(i, clamp(j + 1_c), h);
    s.s.diag_a *= holds(clamp(i + 1_c), clamp(j + 1_c), hd);
    s.s.diag_b *= holds(clamp(i + 1_c), clamp(j - 1_c), hd);
    s.s.bend_r *= holds(clamp(i + 2_c), j, hb);
    s.s.bend_u *= holds(i, clamp(j + 2_c), hb);
}

// One tick: the sag gravity adds per substep shrinks with its square,
// which buys more than iterations do.
void step(State &s, const Grid &W, const Params &p) {
    for (int t = 0; t < kSubsteps; ++t)
        substep(s, W, p);
}

// A wire's edges for one family, in the mask's own order — spring (c, r)
// is edge c*N + r — so the mask publishes as it is. A spring whose far end
// is off the grid is degenerate, and masked forever.
std::vector<u32> edges_of(Reach k) {
    std::vector<u32> e;
    e.reserve(N * N * 2);

    for (int c = 0; c < int(N); ++c)
        for (int r = 0; r < int(N); ++r) {
            int c2 = std::clamp(c + k.dc, 0, int(N) - 1);
            int r2 = std::clamp(r + k.dr, 0, int(N) - 1);
            e.push_back(u32(c * int(N) + r));
            e.push_back(u32(c2 * int(N) + r2));
        }

    return e;
}

int main() {
    sv::App app({
        .title = "simview — cloth",
        .size = {1280, 860},
    });
    if (!app)
        return 1;

    Shared shared;
    Params knobs; // the panel's copy, handed over once a frame
    State state = rest();
    Grid W = pinned(knobs.pin);
    int pinned_as = knobs.pin;

    sv::Sync<std::vector<float>> pos, alive_r, alive_u, ball_pos;

    auto publish = [&](const Params &p) {
        auto flat = [](const auto &t) {
            return std::vector<float>(t.data(), t.data() + t.size());
        };
        pos.Publish(flat(state.x));
        alive_r.Publish(flat(state.s.right));
        alive_u.Publish(flat(state.s.up));
        ball_pos.Publish(p.ball ? std::vector<float>(p.ball_at, p.ball_at + 3)
                                : std::vector<float>{});
    };

    auto world = app.World({});

    world
        .Camera({
            .focus = {0.0f, 0.0f, 0.85f},
            .distance = 3.4f,
            .azimuth_deg = -38.0f,
            .elevation_deg = 14.0f,
        })
        .Light({
            .direction = {0.3f, -0.5f, 0.8f},
            .intensity = 0.8f,
        })
        .Ambient(0.42f, 0.44f, 0.50f);

    sv::WireDesc thread{
        .color = {0.86f, 0.89f, 0.96f, 1.0f},
        .width = 1.6f,
    };
    auto wire_r = world.Wire(pos, edges_of(kReach[0]), thread);
    auto wire_u = world.Wire(pos, edges_of(kReach[1]), thread);

    sv::CloudDesc sphere{
        .color = {0.95f, 0.62f, 0.28f, 1.0f},
        .radius = ball_r,
        .shape = sv::CloudShape::Sphere,
    };
    auto ball = world.Cloud(ball_pos, sphere);
    if (!wire_r || !wire_u || !ball)
        return 1;

    wire_r.Mask(alive_r);
    wire_u.Mask(alive_u);
    publish(knobs);

    sv::Executor sim([&](const sv::Tick &) {
        Params p;
        std::vector<Cut> cuts;
        bool mend = false;

        {
            std::lock_guard l(shared.lock);
            p = shared.params;
            cuts.swap(shared.cuts);
            mend = std::exchange(shared.mend, false);
        }

        for (Cut k : cuts)
            family(state.s, k.family)[idx(k.c), idx(k.r)] = 0.0f;

        if (mend)
            state.s = rest().s;

        if (p.pin != pinned_as) {
            W = pinned(p.pin);
            pinned_as = p.pin;
        }

        step(state, W, p);
        publish(p);
    });

    sim.OnRestart([&] {
        state = rest();
        Params p;
        {
            std::lock_guard l(shared.lock);
            shared.cuts.clear();
            p = shared.params;
        }
        publish(p);
    });

    sim.SetDt(kDt);
    sim.SetDelayNs(16'666'667); // 60 ticks a second: real time
    sim.Play();

    int tool = 0;
    app.Panel("cloth")
        .Transport(sim)
        .Choice("tool", tool, {"camera", "cut"})
        .Choice("hang from", knobs.pin, {"the top edge", "two corners"})
        .Checkbox("ball", knobs.ball)
        .Button("mend",
                [&] {
                    std::lock_guard l(shared.lock);
                    shared.mend = true;
                })
        .Slider("gravity", knobs.gravity, 0.0f, 20.0f)
        .Slider("tears at x rest", knobs.tear, 1.2f, 4.0f);

    int tool_shown = 0, pin_shown = knobs.pin;
    app.OnFrame([&] {
        {
            std::lock_guard l(shared.lock);
            shared.params = knobs;
        }

        // The tool has two switches, the panel's and the picture's: the
        // one that moved since last frame wins.
        int picture = world.Tool() == sv::Tool::Cut ? 1 : 0;
        if (tool != tool_shown)
            world.Tool(tool ? sv::Tool::Cut : sv::Tool::Camera);
        else
            tool = picture;
        tool_shown = tool;

        if (knobs.pin != pin_shown)
            sim.Restart();
        pin_shown = knobs.pin;
    });

    app.OnKey(sv::Key::Space, [&] { sim.Toggle(); })
        .OnKey(sv::Key::R, [&] { sim.Restart(); })
        .OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    // A stroke that began on the ball carries it; any other cuts every
    // spring whose segment it crosses, tested against the frame's own
    // copy of the positions and queued for the next tick.
    world.OnStroke([&](const sv::Stroke &st) {
        if (st.On(ball)) {
            st.Carry(knobs.ball_at, knobs.ball_at);
            return;
        }

        auto &x = pos.Shown();
        if (x.size() < N * N * 3)
            return;

        std::lock_guard l(shared.lock);
        for (int f = 0; f < 6; ++f) {
            auto [dc, dr] = kReach[f];
            for (int c = 0; c + dc < int(N); ++c)
                for (int r = std::max(0, -dr); r + dr < int(N); ++r) {
                    idx a = idx(c * int(N) + r) * 3;
                    idx b = idx((c + dc) * int(N) + r + dr) * 3;
                    if (st.Crosses(&x[a], &x[b]))
                        shared.cuts.push_back({f, c, r});
                }
        }
    });

    app.Run();
}
