// The field's headless verification: every colormap draws a real
// picture, the ramp's geometry survives into the pixels, and every
// refusal fires with its sentence.
#include "harness/Harness.h"

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
    // The argument refusals, on a REAL field handle. (These once
    // passed an App pointer through a Field handle, which read an
    // App's bytes as an item's and happened to refuse on arithmetic.
    // It was undefined behaviour that looked like a test.)
    double d[64]{};
    CHECK(!impl::field_update(f1.Raw(), d, DType::f64, 64));
    float shortv[8]{};
    CHECK(!impl::field_update(f1.Raw(), shortv, DType::f32, 8));

    // A handle of the WRONG KIND is refused BY NAME. The sentence is
    // the assertion, not the false: read as another kind's bytes, the
    // arithmetic checks refuse too — for the wrong reason — so a bare
    // `!update(...)` here passes with no kind check at all. Measured:
    // it did.
    auto wrong = app.Particles({.radius = 2.0f});
    REQUIRE(bool(wrong));
    CHECK(!impl::field_update(impl::Field{wrong.Raw().p}, small.data(),
                              DType::f32, 64));
    CHECK(std::string(LastError()).find("not a field handle") !=
          std::string::npos);
    CHECK(
        !impl::particles_update(impl::Particles{f1.Raw().p}, small.data(), 32));
    CHECK(std::string(LastError()).find("not a particles handle") !=
          std::string::npos);

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

    // A named range may reach below zero — a phase-space plot is the
    // case. If the scene refused it and fell back to the unit square,
    // every one of these points would land far off the target and the
    // shot would be nothing but the clear colour.
    {
        App app3({.headless = true});
        if (app3) {
            app3.SceneRange({0.0, -80.0, 100.0, 80.0});
            auto cloud = app3.Particles(
                {.color = {1.0f, 1.0f, 1.0f, 1.0f}, .radius = 6.0f});
            REQUIRE(bool(cloud));
            const float pts[] = {50.0f, -70.0f, 50.0f, 70.0f, 20.0f, 0.0f};
            CHECK(cloud.Update(pts));

            Bmp img;
            CHECK(harness::shot(app3, "phase", img));
            if (!img.px.empty()) {
                const Bmp::Box box = img.content({23, 23, 26});
                CHECK(!box.empty);
                CHECK_GT(box.h(), img.h / 2);
            }
        }
    }

    return check::summary("field");
}
