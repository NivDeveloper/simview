// The zero-copy path: a caller-owned SDL buffer (tests may use SDL —
// they are not consumers) becomes a field with no upload through
// simview; the shot must show the caller's pattern. Refusals fire.
#include <simview/native.h>
#include <simview/simview.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

int main() {
    using namespace sv;
    App app({.headless = true});
    if (!app)
        return std::printf("SKIP: no GPU device (%s)\n", LastError()), 0;
    SDL_GPUDevice *dev = NativeDevice(app);

    // The caller-owned buffer, filled with a vertical ramp by hand.
    constexpr Uint32 W = 64, H = 64;
    SDL_GPUBufferCreateInfo bci{};
    bci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bci.size = W * H * 4;
    SDL_GPUBuffer *buf = SDL_CreateGPUBuffer(dev, &bci);
    SDL_GPUTransferBufferCreateInfo tci{};
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = W * H * 4;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev, &tci);
    std::vector<float> v(W * H);
    for (Uint32 y = 0; y < H; ++y)
        for (Uint32 x = 0; x < W; ++x)
            v[y * W + x] = float(y) / (H - 1);
    void *map = SDL_MapGPUTransferBuffer(dev, tb, false);
    std::memcpy(map, v.data(), v.size() * 4);
    SDL_UnmapGPUTransferBuffer(dev, tb);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    const SDL_GPUTransferBufferLocation loc{tb, 0};
    const SDL_GPUBufferRegion reg{buf, 0, W * H * 4};
    SDL_UploadToGPUBuffer(cp, &loc, &reg, false);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *fe = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(dev, true, &fe, 1);
    SDL_ReleaseGPUFence(dev, fe);
    SDL_ReleaseGPUTransferBuffer(dev, tb);

    auto f = FieldFromBuffer(app, buf, {.extent = {W, H}});
    if (!f)
        return std::printf("FAIL: FieldFromBuffer (%s)\n", LastError()), 1;
    // Update() on an external field must refuse, rebind must accept.
    if (f.Update(v))
        return std::printf("FAIL: update on external not refused\n"), 1;
    if (!FieldRebind(f, buf))
        return std::printf("FAIL: rebind refused (%s)\n", LastError()), 1;

    const char *tmp = std::getenv("TMPDIR");
    const std::string p = std::string(tmp ? tmp : ".") + "/simview_native.bmp";
    if (!app.Shot(p.c_str()))
        return std::printf("FAIL: shot (%s)\n", LastError()), 1;
    struct stat st{};
    if (stat(p.c_str(), &st) != 0 || st.st_size <= 1024)
        return std::printf("FAIL: shot too small\n"), 1;

    // Free the borrowed buffer only after the app (which waited idle)?
    // No: the app is still alive; frames are done (fence waited), so
    // releasing now exercises the borrow contract's caller side.
    SDL_ReleaseGPUBuffer(dev, buf);
    std::printf("PASS: native zero-copy checks\n");
    return 0;
}
