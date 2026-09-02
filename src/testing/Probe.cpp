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
#include <imgui.h>
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

namespace {

// Every entry point below writes into the App's OWN context: a check
// may hold two apps, and ImGui's current-context is global state.
ImGuiIO *io_of(impl::App *a) {
    if (!a || !a->ui.ctx)
        return nullptr;
    ImGui::SetCurrentContext(a->ui.ctx);
    return &ImGui::GetIO();
}

impl::WorldState *world_of(impl::App *a, const char *title) {
    if (!a)
        return nullptr;
    if (!title || !*title)
        return a->world.get();
    for (impl::View &v : a->views)
        if (v.world && v.title == title)
            return v.world.get();
    return nullptr;
}

} // namespace

void mouse_move(impl::App *a, float x, float y) {
    if (ImGuiIO *io = io_of(a))
        io->AddMousePosEvent(x, y);
}

void mouse_button(impl::App *a, int button, bool down) {
    if (ImGuiIO *io = io_of(a))
        io->AddMouseButtonEvent(button, down);
}

void mouse_wheel(impl::App *a, float dy) {
    if (ImGuiIO *io = io_of(a))
        io->AddMouseWheelEvent(0.0f, dy);
}

void mouse_modifier_shift(impl::App *a, bool down) {
    if (ImGuiIO *io = io_of(a))
        io->AddKeyEvent(ImGuiMod_Shift, down);
}

std::size_t mesh_tiers(impl::App *a, const char *title, unsigned *triangles,
                       std::size_t cap) {
    impl::WorldState *w = world_of(a, title);
    if (!w)
        return 0;
    std::size_t n = 0;
    for (const auto &m : w->meshes)
        if (n < cap)
            triangles[n++] = m.triangles;
    return n;
}

void culling(impl::App *a, bool on) {
    for (impl::View &v : a->views)
        if (v.world)
            v.world->cull = on;
    if (a->world)
        a->world->cull = on;
}

std::size_t item_triangles(impl::App *a, const char *title, std::uint64_t *out,
                           std::size_t cap) {
    impl::WorldState *w = world_of(a, title);
    if (!w)
        return 0;
    std::size_t n = 0;
    for (const impl::WorldItem &it : w->items)
        if (n < cap)
            out[n++] = it.triangles;
    return n;
}

bool camera_of(impl::App *a, const char *title, CameraState *out) {
    impl::WorldState *w = world_of(a, title);
    if (!w || !out)
        return false;
    const impl::Vec3 f = w->camera.forward();
    const impl::Vec3 u = w->camera.up();
    out->focus[0] = w->camera.focus().x;
    out->focus[1] = w->camera.focus().y;
    out->focus[2] = w->camera.focus().z;
    out->distance = w->camera.distance();
    out->forward[0] = f.x;
    out->forward[1] = f.y;
    out->forward[2] = f.z;
    out->up[0] = u.x;
    out->up[1] = u.y;
    out->up[2] = u.z;
    out->orthographic = w->camera.orthographic();
    return true;
}

bool world_preset(impl::App *a, const char *title, int preset) {
    impl::WorldState *w = world_of(a, title);
    std::size_t n = 0;
    const Preset *table = world_presets(&n);
    if (!w || preset < 0 || std::size_t(preset) >= n)
        return false;
    world_look(*w, table[preset].az, table[preset].el);
    return true;
}

bool world_show(impl::App *a, const char *title, int what, bool on) {
    impl::WorldState *w = world_of(a, title);
    if (!w)
        return false;
    impl::WorldItem *it = what == 0 ? w->grid : w->axes;
    if (!it)
        return false;
    it->visible = on;
    return true;
}

std::size_t world_preset_count() {
    std::size_t n = 0;
    world_presets(&n);
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

bool plot_tools(impl::App *a, const char *title, PlotTools *out) {
    if (!a || !title || !out)
        return false;
    for (const impl::PlotState &p : a->plots)
        if (p.title == title) {
            *out = {p.fit_offered, p.fit_pending, p.open, p.canvas_w,
                    p.canvas_h,    p.legend,      p.grid};
            return true;
        }
    return false;
}

void plot_show_legend(impl::App *a, const char *title, bool on) {
    if (!a || !title)
        return;
    for (impl::PlotState &p : a->plots)
        if (p.title == title)
            p.legend = on;
}

} // namespace probe
} // namespace sv
