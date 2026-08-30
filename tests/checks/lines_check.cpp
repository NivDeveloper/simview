// The third scene kind, proven by pixels: a segment lands where the
// scene range says it should, its refusals name themselves, and a
// handle of the wrong kind is refused.
//
// This check exists as much for what it MEASURES as for what it
// asserts. Lines was written after the kinds became data, to count
// what a new kind costs; the commit that adds it says how many files
// outside the kind's own had to change.
#include "harness/Harness.h"

#include <cstdint>
#include <string>
#include <vector>

int main() {
    harness::begin();
    using namespace sv;

    App app({.headless = true});
    if (!app)
        return check::skip("lines", LastError());

    // A 100x100 range, so a coordinate IS a percentage of the target.
    app.SceneRange({0.0, 0.0, 100.0, 100.0});
    auto lines = app.Lines({.color = {1.0f, 1.0f, 1.0f, 1.0f}, .width = 6.0f});
    REQUIRE(bool(lines));

    // One horizontal segment across the middle, from 10% to 90%.
    const float seg[] = {10.0f, 50.0f, 90.0f, 50.0f};
    CHECK(lines.Update(seg));

    Bmp img;
    REQUIRE(harness::shot(app, "lines", img));
    CHECK_EQ(app.Stats().draws, std::uint64_t(1));

    // The segment is bright where it is and absent where it is not:
    // the middle row lights up along the run, the rows a quarter of the
    // way up and down do not.
    const unsigned mid = img.h / 2;
    const unsigned q = img.h / 4;
    const auto &on = img.at(img.w / 2, mid);
    const auto &off_above = img.at(img.w / 2, q);
    const auto &off_below = img.at(img.w / 2, img.h - q);
    CHECK_GT(on[0] + on[1] + on[2], 500);
    CHECK_LT(off_above[0] + off_above[1] + off_above[2], 120);
    CHECK_LT(off_below[0] + off_below[1] + off_below[2], 120);

    // It STOPS at 90%: past the end there is only the clear colour.
    const auto &past = img.at(img.w * 95 / 100, mid);
    CHECK_LT(past[0] + past[1] + past[2], 120);
    // And it is a line, not a fill: the content box is wide and thin.
    const Bmp::Box box = img.content({23, 23, 26});
    CHECK(!box.empty);
    CHECK_GT(box.w(), box.h() * 8);

    // An empty set is not an error; a width must be real.
    CHECK(lines.Update(std::span<const float>()));
    CHECK(!app.Lines({.width = 0.0f}));
    CHECK(std::string(LastError()).find("width above zero") !=
          std::string::npos);

    // The wrong kind is refused BY NAME.
    auto pts = app.Particles({.radius = 2.0f});
    REQUIRE(bool(pts));
    CHECK(!impl::lines_update(impl::Lines{pts.Raw().p}, seg, 1));
    CHECK(std::string(LastError()).find("not a lines handle") !=
          std::string::npos);

    return check::summary("lines");
}
