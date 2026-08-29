// The gpud door: the shared device reached through sv::Device, a
// caller-owned gpud buffer drawn through the pull model (the field
// asks the source at every draw), refusals named. Tests may speak
// gpud — they are not consumers.
#include <simview/gpud.h>
#include <simview/simview.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <vector>

int main() {
    using namespace sv;
    App app({.headless = true});
    if (!app)
        return std::printf("SKIP: no GPU device (%s)\n", LastError()), 0;
    gpud::Device &dev = sv::Device(app);

    // A caller-owned buffer holding a vertical ramp, via pure gpud.
    constexpr std::size_t W = 64, H = 64;
    std::vector<float> v(W * H);
    for (std::size_t y = 0; y < H; ++y)
        for (std::size_t x = 0; x < W; ++x)
            v[y * W + x] = float(y) / (H - 1);
    gpud::Buffer ramp = dev.alloc(W * H * 4);
    dev.write(ramp, v.data(), W * H * 4);

    // The refusal a null source earns, before any real field exists.
    if (app.Field(gpud::BufferSource{}, {.extent = {W, H}}))
        return std::printf("FAIL: null source not refused\n"), 1;

    gpud::BufferSource src{
        +[](void *u) { return static_cast<gpud::Buffer *>(u); }, &ramp};
    auto f = app.Field(src, {.extent = {W, H}});
    if (!f)
        return std::printf("FAIL: Field from source (%s)\n", LastError()), 1;

    // Update on a pulled field must refuse; a second field must too.
    if (f.Update(v))
        return std::printf("FAIL: update on external not refused\n"), 1;
    if (app.Field(src, {.extent = {W, H}}))
        return std::printf("FAIL: second field not refused\n"), 1;

    const char *tmp = std::getenv("TMPDIR");
    const std::string p = std::string(tmp ? tmp : ".") + "/simview_gpud.bmp";
    if (!app.Shot(p.c_str()))
        return std::printf("FAIL: shot (%s)\n", LastError()), 1;
    struct stat st{};
    if (stat(p.c_str(), &st) != 0 || st.st_size <= 1024)
        return std::printf("FAIL: shot too small\n"), 1;

    std::printf("PASS: gpud door checks\n");
    return 0;
}
