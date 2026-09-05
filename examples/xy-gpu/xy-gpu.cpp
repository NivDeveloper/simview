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
#include <simview/gpud.h>
#include <simview/simview.h>

#include <Tensor/Gen.h>
#include <Tensor/Gpu.h>
#include <Tensor/Math.h>
#include <Tensor/Tensor.h>

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

tensor::Gpu<Mask> checkerboard() {
    using namespace tensor;
    auto k = gen::Iota<L, L>(0);
    Gpu<Mask> out = (k / side + k % side) % 2;
    return out;
}

tensor::Gpu<Field> random_angles() {
    using namespace tensor;
    Gpu<Field> out = rng::Uniform<float, L, L>() * two_pi;
    return out;
}

// The neighbour field, one component per eval: every MENTION of a
// leaf is a binding in the emitted program, so the fully fused
// update overflows the 16-slot budget — materialize the stencils.
tensor::Gpu<Field> field_x(const Field &theta) {
    using namespace tensor;
    using namespace tensor::indices;
    Gpu<Field> out = math::Cos(theta[wrap(i + 1_c), j]) +
                     math::Cos(theta[wrap(i - 1_c), j]) +
                     math::Cos(theta[i, wrap(j + 1_c)]) +
                     math::Cos(theta[i, wrap(j - 1_c)]);
    return out;
}
tensor::Gpu<Field> field_y(const Field &theta) {
    using namespace tensor;
    using namespace tensor::indices;
    Gpu<Field> out = math::Sin(theta[wrap(i + 1_c), j]) +
                     math::Sin(theta[wrap(i - 1_c), j]) +
                     math::Sin(theta[i, wrap(j + 1_c)]) +
                     math::Sin(theta[i, wrap(j - 1_c)]);
    return out;
}

// One checkerboard Metropolis sweep: rotate by a uniform angle in
// [-delta, delta]; dE against the neighbour field (hx, hy).
void metropolis(tensor::Gpu<Field> &theta, const Mask &colour, float T) {
    using namespace tensor;
    using namespace tensor::indices;
    const float delta = delta_of(T);
    for (int col = 0; col < 2; ++col) {
        const Gpu<Field> ang =
            (rng::Uniform<float, L, L>() - 0.5f) * (2 * delta);
        const Gpu<Field> u = rng::Uniform<float, L, L>();
        const Gpu<Field> hx = field_x(theta);
        const Gpu<Field> hy = field_y(theta);
        const Gpu<Field> thn = theta + ang;
        auto th = theta[i, j];
        auto tn = thn[i, j];
        auto de = -(math::Cos(tn) - math::Cos(th)) * hx[i, j] -
                  (math::Sin(tn) - math::Sin(th)) * hy[i, j];
        auto acc =
            (colour[i, j] == col) && (u[i, j] < math::Exp(de * (-1.0f / T)));
        theta = where(acc, tn, th);
    }
}

// The microcanonical reflection about the local field: no trig
// identity needed on the host, one Atan2 on the device.
void overrelax(tensor::Gpu<Field> &theta, const Mask &colour) {
    using namespace tensor;
    using namespace tensor::indices;
    for (int col = 0; col < 2; ++col) {
        const Gpu<Field> hx = field_x(theta);
        const Gpu<Field> hy = field_y(theta);
        auto th = theta[i, j];
        theta = where(colour[i, j] == col,
                      2.0f * math::Atan2(hy[i, j], hx[i, j]) - th, th);
    }
}

// theta grows by +-delta per accepted move; rewrap periodically so
// float precision never becomes the colormap's problem.
void rewrap(tensor::Gpu<Field> &theta) {
    using namespace tensor;
    theta = theta - math::Floor(theta * (1.0f / two_pi)) * two_pi;
}

// One frame of dynamics: a Metropolis sweep, then two microcanonical
// sweeps to speed decorrelation.
void step(tensor::Gpu<Field> &theta, const Mask &colour, float T) {
    metropolis(theta, colour, T);
    overrelax(theta, colour);
    overrelax(theta, colour);
}

int main() {
    sv::App app({.title = "simview — xy (tensor on gpud)", .size = {768, 768}});
    if (!app)
        return 1;

    // Everything here runs on the render thread, so one arming does. The
    // device is BORROWED: every Gpu<…> below must die before app, which
    // owns it — the teardown note at the bottom is that rule.
    tensor::use_device(sv::Device(app));

    // The state, resident on the device for the whole run.
    const tensor::Gpu<Mask> colour = checkerboard();
    tensor::Gpu<Field> theta = random_angles();
    float T = 0.4f;

    // One registration; the engine pulls the freshest resident buffer
    // at every draw, however often eval re-parks theta.
    auto field = app.Field(
        theta,
        {.extent = {L, L}, .map = sv::Colormap::Hue, .lo = 0.0f, .hi = two_pi});
    if (!field)
        return 1;

    // The bare pull, on the render thread: the sim steps inside the
    // frame callback, so nothing re-parks theta while a frame reads
    // it. A sim on its own thread wraps its state in sv::Sync instead
    // — examples/ising is that shape.
    bool paused = false;
    std::uint64_t frame = 0;
    app.OnFrame([&] {
        if (paused)
            return;
        step(theta, colour, T);
        if (++frame % 64 == 0)
            rewrap(theta);
    });

    app.OnKey(sv::Key::Space, [&] { paused = !paused; })
        .OnKey(sv::Key::Up, [&] { T = std::min(2.0f, T + 0.05f); })
        .OnKey(sv::Key::Down, [&] { T = std::max(0.05f, T - 0.05f); })
        .OnKey(sv::Key::R, [&] { theta = random_angles(); })
        .OnKey(sv::Key::Escape, [&] { app.RequestQuit(); });

    app.Run();
    // Teardown is the lifetime rule: the parked tensors (the field's
    // pull source points into theta) die before app — the device's
    // owner — in reverse declaration order.
}
