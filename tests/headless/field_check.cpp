// The field's headless verification (what examples must never carry):
// colormap shots, refusal gates, update-then-shot freshness. Prints
// SKIP with the reason when no device exists — a skip and a pass never
// print the same line.
#include "Bmp.h"

#include <simview/simview.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {
long size_of(const std::string &p) {
    struct stat st{};
    return stat(p.c_str(), &st) == 0 ? long(st.st_size) : -1;
}
int fail(const char *what) {
    return std::printf("FAIL: %s (%s)\n", what, sv::LastError()), 1;
}
} // namespace

int main() {
    using namespace sv;
    const char *tmp = std::getenv("TMPDIR");
    const std::string dir = tmp ? tmp : ".";

    {
        App probe({.headless = true});
        if (!probe)
            return std::printf("SKIP: no GPU device (%s)\n", LastError()), 0;
    }

    // The three colormaps over a diagonal ramp, each a real image.
    const Colormap maps[] = {Colormap::Gray, Colormap::Hue, Colormap::Viridis};
    for (int m = 0; m < 3; ++m) {
        App app({.headless = true});
        auto f = app.Field({.extent = {128, 128}, .map = maps[m]});
        if (!f)
            return fail("field_create");
        // A SQUARE diagonal ramp: (x+y) makes the anti-diagonal
        // corners exactly equal, which is what the shot asserts.
        std::vector<float> v(128 * 128);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
                v[y * 128 + x] = (x + y) / float(2 * 128 - 2);
        if (!f.Update(v))
            return fail("field_update");
        const std::string p =
            dir + "/simview_field_" + std::to_string(m) + ".bmp";
        if (!app.Shot(p.c_str()))
            return fail("shot");
        if (size_of(p) <= 1024)
            return fail("shot too small");

        // The ramp is (x+y): its ANTI-diagonal corners hold the same
        // value and its diagonal corners the extremes — a statement
        // about the drawn pixels that holds whichever way the shader
        // maps the grid onto the window.
        Bmp img;
        if (!load_bmp(p, img))
            return fail("shot is not a readable BMP");
        const unsigned lo_x = img.w / 8, hi_x = img.w - img.w / 8;
        const unsigned lo_y = img.h / 8, hi_y = img.h - img.h / 8;
        if (img.diff(lo_x, lo_y, hi_x, hi_y) < 150)
            return fail("the ramp's ends are not far apart");
        if (img.diff(hi_x, lo_y, lo_x, hi_y) > 60)
            return fail("the ramp's equal-value corners disagree");
    }

    // The refusal gates, each with its sentence.
    App app({.headless = true});
    auto f1 = app.Field({.extent = {8, 8}});
    if (!f1)
        return fail("first field");
    if (app.Field({.extent = {8, 8}}))
        return std::printf("FAIL: second field was not refused\n"), 1;
    double d[64]{};
    if (impl::field_update(impl::Field{app.Raw()}, d, DType::f64, 64))
        return std::printf("FAIL: f64 was not refused\n"), 1;
    float shortv[8]{};
    if (impl::field_update(impl::Field{app.Raw()}, shortv, DType::f32, 8))
        return std::printf("FAIL: wrong count was not refused\n"), 1;

    std::printf("PASS: field checks\n");
    return 0;
}
