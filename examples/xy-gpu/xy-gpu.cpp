// The 2-D XY model, computed by the tensor library on the GPU and
// drawn zero-copy: simview owns the SDL device, the gpud runtime
// ADOPTS it, tensor evaluates through the slot dialect, and the
// fragment shader reads the very buffer the compute wrote — three
// libraries, every boundary named, no copies anywhere.
//
// Space pauses, Up/Down move temperature, R reseeds, Esc quits.
#include "xy.h"

#include <simview/native.h>
#include <simview/simview.h>

#include <gpud/Sdl.h>

int main() {
    sv::App app({.title = "simview — xy (tensor on gpud)", .size = {768, 768}});
    if (!app) return 1;

    // Adoption: gpud computes on simview's device.
    auto dev = gpud::sdl::try_open_on(sv::NativeDevice(app));
    if (!dev) return 1;
    tensor::SlotDevice sdev{*dev};

    // The sim state lives resident on the device for the whole run.
    xy::Sim sim;
    sim.seed(sdev);

    auto field = sv::FieldFromBuffer(
        app, gpud::sdl::native_buffer(*tensor::resident_buffer(sim.theta)),
        {.extent = {xy::L, xy::L}, .map = sv::Colormap::Hue,
         .lo = 0.0f, .hi = xy::two_pi});

    bool paused = false;
    std::uint64_t frame = 0;
    app.OnFrame([&] {
        if (paused) return;
        sim.step(sdev);
        if (++frame % 64 == 0) sim.rewrap(sdev);
        // Residency ping-pongs: rebind the freshest buffer each frame.
        sv::FieldRebind(field, gpud::sdl::native_buffer(
                                   *tensor::resident_buffer(sim.theta)));
    });

    app.OnKey(sv::Key::Space, [&] { paused = !paused; });
    app.OnKey(sv::Key::Up, [&] { sim.T = std::min(2.0f, sim.T + 0.05f); });
    app.OnKey(sv::Key::Down, [&] { sim.T = std::max(0.05f, sim.T - 0.05f); });
    app.OnKey(sv::Key::R, [&] { sim.randomize(sdev); });
    app.OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
    // Teardown order is the lifetime rule: the sim's parked tensors
    // (device handles) die with `sim` before `dev`, and `app` — the
    // device's owner — goes last, in reverse declaration order.
}
