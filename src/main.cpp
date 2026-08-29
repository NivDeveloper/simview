// simview: a window on a running tensor simulation. The sim computes
// through tensor's slot dialect on gpud's SDL backend; the fragment
// shader colormaps the very buffer the compute writes — zero copies,
// one SDL_GPUDevice for both. v1 shows the 2-D XY model live.

#include "xy.h"

#include <gpud/Sdl.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <print>
#include <string>
#include <vector>

namespace {

// The resident field's device buffer, reached through tensor's public
// residency seam (the pattern Gpu/Eval.h's park() writes). Upstreaming
// a named accessor into tensor is future work.
gpud::Buffer *field_buffer(const xy::Field &t) {
    auto *slot = std::as_const(t).view().shadow;
    if (!slot || !slot->storage)
        return nullptr;
    auto *g = dynamic_cast<tensor::GpuStorage *>(slot->storage.get());
    return g ? &g->buf : nullptr;
}

std::vector<Uint8> load_spv(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

SDL_GPUShader *make_shader(SDL_GPUDevice *dev, const std::string &path,
                           SDL_GPUShaderStage stage, Uint32 storage,
                           Uint32 uniforms) {
    const auto code = load_spv(path);
    if (code.empty())
        return nullptr;
    SDL_GPUShaderCreateInfo ci{};
    ci.code_size = code.size();
    ci.code = code.data();
    ci.entrypoint = stage == SDL_GPU_SHADERSTAGE_VERTEX ? "vsmain" : "fsmain";
    ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    ci.stage = stage;
    ci.num_storage_buffers = storage;
    ci.num_uniform_buffers = uniforms;
    return SDL_CreateGPUShader(dev, &ci);
}

struct Params {
    Uint32 side;
};

// Renders one frame's pass onto `target`; the swapchain and the --shot
// texture take the same path, so the shot verifies the real pipeline.
void record_pass(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target,
                 SDL_GPUGraphicsPipeline *pipeline, gpud::Buffer *field) {
    const Params p{Uint32(xy::L)};
    SDL_PushGPUFragmentUniformData(cmd, 0, &p, sizeof p);
    SDL_GPUColorTargetInfo ct{};
    ct.texture = target;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_GPUBuffer *fb = gpud::sdl::native_buffer(*field);
    SDL_BindGPUFragmentStorageBuffers(pass, 0, &fb, 1);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

// --shot: render W x H offscreen, download, save a BMP, exit. The
// self-check that needs no eyes on the window.
int shoot(SDL_GPUDevice *dev, SDL_GPUGraphicsPipeline *pipeline,
          gpud::Buffer *field, const char *path, Uint32 w, Uint32 h) {
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ti.width = w;
    ti.height = h;
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(dev, &ti);

    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = w * h * 4;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev, &tci);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    record_pass(cmd, tex, pipeline, field);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion reg{};
    reg.texture = tex;
    reg.w = w;
    reg.h = h;
    reg.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(cp, &reg, &dst);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *f = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(dev, true, &f, 1);
    SDL_ReleaseGPUFence(dev, f);

    void *pixels = SDL_MapGPUTransferBuffer(dev, tb, false);
    SDL_Surface *s = SDL_CreateSurfaceFrom(int(w), int(h),
                                           SDL_PIXELFORMAT_RGBA32, pixels,
                                           int(w * 4));
    const bool ok = SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    SDL_UnmapGPUTransferBuffer(dev, tb);
    SDL_ReleaseGPUTransferBuffer(dev, tb);
    SDL_ReleaseGPUTexture(dev, tex);
    std::print("shot: {} {}\n", path, ok ? "written" : SDL_GetError());
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    auto dev = gpud::sdl::try_open();
    if (!dev)
        return std::print("simview: no slang-slot device (GPUD_LOG=1 says "
                          "why)\n"),
               1;
    tensor::SlotDevice sdev{*dev};
    SDL_GPUDevice *nd = gpud::sdl::native_device(*dev);

    xy::Sim sim;
    sim.seed(sdev);

    // The one graphics pipeline: fullscreen triangle, no vertex
    // buffers, colormap fragment reading the field storage buffer.
    const std::string sh = SIMVIEW_SHADER_DIR;
    SDL_GPUShader *vs =
        make_shader(nd, sh + "/vsmain.spv", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader *fs =
        make_shader(nd, sh + "/fsmain.spv", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    if (!vs || !fs)
        return std::print("simview: shaders: {}\n", SDL_GetError()), 1;

    const bool shot = argc >= 3 && std::string(argv[1]) == "--shot";
    SDL_Window *win = nullptr;
    SDL_GPUTextureFormat target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    if (!shot) {
        win = SDL_CreateWindow("simview — xy", int(xy::L) * 3, int(xy::L) * 3,
                               SDL_WINDOW_RESIZABLE);
        if (!win || !SDL_ClaimWindowForGPUDevice(nd, win))
            return std::print("simview: window: {}\n", SDL_GetError()), 1;
        target_format = SDL_GetGPUSwapchainTextureFormat(nd, win);
    }

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = target_format;
    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vs;
    pci.fragment_shader = fs;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &ctd;
    pci.target_info.num_color_targets = 1;
    SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(nd, &pci);
    SDL_ReleaseGPUShader(nd, vs);
    SDL_ReleaseGPUShader(nd, fs);
    if (!pipeline)
        return std::print("simview: pipeline: {}\n", SDL_GetError()), 1;

    if (shot) {
        const int frames = argc >= 4 ? std::atoi(argv[3]) : 120;
        if (argc >= 5)
            sim.T = std::strtof(argv[4], nullptr);
        for (int t = 0; t < frames; ++t)
            sim.step(sdev);
        sim.rewrap(sdev);
        return shoot(nd, pipeline, field_buffer(sim.theta), argv[2],
                     Uint32(xy::L) * 2, Uint32(xy::L) * 2);
    }

    bool running = true;
    bool paused = false;
    Uint64 last = SDL_GetTicksNS();
    int frames = 0;
    // SIMVIEW_AUTOQUIT_MS: exit after the window ran this long and
    // print the mean fps — the README's number without a stopwatch.
    const char *aq = std::getenv("SIMVIEW_AUTOQUIT_MS");
    const Uint64 quit_at =
        aq ? SDL_GetTicksNS() + Uint64(std::atoll(aq)) * 1'000'000 : 0;
    Uint64 total_frames = 0;
    const Uint64 t0 = SDL_GetTicksNS();
    for (Uint64 frame = 0; running; ++frame) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT)
                running = false;
            if (ev.type == SDL_EVENT_KEY_DOWN)
                switch (ev.key.key) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_SPACE:
                    paused = !paused;
                    break;
                case SDLK_UP:
                    sim.T = std::min(2.0f, sim.T + 0.05f);
                    break;
                case SDLK_DOWN:
                    sim.T = std::max(0.05f, sim.T - 0.05f);
                    break;
                case SDLK_RIGHT:
                    sim.over = std::min(8, sim.over + 1);
                    break;
                case SDLK_LEFT:
                    sim.over = std::max(0, sim.over - 1);
                    break;
                case SDLK_R:
                    sim.randomize(sdev);
                    break;
                default:
                    break;
                }
        }
        if (!paused) {
            sim.step(sdev);
            if (frame % 64 == 0)
                sim.rewrap(sdev);
        }

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(nd);
        SDL_GPUTexture *swap = nullptr;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, win, &swap, nullptr,
                                                   nullptr) ||
            !swap) {
            SDL_CancelGPUCommandBuffer(cmd); // minimized: not an error
            continue;
        }
        record_pass(cmd, swap, pipeline, field_buffer(sim.theta));
        SDL_SubmitGPUCommandBuffer(cmd);

        ++frames;
        ++total_frames;
        if (quit_at && SDL_GetTicksNS() > quit_at)
            running = false;
        if (const Uint64 now = SDL_GetTicksNS(); now - last > 1'000'000'000) {
            SDL_SetWindowTitle(
                win, std::format("simview — xy   T={:.2f}  over={}  {} fps",
                                 sim.T, sim.over, frames)
                         .c_str());
            frames = 0;
            last = now;
        }
    }

    if (quit_at)
        std::print("simview: {:.1f} fps mean over {} frames\n",
                   total_frames * 1e9 / double(SDL_GetTicksNS() - t0),
                   total_frames);
    SDL_ReleaseGPUGraphicsPipeline(nd, pipeline);
    SDL_ReleaseWindowFromGPUDevice(nd, win);
    SDL_DestroyWindow(win);
    return 0;
}
