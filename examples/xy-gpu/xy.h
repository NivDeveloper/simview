#pragma once

// The XY model in ANGLE form — complex elements do not lower to the
// device (by design), so the state is the angle field theta and every
// update is trig the slot dialect emits. The field lives resident on
// the device; a frame never copies it to the host.

#include <Tensor/Gen.h>
#include <Tensor/Gpu.h>
#include <Tensor/Math.h>
#include <Tensor/Tensor.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace xy {

constexpr size_t L = 256;
constexpr int side = L;
using Field = tensor::Tensor<float, L, L>;
using Mask = tensor::Tensor<int, L, L>;

constexpr float pi = std::numbers::pi_v<float>;
constexpr float two_pi = 2.0f * pi;

inline auto delta_of(float T) { return std::min(pi, 0.9f + 0.8f * T); }

struct Sim {
    Field theta;
    Mask colour;
    float T = 0.4f;
    int over = 2; // overrelaxation sweeps per frame

    void seed(tensor::SlotDevice &sdev) {
        using namespace tensor;
        auto k = gen::Iota<L, L>(0);
        colour = eval(sdev, (k / side + k % side) % 2);
        randomize(sdev);
    }

    void randomize(tensor::SlotDevice &sdev) {
        using namespace tensor;
        theta = eval(sdev, rng::Uniform<float, L, L>() * two_pi);
    }

    // The neighbour field, one component per eval: every MENTION of a
    // leaf is a binding in the emitted program, so the fully fused
    // update overflows the 16-slot budget — materialize the stencils.
    Field field_x(tensor::SlotDevice &sdev) {
        using namespace tensor;
        using namespace tensor::indices;
        return eval(sdev, math::Cos(theta[wrap(i + 1_c), j]) +
                              math::Cos(theta[wrap(i - 1_c), j]) +
                              math::Cos(theta[i, wrap(j + 1_c)]) +
                              math::Cos(theta[i, wrap(j - 1_c)]));
    }
    Field field_y(tensor::SlotDevice &sdev) {
        using namespace tensor;
        using namespace tensor::indices;
        return eval(sdev, math::Sin(theta[wrap(i + 1_c), j]) +
                              math::Sin(theta[wrap(i - 1_c), j]) +
                              math::Sin(theta[i, wrap(j + 1_c)]) +
                              math::Sin(theta[i, wrap(j - 1_c)]));
    }

    // One checkerboard Metropolis sweep: rotate by a uniform angle in
    // [-delta, delta]; dE against the neighbour field (hx, hy).
    void metropolis(tensor::SlotDevice &sdev) {
        using namespace tensor;
        using namespace tensor::indices;
        const float delta = delta_of(T);
        for (int col = 0; col < 2; ++col) {
            auto ang =
                eval(sdev, (rng::Uniform<float, L, L>() - 0.5f) * (2 * delta));
            auto u = eval(sdev, rng::Uniform<float, L, L>());
            auto hx = field_x(sdev);
            auto hy = field_y(sdev);
            auto thn = eval(sdev, theta + ang);
            auto th = theta[i, j];
            auto tn = thn[i, j];
            auto de = -(math::Cos(tn) - math::Cos(th)) * hx[i, j] -
                      (math::Sin(tn) - math::Sin(th)) * hy[i, j];
            auto acc = (colour[i, j] == col) &&
                       (u[i, j] < math::Exp(de * (-1.0f / T)));
            theta = eval(sdev, where(acc, tn, th));
        }
    }

    // The microcanonical reflection about the local field: no trig
    // identity needed on the host, one Atan2 on the device.
    void overrelax(tensor::SlotDevice &sdev) {
        using namespace tensor;
        using namespace tensor::indices;
        for (int col = 0; col < 2; ++col) {
            auto hx = field_x(sdev);
            auto hy = field_y(sdev);
            auto th = theta[i, j];
            theta = eval(
                sdev, where(colour[i, j] == col,
                            2.0f * math::Atan2(hy[i, j], hx[i, j]) - th, th));
        }
    }

    // theta grows by +-delta per accepted move; rewrap periodically so
    // float precision never becomes the colormap's problem.
    void rewrap(tensor::SlotDevice &sdev) {
        using namespace tensor;
        theta =
            eval(sdev, theta - math::Floor(theta * (1.0f / two_pi)) * two_pi);
    }

    void step(tensor::SlotDevice &sdev) {
        metropolis(sdev);
        for (int r = 0; r < over; ++r)
            overrelax(sdev);
    }
};

} // namespace xy
