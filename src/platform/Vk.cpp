#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "../core/Error.h"
#include "Vk.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace sv {
namespace {

std::atomic<std::size_t> g_validation_errors{0};
std::atomic<bool> g_validation_abort{false};

// The Khronos layer's messenger. The loader's own chatter is skipped;
// everything else goes to the log with its severity, and an ERROR is
// the gate's signal.
VKAPI_ATTR VkBool32 VKAPI_CALL
vvl_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
             VkDebugUtilsMessageTypeFlagsEXT type,
             const VkDebugUtilsMessengerCallbackDataEXT *data, void *) {
    if (!data || !data->pMessage)
        return VK_FALSE;
    if (data->pMessageIdName &&
        std::strstr(data->pMessageIdName, "Loader Message"))
        return VK_FALSE;
    // Logged, never counted: 1.3.275 ignores the id filter and
    // aborted CI on its own "most likely a validation bug".
    const bool self_diagnosed =
        data->pMessageIdName &&
        std::strcmp(data->pMessageIdName,
                    "UNASSIGNED-VkSemaphore-state-timeout") == 0;
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) &&
        !self_diagnosed)
        vk_validation_error("vulkan", data->pMessage);
    else if (self_diagnosed)
        SDL_Log("validation[layer-self] vulkan: %s", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        SDL_Log("validation[warning] vulkan: %s", data->pMessage);
    else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
        // INFO of the validation kind is shader printf; a driver's own
        // informational chatter (MoltenVK narrates its bring-up) is not.
        SDL_Log("validation[info] vulkan: %s", data->pMessage);
    return VK_FALSE;
}

impl::VkContext::Vvl parse_vvl(const char *env) {
    impl::VkContext::Vvl v;
    if (!env || !*env || std::strcmp(env, "0") == 0)
        return v;
    std::istringstream words(env);
    std::string w;
    while (std::getline(words, w, ',')) {
        if (w == "1" || w == "core")
            v.on = true;
        else if (w == "sync")
            v.on = v.sync = true;
        else if (w == "gpuav")
            v.on = v.gpuav = true;
        else if (w == "printf")
            v.on = v.printf = true;
        else if (w == "best")
            v.on = v.best = true;
        else if (w == "abort")
            v.on = v.abort = true;
        else if (!w.empty())
            SDL_Log("simview: SIMVIEW_VVL ignores \"%s\"", w.c_str());
    }
    if (v.gpuav && v.printf) {
        // The layer runs one or the other; gpuav is the one that finds
        // out-of-range device-address reads, so it wins.
        SDL_Log("simview: SIMVIEW_VVL: gpuav and printf are exclusive — "
                "keeping gpuav");
        v.printf = false;
    }
    return v;
}

// SDL dlopens the loader by bare name and misses /usr/local/lib on
// macOS. simview creates the device now, so the hint duty moves here
// from gpud (docs/design.md's "whoever creates the device owns it").
bool load_vulkan_library() {
#ifdef __APPLE__
    // Newest loader first: a stale /usr/local SDK install shadows the
    // homebrew one under the bare-name dlopen and then only exposes a
    // Vulkan 1.2 MoltenVK. A user's SDL_VULKAN_LIBRARY hint wins.
    if (!SDL_GetHint(SDL_HINT_VULKAN_LIBRARY))
        for (const char *p : {"/opt/homebrew/lib/libvulkan.1.dylib",
                              "/usr/local/lib/libvulkan.1.dylib"})
            if (SDL_Vulkan_LoadLibrary(p))
                return true;
#endif
    return SDL_Vulkan_LoadLibrary(nullptr);
}

bool has_layer(const char *name) {
    for (const auto &l : vk::enumerateInstanceLayerProperties())
        if (std::strcmp(l.layerName, name) == 0)
            return true;
    return false;
}

bool has_instance_extension(const char *name) {
    for (const auto &e : vk::enumerateInstanceExtensionProperties())
        if (std::strcmp(e.extensionName, name) == 0)
            return true;
    return false;
}

