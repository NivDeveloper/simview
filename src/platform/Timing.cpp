#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include "Timing.h"

#include "../core/Trace.h"
#include "Device.h"

#include <SDL3/SDL.h>
#include <nvrhi/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sv {
namespace {

VkCommandBuffer raw(nvrhi::ICommandList *cl) {
    return static_cast<VkCommandBuffer>(
        cl->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer).pointer);
}

VkQueryPool pool_of(const impl::Timing &t) {
    return reinterpret_cast<VkQueryPool>(t.pool);
}

// One stamp on the graphics queue, submitted and waited: "now" on the
// device clock, for the trace contexts' calibration. Uses the section
// pool's first pair before any frame has.
std::uint64_t gpu_now_ns(impl::Platform &pl) {
    impl::Timing &t = pl.timing;
    nvrhi::CommandListHandle cl = pl.ndev->createCommandList();
    cl->open();
    VkCommandBuffer cmd = raw(cl);
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdResetQueryPool(cmd, pool_of(t), 0, 1);
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdWriteTimestamp(
        cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_of(t), 0);
    cl->close();
    platform_execute(pl, cl);
    platform_wait_graphics(pl, pl.gfx_last, "the clock calibration");
    std::uint64_t ticks = 0;
    VULKAN_HPP_DEFAULT_DISPATCHER.vkGetQueryPoolResults(
        pl.vk.device, pool_of(t), 0, 1, sizeof ticks, &ticks, sizeof ticks,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    return std::uint64_t(double(ticks) * double(t.period));
}

#if SIMVIEW_TRACE
// Tracy's GPU protocol, spoken directly — the same items its Vulkan
// header emits, minus that header's own query pool, because the
// stamps here come from two places (the frame's ring and gpud's
// batches) and one emitter serves both. A context per queue; both
// share the one calibration, since both read the device clock.
struct TraceGpu {
    bool ready = false;
    std::uint8_t gfx = 0, compute = 0;
    std::uint16_t gfx_query = 0, compute_query = 0;
};
TraceGpu g_trace;

std::uint8_t trace_context(const char *name, std::int64_t tcpu,
                           std::int64_t tgpu) {
    const auto id =
        tracy::GetGpuCtxCounter().fetch_add(1, std::memory_order_relaxed);
    {
        auto *item = tracy::Profiler::QueueSerial();
        tracy::MemWrite(&item->hdr.type, tracy::QueueType::GpuNewContext);
        tracy::MemWrite(&item->gpuNewContext.cpuTime, tcpu);
        tracy::MemWrite(&item->gpuNewContext.gpuTime, tgpu);
        std::memset(&item->gpuNewContext.thread, 0,
                    sizeof item->gpuNewContext.thread);
        tracy::MemWrite(&item->gpuNewContext.period, 1.0f); // stamps in ns
        tracy::MemWrite(&item->gpuNewContext.context, id);
        tracy::MemWrite(&item->gpuNewContext.flags, tracy::GpuContextFlags(0));
        tracy::MemWrite(&item->gpuNewContext.type,
                        tracy::GpuContextType::Vulkan);
        tracy::GetProfiler().DeferItem(*item);
        tracy::Profiler::QueueSerialFinish();
    }
    {
        const auto len = std::uint16_t(std::strlen(name));
        auto *ptr = static_cast<char *>(tracy::tracy_malloc(len));
        std::memcpy(ptr, name, len);
        auto *item = tracy::Profiler::QueueSerial();
        tracy::MemWrite(&item->hdr.type, tracy::QueueType::GpuContextName);
        tracy::MemWrite(&item->gpuContextNameFat.context, id);
        tracy::MemWrite(&item->gpuContextNameFat.ptr,
                        reinterpret_cast<std::uint64_t>(ptr));
        tracy::MemWrite(&item->gpuContextNameFat.size, len);
        tracy::GetProfiler().DeferItem(*item);
        tracy::Profiler::QueueSerialFinish();
    }
    return id;
}

// One GPU zone, complete: begin and end in the serial stream, then
// the two stamps that place them. The name is an ALLOCATED source
// location, one per zone: the client frees it after sending, so a
// cached one would be read after free — the begin item's type is what
// tells the client the pointer is its own to free.
void trace_zone(std::uint8_t ctx, std::uint16_t &next, const char *name,
                std::size_t name_len, std::uint64_t begin_ns,
                std::uint64_t end_ns) {
    const std::uint16_t q0 = next++, q1 = next++;
    const std::uint64_t srcloc = tracy::Profiler::AllocSourceLocation(
        __LINE__, __FILE__, "gpu", name, name_len);
    {
        auto *item = tracy::Profiler::QueueSerial();
        tracy::MemWrite(&item->hdr.type,
                        tracy::QueueType::GpuZoneBeginAllocSrcLocSerial);
        tracy::MemWrite(&item->gpuZoneBegin.cpuTime,
                        tracy::Profiler::GetTime());
        tracy::MemWrite(&item->gpuZoneBegin.srcloc, srcloc);
        tracy::MemWrite(&item->gpuZoneBegin.thread, tracy::GetThreadHandle());
        tracy::MemWrite(&item->gpuZoneBegin.queryId, q0);
        tracy::MemWrite(&item->gpuZoneBegin.context, ctx);
        tracy::Profiler::QueueSerialFinish();
    }
    {
        auto *item = tracy::Profiler::QueueSerial();
        tracy::MemWrite(&item->hdr.type, tracy::QueueType::GpuZoneEndSerial);
        tracy::MemWrite(&item->gpuZoneEnd.cpuTime, tracy::Profiler::GetTime());
        tracy::MemWrite(&item->gpuZoneEnd.thread, tracy::GetThreadHandle());
        tracy::MemWrite(&item->gpuZoneEnd.queryId, q1);
        tracy::MemWrite(&item->gpuZoneEnd.context, ctx);
        tracy::Profiler::QueueSerialFinish();
    }
    for (const auto [q, ns] :
         {std::pair{q0, begin_ns}, std::pair{q1, end_ns}}) {
        auto *item = tracy::Profiler::QueueSerial();
        tracy::MemWrite(&item->hdr.type, tracy::QueueType::GpuTime);
        tracy::MemWrite(&item->gpuTime.gpuTime, std::int64_t(ns));
        tracy::MemWrite(&item->gpuTime.queryId, q);
        tracy::MemWrite(&item->gpuTime.context, ctx);
        tracy::Profiler::QueueSerialFinish();
    }
}

void trace_init(impl::Platform &pl) {
    const std::uint64_t tgpu = gpu_now_ns(pl);
    const std::int64_t tcpu = tracy::Profiler::GetTime();
    g_trace.gfx = trace_context("graphics", tcpu, std::int64_t(tgpu));
    g_trace.compute = trace_context("compute (gpud)", tcpu, std::int64_t(tgpu));
    g_trace.ready = true;
}

void trace_sections(const std::vector<impl::GpuSection> &sections) {
    if (!g_trace.ready || !tracy::GetProfiler().IsConnected())
        return;
    for (const impl::GpuSection &s : sections)
        trace_zone(g_trace.gfx, g_trace.gfx_query, s.name, std::strlen(s.name),
                   s.begin_ns, s.end_ns);
}

void trace_batches(const std::vector<gpud::Device::BatchTiming> &batches) {
    if (!g_trace.ready || !tracy::GetProfiler().IsConnected())
        return;
    // Named by dispatch count, so the statistics view groups batches
    // of a size; the ticket range is in the probe, not the name.
    for (const auto &b : batches) {
        char name[32];
        const int len =
            std::snprintf(name, sizeof name, "gpud batch x%u", b.dispatches);
        trace_zone(g_trace.compute, g_trace.compute_query, name,
                   std::size_t(len), b.gpu_begin_ns, b.gpu_end_ns);
    }
}
#else
void trace_init(impl::Platform &) {}
void trace_sections(const std::vector<impl::GpuSection> &) {}
void trace_batches(const std::vector<gpud::Device::BatchTiming> &) {}
#endif

void log_line(impl::Timing &t) {
    std::string gfx;
    for (std::uint32_t i = 0; i < t.section_count; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s%s %.2f ms", i ? ", " : "",
                      t.section_names[i], t.section_ms[i] / t.frames);
        gfx += buf;
    }
    const auto interval_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - t.last_line)
                                 .count();
    SDL_Log("simview timings: %u frames, %.1f ms/frame | graphics: %s | "
            "compute: %llu batches (%llu dispatches), %.3f ms/batch, busy "
            "%.0f%%",
            t.frames, t.frames ? t.frame_ms / t.frames : 0.0,
            gfx.empty() ? "-" : gfx.c_str(),
            static_cast<unsigned long long>(t.batch_count),
            static_cast<unsigned long long>(t.dispatch_count),
            t.batch_count ? t.batch_ms / double(t.batch_count) : 0.0,
            interval_ms > 0 ? 100.0 * t.batch_ms / interval_ms : 0.0);
    t.frames = 0;
    t.frame_ms = 0;
    for (double &ms : t.section_ms)
        ms = 0;
    t.section_count = 0;
    t.batch_count = t.dispatch_count = 0;
    t.batch_ms = 0;
    t.last_line = std::chrono::steady_clock::now();
}

} // namespace

