// The swapchain half of the platform: raw Vulkan WSI through the one
// app dispatcher, images wrapped once for NVRHI.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include "../core/Error.h"
#include "Swapchain.h"

#include <nvrhi/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstdlib>
#include <string>

namespace sv {
namespace {

nvrhi::vulkan::IDevice *vkdev(nvrhi::IDevice *d) {
    return static_cast<nvrhi::vulkan::IDevice *>(
        d->getNativeObject(nvrhi::ObjectTypes::Nvrhi_VK_Device).pointer);
}

bool build_chain(impl::Swapchain &sc, nvrhi::IDevice *ndev) {
    vk::Device dev(sc.vk->device);
    vk::SurfaceKHR surface(reinterpret_cast<VkSurfaceKHR>(sc.surface));

    const auto caps =
        vk::PhysicalDevice(sc.vk->physical).getSurfaceCapabilitiesKHR(surface);
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(sc.win, &pw, &ph);
    vk::Extent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = std::uint32_t(pw);
        extent.height = std::uint32_t(ph);
    }
    extent.width = std::clamp(extent.width, caps.minImageExtent.width,
                              caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height,
                               caps.maxImageExtent.height);
    if (extent.width == 0 || extent.height == 0)
        return false; // minimized: nothing to build

    std::uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount)
        count = std::min(count, caps.maxImageCount);

    vk::SwapchainCreateInfoKHR ci;
    ci.surface = surface;
    ci.minImageCount = count;
    ci.imageFormat = vk::Format(sc.vk_format);
    ci.imageColorSpace = vk::ColorSpaceKHR(sc.vk_color_space);
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    // TRANSFER_DST too: NVRHI clears a target with vkCmdClearColorImage,
    // which the validation layer refuses on a bare color attachment.
    ci.imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eTransferDst;
    if (!(caps.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst))
        return set_error("swapchain: the surface cannot be a transfer "
                         "destination, which the scene clear needs"),
               false;
    ci.imageSharingMode = vk::SharingMode::eExclusive;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    ci.presentMode = vk::PresentModeKHR(sc.present_mode);
    ci.clipped = VK_TRUE;
    const vk::SwapchainKHR old(reinterpret_cast<VkSwapchainKHR>(sc.chain));
    ci.oldSwapchain = old;
    vk::SwapchainKHR chain = dev.createSwapchainKHR(ci);
    if (old)
        dev.destroySwapchainKHR(old);
    sc.chain = reinterpret_cast<std::uint64_t>(VkSwapchainKHR(chain));
    sc.w = extent.width;
    sc.h = extent.height;

    // Wrap each image ONCE; NVRHI transitions from/to Present per
    // command list because the initial state is kept.
    sc.images.clear();
    sc.fbs.clear();
    // One depth image for every swapchain image: only one frame is in
    // flight, so they cannot be in use at once, and matching the
    // colour images one for one would buy nothing but memory.
    sc.depth = nullptr;
    if (sc.want_depth) {
        sc.depth = ndev->createTexture(
            nvrhi::TextureDesc()
                .setWidth(sc.w)
                .setHeight(sc.h)
                .setFormat(nvrhi::Format::D32)
                .setIsRenderTarget(true)
                .setInitialState(nvrhi::ResourceStates::DepthWrite)
                .setKeepInitialState(true)
                .setDebugName("window depth"));
        if (!sc.depth)
            return set_error("window depth texture: creation failed"), false;
    }
    for (VkImage img : dev.getSwapchainImagesKHR(chain)) {
        auto tex = ndev->createHandleForNativeTexture(
            nvrhi::ObjectTypes::VK_Image, nvrhi::Object(img),
            nvrhi::TextureDesc()
                .setWidth(sc.w)
                .setHeight(sc.h)
                .setFormat(sc.format)
                .setIsRenderTarget(true)
                .setInitialState(nvrhi::ResourceStates::Present)
                .setKeepInitialState(true)
                .setDebugName("swapchain"));
        if (!tex)
            return set_error("wrapping a swapchain image failed"), false;
        auto fbd = nvrhi::FramebufferDesc().addColorAttachment(tex);
        if (sc.depth)
            fbd.setDepthAttachment(sc.depth);
        sc.fbs.push_back(ndev->createFramebuffer(fbd));
        sc.images.push_back(std::move(tex));
    }

