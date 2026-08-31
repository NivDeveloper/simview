#pragma once

// Internal to src/ — the app-owned Vulkan objects: one instance, one
// device, and the two queues the whole design turns on (graphics for
// the renderer, compute for gpud). Raw C handle typedefs only, spelled
// as vulkan_core.h spells them, so including this does not drag
// vulkan.hpp into every TU; Vk.cpp is the one place that speaks the
// full API and owns the app's single vulkan-hpp dispatcher storage.

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkDevice_T *VkDevice;
typedef struct VkQueue_T *VkQueue;
typedef void (*PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance instance,
                                                        const char *pName);

namespace sv {
namespace impl {

struct VkContext {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue gfx_q{};
    std::uint32_t gfx_family = 0;
    VkQueue comp_q{};
    std::uint32_t comp_family = 0;
    // One VkQueue total (lavapipe): gfx_q == comp_q, and every queue
    // access — ours and gpud's, via its AdoptDesc lock callbacks —
    // brackets with queue_m.
    bool shared_queue = false;
    std::mutex queue_m;
    PFN_vkGetInstanceProcAddr gipa{};
    bool validation = false;             // instance layer + nvrhi wrapper
    std::vector<std::string> device_extensions; // what vk_open enabled
};

} // namespace impl

// Bring the stack up (windowed adds SDL's surface extensions and
// KHR_swapchain). False = refusal, by name, in LastError(); the video
// subsystem must already be initialized. Validation: SIMVIEW_VVL=1.
bool vk_open(impl::VkContext &, bool windowed);
void vk_close(impl::VkContext &);

// The lock pair handed to gpud's AdoptDesc when shared_queue — and
// used by the frame's own submits then, so both sides serialize on
// VkContext::queue_m.
void vk_queue_lock(void *ctx);
void vk_queue_unlock(void *ctx);

} // namespace sv