bool timing_wanted() {
    const char *env = std::getenv("SIMVIEW_TIMINGS");
    return kTraceBuild || (env && *env == '1');
}

void timing_init(impl::Platform &pl) {
    impl::Timing &t = pl.timing;
    const char *env = std::getenv("SIMVIEW_TIMINGS");
    t.log = env && *env == '1';
    t.on = timing_wanted();
    if (!t.on)
        return;
    if (!pl.vk.timestamps) {
        SDL_Log("simview: timings requested, but the graphics queue family "
                "has no timestamps — sections off");
        t.on = false;
        return;
    }
    t.period = pl.vk.timestamp_period;
    VkQueryPoolCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qci.queryCount = impl::Timing::kRings * 2 * impl::Timing::kMaxSections;
    VkQueryPool pool{};
    if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateQueryPool(
            pl.vk.device, &qci, nullptr, &pool) != VK_SUCCESS) {
        SDL_Log("simview: timings requested, but vkCreateQueryPool failed");
        t.on = false;
        return;
    }
    t.pool = reinterpret_cast<std::uint64_t>(pool);
    t.last_line = t.last_collect = std::chrono::steady_clock::now();
    trace_init(pl);
}

void timing_quit(impl::Platform &pl) {
    impl::Timing &t = pl.timing;
    if (t.pool)
        VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyQueryPool(pl.vk.device,
                                                         pool_of(t), nullptr);
    t.pool = 0;
    t.on = false;
}

