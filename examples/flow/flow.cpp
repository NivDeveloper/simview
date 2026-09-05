// A quarter of a million tracers in a chaotic flow, stepped on the GPU
// and drawn from the buffers tensor evaluated into.
//
// The velocity is the ABC flow (Arnold-Beltrami-Childress), the standard
// analytic example of a steady three-dimensional flow with chaotic
// streamlines:
//
//   u = A sin z + C cos y
//   v = B sin x + A cos z
//   w = C sin y + B cos x
//
// Two properties make it worth drawing. It is EXACTLY divergence-free —
// du/dx + dv/dy + dw/dz cancels term by term, not to round-off — so
// tracers neither pile up nor thin out however long it runs, and what
// you see is mixing rather than an artifact of a leaky integrator. And
// its streamlines are chaotic, so an initially smooth blob of colour is
// stretched and folded into sheets: the picture is structure, not noise.
//
// The colour is a MATERIAL tag, not a state. Each tracer keeps the
// colour of where it started, so folding is visible directly — the
// alternative, colouring by speed, is smooth here too and switches
// live.
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
#include <cmath>
#include <cstddef>
#include <utility>

using namespace tensor;
using namespace tensor::math;
using tensor::indices::i, tensor::indices::n;
using tensor::indices::operator""_c;

using f32 = float;
using idx = size_t;

constexpr idx N = 262144; // tracers
constexpr f32 tau = 6.283185307179586f;
constexpr f32 box = tau; // the flow is 2*pi periodic in every axis

using Vecs = Tensor<f32, N, 3>;
using Ones = Tensor<f32, N>;

namespace {

gpud::Device *device = nullptr;

// Every declaration whose type is Dest<…> evaluates on the ambient device
// (use_device, armed per thread below).
template <typename T> using Dest = Gpu<T>;

// The three unit vectors, so a per-component formula can be written as
// a sum rather than a branch on the component index.
struct Basis {
    Tensor<f32, 3> x{1.0f, 0.0f, 0.0f};
    Tensor<f32, 3> y{0.0f, 1.0f, 0.0f};
    Tensor<f32, 3> z{0.0f, 0.0f, 1.0f};
};

// The ABC velocity at each tracer, as ONE fused expression.
//
// The three formulas differ only in which sines and cosines they pick,
// so each is multiplied by a basis vector and the three are summed —
// which places the component index AFTER the particle index, giving an
// [N][3] result. Free indices take first-appearance order, so leading
// with a component test instead would transpose the answer.
//
// One expression means one pass and one dispatch for the whole
// velocity field; a stage that evaluated the six trig terms separately
// would be ten.
Dest<Vecs> velocity(const Vecs &P, const Basis &e, f32 A, f32 B, f32 C) {
    Dest<Vecs> out = (A * Sin(P[i, 2_c]) + C * Cos(P[i, 1_c])) * e.x[n] +
                     (B * Sin(P[i, 0_c]) + A * Cos(P[i, 2_c])) * e.y[n] +
                     (C * Sin(P[i, 1_c]) + B * Cos(P[i, 0_c])) * e.z[n];
    return out;
}

// Classical RK4. The flow is smooth and the step is well inside its
// stability limit, so the integrator is not the interesting part — it
// is here so the mixing shown is the flow's and not the scheme's.
Dest<Vecs> advance(const Vecs &P, const Basis &e, f32 h, f32 A, f32 B, f32 C) {
    const Dest<Vecs> k1 = velocity(P, e, A, B, C);
    const Dest<Vecs> p2 = P[i, n] + (0.5f * h) * k1[i, n];
    const Dest<Vecs> k2 = velocity(p2, e, A, B, C);
    const Dest<Vecs> p3 = P[i, n] + (0.5f * h) * k2[i, n];
    const Dest<Vecs> k3 = velocity(p3, e, A, B, C);
    const Dest<Vecs> p4 = P[i, n] + h * k3[i, n];
    const Dest<Vecs> k4 = velocity(p4, e, A, B, C);

    const Dest<Vecs> moved =
        P[i, n] +
        (h / 6.0f) * (k1[i, n] + 2.0f * k2[i, n] + 2.0f * k3[i, n] + k4[i, n]);
    // Periodic: the flow repeats every 2*pi, so a tracer leaving one
    // face is the same tracer entering the opposite one.
    Dest<Vecs> out = moved - box * Floor(moved / box);
    return out;
}

} // namespace

