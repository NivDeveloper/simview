// Device bring-up, the window, and the App's lifecycle — what exists
// before a frame and outlives the last one.

#include "../core/App.h"
#include "../ui/Ui.h"

#include <simview/simview.h>

#include <gpud/Vulkan.h>
#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>

#include <vector>

namespace sv {
namespace {

// NVRHI's one callback: everything it says goes to the log, and an
// error joins the validation tally (SIMVIEW_VVL's abort included) —
// the validation wrapper is what turns mistakes into errors here.
struct Logger final : nvrhi::IMessageCallback {
    void message(nvrhi::MessageSeverity sev, const char *text) override {
        if (sev >= nvrhi::MessageSeverity::Error)
            vk_validation_error("nvrhi", text);
        else
            SDL_Log("nvrhi: %s", text);
    }
};
Logger g_nvrhi_log;

} // namespace

void platform_execute(impl::Platform &pl, nvrhi::ICommandList *cl) {
    if (pl.vk.shared_queue)
        pl.vk.queue_m.lock();
    pl.gfx_last = pl.ndev->executeCommandList(cl);
    if (pl.vk.shared_queue)
        pl.vk.queue_m.unlock();
}

void platform_wait_graphics(impl::Platform &pl, std::uint64_t instance,
                            const char *what) {
    if (!instance)
        return;
    auto *vknv = static_cast<nvrhi::vulkan::IDevice *>(
        pl.nraw->getNativeObject(nvrhi::ObjectTypes::Nvrhi_VK_Device).pointer);
    const auto gfx = nvrhi::CommandQueue::Graphics;
    if (vknv->queueGetCompletedInstance(gfx) >= instance)
        return;
    const WaitResult r = vk_wait_timeline(
        pl.vk, reinterpret_cast<std::uint64_t>(vknv->getQueueSemaphore(gfx)),
        instance);
    if (r == WaitResult::done)
        return;

    if (r == WaitResult::lost)
        vk_fatal(std::string("the device was lost while waiting for ") + what +
                 " (VK_ERROR_DEVICE_LOST): a kernel faulted, ran past a "
                 "buffer's end, a buffer was destroyed while a dispatch "
                 "still used it, or a queue waited on a value nothing "
                 "signalled until the driver's watchdog gave up — "
                 "SIMVIEW_VVL=gpuav names the kernel, MTL_DEBUG_LAYER=1 "
                 "the object");
    // Both timelines in the sentence: the value the frame itself waits
    // on GPU-side is the usual culprit — a stamp compute will never
    // reach, or a batch nobody submitted.
    std::string compute =
        "compute timeline waited at " + std::to_string(pl.compute_waited);
    try {
        compute += ", completed " + std::to_string(pl.gdev->completed().value) +
                   ", submitted " + std::to_string(pl.gdev->submitted().value);
    } catch (const std::exception &e) {
        compute += " (" + std::string(e.what()) + ")";
    }
    vk_fatal("waited " + std::to_string(pl.vk.wait_ns / 1000000ull) +
             " ms for " + what + " (graphics instance " +
             std::to_string(instance) + ", completed " +
             std::to_string(vknv->queueGetCompletedInstance(gfx)) + "; " +
             compute +
             "): a dispatch still running, or a value nothing "
             "will ever signal — SIMVIEW_WAIT_MS bounds this wait, unset it "
             "is unbounded");
}

void platform_gfx_idle(impl::Platform &pl) {
    platform_wait_graphics(pl, pl.gfx_last, "the graphics queue to drain");
}

namespace impl {

App *app_init(const Config &c) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return set_error(SDL_GetError()), nullptr;

    App *a = new App;
    Platform &pl = a->platform;
    // LastError is already set by whichever layer refused.
    const auto fail = [&]() -> App * {
        if (pl.win) {
            swapchain_close(pl.sc, pl.ndev);
            SDL_DestroyWindow(pl.win);
            pl.win = nullptr;
        }
        pl.gdev.reset();
        pl.ndev = nullptr;
        pl.nraw = nullptr;
        vk_close(pl.vk);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        delete a;
        return nullptr;
    };

    // The app owns the Vulkan stack; the renderer and the compute
    // library both ADOPT it — one device, two queues, and the timeline
    // between them.
    if (!vk_open(pl.vk, !c.headless))
        return fail();

    std::vector<const char *> dexts, iexts;
    for (const auto &e : pl.vk.device_extensions)
        dexts.push_back(e.c_str());
    for (const auto &e : pl.vk.instance_extensions)
        iexts.push_back(e.c_str());
    nvrhi::vulkan::DeviceDesc dd;
    dd.errorCB = &g_nvrhi_log;
    dd.instance = pl.vk.instance;
    dd.physicalDevice = pl.vk.physical;
    dd.device = pl.vk.device;
    dd.graphicsQueue = pl.vk.gfx_q;
    dd.graphicsQueueIndex = int(pl.vk.gfx_family);
    // The instance list is how NVRHI learns VK_EXT_debug_utils is on —
    // its debugNames become object names and its markers labels.
    dd.instanceExtensions = iexts.data();
    dd.numInstanceExtensions = iexts.size();
    dd.deviceExtensions = dexts.data();
    dd.numDeviceExtensions = dexts.size();
    pl.nraw = nvrhi::vulkan::createDevice(dd);
    if (!pl.nraw) {
        set_error("nvrhi device creation failed");
        return fail();
    }
    vk_name_semaphore(
        pl.vk,
        reinterpret_cast<std::uint64_t>(
            static_cast<nvrhi::vulkan::IDevice *>(pl.nraw.Get())
                ->getQueueSemaphore(nvrhi::CommandQueue::Graphics)),
        "graphics timeline");
    pl.ndev = pl.vk.validation
                  ? nvrhi::validation::createValidationLayer(pl.nraw)
                  : pl.nraw;