namespace {

std::uint32_t first_query(const impl::Timing &t, std::uint32_t ring) {
    return ring * 2 * impl::Timing::kMaxSections;
}

// Read one ring's stamps into `last`. False when the driver has not
// published them yet — the caller leaves the ring unread and comes
// back; `wait` forces the read (a ring two frames old, about to be
// reset) and is the one bounded exception to never waiting.
bool read_ring(impl::Platform &pl, std::uint32_t ring, bool wait) {
    impl::Timing &t = pl.timing;
    impl::Timing::Ring &r = t.rings[ring];
    if (!r.written)
        return true;
    std::uint64_t ticks[2 * impl::Timing::kMaxSections] = {};
    const VkResult res = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetQueryPoolResults(
        pl.vk.device, pool_of(t), first_query(t, ring), 2 * r.written,
        sizeof(std::uint64_t) * 2 * r.written, ticks, sizeof(std::uint64_t),
        VK_QUERY_RESULT_64_BIT | (wait ? VK_QUERY_RESULT_WAIT_BIT : 0));
    if (res == VK_NOT_READY)
        return false;
    if (res == VK_SUCCESS)
        for (std::uint32_t i = 0; i < r.written; ++i)
            t.last.push_back(
                {r.names[i],
                 std::uint64_t(double(ticks[2 * i]) * double(t.period)),
                 std::uint64_t(double(ticks[2 * i + 1]) * double(t.period))});
    return true;
}

} // namespace

