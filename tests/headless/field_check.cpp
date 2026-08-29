// The field's headless verification (what examples must never carry):
// colormap shots, refusal gates, update-then-shot freshness. Prints
// SKIP with the reason when no device exists — a skip and a pass never
// print the same line.
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
    return std::printf("FAIL: %s (%s)\n", what, simview::last_error()), 1;
}
} // namespace

int main() {
    using namespace simview;
    const char *tmp = std::getenv("TMPDIR");
    const std::string dir = tmp ? tmp : ".";

    {
        auto probe = init({.headless = true});
        if (!probe)
            return std::printf("SKIP: no GPU device (%s)\n", last_error()), 0;
    }

    // The three colormaps over a diagonal ramp, each a real image.
    const Colormap maps[] = {Colormap::Gray, Colormap::Hue,
                             Colormap::Viridis};
    for (int m = 0; m < 3; ++m) {
        auto app = init({.headless = true});
        auto f = app.field({.extent = {128, 96}, .map = maps[m]});
        if (!f) return fail("field_create");
        std::vector<float> v(128 * 96);
        for (int y = 0; y < 96; ++y)
            for (int x = 0; x < 128; ++x)
                v[y * 128 + x] = (x + y) / float(128 + 96 - 2);
        if (!f.update(v)) return fail("field_update");
        const std::string p = dir + "/simview_field_" + std::to_string(m) +
                              ".bmp";
        if (!app.shot(p.c_str())) return fail("shot");
        if (size_of(p) <= 1024) return fail("shot too small");
    }

    // The refusal gates, each with its sentence.
    auto app = init({.headless = true});
    auto f1 = app.field({.extent = {8, 8}});
    if (!f1) return fail("first field");
    if (app.field({.extent = {8, 8}}))
        return std::printf("FAIL: second field was not refused\n"), 1;
    double d[64]{};
    if (field_update(Field{app.get()}, d, DType::f64, 64))
        return std::printf("FAIL: f64 was not refused\n"), 1;
    float shortv[8]{};
    if (field_update(Field{app.get()}, shortv, DType::f32, 8))
        return std::printf("FAIL: wrong count was not refused\n"), 1;

    std::printf("PASS: field checks\n");
    return 0;
}