    gpud::vulkan::AdoptDesc ad;
    ad.instance = pl.vk.instance;
    ad.physical = pl.vk.physical;
    ad.device = pl.vk.device;
    ad.queue = pl.vk.comp_q;
    ad.queue_family = pl.vk.comp_family;
    ad.get_instance_proc_addr = pl.vk.gipa;
    const std::uint32_t share[] = {pl.vk.gfx_family};
    if (pl.vk.gfx_family != pl.vk.comp_family) {
        ad.share_families = share;
        ad.share_family_count = 1;
    }
    if (pl.vk.shared_queue) {
        ad.queue_lock = vk_queue_lock;
        ad.queue_unlock = vk_queue_unlock;
        ad.queue_user = &pl.vk;
    }
    // gpud's waits take the same bound, so a hang on either queue is
    // one sentence or the other, never a freeze.
    gpud::Options go;
    go.wait_ms = std::uint32_t(pl.vk.wait_ns / 1000000ull);
    go.profile = timing_wanted(); // its batches on the same clock
    pl.gdev = gpud::vulkan::try_open_on(ad, go);
    if (!pl.gdev) {
        set_error("gpud could not adopt the device (GPUD_LOG=1 says why)");
        return fail();
    }

    pl.ui_size = c.size;
    // The scene borrows what it needs from the App it lives in.
    a->scene.gpu = {pl.ndev, pl.gdev.get()};
    a->scene.stats = &a->stats;
    a->scene.pipelines = &a->pipelines;
    a->scene.gates = &a->gates;

    if (!c.headless) {
        pl.win = SDL_CreateWindow(c.title, int(c.size.w), int(c.size.h),
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN |
                                      SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!pl.win) {
            set_error(SDL_GetError());
            return fail();
        }
        if (!swapchain_open(pl.sc, pl.vk, pl.ndev, pl.win))
            return fail();
    }
    pl.cl = pl.ndev->createCommandList();
    timing_init(pl);
    ui_init(a, c);
    return a;
}

void app_quit(App *a) {
    if (!a)
        return;
    Platform &pl = a->platform;
    // The drain, bounded, BEFORE the device-wide idle (which cannot be
    // bounded and is where a hung queue would turn quit into a freeze):
    // compute first — gpud's wait carries the same bound and throws its
    // own sentence — then the graphics queue.
    try {
        pl.gdev->submit();
        pl.gdev->wait(pl.gdev->submitted());
    } catch (const std::exception &e) {
        vk_fatal(std::string("quit: ") + e.what());
    }
    platform_wait_graphics(pl, pl.gfx_last, "the last frame at quit");
    pl.ndev->waitForIdle();
    // One release per layer, top down. Before the devices die: the
    // renderer backend holds pipelines and buffers, the views hold
    // textures, and the swapchain holds wrapped images.
    ui_quit(a);
    pipelines_release(a->pipelines);
    world_pipelines_release(a->world_pipelines);
    if (a->world)
        world_release(*a->world);
    scene_release(a->scene);
    for (View &v : a->views) {
        if (v.world)
            world_release(*v.world);
        scene_release(v.scene);
        target_release(v.target);
    }
    for (SyncGate g : a->gates)
        sync_gate_release(g);
    a->gates.clear();
    pl.cl = nullptr;
    timing_quit(pl);
    if (pl.win) {
        swapchain_close(pl.sc, pl.ndev);
        SDL_DestroyWindow(pl.win);
    }
    // gpud idles only ITS queue; NVRHI then releases against a live
    // device; vk_close waits the whole device idle last.
    pl.gdev.reset();
    pl.ndev = nullptr;
    pl.nraw = nullptr;
    vk_close(pl.vk);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    delete a;
}

void app_on_frame(App *a, void (*fn)(void *), void *user) {
    if (a && fn)
        a->platform.frame_cbs.push_front({fn, user});
}

void app_on_event(App *a, void (*fn)(const Event &, void *), void *user) {
    if (a && fn)
        a->input.event_cbs.push_front({fn, user});
}

void app_on_ui(App *a, void (*fn)(void *), void *user) {
    if (a && fn)
        a->ui.cbs.push_front({fn, user});
}

void app_request_quit(App *a) {
    if (a)
        a->platform.quit = true;
}

void app_post_event(App *a, const Event &e) {
    if (a)
        a->input.posted.push_back(e);
}

Stats app_stats(App *a) { return a ? a->stats : Stats{}; }

Scene app_scene(App *a) { return a ? Scene{&a->scene} : Scene{}; }

} // namespace impl
} // namespace sv
