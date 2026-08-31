// The flagship: tensor's own BGK example, stepped on the GPU and drawn
// in 3-D without the particles ever reaching the host.
//
// Two discs of test particles collide off-axis and thermalize. Per
// step the sim streams them, measures n/p/E per cell, histograms the
// momenta, relaxes toward the Maxwellian by the relaxation-time
// approximation, resamples, then shifts and scales so the cell's
// momentum and energy come out unchanged. Thirty-odd fused
// expressions, every one of them evaluated on the device.
//
// The physics is INCLUDED, not copied. tensor's example defines
// BGK_NO_MAIN for exactly this — its own benchmark includes the file
// the same way — so what runs here is that example's step(), and it
// cannot drift from it.
//
// What the flagship is actually showing is the seam: the positions
// tensor evaluates into are the buffers the vertex shader reads.
// simview owns the device, tensor evaluates on it, and a frame copies
// nothing. The sim runs on the Executor's thread and the frame on the
// main one; they share the state through sv::Sync, which is what
// makes "no copy" also mean "no lock and no torn read".
//
// Space toggles, Up/Down move the relaxation time, R restarts,
// Esc quits.
#define BGK_NO_MAIN
#include BGK_SOURCE // tensor's examples/bgk/bgk.cpp, path from CMake

#include <simview/gpud.h>
#include <simview/simview.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace {

// The view's copy of a state tensor, made ON THE DEVICE: one identity
// kernel over 24K floats, not a round trip through the host.
//
// It exists because tensor's step() owns its state by reference and
// hands back the same two tensors every step, while a Sync slot wants
// a value it can keep while the sim moves on. Two cheap kernels a step
// is what that costs, and it buys the physics above being the
// example's own rather than a transcription of it here.
void publish(sv::Sync<Vecs> &s, const Vecs &v) {
    using tensor::indices::i, tensor::indices::n;
    s.Next() = go(v[i, n]);
    s.Publish();
}

} // namespace

int main() {
    sv::App app(
        {.title = "simview — bgk (tensor on gpud)", .size = {1200, 820}});
    if (!app)
        return 1;

    // tensor's example reaches for this pointer in every go(); simview
    // owns the device, so this is the whole of the handshake.
    device = &sv::Device(app);

    tensor::rng::Seed(20260814);
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
