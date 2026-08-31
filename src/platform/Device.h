#pragma once

// Internal to src/ — the platform's state: the Vulkan stack, the
// renderer device, the compute device, the window. What exists before
// a frame and outlives the last one.

#include "../core/Callbacks.h"
#include "Swapchain.h"
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
} // namespace sv
