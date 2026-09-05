// The 2-D Ising model, computed by the tensor library on the GPU and
// drawn zero-copy: simview owns the device, tensor evaluates on it,
// and the field reads the very buffer the compute wrote. The spin
// lattice lives resident on the device for the whole run; a frame
// never copies it to the host.
//
// The spins are a float field of +-1 (the device lowers no bool
// buffers). One tick is one checkerboard Metropolis sweep: every cell
// of a colour flips together with probability min(1, exp(-dE/T)),
// dE = 2 s sum(neighbours), which is one fused expression.
//
// The sim runs on the Executor's thread and the frame on the main
// one, and they share the lattice through sv::Sync: three slots with
// roles, the sim reading Current() and publishing a fresh lattice
// each sweep, the field drawing Shown(), which the frame flips once
// and the sim never writes. No copy, no lock around the data, and no
// buffer re-parked under a reader.
//
// Space toggles, Up/Down move temperature, R restarts, Esc quits —
// through the Executor, as the transport panel does.
#include <simview/gpud.h>
#include <simview/simview.h>

#include <Tensor/Gen.h>
#include <Tensor/Gpu.h>
#include <Tensor/Math.h>
#include <Tensor/Tensor.h>

#include <algorithm>
#include <atomic>

constexpr size_t L = 256;
constexpr int side = L;
using Spins = tensor::Tensor<float, L, L>;
using Mask = tensor::Tensor<int, L, L>;

tensor::Gpu<Mask> checkerboard() {
    using namespace tensor;
    auto k = gen::Iota<L, L>(0);
    Gpu<Mask> out = (k / side + k % side) % 2;
    return out;
}

// A random +-1 lattice: a fresh uniform draw, thresholded.
tensor::Gpu<Spins> random_spins() {
    using namespace tensor;
    using namespace tensor::indices;
    const Gpu<Spins> u = rng::Uniform<float, L, L>();
    Gpu<Spins> out = where(u[i, j] < 0.5f, -1.0f, 1.0f);
    return out;
}

// The neighbour sum, materialized: each mention of a leaf is a slot in
// the emitted program, and the fully fused update would overflow the
// budget — the same reason xy-gpu materializes its stencils.
tensor::Gpu<Spins> neighbours(const Spins &s) {
    using namespace tensor;
    using namespace tensor::indices;
    Gpu<Spins> out = s[wrap(i + 1_c), j] + s[wrap(i - 1_c), j] +
                     s[i, wrap(j + 1_c)] + s[i, wrap(j - 1_c)];
    return out;
}

// One colour of a Metropolis sweep: a fused accept-or-keep over the
// whole lattice, reading one lattice and producing the next.
tensor::Gpu<Spins> pass(const Spins &s, const Mask &colour, int col, float T) {
    using namespace tensor;
    using namespace tensor::indices;
    const Gpu<Spins> u = rng::Uniform<float, L, L>();
    const Gpu<Spins> nn = neighbours(s);
    auto de = 2.0f * s[i, j] * nn[i, j];
    auto acc = (colour[i, j] == col) && (u[i, j] < math::Exp(de * (-1.0f / T)));
    Gpu<Spins> out = where(acc, -s[i, j], s[i, j]);
    return out;
}

// One checkerboard sweep: both colours.
tensor::Gpu<Spins> sweep(const Spins &s, const Mask &colour, float T) {
    return pass(pass(s, colour, 0, T), colour, 1, T);
}

int main() {
    sv::App app(
        {.title = "simview — ising (tensor on gpud)", .size = {768, 768}});
    if (!app)
        return 1;

    // The device is BORROWED and ambient per thread: this one builds the
    // colour mask and the initial lattice, and the sim thread arms itself
    // below. Every Gpu<…> must be destroyed before app, which owns it.
    tensor::use_device(sv::Device(app));
    const tensor::Gpu<Mask> colour = checkerboard();

    // The state: three resident lattices the sim and the frame share
    // by role. Publish the initial condition before anyone reads.
    sv::Sync<Spins> spins;
    spins.Publish(random_spins());

    // The slider moves a plain float on the main thread; the frame
    // publishes it to the atomic the sim thread reads.
    float temperature = 2.269f; // the critical point
    std::atomic<float> T{temperature};

    // One registration: the field draws whichever lattice the frame
    // flipped to, and the frame flips once, first thing.
    auto field = app.Field(spins, {.extent = {L, L}, .lo = -1.0f, .hi = 1.0f});
    if (!field)
        return 1;

    // The sim on its own thread. The Executor keeps the clock; one
    // tick is one sweep, evaluated on the device from the lattice the
    // sim published last.
    sv::Executor sim([&](const sv::Tick &) {
        // The ambient device is per THREAD, so the executor's arms itself
        // once, on its first tick.
        static thread_local const bool armed =
            (tensor::use_device(sv::Device(app)), true);
        (void)armed;

        spins.Publish(
            sweep(spins.Current(), colour, T.load(std::memory_order_relaxed)));
    });
    // Restart re-randomises on the worker, between ticks — it can never
    // overlap a sweep.
    sim.OnRestart([&] { spins.Publish(random_spins()); });
    sim.Play();

    app.Controls(sim).Slider("temperature", temperature, 0.05f, 4.5f);

    app.OnFrame([&] { T.store(temperature, std::memory_order_relaxed); });

    app.OnKey(sv::Key::Space, [&] { sim.Toggle(); })
        .OnKey(sv::Key::Up,
               [&] { temperature = std::min(4.5f, temperature + 0.05f); })
        .OnKey(sv::Key::Down,
               [&] { temperature = std::max(0.05f, temperature - 0.05f); })
        .OnKey(sv::Key::R, [&] { sim.Restart(); })
        .OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
    // Teardown is the lifetime rule: the Executor (which writes spins)
    // dies before spins, which dies before app — the device's owner —
    // in reverse declaration order.
}
