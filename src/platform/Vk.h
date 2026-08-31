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
    bool validation = false; // == vvl.on: instance layer + nvrhi wrapper
    // SIMVIEW_WAIT_MS, as nanoseconds: the bound on every host wait
    // for the device — the previous frame, a shot's readback, the
    // swapchain acquire, the drain at quit, and gpud's own waits
    // (handed to it at adoption). 0 = unbounded, the default.
    std::uint64_t wait_ns = 0;
    std::uint64_t messenger = 0; // VkDebugUtilsMessengerEXT, when the
                                 // Khronos layer is actually present
    // VK_EXT_debug_utils, enabled whenever the loader offers it: object
    // names and command-buffer labels are what a capture, a validation
    // message or a trace shows instead of handles. Cheap, so always.
    bool debug_utils = false;
    // The graphics family timestamps (timestampValidBits > 0), and the
    // device's ns per tick — what platform/Timing.h stamps with.
    bool timestamps = false;
    float timestamp_period = 1.0f;
    std::vector<std::string> instance_extensions; // what vk_open enabled
    std::vector<std::string> device_extensions;   // what vk_open enabled
};

} // namespace impl

// The validation tally, PROCESS-wide on purpose: the Khronos layer's
// messenger and NVRHI's message callback both feed it, and the tests'
// shape is one App per process. `vk_validation_error` logs, counts,
// and honors SIMVIEW_VVL's `abort`.
std::size_t vk_validation_errors();
void vk_validation_error(const char *source, const char *text);

// Name a queue or a semaphore (handles as uint64_t) for whatever
// captures the device; no-ops without VK_EXT_debug_utils.
void vk_name_queue(impl::VkContext &, VkQueue, const char *);
void vk_name_semaphore(impl::VkContext &, std::uint64_t, const char *);

// A host wait on a timeline semaphore (the handle as uint64_t), bounded
// by wait_ns when set. `lost` is VK_ERROR_DEVICE_LOST.
enum class WaitResult { done, timeout, lost };
WaitResult vk_wait_timeline(impl::VkContext &, std::uint64_t semaphore,
                            std::uint64_t value);

// The end of a process whose GPU never comes back: the sentence is
// logged and kept for LastError(), then the process exits — nothing
// below can be freed while the device may still be using it, so the
// alternative to this exit is the hang it replaces.
[[noreturn]] void vk_fatal(const std::string &sentence);

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