bool vk_open_inner(impl::VkContext &c, bool windowed) {
    c.gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!c.gipa)
        return set_error(SDL_GetError()), false;
    VULKAN_HPP_DEFAULT_DISPATCHER.init(
        reinterpret_cast<::PFN_vkGetInstanceProcAddr>(c.gipa));

    // Instance: 1.3 is the floor (dynamic rendering, sync2). SDL names
    // the surface extensions; portability enumeration is the generic
    // Khronos pattern for drivers like MoltenVK.
    std::vector<const char *> exts;
    if (windowed) {
        Uint32 n = 0;
        const char *const *se = SDL_Vulkan_GetInstanceExtensions(&n);
        if (!se)
            return set_error(SDL_GetError()), false;
        exts.assign(se, se + n);
    } else if (has_instance_extension("VK_KHR_surface")) {
        // Headless too: the ImGui backend's loader refuses a table
        // with missing WSI entry points, and the extension is only
        // entry points until a surface exists.
        exts.push_back("VK_KHR_surface");
    }
    vk::InstanceCreateFlags flags{};
    const bool portability =
        has_instance_extension("VK_KHR_portability_enumeration");
    if (portability) {
        exts.push_back("VK_KHR_portability_enumeration");
        flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    }

    // MoltenVK 1.3 defaults Metal argument buffers ON, and with a
    // device-address compute ABI every dispatch then re-binds every
    // addressable buffer — measured 4x on gpud's path.
    static const VkBool32 kFalse = VK_FALSE, kTrue = VK_TRUE;
    static const char *const kCallbackOnly = "VK_DBG_LAYER_ACTION_CALLBACK";
    // One entry: UNASSIGNED-VkSemaphore-state-timeout, VVL 1.3.275 on
    // lavapipe, whose own completed value is already past the awaited
    // one. Not seen on 1.4.304; costs the layer's 10 s timeout.
    static const char *const kSelfDiagnosed =
        "UNASSIGNED-VkSemaphore-state-timeout";
    std::vector<vk::LayerSettingEXT> settings;
    const auto setting = [&](const char *layer, const char *name,
                             const VkBool32 &value) {
        vk::LayerSettingEXT s;
        s.pLayerName = layer;
        s.pSettingName = name;
        s.type = vk::LayerSettingTypeEXT::eBool32;
        s.valueCount = 1;
        s.pValues = &value;
        settings.push_back(s);
    };
    setting("MoltenVK", "MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", kFalse);

    // Its messages come through OUR messenger, so the tally and the
    // abort behave the same however they were turned on.
    c.vvl = parse_vvl(std::getenv("SIMVIEW_VVL"));
    c.validation = c.vvl.on;
    if (const char *w = std::getenv("SIMVIEW_WAIT_MS"); w && *w)
        c.wait_ns = std::strtoull(w, nullptr, 10) * 1000000ull;
    g_validation_abort.store(c.vvl.abort);
    const bool layer_on = c.vvl.on && has_layer("VK_LAYER_KHRONOS_validation");
    if (c.vvl.on && !layer_on)
        SDL_Log("simview: SIMVIEW_VVL set but VK_LAYER_KHRONOS_validation "
                "is not installed — only NVRHI's validation runs");
    std::vector<const char *> layers;
    if (layer_on) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        const char *vl = "VK_LAYER_KHRONOS_validation";
        setting(vl, "validate_core", kTrue);
        setting(vl, "validate_sync", c.vvl.sync ? kTrue : kFalse);
        setting(vl, "gpuav_enable", c.vvl.gpuav ? kTrue : kFalse);
        setting(vl, "printf_enable", c.vvl.printf ? kTrue : kFalse);
        setting(vl, "printf_to_stdout", kFalse);
        setting(vl, "validate_best_practices", c.vvl.best ? kTrue : kFalse);
        const auto text = [&](const char *name, const char *const &value) {
            vk::LayerSettingEXT s;
            s.pLayerName = vl;
            s.pSettingName = name;
            s.type = vk::LayerSettingTypeEXT::eString;
            s.valueCount = 1;
            s.pValues = &value;
            settings.push_back(s);
        };
        text("debug_action", kCallbackOnly);
        text("message_id_filter", kSelfDiagnosed);
    }
    vk::LayerSettingsCreateInfoEXT layer_settings;
    layer_settings.setSettings(settings);
    // Widened from "portability only": the Linux CI leg (lavapipe) has
    // no portability extension and every validation setting rides here.
    const bool layer_settings_ok =
        (portability || layer_on) &&
        has_instance_extension("VK_EXT_layer_settings");
    if (layer_settings_ok)
        exts.push_back("VK_EXT_layer_settings");
    c.debug_utils = has_instance_extension("VK_EXT_debug_utils");
    if (c.debug_utils)
        exts.push_back("VK_EXT_debug_utils");
    const bool messenger_ok = layer_on && c.debug_utils;
    c.instance_extensions.assign(exts.begin(), exts.end());

    vk::ApplicationInfo app;
    app.pApplicationName = "simview";
    app.apiVersion = VK_API_VERSION_1_3;
    vk::InstanceCreateInfo ici;
    if (layer_settings_ok)
        ici.pNext = &layer_settings;
    ici.flags = flags;
    ici.pApplicationInfo = &app;
    ici.setPEnabledExtensionNames(exts);
    ici.setPEnabledLayerNames(layers);
    c.instance = vk::createInstance(ici);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance(c.instance));

    if (messenger_ok) {
        // The C entry points through the app's dispatcher: vulkan-hpp's
        // typed callback signature is a needless friction here.
        VkDebugUtilsMessengerCreateInfoEXT mci{};
        mci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mci.pfnUserCallback = vvl_callback;
        VkDebugUtilsMessengerEXT m{};
        if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateDebugUtilsMessengerEXT(
                c.instance, &mci, nullptr, &m) == VK_SUCCESS)
            c.messenger = reinterpret_cast<std::uint64_t>(m);
        SDL_Log("simview: validation on (core%s%s%s%s%s)",
                c.vvl.sync ? ", sync" : "", c.vvl.gpuav ? ", gpuav" : "",
                c.vvl.printf ? ", printf" : "", c.vvl.best ? ", best" : "",
                c.vvl.abort ? ", abort on error" : "");
    }

    // Physical device: first discrete, else the first. The floor and
    // the features are refused BY NAME — a user can act on a sentence.
    const auto devs = vk::Instance(c.instance).enumeratePhysicalDevices();
    if (devs.empty())
        return set_error("no Vulkan device"), false;
    // Two ICDs for one GPU enumerate the same silicon twice at
    // different API versions, so rank rather than take devs[0].
    vk::PhysicalDevice phys = devs[0];
    auto rank = [](vk::PhysicalDevice d) {
        const auto pr = d.getProperties();
        return (std::uint64_t(pr.apiVersion >= VK_API_VERSION_1_3) << 63) |
               (std::uint64_t(pr.deviceType ==
                              vk::PhysicalDeviceType::eDiscreteGpu)
                << 62) |
               pr.apiVersion;
    };
    for (auto d : devs)
        if (rank(d) > rank(phys))
            phys = d;
    c.physical = phys;
    const auto props = phys.getProperties();
    if (props.apiVersion < VK_API_VERSION_1_3)
        return set_error(
                   std::string("Vulkan 1.3 is the floor; ") +
                   props.deviceName.data() + " reports " +
                   std::to_string(VK_API_VERSION_MAJOR(props.apiVersion)) +
                   "." +
                   std::to_string(VK_API_VERSION_MINOR(props.apiVersion))),
               false;

    vk::PhysicalDeviceVulkan13Features f13q;
    vk::PhysicalDeviceVulkan12Features f12q;
    f12q.pNext = &f13q;
    vk::PhysicalDeviceVulkan11Features f11q;
    f11q.pNext = &f12q;
    vk::PhysicalDeviceFeatures2 f2q;
    f2q.pNext = &f11q;
    phys.getFeatures2(&f2q);
    std::string missing;
    const auto need = [&](vk::Bool32 have, const char *name) {
        if (!have)
            missing += missing.empty() ? name : (std::string(", ") + name);
    };
    // slangc's SV_InstanceID lowers through the DrawParameters
    // capability (gl_BaseInstance), a 1.1 feature that is opt-in.
    need(f11q.shaderDrawParameters, "shaderDrawParameters");
    need(f12q.timelineSemaphore, "timelineSemaphore");
    need(f12q.bufferDeviceAddress, "bufferDeviceAddress");
    need(f13q.dynamicRendering, "dynamicRendering");
    need(f13q.synchronization2, "synchronization2");
    need(f13q.maintenance4, "maintenance4");
    if (!missing.empty())
        return set_error(std::string(props.deviceName.data()) +
                         " lacks required features: " + missing),
               false;

    // Queue policy: (a) a compute-capable family distinct from the
    // graphics one — true async compute; (b) the graphics family twice
    // when it has two queues; (c) one shared queue, bracketed.
    const auto fams = phys.getQueueFamilyProperties();
    c.gfx_family = std::uint32_t(fams.size());
    for (std::uint32_t i = 0; i < fams.size(); ++i)
        if (fams[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            c.gfx_family = i;
            break;
        }
    if (c.gfx_family == fams.size())
        return set_error("no graphics queue family"), false;
    c.timestamps = fams[c.gfx_family].timestampValidBits > 0;
    c.timestamp_period = props.limits.timestampPeriod;
    // Four samples: beyond it the cost climbs faster than the picture
    // improves.
    const auto counts = props.limits.framebufferColorSampleCounts &
                        props.limits.framebufferDepthSampleCounts;
    c.samples = (counts & vk::SampleCountFlagBits::e4) ? 4u : 1u;
    c.comp_family = std::uint32_t(fams.size());
    for (std::uint32_t i = 0; i < fams.size(); ++i)
        if (i != c.gfx_family &&
            (fams[i].queueFlags & vk::QueueFlagBits::eCompute)) {
            c.comp_family = i;
            break;
        }
    bool same_family_two = false;
    // SIMVIEW_ONE_QUEUE=1: force the one-shared-queue path (what a
    // single-queue driver gets), for measuring what a second queue buys
    // — 5.4x on examples/ising (3367 against 628 sweeps/s, MoltenVK).
    if (const char *oq = std::getenv("SIMVIEW_ONE_QUEUE"); oq && *oq == '1')
        c.comp_family = std::uint32_t(fams.size());
    if (c.comp_family == fams.size()) {
        if (fams[c.gfx_family].queueCount >= 2) {
            c.comp_family = c.gfx_family;
            same_family_two = true;
        } else {
            c.comp_family = c.gfx_family;
            c.shared_queue = true;
        }
    }

    const float prios[2] = {1.0f, 1.0f};
    std::vector<vk::DeviceQueueCreateInfo> qcis;
    if (same_family_two) {
        qcis.push_back({{}, c.gfx_family, 2, prios});
    } else if (c.shared_queue) {
        qcis.push_back({{}, c.gfx_family, 1, prios});
    } else {
        qcis.push_back({{}, c.gfx_family, 1, prios});
        qcis.push_back({{}, c.comp_family, 1, prios});
    }

    c.device_extensions.clear();
    for (const auto &e : phys.enumerateDeviceExtensionProperties()) {
        // Swapchain rides along headless too (see the instance note);
        // portability_subset is the spec's own requirement when
        // advertised.
        if (std::strcmp(e.extensionName, "VK_KHR_swapchain") == 0 &&
            !exts.empty())
            c.device_extensions.push_back("VK_KHR_swapchain");
        if (std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0)
            c.device_extensions.push_back("VK_KHR_portability_subset");
    }
    std::vector<const char *> dexts;
    for (const auto &e : c.device_extensions)
        dexts.push_back(e.c_str());

    // Enable exactly what was demanded above — nothing speculative.
    vk::PhysicalDeviceVulkan13Features f13;
    f13.dynamicRendering = VK_TRUE;
    f13.synchronization2 = VK_TRUE;
    f13.maintenance4 = VK_TRUE;
    vk::PhysicalDeviceVulkan12Features f12;
    f12.pNext = &f13;
    f12.timelineSemaphore = VK_TRUE;
    f12.bufferDeviceAddress = VK_TRUE;
    vk::PhysicalDeviceVulkan11Features f11;
    f11.pNext = &f12;
    f11.shaderDrawParameters = VK_TRUE;
    vk::PhysicalDeviceFeatures2 f2;
    f2.pNext = &f11;
    vk::DeviceCreateInfo dci;
    dci.pNext = &f2;
    dci.setQueueCreateInfos(qcis);
    dci.setPEnabledExtensionNames(dexts);
    c.device = phys.createDevice(dci);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Device(c.device));

    vk::Device dev(c.device);
    c.gfx_q = static_cast<VkQueue>(dev.getQueue(c.gfx_family, 0));
    if (c.shared_queue)
        c.comp_q = c.gfx_q;
    else if (same_family_two)
        c.comp_q = static_cast<VkQueue>(dev.getQueue(c.gfx_family, 1));
    else
        c.comp_q = static_cast<VkQueue>(dev.getQueue(c.comp_family, 0));
    // gpud names the compute queue at adoption; a shared queue is both.
    vk_name_queue(c, c.gfx_q,
                  c.shared_queue ? "graphics + compute queue (shared)"
                                 : "graphics queue");
    return true;
}

} // namespace

