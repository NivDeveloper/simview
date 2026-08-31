// S2's bring-up gate for the renderer swap: the app-owned Vulkan
// stack comes up headless, NVRHI adopts the graphics queue, gpud
// v0.7 adopts the compute queue (no shader compiler needed for
// storage), and a cleared NVRHI texture reads back its clear color
// through a staging map. Speaks src/ internals on purpose — the App
// does not drive this stack until the swap commit.

#include "harness/Check.h"

#include "platform/Vk.h"

#include <simview/Types.h>

#include <gpud/Vulkan.h>
#include <nvrhi/nvrhi.h>
#include <nvrhi/validation.h>
#include <nvrhi/vulkan.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

struct Msg final : nvrhi::IMessageCallback {
    int errors = 0;
    void message(nvrhi::MessageSeverity sev, const char *text) override {
        std::printf("nvrhi[%d]: %s\n", int(sev), text);
        if (sev >= nvrhi::MessageSeverity::Error)
            ++errors;
    }
};

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return check::skip("platform", SDL_GetError());
    sv::impl::VkContext vk;
    if (!sv::vk_open(vk, /*windowed=*/false)) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return check::skip("platform", sv::LastError());
    }
    std::printf("queues: gfx family %u, compute family %u%s\n", vk.gfx_family,
                vk.comp_family, vk.shared_queue ? " (one shared queue)" : "");

    {
        // NVRHI adopts the graphics half.
        Msg msg;
        std::vector<const char *> dexts;
        for (const auto &e : vk.device_extensions)
            dexts.push_back(e.c_str());
        nvrhi::vulkan::DeviceDesc dd;
        dd.errorCB = &msg;
        dd.instance = vk.instance;
        dd.physicalDevice = vk.physical;
        dd.device = vk.device;
        dd.graphicsQueue = vk.gfx_q;
        dd.graphicsQueueIndex = int(vk.gfx_family);
        dd.deviceExtensions = dexts.data();
        dd.numDeviceExtensions = dexts.size();
        nvrhi::DeviceHandle raw = nvrhi::vulkan::createDevice(dd);
        REQUIRE(raw != nullptr);
        nvrhi::DeviceHandle ndev =
            vk.validation ? nvrhi::validation::createValidationLayer(raw) : raw;

        // gpud adopts the compute half; storage round-trips without a
        // shader compiler (v0.7's lazy slangc).
        gpud::vulkan::AdoptDesc ad;
        ad.instance = vk.instance;
        ad.physical = vk.physical;
        ad.device = vk.device;
        ad.queue = vk.comp_q;
        ad.queue_family = vk.comp_family;
        ad.get_instance_proc_addr = vk.gipa;
        const std::uint32_t share[] = {vk.gfx_family};
        if (vk.gfx_family != vk.comp_family) {
            ad.share_families = share;
            ad.share_family_count = 1;
        }
        if (vk.shared_queue) {
            ad.queue_lock = sv::vk_queue_lock;
            ad.queue_unlock = sv::vk_queue_unlock;
            ad.queue_user = &vk;
        }
        auto gdev = gpud::vulkan::try_open_on(ad);
        REQUIRE(gdev != nullptr);
        CHECK(gdev->dialect() == "slang-vulkan");
        {
            gpud::Buffer b = gdev->alloc(64 * sizeof(float));
            float in[64], out[64] = {};
            for (int i = 0; i < 64; ++i)
                in[i] = float(i) * 0.5f;
            gdev->write(b, in, sizeof in);
            gdev->read(b, out, sizeof out);
            CHECK(std::memcmp(in, out, sizeof in) == 0);
            CHECK(gpud::vulkan::native_buffer(b) != 0u);
        }
        CHECK(gpud::vulkan::native_timeline(*gdev) != 0u);

        // The renderer half: clear a texture, read it back through a
        // staging map — the spike's headless path, now in-tree.
        constexpr std::uint32_t W = 64, H = 64;
        auto tex = ndev->createTexture(nvrhi::TextureDesc()
                                           .setWidth(W)
                                           .setHeight(H)
                                           .setFormat(nvrhi::Format::RGBA8_UNORM)
                                           .setIsRenderTarget(true)
                                           .setInitialState(
                                               nvrhi::ResourceStates::RenderTarget)
                                           .setKeepInitialState(true)
                                           .setDebugName("platform_check"));
        REQUIRE(tex != nullptr);
        auto staging = ndev->createStagingTexture(
            nvrhi::TextureDesc().setWidth(W).setHeight(H).setFormat(
                nvrhi::Format::RGBA8_UNORM),
            nvrhi::CpuAccessMode::Read);
        REQUIRE(staging != nullptr);

        auto cl = ndev->createCommandList();
        cl->open();
        cl->clearTextureFloat(tex, nvrhi::AllSubresources,
                              nvrhi::Color(0.25f, 0.5f, 0.75f, 1.0f));
        cl->copyTexture(staging, nvrhi::TextureSlice(), tex,
                        nvrhi::TextureSlice());
        cl->close();
        ndev->executeCommandList(cl);
        ndev->waitForIdle();

        size_t pitch = 0;
        const auto *px = static_cast<const std::uint8_t *>(
            ndev->mapStagingTexture(staging, nvrhi::TextureSlice(),
                                    nvrhi::CpuAccessMode::Read, &pitch));
        REQUIRE(px != nullptr);
        const auto near8 = [](int got, int want) {
            return got >= want - 1 && got <= want + 1;
        };
        for (std::uint32_t y : {0u, H - 1}) {
            const std::uint8_t *row = px + y * pitch;
            CHECK(near8(row[0], 64));
            CHECK(near8(row[1], 128));
            CHECK(near8(row[2], 191));
            CHECK_EQ(int(row[3]), 255);
            CHECK(near8(row[(W - 1) * 4 + 0], 64));
        }
        ndev->unmapStagingTexture(staging);

        CHECK_EQ(msg.errors, 0);
        // Teardown order: handles, gpud (idles only its queue), then
        // NVRHI, then the stack.
        cl = nullptr;
        staging = nullptr;
        tex = nullptr;
        gdev.reset();
        ndev = nullptr;
        raw = nullptr;
    }
    sv::vk_close(vk);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return check::summary("platform");
}
