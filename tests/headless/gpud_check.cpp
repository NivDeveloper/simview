// The gpud door: the shared device reached through sv::Device, a
// caller-owned gpud buffer drawn through the pull model (the field
// asks the source at every draw), refusals named. Tests may speak
// gpud — they are not consumers.
#include "Harness.h"

#include <simview/gpud.h>

#include <vector>

int main() {
    harness::begin();
    using namespace sv;

    App app({.headless = true});
    if (!app)
        return check::skip("gpud", LastError());
    gpud::Device &dev = sv::Device(app);

    // A caller-owned buffer holding a ramp along ONE axis, via pure
    // gpud — no raw SDL anywhere in this test.
    constexpr std::size_t W = 64, H = 64;
    std::vector<float> v(W * H);
    for (std::size_t y = 0; y < H; ++y)
        for (std::size_t x = 0; x < W; ++x)
            v[y * W + x] = float(y) / (H - 1);
    gpud::Buffer ramp = dev.alloc(W * H * 4);
    dev.write(ramp, v.data(), W * H * 4);

    // A source that cannot answer is refused before anything is drawn.
    CHECK(!app.Field(gpud::BufferSource{}, {.extent = {W, H}}));

    gpud::BufferSource src{
        +[](void *u) { return static_cast<gpud::Buffer *>(u); }, &ramp};
    auto f = app.Field(src, {.extent = {W, H}});
    REQUIRE(bool(f));

    CHECK(!f.Update(v)); // pulled, not pushed

    Bmp img;
    CHECK(harness::shot(app, "gpud", img));
    if (!img.px.empty()) {
        // A one-axis ramp: the image varies along one axis and holds
        // still along the other, whichever way the shader maps it.
        const int across =
            img.diff(img.w / 8, img.h / 2, img.w - img.w / 8, img.h / 2);
        const int down =
            img.diff(img.w / 2, img.h / 8, img.w / 2, img.h - img.h / 8);
        CHECK((across > 60) != (down > 60));
        CHECK_GT(img.distinct(), std::size_t(4));
    }

    return check::summary("gpud");
}