bool vk_open(impl::VkContext &c, bool windowed) {
    if (!load_vulkan_library())
        return set_error(SDL_GetError()), false;
    // The impl never throws; vulkan-hpp does. One boundary, here.
    try {
        if (vk_open_inner(c, windowed))
            return true;
    } catch (const std::exception &e) {
        set_error(std::string("vulkan bring-up: ") + e.what());
    }
    vk_close(c);
    return false;
}

std::size_t vk_validation_errors() {
    return g_validation_errors.load(std::memory_order_relaxed);
}

void vk_validation_error(const char *source, const char *text) {
    g_validation_errors.fetch_add(1, std::memory_order_relaxed);
    SDL_Log("validation[error] %s: %s", source, text);
    if (g_validation_abort.load()) {
        SDL_Log("SIMVIEW_VVL abort: first validation error");
        std::abort();
    }
}

namespace {
void vk_name(impl::VkContext &c, VkObjectType type, std::uint64_t handle,
             const char *text) {
    if (!c.debug_utils || !c.device || !handle)
        return;
    VkDebugUtilsObjectNameInfoEXT ni{};
    ni.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    ni.objectType = type;
    ni.objectHandle = handle;
    ni.pObjectName = text;
    VULKAN_HPP_DEFAULT_DISPATCHER.vkSetDebugUtilsObjectNameEXT(c.device, &ni);
}
} // namespace

