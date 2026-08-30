// The gpud door: the shared device reached through sv::Device, and
// caller-owned gpud buffers drawn through the pull model — both scene
// kinds ask their source at every draw. Refusals named. Tests may
// speak gpud, they are not consumers.
#include "harness/Harness.h"
#include "probe/Probe.h"

#include <simview/gpud.h>

#include <cstdint>
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

    // Particles from the same pull model, over the same field. The
    // cloud is a row of points across the middle of the lattice, in
    // the scene's cell coordinates.
    std::vector<float> pts;
    for (int i = 0; i < 24; ++i) {
        pts.push_back(4.0f + 56.0f * float(i) / 23.0f);
        pts.push_back(float(H) / 2.0f);
    }
    gpud::Buffer cloud = dev.alloc(pts.size() * 4);
    dev.write(cloud, pts.data(), pts.size() * 4);

    CHECK(!app.Particles(gpud::BufferSource{}));

    gpud::BufferSource psrc{
        +[](void *u) { return static_cast<gpud::Buffer *>(u); }, &cloud};
    auto cl = app.Particles(
        psrc, {.color = {1.0f, 0.3f, 0.1f, 1.0f}, .radius = 3.0f});
    REQUIRE(bool(cl));

    CHECK(!cl.Update(pts)); // pulled, not pushed

    const std::uint64_t before_draws = app.Stats().draws;
    Bmp with_points;
    CHECK(harness::shot(app, "gpud_points", with_points));
    // Two items drew, and the second one put pixels on the target the
    // first did not: the count came from the buffer, nothing else
    // could have told the draw how many points there are.
    CHECK_EQ(app.Stats().draws - before_draws, std::uint64_t(2));
    if (!img.px.empty() && !with_points.px.empty())
        CHECK(!similar(img, with_points));

    // A Sync of gpud buffers through the door: the SHOWN slot is what
    // draws. Before the first publish the three slots are empty
    // buffers — nothing draws, nothing crashes. A publish is a new
    // picture at the next frame; no publish, the same picture.
    {
        App app2({.headless = true});
        REQUIRE(bool(app2));
        gpud::Device &d2 = sv::Device(app2);
        Sync<gpud::Buffer> synced;
        auto sf = app2.Field(synced, {.extent = {W, H}});
        REQUIRE(bool(sf));
        CHECK_EQ(probe::gate_count(app2.Raw()), std::size_t(1));
        CHECK(!sf.Update(v)); // pulled, not pushed

        Bmp none;
        CHECK(harness::shot(app2, "sync_none", none));

        auto ramp = [&](bool along_x) {
            std::vector<float> r(W * H);
            for (std::size_t y = 0; y < H; ++y)
                for (std::size_t x = 0; x < W; ++x)
                    r[y * W + x] =
                        along_x ? float(x) / (W - 1) : float(y) / (H - 1);
            gpud::Buffer b = d2.alloc(W * H * 4);
            d2.write(b, r.data(), W * H * 4);
            return b;
        };
        synced.Publish(ramp(false));
        Bmp a;
        CHECK(harness::shot(app2, "sync_a", a));
        if (!a.px.empty()) {
            CHECK(!similar(none, a));
            const int across = a.diff(a.w / 8, a.h / 2, a.w - a.w / 8, a.h / 2);
            const int down = a.diff(a.w / 2, a.h / 8, a.w / 2, a.h - a.h / 8);
            CHECK((across > 60) != (down > 60));
        }
        Bmp again;
        CHECK(harness::shot(app2, "sync_again", again));
        CHECK(similar(a, again));

        synced.Publish(ramp(true));
        Bmp b;
        CHECK(harness::shot(app2, "sync_b", b));
        CHECK(!similar(a, b));
    }

    // Drawn by the main scene AND a view, a Sync is tracked once.
    {
        App app3({.headless = true});
        REQUIRE(bool(app3));
        Sync<gpud::Buffer> twice;
        CHECK(bool(app3.Field(twice, {.extent = {W, H}})));
        CHECK(bool(
            app3.View({.title = "twin"}).Field(twice, {.extent = {W, H}})));
        CHECK_EQ(probe::gate_count(app3.Raw()), std::size_t(1));
    }

    return check::summary("gpud");
}