int main() {
    sv::App app(
        {.title = "simview — flow (ABC, tensor on gpud)", .size = {1280, 860}});
    if (!app)
        return 1;
    device = &sv::Device(app);
    // The ambient device is per THREAD. This one publishes the initial
    // frames; the executor's thread arms itself on its first tick. Every
    // Dest<…> must be destroyed before app, which owns the device.
    use_device(*device);
    rng::Seed(20260901);
    const Basis basis;

    // A BALL of dye, not a full box. Filling the box gives a solid
    // wall of tracers with nothing to see into; a compact blob stretched
    // by the flow leaves the structure standing in empty space, which
    // is the whole picture. Uniform by VOLUME — cbrt of a uniform, not
    // a uniform radius, or the middle is far denser than the shell.
    constexpr f32 R = 1.15f;
    const Vecs dir = rng::Normal<f32, N, 3>();
    const Ones len = Sqrt(Fmax(fold<1>(dir * dir), 1e-20f));
    const Ones rad = R * Pow(rng::Uniform<f32, N>(), 1.0f / 3.0f);
    const Vecs offset = dir[i, n] / len[i] * rad[i];
    Vecs P = 0.5f * box + offset[i, n];

    // The material tag: where in the ball each tracer began. It never
    // changes, so folding is visible as the colours laminate.
    const Vecs origin = offset[i, n] * (1.0f / R);

    sv::Sync<Vecs> pos, tag;
    // Sync holds HOST buffers the cloud draws, so publishing a frame is a
    // real device-to-host crossing and stays an explicit eval.
    pos.Next() = eval(*device, P[i, n]);
    pos.Publish();
    tag.Next() = eval(*device, origin[i, n]);
    tag.Publish();

    // One const read to sync: the compute backend batches eagerly, so
    // without it the first frames draw an empty box while the first
    // step compiles its kernels.
    (void)pos.Current()[0, 0];
    (void)tag.Current()[0, 0];

    auto world = app.World({.grid = false});
    world
        .Camera({.focus = {box / 2, box / 2, box / 2},
                 .distance = 15.0f,
                 .azimuth_deg = -50.0f,
                 .elevation_deg = 18.0f})
        .Light({.direction = {0.4f, 0.5f, 0.8f}, .intensity = 0.75f})
        .Ambient(0.42f, 0.43f, 0.5f);

    auto tracers = world.Cloud(
        pos,
        {.radius = 0.010f, .map = sv::CloudMap::Components, .map_scale = 1.0f});
    if (!tracers)
        return 1;
    tracers.Colors(tag);

    float A = 1.7320508f, B = 1.4142136f, C = 1.0f, h = 0.02f;
    std::atomic<float> A_now{A}, B_now{B}, C_now{C}, h_now{h};

    sv::Executor sim([&](const sv::Tick &) {
        // The ambient device is per THREAD: the executor's arms itself once.
        static thread_local const bool armed = (use_device(*device), true);
        (void)armed;

        P = advance(P, basis, h_now.load(std::memory_order_relaxed),
                    A_now.load(std::memory_order_relaxed),
                    B_now.load(std::memory_order_relaxed),
                    C_now.load(std::memory_order_relaxed));
        pos.Next() = eval(*device, P[i, n]);
        pos.Publish();
    });
    sim.OnRestart([&] {
        P = 0.5f * box + offset[i, n];
        pos.Next() = eval(*device, P[i, n]);
        pos.Publish();
    });
    sim.SetDt(double(h));
    sim.Play();

    app.Controls(sim);
    app.Panel("abc flow")
        .Text("u = A sin z + C cos y")
        .Text("v = B sin x + A cos z")
        .Text("w = C sin y + B cos x")
        .Separator()
        .Slider("A", A, 0.0f, 3.0f)
        .Slider("B", B, 0.0f, 3.0f)
        .Slider("C", C, 0.0f, 3.0f)
        .Slider("step", h, 0.002f, 0.06f)
        .Separator()
        .Text("262144 tracers, exactly divergence-free");

    app.OnFrame([&] {
        A_now.store(A, std::memory_order_relaxed);
        B_now.store(B, std::memory_order_relaxed);
        C_now.store(C, std::memory_order_relaxed);
        h_now.store(h, std::memory_order_relaxed);
        sim.SetDt(double(h));
    });

    app.OnKey(sv::Key::Space, [&] { sim.Toggle(); })
        .OnKey(sv::Key::R, [&] { sim.Restart(); })
        .OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
}
