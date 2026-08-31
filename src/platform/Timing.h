#pragma once

// Internal to src/ — where the GPU time goes, on ONE clock.
//
// A frame's graphics work is stamped by section (views, scene, ui,
// readback) with timestamp queries on the frame's own command buffer;
// gpud stamps every compute batch it submits (Options::profile). Both
// read the device clock, so the two queues lay out on one timeline —
// the picture that says whether compute is saturated, whether the
// frame waits on it, and what a frame's milliseconds are spent on.
//
// Three sinks, one mechanism: the periodic line SIMVIEW_TIMINGS=1
// logs, the probe a check reads, and — in a SIMVIEW_TRACE build —
// Tracy's GPU zones, where the flame graph is. Off unless asked: a
// stamp pair per section is cheap, but nothing runs that nobody
// wanted.

#include <gpud/Device.h>
#include <nvrhi/nvrhi.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace sv {
namespace impl {

struct Platform;

struct GpuSection {
    const char *name = nullptr;
    std::uint64_t begin_ns = 0, end_ns = 0; // device clock
};

struct Timing {
    static constexpr std::uint32_t kMaxSections = 32;

    static constexpr std::uint32_t kRings = 2;

    bool on = false;        // stamping: SIMVIEW_TIMINGS=1 or a trace build
    bool log = false;       // SIMVIEW_TIMINGS=1: the once-a-second line
    std::uint64_t pool = 0; // VkQueryPool: kRings x two stamps per section
    float period = 1.0f;    // ns per tick on the graphics family

    // The frame being recorded: which ring it stamps into, the sections
    // stamped so far, and whether one is open. A ring per frame in
    // flight plus one: a driver may publish a completed frame's stamps
    // AFTER the semaphore the wait returned on (MoltenVK does, from the
    // completion handler), so a ring is read at the collect after the
    // one that found it unready, and reset only when the next frame
    // needs it back.
    struct Ring {
        std::uint32_t written = 0;
        const char *names[kMaxSections]{};
        bool unread = false;
    } rings[kRings];
    std::uint32_t ring = 0; // the one the open frame stamps into
    bool open = false;

    // What the last collect read: the previous frame's sections and
    // the compute batches gpud had completed by then.
    std::vector<GpuSection> last;
    std::vector<gpud::Device::BatchTiming> batches;

    // The line's accumulators, reset each time it prints.
    std::chrono::steady_clock::time_point last_line{};
    std::chrono::steady_clock::time_point last_collect{};
    unsigned frames = 0;
    double frame_ms = 0;
    double section_ms[kMaxSections]{};
    const char *section_names[kMaxSections]{};
    std::uint32_t section_count = 0;
    std::uint64_t batch_count = 0, dispatch_count = 0;
    double batch_ms = 0;
};

} // namespace impl

// Whether stamping is wanted at all — read before gpud is adopted, so
// its Options::profile can match.
bool timing_wanted();

// The pool and the trace contexts; refuses (by log line, never an
// error) where the graphics family cannot timestamp.
void timing_init(impl::Platform &);
void timing_quit(impl::Platform &);

// Right after the frame's command list opens: the ring is reset
// in-stream, before any render pass.
void timing_frame_open(impl::Platform &, nvrhi::ICommandList *);

// A section: a debug label (what a capture names) always, a stamp
// pair when stamping is on. Sections do not nest.
void timing_begin(impl::Platform &, nvrhi::ICommandList *, const char *name);
void timing_end(impl::Platform &, nvrhi::ICommandList *);

// After the frame's wait: read its sections, drain gpud's batches,
// feed every sink.
void timing_collect(impl::Platform &);

} // namespace sv