    // One acquire semaphore per frame slot, one present per image.
    const auto make_sems = [&](std::vector<std::uint64_t> &v) {
        for (std::uint64_t s : v)
            dev.destroySemaphore(reinterpret_cast<VkSemaphore>(s));
        v.clear();
        for (std::size_t i = 0; i < sc.images.size(); ++i)
            v.push_back(reinterpret_cast<std::uint64_t>(
                VkSemaphore(dev.createSemaphore({}))));
    };
    make_sems(sc.acquire_sems);
    make_sems(sc.present_sems);
    sc.recreate = false;
    return true;
}

} // namespace

bool swapchain_open(impl::Swapchain &sc, impl::VkContext &vk,
                    nvrhi::IDevice *ndev, SDL_Window *win) {
    sc.vk = &vk;
    sc.win = win;
    VkSurfaceKHR surface{};
    if (!SDL_Vulkan_CreateSurface(win, vk.instance, nullptr, &surface))
        return set_error(SDL_GetError()), false;
    sc.surface = reinterpret_cast<std::uint64_t>(surface);

    try {
        vk::PhysicalDevice phys(vk.physical);
        // UNORM keeps the pixel gates literal; BGRA is the common
        // native order, RGBA the fallback, else whatever is first.
        const auto formats = phys.getSurfaceFormatsKHR(vk::SurfaceKHR(surface));
        vk::SurfaceFormatKHR pick = formats[0];
        for (const auto &f : formats)
            if (f.format == vk::Format::eB8G8R8A8Unorm) {
                pick = f;
                break;
            } else if (f.format == vk::Format::eR8G8B8A8Unorm) {
                pick = f;
            }
        sc.vk_format = std::uint32_t(VkFormat(pick.format));
        sc.vk_color_space = std::uint32_t(VkColorSpaceKHR(pick.colorSpace));
        sc.format = pick.format == vk::Format::eB8G8R8A8Unorm
                        ? nvrhi::Format::BGRA8_UNORM
                        : nvrhi::Format::RGBA8_UNORM;

        // IMMEDIATE first — vklib's policy: the frame loop uncapped,
        // tearing accepted in a sim viewer — EXCEPT on a portability
        // driver. Measured on MoltenVK 1.3 (examples/ising, M4 Pro):
        // IMMEDIATE spins in [CAMetalLayer nextDrawable] inside
        // vkQueueSubmit and the compute queue starves — 629 sweeps/s
        // against 3367 under FIFO, at 60 frames/s either way. FIFO is
        // the mandated fallback everywhere. SIMVIEW_PRESENT=fifo|immediate
        // overrides, for measuring.
        bool want_immediate = true;
        for (const auto &e : vk.device_extensions)
            if (e == "VK_KHR_portability_subset")
                want_immediate = false;
        if (const char *ov = std::getenv("SIMVIEW_PRESENT"))
            want_immediate = std::string(ov) == "immediate";
        sc.present_mode = std::uint32_t(VK_PRESENT_MODE_FIFO_KHR);
        if (want_immediate)
            for (auto m :
                 phys.getSurfacePresentModesKHR(vk::SurfaceKHR(surface)))
                if (m == vk::PresentModeKHR::eImmediate) {
                    sc.present_mode =
                        std::uint32_t(VK_PRESENT_MODE_IMMEDIATE_KHR);
                    break;
                }
        if (!build_chain(sc, ndev))
            return set_error("swapchain: nothing to build (zero extent?)"),
                   false;
        if (vk.validation)
            SDL_Log("simview: swapchain %ux%u, %u images, %s", sc.w, sc.h,
                    unsigned(sc.images.size()),
                    sc.present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR
                        ? "IMMEDIATE"
                        : "FIFO");
        return true;
    } catch (const std::exception &e) {
        return set_error(std::string("swapchain: ") + e.what()), false;
    }
}

bool swapchain_rebuild(impl::Swapchain &sc, nvrhi::IDevice *ndev) {
    if (!sc.chain)
        return true; // headless, or not up yet: the next build reads the flag
    ndev->waitForIdle();
    return build_chain(sc, ndev);
}

