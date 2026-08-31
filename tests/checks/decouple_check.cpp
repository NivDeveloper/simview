// The decoupling contract: what a frame shows is a generation some
// Publish finished — never torn, never future — while the producer
// runs free on its own thread and gpud's own queue, and the frame
// orders itself GPU-side on the stamps Publish took. Every shot must
// be UNIFORM at a published value: a broken wait shows a buffer the
// fill dispatch has not reached (zero or garbage), a broken stamp
// shows a torn or future frame.

#include "harness/Harness.h"
#include "probe/Probe.h"

#include <simview/gpud.h>

#include <atomic>
#include <cstdio>
#include <vector>

namespace {

// The "slang-vulkan" dialect by hand, exactly as gpud's conformance
// suite spells it: scalars-then-pointers push constants.
constexpr char fill_src[] = R"(
struct Buf_float { float data[1]; };
struct PC {
  float s0;
  uint s1;
  Buf_float* out_buf;
};
[[vk::push_constant]] PC pc;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  uint i = tid.x;
  if (i >= pc.s1) return;
  pc.out_buf.data[i] = pc.s0;
}
)";

struct FillScalars {
    float s0;
    std::uint32_t s1;
};

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.headless = true});
    if (!app)
        return check::skip("decouple", LastError());
    gpud::Device &dev = sv::Device(app);
    const gpud::Kernel *fill = nullptr;
    try {
        fill = &dev.compile(fill_src);
    } catch (const std::exception &e) {
        return check::skip("decouple", e.what());
    }

    constexpr std::uint32_t W = 32, H = 32, N = W * H;
    Sync<gpud::Buffer> field;
    std::atomic<std::uint32_t> published{0};

    Executor sim([&] {
        const std::uint32_t v = 10 + published.load() % 200;
        gpud::Buffer b = dev.alloc(N * sizeof(float));
        FillScalars sc{float(v), N};
        gpud::Buffer *bufs[] = {&b};
        dev.run(*fill, (N + 63) / 64,
                {reinterpret_cast<const std::byte *>(&sc), sizeof sc}, bufs);
        field.Publish(std::move(b));
        published.fetch_add(1, std::memory_order_release);
    });

    auto f = app.Field(field, {.extent = {W, H}, .lo = 0.0f, .hi = 255.0f});
    REQUIRE(bool(f));
    CHECK_EQ(probe::gate_count(app.Raw()), std::size_t(1));

    sim.Play();
    int shown = 0;
    for (int i = 0; i < 20; ++i) {
        app.Step();
        Bmp img;
        REQUIRE(harness::shot(app, "decouple", img));
        if (!published.load(std::memory_order_acquire))
            continue; // nothing published yet: the letterbox fallback
        // Uniform at a plausible published value — never torn, never
        // a buffer the fill has not reached.
        const int c = img.at(img.w / 2, img.h / 2)[0];
        if (c == 0 && !shown)
            continue; // showing the pre-first-publish empty slot
        ++shown;
        CHECK_GT(c, 9);
        CHECK_LT(c, 211);
        for (auto [x, y] : {std::pair{1u, 1u},
                            {img.w - 2, 1u},
                            {1u, img.h - 2},
                            {img.w - 2, img.h - 2}}) {
            const int e = img.at(x, y)[0];
            CHECK_LT(std::abs(e - c), 3);
        }
    }
    sim.Pause();
    CHECK_GT(shown, 0);
    std::printf("(%u generations published, %d frames judged)\n",
                published.load(), shown);
    return check::summary("decouple");
}
