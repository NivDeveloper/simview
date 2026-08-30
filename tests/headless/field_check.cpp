// The field's headless verification: every colormap draws a real
// picture, the ramp's geometry survives into the pixels, and every
// refusal fires with its sentence.
#include "Harness.h"

#include <vector>

int main() {
    harness::begin();
    using namespace sv;

    App probe({.headless = true});
    if (!probe)
        return check::skip("field", LastError());

    // A SQUARE diagonal ramp: (x+y) makes the anti-diagonal corners
    // exactly equal, which is what the shots assert.
    constexpr unsigned N = 128;
    std::vector<float> v(N * N);
    for (unsigned y = 0; y < N; ++y)
        for (unsigned x = 0; x < N; ++x)
            v[y * N + x] = (x + y) / float(2 * N - 2);

    const Colormap maps[] = {Colormap::Gray, Colormap::Hue, Colormap::Viridis};
    const char *names[] = {"gray", "hue", "viridis"};
    for (int m = 0; m < 3; ++m) {
        App app({.headless = true});
        auto f = app.Field({.extent = {N, N}, .map = maps[m]});
        CHECK(bool(f));
        CHECK(f.Update(v));

        Bmp img;
        CHECK(harness::shot(app, names[m], img));
        if (img.px.empty())
            continue;

        // A picture, not a fill.
        CHECK_GT(img.distinct(), std::size_t(8));

        const unsigned lo = img.w / 8, hi = img.w - img.w / 8;
        const unsigned t = img.h / 8, b = img.h - img.h / 8;
        // The ramp's ends are far apart; its equal-value corners agree.
        CHECK_GT(img.diff(lo, t, hi, b), 150);
        CHECK_LT(img.diff(hi, t, lo, b), 60);
    }

    // The refusals, each with its sentence.
    App app({.headless = true});
    auto f1 = app.Field({.extent = {8, 8}});
    CHECK(bool(f1));

    // A scene is a LIST: a second field is another item, and both
    // draw into the one pass.
    auto f2 = app.Field({.extent = {8, 8}});
    CHECK(bool(f2));
    std::vector<float> small(64, 0.5f);
    CHECK(f1.Update(small));
    CHECK(f2.Update(small));
    Bmp two;
    CHECK(harness::shot(app, "two", two));
    CHECK_EQ(app.Stats().draws, std::uint64_t(2));
    CHECK_EQ(app.Stats().frames, std::uint64_t(1)); // a frame, not a field
    double d[64]{};
    CHECK(!impl::field_update(impl::Field{app.Raw()}, d, DType::f64, 64));
    float shortv[8]{};
    CHECK(!impl::field_update(impl::Field{app.Raw()}, shortv, DType::f32, 8));

    // Particles are the second scene kind: same list, same pass,
    // composited over whatever came before them.
    {
        App app2({.headless = true});
        auto bg = app2.Field({.extent = {32, 32}});
        std::vector<float> flat(32 * 32, 0.0f);
        CHECK(bg.Update(flat));
        Bmp before;
        CHECK(harness::shot(app2, "nopoints", before));

        auto pts =
            app2.Particles({.color = {1.0f, 1.0f, 1.0f, 1.0f}, .radius = 6.0f});
        CHECK(bool(pts));
        // Cell coordinates: the scene's range defaults to the field's
        // grid, so a point at (16,16) sits at the lattice centre.
        const float xy[] = {16.0f, 16.0f, 8.0f, 8.0f, 24.0f, 24.0f};
        CHECK(pts.Update(xy));
        Bmp after;
        CHECK(harness::shot(app2, "points", after));

        CHECK_EQ(app2.Stats().draws, std::uint64_t(3)); // 1 + 2 items
        CHECK(!similar(before, after));                 // they really drew
        CHECK_GT(after.distinct(), before.distinct());

        // An empty cloud is not an error, and a radius must be real.
        CHECK(pts.Update(std::span<const float>()));
        CHECK(!app2.Particles({.radius = 0.0f}));
    }

    return check::summary("field");
}
