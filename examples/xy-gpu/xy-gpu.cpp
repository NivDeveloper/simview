// The 2-D XY model, computed by the tensor library on the GPU and
// drawn zero-copy: simview owns the SDL device, the gpud runtime
// ADOPTS it, tensor evaluates through the slot dialect, and the
// fragment shader reads the very buffer the compute wrote — three
// libraries, every boundary named, no copies anywhere.
//
// The model is in ANGLE form — complex elements do not lower to the
// device (by design), so the state is the angle field theta and every
// update is trig the slot dialect emits. It lives resident on the
// device; a frame never copies it to the host.
//
// Space pauses, Up/Down move temperature, R reseeds, Esc quits.
#include <simview/native.h>
#include <simview/simview.h>

#include <Tensor/Gen.h>
#include <Tensor/Gpu.h>
#include <Tensor/Math.h>
#include <Tensor/Tensor.h>

#include <gpud/Sdl.h>

#include <algorithm>
#include <cmath>
#include <numbers>

constexpr size_t L = 256;
constexpr int side = L;
using Field = tensor::Tensor<float, L, L>;
using Mask = tensor::Tensor<int, L, L>;

constexpr float pi = std::numbers::pi_v<float>;
constexpr float two_pi = 2.0f * pi;

float delta_of(float T) { return std::min(pi, 0.9f + 0.8f * T); }

Mask checkerboard(tensor::SlotDevice &sdev) {
    using namespace tensor;
    auto k = gen::Iota<L, L>(0);
    return eval(sdev, (k / side + k % side) % 2);
}

Field random_angles(tensor::SlotDevice &sdev) {
    using namespace tensor;
    return eval(sdev, rng::Uniform<float, L, L>() * two_pi);
}

// The neighbour field, one component per eval: every MENTION of a
// leaf is a binding in the emitted program, so the fully fused
// update overflows the 16-slot budget — materialize the stencils.
Field field_x(tensor::SlotDevice &sdev, const Field &theta) {
    using namespace tensor;
    using namespace tensor::indices;
    return eval(sdev, math::Cos(theta[wrap(i + 1_c), j]) +
                          math::Cos(theta[wrap(i - 1_c), j]) +
                          math::Cos(theta[i, wrap(j + 1_c)]) +
                          math::Cos(theta[i, wrap(j - 1_c)]));
}
Field field_y(tensor::SlotDevice &sdev, const Field &theta) {
    using namespace tensor;
    using namespace tensor::indices;
    return eval(sdev, math::Sin(theta[wrap(i + 1_c), j]) +
                          math::Sin(theta[wrap(i - 1_c), j]) +
                          math::Sin(theta[i, wrap(j + 1_c)]) +
                          math::Sin(theta[i, wrap(j - 1_c)]));
}

// One checkerboard Metropolis sweep: rotate by a uniform angle in
// [-delta, delta]; dE against the neighbour field (hx, hy).
void metropolis(tensor::SlotDevice &sdev, Field &theta, const Mask &colour,
                float T) {
    using namespace tensor;
    using namespace tensor::indices;
    const float delta = delta_of(T);
    for (int col = 0; col < 2; ++col) {
        auto ang =
            eval(sdev, (rng::Uniform<float, L, L>() - 0.5f) * (2 * delta));
        auto u = eval(sdev, rng::Uniform<float, L, L>());
        auto hx = field_x(sdev, theta);
        auto hy = field_y(sdev, theta);
        auto thn = eval(sdev, theta + ang);
        auto th = theta[i, j];
        auto tn = thn[i, j];
        auto de = -(math::Cos(tn) - math::Cos(th)) * hx[i, j] -
                  (math::Sin(tn) - math::Sin(th)) * hy[i, j];
        auto acc =
            (colour[i, j] == col) && (u[i, j] < math::Exp(de * (-1.0f / T)));
        theta = eval(sdev, where(acc, tn, th));
    }
}

// The microcanonical reflection about the local field: no trig
// identity needed on the host, one Atan2 on the device.
void overrelax(tensor::SlotDevice &sdev, Field &theta, const Mask &colour) {
    using namespace tensor;
    using namespace tensor::indices;
    for (int col = 0; col < 2; ++col) {
        auto hx = field_x(sdev, theta);
        auto hy = field_y(sdev, theta);
        auto th = theta[i, j];
        theta =
            eval(sdev, where(colour[i, j] == col,
                             2.0f * math::Atan2(hy[i, j], hx[i, j]) - th, th));
    }
}

// theta grows by +-delta per accepted move; rewrap periodically so
// float precision never becomes the colormap's problem.
void rewrap(tensor::SlotDevice &sdev, Field &theta) {
    using namespace tensor;
    theta = eval(sdev, theta - math::Floor(theta * (1.0f / two_pi)) * two_pi);
}

// One frame of dynamics: a Metropolis sweep, then two microcanonical
// sweeps to speed decorrelation.
void step(tensor::SlotDevice &sdev, Field &theta, const Mask &colour, float T) {
    metropolis(sdev, theta, colour, T);
    overrelax(sdev, theta, colour);
    overrelax(sdev, theta, colour);
}

int main() {
    sv::App app({.title = "simview — xy (tensor on gpud)", .size = {768, 768}});
    if (!app)
        return 1;

    // Adoption: gpud computes on simview's device.
    auto dev = gpud::sdl::try_open_on(sv::NativeDevice(app));
    if (!dev)
        return 1;
    tensor::SlotDevice sdev{*dev};

    // The state, resident on the device for the whole run.
    Mask colour = checkerboard(sdev);
    Field theta = random_angles(sdev);
    float T = 0.4f;

    auto field = sv::FieldFromBuffer(
        app, gpud::sdl::native_buffer(*tensor::resident_buffer(theta)),
        {.extent = {L, L}, .map = sv::Colormap::Hue, .lo = 0.0f, .hi = two_pi});

    bool paused = false;
    std::uint64_t frame = 0;
    app.OnFrame([&] {
        if (paused)
            return;
        step(sdev, theta, colour, T);
        if (++frame % 64 == 0)
            rewrap(sdev, theta);
        // Residency ping-pongs: rebind the freshest buffer each frame.
        sv::FieldRebind(
            field, gpud::sdl::native_buffer(*tensor::resident_buffer(theta)));
    });

    app.OnKey(sv::Key::Space, [&] { paused = !paused; });
    app.OnKey(sv::Key::Up, [&] { T = std::min(2.0f, T + 0.05f); });
    app.OnKey(sv::Key::Down, [&] { T = std::max(0.05f, T - 0.05f); });
    app.OnKey(sv::Key::R, [&] { theta = random_angles(sdev); });
    app.OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
    // Teardown is the lifetime rule: the parked tensors die before
    // sdev and dev, and app — the device's owner — goes last, in
    // reverse declaration order.
}
