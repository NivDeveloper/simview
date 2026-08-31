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
    // SIMVIEW_VVL, parsed once: a comma list of `1`/`core`, `sync`,
    // `gpuav`, `printf`, `best`, `abort`. `on` also turns on NVRHI's
    // validation wrapper; the rest are Khronos-layer features carried
    // by VK_EXT_layer_settings; `abort` makes the first validation
    // ERROR from either source a std::abort() — the gate's exit code.
    struct Vvl {
        bool on = false, sync = false, gpuav = false, printf = false,
             best = false, abort = false;
    } vvl;
    bool validation = false;     // == vvl.on: instance layer + nvrhi wrapper
    std::uint64_t messenger = 0; // VkDebugUtilsMessengerEXT, when the
                                 // Khronos layer is actually present
    std::vector<std::string> device_extensions; // what vk_open enabled
};

} // namespace impl

// The validation tally, PROCESS-wide on purpose: the Khronos layer's
// messenger and NVRHI's message callback both feed it, and the tests'
// shape is one App per process. `vk_validation_error` logs, counts,
// and honors SIMVIEW_VVL's `abort`.
std::size_t vk_validation_errors();
void vk_validation_error(const char *source, const char *text);

// Bring the stack up (windowed adds SDL's surface extensions and
// KHR_swapchain). False = refusal, by name, in LastError(); the video
// subsystem must already be initialized. Validation: SIMVIEW_VVL (the
// table in CLAUDE.md's "The dev surface").
bool vk_open(impl::VkContext &, bool windowed);
void vk_close(impl::VkContext &);

// The lock pair handed to gpud's AdoptDesc when shared_queue — and
// used by the frame's own submits then, so both sides serialize on
// VkContext::queue_m.
void vk_queue_lock(void *ctx);
void vk_queue_unlock(void *ctx);

} // namespace sv
