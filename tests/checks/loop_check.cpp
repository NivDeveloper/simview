// The headless loop contract: Step fires frame callbacks in
// registration order and delivers posted events, quit is idempotent,
// and a headless Run returns rather than blocking.
#include "harness/Harness.h"
#include "probe/Probe.h"

#include <cstdint>
#include <string>
#include <vector>

int main() {
    harness::begin();
    using namespace sv;

    App app({.headless = true});
    if (!app)
        return check::skip("loop", LastError());

    std::vector<int> order;
    app.OnFrame([&] { order.push_back(1); });
    app.OnFrame([&] { order.push_back(2); });
    app.Step();
    app.Step();
    CHECK_EQ(order.size(), std::size_t(4));
    CHECK(order == std::vector<int>({1, 2, 1, 2}));

    // Posted events reach the callbacks Step drives — the automation
    // seam, and the only way input is testable at all.
    int space = 0, other = 0;
    app.OnKey(Key::Space, [&] { ++space; });
    app.OnEvent([&](const Event &e) {
        if (!Is(e, Key::Space))
            ++other;
    });
    app.PostEvent(KeyDown(Key::Space));
    app.PostEvent(KeyDown(Key::Escape));
    CHECK_EQ(space, 0); // nothing is delivered before a Step
    app.Step();
    CHECK_EQ(space, 1);
    CHECK_EQ(other, 1);
    app.Step();
    CHECK_EQ(space, 1); // the queue is drained, not replayed

    // A key REPEAT is not a press: OnKey ignores it.
    app.PostEvent(KeyDown(Key::Space, true));
    app.Step();
    CHECK_EQ(space, 1);

    // The counters: a Step draws nothing (no window, no shot), and a
    // field with no new data uploads once and only once.
    const auto before = app.Stats();
    CHECK_EQ(before.frames, std::uint64_t(0));
    auto field = app.Field({.extent = {8, 8}});
    std::vector<float> v(64, 0.5f);
    CHECK(field.Update(v));
    Bmp img;
    CHECK(harness::shot(app, "loop", img));
    CHECK_EQ(app.Stats().uploads, std::uint64_t(1));
    CHECK_EQ(app.Stats().frames, std::uint64_t(1));
    CHECK(harness::shot(app, "loop", img));
    CHECK_EQ(app.Stats().uploads, std::uint64_t(1)); // unchanged: no re-upload
    CHECK_EQ(app.Stats().frames, std::uint64_t(2));
    CHECK_EQ(app.Stats().pipelines,
             std::uint64_t(1)); // one format, one pipeline

    // The frame flips every tracked Sync ONCE, before its callbacks:
    // a Publish before a Step is what that Step's callbacks see.
    // Tracked twice, counted once.
    Sync<int> s;
    impl::scene_track(app.Scene().Raw(), s.Gate());
    impl::scene_track(app.Scene().Raw(), s.Gate());
    CHECK_EQ(probe::gate_count(app.Raw()), std::size_t(1));
    s.Publish();
    std::uint64_t seen = 0;
    app.OnFrame([&] { seen = s.Generation(); });
    app.Step();
    CHECK_EQ(seen, std::uint64_t(1));
    CHECK_EQ(s.Generation(), std::uint64_t(1));

    app.RequestQuit();
    app.RequestQuit(); // idempotent
    app.Run();         // headless: returns immediately

    return check::summary("loop");
}
