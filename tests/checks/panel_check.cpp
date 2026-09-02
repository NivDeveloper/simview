// The panel's widget vocabulary: that a malformed control is refused by
// name at the call that made it, that a group whose body is not showing
// draws NOTHING of it, and that a real click and a real drag reach the
// values behind two widgets.
//
// Tests may speak ImGui — they are not consumers. The window rectangle
// comes from ImGui because a check that hardcoded one would be testing
// the layout, not the widget.
#include "harness/Harness.h"
#include "harness/Input.h"

#include "probe/Probe.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

namespace {

// Where a panel's first widget sits: below the title bar and one
// window padding in. Everything here puts the widget under test FIRST,
// so this is the only geometry the check needs to know.
ImVec2 first_widget(const char *title) {
    const ImGuiWindow *w = ImGui::FindWindowByName(title);
    if (!w)
        return ImVec2(-1.0f, -1.0f);
    const ImGuiStyle &s = ImGui::GetStyle();
    const float bar = ImGui::GetFontSize() + s.FramePadding.y * 2.0f;
    return ImVec2(w->Pos.x + s.WindowPadding.x,
                  w->Pos.y + bar + s.WindowPadding.y +
                      ImGui::GetFrameHeight() * 0.5f);
}

int vertices(sv::App &app) {
    app.Step();
    return ImGui::GetDrawData()->TotalVtxCount;
}

} // namespace

int main() {
    harness::begin();
    using namespace sv;

    App app({.headless = true});
    if (!app)
        return check::skip("panel", LastError());

    // Every refusal names the widget and what it wanted. A control that
    // silently does nothing is the failure this prevents.
    int index = 0;
    float f = 0.0f;
    {
        Panel p = app.Panel("refusals");
        CHECK(p);
        float *none = nullptr;
        CHECK(!impl::panel_widget(
            p.Raw(),
            impl::WidgetDesc{.label = "x", .kind = impl::WidgetKind::Slider}));
        CHECK(std::string(LastError()).find("needs a value to move") !=
              std::string::npos);
        CHECK(!impl::panel_widget(
            p.Raw(), impl::WidgetDesc{.label = "x",
                                      .kind = impl::WidgetKind::Choice,
                                      .target = &index}));
        CHECK(std::string(LastError()).find("can select nothing") !=
              std::string::npos);
        CHECK(!impl::panel_widget(
            p.Raw(), impl::WidgetDesc{.label = "x",
                                      .kind = impl::WidgetKind::Progress}));
        CHECK(std::string(LastError()).find("callable to read") !=
              std::string::npos);
        (void)none;

        // An icon control has no visible label, so the tooltip is the
        // only place its name can appear — and a control nobody can
        // name is one the reader has to click to find out about.
        CHECK(!impl::panel_widget(
            p.Raw(), impl::WidgetDesc{.kind = impl::WidgetKind::IconButton,
                                      .icon = Icon::Gear}));
        CHECK(std::string(LastError()).find("an icon is not a name") !=
              std::string::npos);
        CHECK(!impl::panel_widget(
            p.Raw(), impl::WidgetDesc{.label = "grid",
                                      .kind = impl::WidgetKind::IconToggle,
                                      .icon = Icon::Grid}));
        CHECK(std::string(LastError()).find("needs a value to toggle") !=
              std::string::npos);

        // A tab only exists inside a bar, and a bar draws tabs and
        // nothing else: both are ImGui assertions inside a draw nobody
        // is watching, so they are refused at registration instead.
        CHECK(!impl::panel_widget(
            p.Raw(), impl::WidgetDesc{.label = "t",
                                      .kind = impl::WidgetKind::GroupBegin,
                                      .group = impl::Group::Tab}));
        CHECK(std::string(LastError()).find("only exists inside Tabs") !=
              std::string::npos);
        p.Tabs([&](Panel &q) { q.Slider("stray", f, 0.0f, 1.0f); });
        CHECK(std::string(LastError()).find("holds tabs and nothing else") !=
              std::string::npos);
    }

    // A group whose body is not showing draws none of it. The two
    // panels differ ONLY in what the tab that is NOT in front holds, so
    // equal geometry is the assertion that the walk skipped it.
    const char *filler = "a line of text with real glyphs in it";
    App a1({.headless = true}), a2({.headless = true});
    a1.Panel("tabs").Tabs([&](Panel &p) {
        p.Tab("front", [&](Panel &q) {
             q.Text(filler);
         }).Tab("back", [&](Panel &q) { q.Text(filler); });
    });
    a2.Panel("tabs").Tabs([&](Panel &p) {
        p.Tab("front", [&](Panel &q) {
             q.Text(filler);
         }).Tab("back", [](Panel &) {});
    });
    a1.Step();
    a2.Step();
    const int with = vertices(a1), without = vertices(a2);
    CHECK_EQ(with, without);
    CHECK_GT(with, 0);

    // A click reaches the callback behind a Button.
    int clicks = 0;
    App b({.headless = true});
    b.Panel("click").Button("go", [&] { ++clicks; });
    b.Step();
    const ImVec2 at = first_widget("click");
    CHECK_GT(at.x, 0.0f);
    input::move(b, at.x + 12.0f, at.y);
    input::press(b);
    input::release(b);
    CHECK_EQ(clicks, 1);

    // And behind an IconToggle, which has no label to click on — only
    // the square the glyph is drawn in.
    bool shown = false;
    App d({.headless = true});
    d.Panel("toggle").IconToggle(Icon::Eye, "show it", shown);
    d.Step();
    const ImVec2 t0 = first_widget("toggle");
    CHECK_GT(t0.x, 0.0f);
    input::move(d, t0.x + 10.0f, t0.y + 10.0f);
    input::press(d);
    input::release(d);
    CHECK(shown);

    // A drag reaches the value behind a Slider: from its left edge to
    // well past its right lands on the maximum.
    float speed = 1.0f;
    App c({.headless = true});
    c.Panel("drag").Slider("speed", speed, 0.0f, 10.0f);
    c.Step();
    const ImVec2 s0 = first_widget("drag");
    CHECK_GT(s0.x, 0.0f);
    input::drag(c, s0.x + 4.0f, s0.y, s0.x + 900.0f, s0.y);
    CHECK_EQ(speed, 10.0f);

    return check::summary("panel");
}
