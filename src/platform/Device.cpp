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

// NVRHI's one callback: everything it says goes to the log; the
// validation wrapper (SIMVIEW_VVL=1) is what turns mistakes into
// messages worth reading.
struct Logger final : nvrhi::IMessageCallback {
    void message(nvrhi::MessageSeverity, const char *text) override {
        SDL_Log("nvrhi: %s", text);
    }
};
Logger g_nvrhi_log;

} // namespace

void platform_execute(impl::Platform &pl, nvrhi::ICommandList *cl) {
    if (pl.vk.shared_queue)
        pl.vk.queue_m.lock();
    pl.ndev->executeCommandList(cl);
    if (pl.vk.shared_queue)
        pl.vk.queue_m.unlock();
}

void platform_gfx_idle(impl::Platform &pl) {
    pl.ndev->setEventQuery(pl.idle_query, nvrhi::CommandQueue::Graphics);
    pl.ndev->waitEventQuery(pl.idle_query);
    pl.ndev->resetEventQuery(pl.idle_query);
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

    std::vector<const char *> dexts;
    for (const auto &e : pl.vk.device_extensions)
        dexts.push_back(e.c_str());
    nvrhi::vulkan::DeviceDesc dd;
    dd.errorCB = &g_nvrhi_log;
    dd.instance = pl.vk.instance;
    dd.physicalDevice = pl.vk.physical;
    dd.device = pl.vk.device;
    dd.graphicsQueue = pl.vk.gfx_q;
    dd.graphicsQueueIndex = int(pl.vk.gfx_family);
    dd.deviceExtensions = dexts.data();
    dd.numDeviceExtensions = dexts.size();
    pl.nraw = nvrhi::vulkan::createDevice(dd);
    if (!pl.nraw) {
        set_error("nvrhi device creation failed");
        return fail();
    }
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
    pl.gdev = gpud::vulkan::try_open_on(ad);
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
    pl.frame_query = pl.ndev->createEventQuery();
    pl.idle_query = pl.ndev->createEventQuery();
    ui_init(a, c);
    return a;
}

void app_quit(App *a) {
    if (!a)
        return;
    Platform &pl = a->platform;
    pl.ndev->waitForIdle();
    // One release per layer, top down. Before the devices die: the
    // renderer backend holds pipelines and buffers, the views hold
    // textures, and the swapchain holds wrapped images.
    ui_quit(a);
    pipelines_release(a->pipelines);
    scene_release(a->scene);
    for (View &v : a->views) {
        scene_release(v.scene);
        target_release(v.target);
    }
    for (SyncGate g : a->gates)
        sync_gate_release(g);
    a->gates.clear();
    pl.cl = nullptr;
    pl.frame_query = nullptr;
    pl.idle_query = nullptr;
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