void vk_name_queue(impl::VkContext &c, VkQueue q, const char *text) {
    vk_name(c, VK_OBJECT_TYPE_QUEUE, reinterpret_cast<std::uint64_t>(q), text);
}

void vk_name_semaphore(impl::VkContext &c, std::uint64_t s, const char *text) {
    vk_name(c, VK_OBJECT_TYPE_SEMAPHORE, s, text);
}

WaitResult vk_wait_timeline(impl::VkContext &c, std::uint64_t semaphore,
                            std::uint64_t value) {
    const VkSemaphore sem = reinterpret_cast<VkSemaphore>(semaphore);
    VkSemaphoreWaitInfo wi{};
    wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wi.semaphoreCount = 1;
    wi.pSemaphores = &sem;
    wi.pValues = &value;
    const VkResult r = VULKAN_HPP_DEFAULT_DISPATCHER.vkWaitSemaphores(
        c.device, &wi, c.wait_ns ? c.wait_ns : UINT64_MAX);
    if (r == VK_SUCCESS)
        return WaitResult::done;
    return r == VK_TIMEOUT ? WaitResult::timeout : WaitResult::lost;
}

void vk_fatal(const std::string &sentence) {
    set_error(sentence);
    SDL_Log("simview: exiting — nothing can be cleaned up under a GPU that "
            "never comes back");
    // _Exit, not abort: the sentence is the diagnosis, exit handlers
    // would wait on the same device, and a gate reads an exit code
    // where it cannot read a signal.
    std::_Exit(EXIT_FAILURE);
}

void vk_close(impl::VkContext &c) {
    if (c.device) {
        vk::Device(c.device).waitIdle();
        vk::Device(c.device).destroy();
    }
    if (c.instance && c.messenger)
        VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyDebugUtilsMessengerEXT(
            c.instance, reinterpret_cast<VkDebugUtilsMessengerEXT>(c.messenger),
            nullptr);
    c.messenger = 0;
    if (c.instance)
        vk::Instance(c.instance).destroy();
    c.device = nullptr;
    c.instance = nullptr;
    c.physical = nullptr;
    c.gfx_q = c.comp_q = nullptr;
    SDL_Vulkan_UnloadLibrary();
}

void vk_queue_lock(void *ctx) {
    static_cast<impl::VkContext *>(ctx)->queue_m.lock();
}
void vk_queue_unlock(void *ctx) {
    static_cast<impl::VkContext *>(ctx)->queue_m.unlock();
}

} // namespace sv
