// The one TU that speaks the full Vulkan API. It owns the app's single
// vulkan-hpp dynamic-dispatcher storage: NVRHI's static libraries
// dispatch through this very global, which is why it must exist
// exactly once in the app and be initialized before any NVRHI call
// (measured the hard way in the adoption spike).

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include "../core/Error.h"
#include "Vk.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <cstdlib>
#include <cstring>

namespace sv {
namespace {

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

    // MoltenVK 1.3 turned Metal argument buffers on by default, and with
    // a device-address compute ABI every dispatch then re-binds every
    // addressable buffer: examples/ising fell from 9126 sweeps/s (1.2.11)
    // to 2876 (1.3.0, 1.4.2 alike) and came back to 9798 with them off.
    // The standard VK_EXT_layer_settings extension carries the setting
    // (layer name "MoltenVK"); a driver that is not MoltenVK ignores a
    // setting it does not own, and one without the extension never sees
    // it. The renderer's own binding sets are small enough not to care;
    // bindless (descriptorIndexing) would want them back ON.
    const VkBool32 no_argument_buffers = VK_FALSE;
    vk::LayerSettingEXT mvk_setting;
    mvk_setting.pLayerName = "MoltenVK";
    mvk_setting.pSettingName = "MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS";
    mvk_setting.type = vk::LayerSettingTypeEXT::eBool32;
    mvk_setting.valueCount = 1;
    mvk_setting.pValues = &no_argument_buffers;
    vk::LayerSettingsCreateInfoEXT layer_settings;
    layer_settings.settingCount = 1;
    layer_settings.pSettings = &mvk_setting;
    const bool layer_settings_ok =
        portability && has_instance_extension("VK_EXT_layer_settings");
    if (layer_settings_ok)
        exts.push_back("VK_EXT_layer_settings");
    const char *env = std::getenv("SIMVIEW_VVL");
    c.validation = env && *env && *env != '0';
    std::vector<const char *> layers;
    if (c.validation && has_layer("VK_LAYER_KHRONOS_validation"))
        layers.push_back("VK_LAYER_KHRONOS_validation");

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

    // Physical device: first discrete, else the first. The floor and
    // the features are refused BY NAME — a user can act on a sentence.
    const auto devs = vk::Instance(c.instance).enumeratePhysicalDevices();
    if (devs.empty())
        return set_error("no Vulkan device"), false;
    // Two ICDs for one GPU (a stale SDK beside homebrew) enumerate the
    // same silicon twice at different API versions — rank rather than
    // take devs[0]: meets-the-floor first, then discrete, then the
    // highest apiVersion.
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

void vk_close(impl::VkContext &c) {
    if (c.device) {
        vk::Device(c.device).waitIdle();
        vk::Device(c.device).destroy();
    }
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