bool swapchain_acquire(impl::Swapchain &sc, nvrhi::IDevice *ndev,
                       void (*gfx_idle)(void *), void *user) {
    try {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(sc.win, &pw, &ph);
        if (sc.recreate || std::uint32_t(pw) != sc.w ||
            std::uint32_t(ph) != sc.h) {
            gfx_idle(user); // the old images' last frame, graphics only
            if (!build_chain(sc, ndev))
                return false;
        }

        vk::Device dev(sc.vk->device);
        const VkSemaphore acq = reinterpret_cast<VkSemaphore>(
            sc.acquire_sems[sc.frame++ % sc.acquire_sems.size()]);
        const auto r = dev.acquireNextImageKHR(
            reinterpret_cast<VkSwapchainKHR>(sc.chain),
            sc.vk->wait_ns ? sc.vk->wait_ns : UINT64_MAX, vk::Semaphore(acq),
            nullptr);
        if (r.result == vk::Result::eTimeout ||
            r.result == vk::Result::eNotReady)
            vk_fatal("waited " + std::to_string(sc.vk->wait_ns / 1000000ull) +
                     " ms for the presentation engine to hand back a "
                     "swapchain image: every image is still queued behind a "
                     "frame that has not finished — SIMVIEW_WAIT_MS bounds "
                     "this wait, unset it is unbounded");
        if (r.result == vk::Result::eErrorOutOfDateKHR) {
            sc.recreate = true;
            return false;
        }
        if (r.result == vk::Result::eSuboptimalKHR)
            sc.recreate = true;
        sc.index = r.value;
        vkdev(ndev)->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, acq,
                                           0);
        return true;
    } catch (const vk::OutOfDateKHRError &) {
        sc.recreate = true;
        return false;
    } catch (const std::exception &e) {
        set_error(std::string("swapchain acquire: ") + e.what());
        return false;
    }
}

void swapchain_ready_present(impl::Swapchain &sc, nvrhi::IDevice *ndev) {
    vkdev(ndev)->queueSignalSemaphore(
        nvrhi::CommandQueue::Graphics,
        reinterpret_cast<VkSemaphore>(sc.present_sems[sc.index]), 0);
}

void swapchain_present(impl::Swapchain &sc) {
    const VkSemaphore wait =
        reinterpret_cast<VkSemaphore>(sc.present_sems[sc.index]);
    const VkSwapchainKHR chain = reinterpret_cast<VkSwapchainKHR>(sc.chain);
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &wait;
    pi.swapchainCount = 1;
    pi.pSwapchains = &chain;
    pi.pImageIndices = &sc.index;
    // The one-queue driver shares this VkQueue with gpud: bracketed.
    if (sc.vk->shared_queue)
        sc.vk->queue_m.lock();
    const VkResult r =
        VULKAN_HPP_DEFAULT_DISPATCHER.vkQueuePresentKHR(sc.vk->gfx_q, &pi);
    if (sc.vk->shared_queue)
        sc.vk->queue_m.unlock();
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
        sc.recreate = true;
}

void swapchain_close(impl::Swapchain &sc, nvrhi::IDevice *ndev) {
    if (ndev)
        ndev->waitForIdle();
    vk::Device dev(sc.vk ? sc.vk->device : nullptr);
    sc.fbs.clear();
    sc.images.clear();
    sc.depth = nullptr;
    if (dev) {
        for (std::uint64_t s : sc.acquire_sems)
            dev.destroySemaphore(reinterpret_cast<VkSemaphore>(s));
        for (std::uint64_t s : sc.present_sems)
            dev.destroySemaphore(reinterpret_cast<VkSemaphore>(s));
        if (sc.chain)
            dev.destroySwapchainKHR(reinterpret_cast<VkSwapchainKHR>(sc.chain));
    }
    sc.acquire_sems.clear();
    sc.present_sems.clear();
    sc.chain = 0;
    if (sc.surface && sc.vk)
        SDL_Vulkan_DestroySurface(sc.vk->instance,
                                  reinterpret_cast<VkSurfaceKHR>(sc.surface),
                                  nullptr);
    sc.surface = 0;
}

} // namespace sv
