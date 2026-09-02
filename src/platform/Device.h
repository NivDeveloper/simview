#pragma once

// Internal to src/ — the platform's state: the Vulkan stack, the
// renderer device, the compute device, the window. What exists before
// a frame and outlives the last one.

#include "../core/Callbacks.h"
#include "Swapchain.h"
#include "Timing.h"
#include "Vk.h"

#include <simview/Types.h>

#include <SDL3/SDL.h>
#include <gpud/Device.h>
#include <nvrhi/nvrhi.h>

#include <forward_list>
#include <memory>

namespace sv {
namespace impl {

struct Platform {
    // ORDER IS LOAD-BEARING: vk owns the VkDevice everything below
    // lives on. Teardown is explicit in app_quit, in reverse; member
    // destruction order backs it up.
    VkContext vk;
    nvrhi::DeviceHandle nraw;           // the vulkan device
    nvrhi::DeviceHandle ndev;           // nraw, or the validation wrapper
    std::unique_ptr<gpud::Device> gdev; // adopted; idles only its queue
    nvrhi::CommandListHandle cl;        // the frame's list, reused
    // Waited at the top of the NEXT iteration, BEFORE the flips: what
    // makes a slot leaving Shown safe for the producer to reuse.
    std::uint64_t frame_instance = 0;
    std::uint64_t gfx_last = 0;       // the newest graphics submission
    std::uint64_t compute_waited = 0; // the compute-timeline value the
                                      // last frame waited on GPU-side
    Timing timing;
    Swapchain sc;
    // Null when headless — the ONE spelling of that fact.
    SDL_Window *win = nullptr;
    // What a headless UI frame is laid out for. A composited shot must
    // be this size or ImGui's projection puts most of the frame off
    // the target.
    Extent2 ui_size{};
    bool quit = false;
    std::forward_list<Cb> frame_cbs; // registration order at run()
};

} // namespace impl

// On a one-queue driver the VkQueue is gpud's too and vkQueueSubmit
// wants external synchronization; the bracket is gpud's own lock.
void platform_execute(impl::Platform &, nvrhi::ICommandList *);

// Bounded by SIMVIEW_WAIT_MS when set, unbounded otherwise. Past the
// bound, or on a lost device, the sentence names `what`.
void platform_wait_graphics(impl::Platform &, std::uint64_t instance,
                            const char *what);

// Wait for the GRAPHICS queue to drain — never vkDeviceWaitIdle, which
// would stall (and race) the compute queue a sim thread is feeding.
void platform_gfx_idle(impl::Platform &);

} // namespace sv
