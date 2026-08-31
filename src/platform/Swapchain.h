#pragma once

// Internal to src/ — the window's images: an app-managed
// VkSwapchainKHR whose images NVRHI wraps once and whose pacing is
// IMMEDIATE when the driver offers it (FIFO otherwise) — the sim is
// decoupled by design, so this choice paces only the frame loop.
// Binary acquire/present semaphores ride NVRHI's queue wait/signal
// lists, the donut-proven pattern.

#include "Vk.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <vector>

struct SDL_Window;

namespace sv {
namespace impl {

struct Swapchain {
    VkContext *vk = nullptr;
    SDL_Window *win = nullptr;
    std::uint64_t surface = 0; // VkSurfaceKHR
    std::uint64_t chain = 0;   // VkSwapchainKHR
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    std::uint32_t vk_format = 0;      // VkFormat, for recreation
    std::uint32_t vk_color_space = 0; // VkColorSpaceKHR
    std::uint32_t present_mode = 0;   // VkPresentModeKHR, chosen once
    std::uint32_t w = 0, h = 0;
    std::vector<nvrhi::TextureHandle> images;
    std::vector<nvrhi::FramebufferHandle> fbs;
    std::vector<std::uint64_t> acquire_sems; // binary, rotated by frame
    std::vector<std::uint64_t> present_sems; // binary, per image
    std::uint32_t index = 0;                 // the acquired image
    std::uint64_t frame = 0;                 // rotates acquire_sems
    bool recreate = false; // suboptimal/resize seen; rebuild next acquire
};

} // namespace impl

bool swapchain_open(impl::Swapchain &, impl::VkContext &, nvrhi::IDevice *,
                    SDL_Window *);

// Acquire the next image and register its wait on NVRHI's graphics
// queue. False = nothing to draw to this frame (minimized, or the
// chain was just rebuilt after a resize) — benign, like a failed
// Presenter::acquire always was.
bool swapchain_acquire(impl::Swapchain &, nvrhi::IDevice *,
                       void (*gfx_idle)(void *), void *user);

// Register the present semaphore on the NEXT executeCommandLists,
// which the caller then runs before calling present.
void swapchain_ready_present(impl::Swapchain &, nvrhi::IDevice *);
void swapchain_present(impl::Swapchain &);

void swapchain_close(impl::Swapchain &, nvrhi::IDevice *);

} // namespace sv
