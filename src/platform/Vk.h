#pragma once

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
    // SIMVIEW_VVL: a comma list of `1`/`core`, `sync`, `gpuav`,
    // `printf`, `best`, `abort`. See CLAUDE.md's validation table.
    struct Vvl {
        bool on = false, sync = false, gpuav = false, printf = false,
             best = false, abort = false;
    } vvl;
    bool validation = false; // == vvl.on: instance layer + nvrhi wrapper
    // SIMVIEW_WAIT_MS as nanoseconds. 0 = unbounded.
    std::uint64_t wait_ns = 0;
    std::uint64_t messenger = 0; // when the Khronos layer is present

    // Object names and labels, what a capture shows instead of handles.
    bool debug_utils = false;
    // The graphics family timestamps (timestampValidBits > 0), and the
    // device's ns per tick — what platform/Timing.h stamps with.
    bool timestamps = false;
    float timestamp_period = 1.0f;
    // How many samples a colour AND depth attachment can both carry.
    // Four where the device allows it, one where it does not — a
    // world asks for this and never for a number of its own.
    std::uint32_t samples = 1;
    std::vector<std::string> instance_extensions; // what vk_open enabled
    std::vector<std::string> device_extensions;   // what vk_open enabled
};

} // namespace impl

// PROCESS-wide on purpose: both messengers feed it, and the tests'
// shape is one App per process.
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

// Nothing below can be freed while the device may still be using it,
// so the alternative to this exit is undefined behaviour.
[[noreturn]] void vk_fatal(const std::string &sentence);

// False = refusal, by name, in LastError(). The video subsystem must
// already be initialized.
bool vk_open(impl::VkContext &, bool windowed);
void vk_close(impl::VkContext &);

// The lock pair handed to gpud's AdoptDesc when shared_queue — and
// used by the frame's own submits then, so both sides serialize on
// VkContext::queue_m.
void vk_queue_lock(void *ctx);
void vk_queue_unlock(void *ctx);

} // namespace sv
