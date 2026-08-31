// The test surface, and nothing else.
//
// Everything here exists so a check can ask a question a consumer has
// no business asking. It is compiled into its OWN archive
// (simview_probe), built only under SIMVIEW_BUILD_TESTS and never
// installed — so the boundary is a link boundary, not a naming
// convention, and "is any of this in a release build?" is answered by
// looking for a file rather than by trusting a header not to declare
// it.
//
// The rule that keeps it honest: test-only exports live in sv::probe,
// never sv::impl. One grep answers what exists only for tests, and
// tools/lint.sh refuses the namespace anywhere but here and tests/.

#include "../core/App.h"

#include "../../tests/probe/Probe.h"

#include <gpud/Vulkan.h>
#include <nvrhi/vulkan.h>

namespace sv {
namespace probe {

ImGuiContext *ui_context(impl::App *a) { return a ? a->ui.ctx : nullptr; }

ImPlotContext *plot_context(impl::App *a) { return a ? a->ui.plot : nullptr; }

ImPlot3DContext *plot3d_context(impl::App *a) {
    return a ? a->ui.plot3d : nullptr;
}

std::size_t gate_count(impl::App *a) { return a ? a->gates.size() : 0; }

void *render_device(impl::App *a) {
    return a ? static_cast<void *>(a->platform.ndev.Get()) : nullptr;
}

void queue_lock(impl::App *a) {
    if (a && a->platform.vk.shared_queue)
        a->platform.vk.queue_m.lock();
}

void queue_unlock(impl::App *a) {
    if (a && a->platform.vk.shared_queue)
        a->platform.vk.queue_m.unlock();
}

bool validation_on(impl::App *a) { return a && a->platform.vk.vvl.on; }

std::size_t validation_errors(impl::App *) { return vk_validation_errors(); }

bool timings_on(impl::App *a) { return a && a->platform.timing.on; }

std::size_t gpu_sections(impl::App *a, GpuSection *out, std::size_t cap) {
    if (!a)
        return 0;
    std::size_t n = 0;
    for (const impl::GpuSection &s : a->platform.timing.last)
        if (n < cap)
            out[n++] = {s.name, s.begin_ns, s.end_ns};
    return n;
}

std::size_t compute_batches(impl::App *a, ComputeBatch *out, std::size_t cap) {
    if (!a)
        return 0;
    std::size_t n = 0;
    for (const auto &b : a->platform.timing.batches)
        if (n < cap)
            out[n++] = {b.first.value, b.last.value, b.dispatches,
                        b.gpu_begin_ns, b.gpu_end_ns};
    return n;
}

void stall_frame(impl::App *a) {
    if (!a)
        return;
    auto *vknv = static_cast<nvrhi::vulkan::IDevice *>(
        a->platform.nraw->getNativeObject(nvrhi::ObjectTypes::Nvrhi_VK_Device)
            .pointer);
    vknv->queueWaitForSemaphore(
        nvrhi::CommandQueue::Graphics,
        reinterpret_cast<VkSemaphore>(
            gpud::vulkan::native_timeline(*a->platform.gdev)),
        a->platform.gdev->submitted().value + 1000000);
}

Extent2 view_extent(impl::App *a, const char *title) {
    if (!a || !title)
        return {};
    for (const impl::View &v : a->views)
        if (v.title == title)
            return {v.target.w, v.target.h};
    return {};
}

} // namespace probe
} // namespace sv
