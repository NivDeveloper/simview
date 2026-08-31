// Where the time goes, proven real: under SIMVIEW_TIMINGS=1 a frame's
// graphics sections carry stamps that end after they begin and follow
// one another, and gpud's compute batches carry stamps on the SAME
// clock — a batch and a frame lie within seconds of each other on it,
// which a second clock could not promise. A stamp that never landed
// reads as zero, and zero fails every check below.

#include "harness/Harness.h"
#include "probe/Probe.h"

#include <simview/gpud.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

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

std::uint64_t abs_diff(std::uint64_t a, std::uint64_t b) {
    return a > b ? a - b : b - a;
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.headless = true});
    if (!app)
        return check::skip("timing", LastError());
    if (!probe::timings_on(app.Raw()))
        return check::skip("timing", "SIMVIEW_TIMINGS is unset or the queue "
                                     "cannot timestamp");
    gpud::Device &dev = sv::Device(app);
    const gpud::Kernel *fill = nullptr;
    try {
        fill = &dev.compile(fill_src);
    } catch (const std::exception &e) {
        return check::skip("timing", e.what());
    }

    // Compute: 24 dispatches, eagerly batched, on gpud's queue.
    constexpr std::uint32_t N = 1 << 16;
    gpud::Buffer buf = dev.alloc(N * sizeof(float));
    FillScalars sc{1.0f, N};
    gpud::Buffer *bufs[] = {&buf};
    for (int i = 0; i < 24; ++i)
        dev.run(*fill, N / 64,
                {reinterpret_cast<const std::byte *>(&sc), sizeof sc}, bufs);
    dev.submit();
    dev.flush();

    // Graphics: three shots, each a frame that is waited and collected.
    // Shoot until a frame's stamps come back, not a fixed number of
    // times. A driver may publish them AFTER the semaphore the frame's
    // wait returned on — MoltenVK does, from the command buffer's
    // completion handler — so a collect that finds none is a frame
    // that has not been published yet, not a frame that was not
    // stamped. Waiting for them on the hot path is what the engine
    // refuses to do, so the check waits instead, by asking again.
    std::vector<probe::ComputeBatch> batches;
    probe::GpuSection sections[32];
    std::size_t n_sections = 0;
    for (int i = 0; i < 10 && n_sections == 0; ++i) {
        Bmp img;
        CHECK(harness::shot(app, "timing", img));
        n_sections = probe::gpu_sections(app.Raw(), sections, 32);
        probe::ComputeBatch got[64];
        const std::size_t nb = probe::compute_batches(app.Raw(), got, 64);
        batches.insert(batches.end(), got, got + nb);
    }

    // The frame's sections: present, ordered, and each a real span.
    CHECK_GT(n_sections, std::size_t(1));
    for (std::size_t i = 0; i < n_sections; ++i) {
        std::printf("  section %-10s %8.3f ms\n", sections[i].name,
                    double(sections[i].end_ns - sections[i].begin_ns) / 1e6);
        CHECK_GT(sections[i].begin_ns, std::uint64_t(0));
        CHECK(sections[i].end_ns >= sections[i].begin_ns);
        if (i)
            CHECK(sections[i].begin_ns >= sections[i - 1].end_ns);
    }
    if (n_sections)
        CHECK_LT(sections[n_sections - 1].end_ns - sections[0].begin_ns,
                 std::uint64_t(1000000000));

    // The batches: every dispatch accounted for, each a real span.
    std::uint64_t dispatches = 0;
    for (const probe::ComputeBatch &b : batches) {
        std::printf("  batch tickets %llu-%llu (%u dispatches) %8.3f ms\n",
                    static_cast<unsigned long long>(b.first),
                    static_cast<unsigned long long>(b.last), b.dispatches,
                    double(b.end_ns - b.begin_ns) / 1e6);
        CHECK(b.first <= b.last);
        CHECK_GT(b.end_ns, b.begin_ns);
        dispatches += b.dispatches;
    }
    CHECK_EQ(dispatches, std::uint64_t(24));

    // One clock: a batch and a frame of the same second lie within
    // seconds of each other on it.
    if (n_sections && !batches.empty())
        CHECK_LT(abs_diff(batches.front().begin_ns, sections[0].begin_ns),
                 std::uint64_t(30000000000ull));

    return check::summary("timing");
}