void timing_frame_open(impl::Platform &pl, nvrhi::ICommandList *cl) {
    impl::Timing &t = pl.timing;
    t.open = false;
    if (!t.on)
        return;
    t.ring = (t.ring + 1) % impl::Timing::kRings;
    impl::Timing::Ring &r = t.rings[t.ring];
    if (r.unread) {
        // The ring comes round to a frame still unread: read it now,
        // waited, rather than reset stamps nobody saw.
        t.last.clear();
        read_ring(pl, t.ring, /*wait=*/true);
        trace_sections(t.last);
        r.unread = false;
    }
    r.written = 0;
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdResetQueryPool(
        raw(cl), pool_of(t), first_query(t, t.ring),
        2 * impl::Timing::kMaxSections);
}

void timing_begin(impl::Platform &pl, nvrhi::ICommandList *cl,
                  const char *name) {
    impl::Timing &t = pl.timing;
    cl->beginMarker(name);
    impl::Timing::Ring &r = t.rings[t.ring];
    if (!t.on || t.open || r.written >= impl::Timing::kMaxSections)
        return;
    r.names[r.written] = name;
    t.open = true;
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdWriteTimestamp(
        raw(cl), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_of(t),
        first_query(t, t.ring) + 2 * r.written);
}

void timing_end(impl::Platform &pl, nvrhi::ICommandList *cl) {
    impl::Timing &t = pl.timing;
    cl->endMarker();
    if (!t.open)
        return;
    impl::Timing::Ring &r = t.rings[t.ring];
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdWriteTimestamp(
        raw(cl), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_of(t),
        first_query(t, t.ring) + 2 * r.written + 1);
    ++r.written;
    t.open = false;
}

void timing_collect(impl::Platform &pl) {
    impl::Timing &t = pl.timing;
    if (!t.on)
        return;
    const auto now = std::chrono::steady_clock::now();

    // The completed frame's sections — and first any older ring the
    // driver had not published at its own collect. In ring order, so
    // the zones stay in time order; the first unready ring stops the
    // pass, and its frame reads at the next collect.
    t.last.clear();
    t.rings[t.ring].unread = true;
    for (std::uint32_t k = 1; k <= impl::Timing::kRings; ++k) {
        const std::uint32_t ring = (t.ring + k) % impl::Timing::kRings;
        impl::Timing::Ring &r = t.rings[ring];
        if (!r.unread)
            continue;
        if (!read_ring(pl, ring, /*wait=*/false))
            break;
        r.unread = false;
    }

    // gpud's completed batches since the last collect.
    t.batches.clear();
    if (pl.gdev) {
        gpud::Device::BatchTiming buf[256];
        for (std::size_t n; (n = pl.gdev->take_timings(buf)) > 0;)
            t.batches.insert(t.batches.end(), buf, buf + n);
    }

    trace_sections(t.last);
    trace_batches(t.batches);

    if (!t.log)
        return;
    ++t.frames;
    t.frame_ms +=
        std::chrono::duration<double, std::milli>(now - t.last_collect).count();
    t.last_collect = now;
    for (const impl::GpuSection &s : t.last) {
        std::uint32_t i = 0;
        for (; i < t.section_count; ++i)
            if (t.section_names[i] == s.name)
                break;
        if (i == t.section_count && i < impl::Timing::kMaxSections)
            t.section_names[t.section_count++] = s.name;
        if (i < impl::Timing::kMaxSections)
            t.section_ms[i] += double(s.end_ns - s.begin_ns) / 1e6;
    }
    for (const auto &b : t.batches) {
        ++t.batch_count;
        t.dispatch_count += b.dispatches;
        t.batch_ms += double(b.gpu_end_ns - b.gpu_begin_ns) / 1e6;
    }
    if (now - t.last_line >= std::chrono::seconds(1))
        log_line(t);
}

} // namespace sv
