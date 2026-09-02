#include "Icons.h"
#include "PlotState.h"

#include "../core/App.h"
#include "Ui.h"

#include <imgui.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sv {
namespace {

// Numbers wear the monospaced face. Digits that do not share a width
// make a column of readouts jump as the last figure changes, which is
// the one thing a reader is watching.
struct Numerals {
    explicit Numerals(::ImFont *f) {
        ImGui::PushFont(f, ImGui::GetStyle().FontSizeBase * 0.94f);
    }
    ~Numerals() { ImGui::PopFont(); }
};

// Every control READS the Executor and WRITES it back, so the panel,
// the keys and the code cannot disagree about what the sim is doing.
void transport_draw(void *target, ::ImFont *mono) {
    const impl::Executor ex{target};
    ImGui::PushID(target);
    const bool playing = impl::executor_playing(ex);
    if (impl::icon_button(playing ? Icon::Pause : Icon::Play, "tp_play",
                          playing ? "pause" : "play"))
        playing ? impl::executor_pause(ex) : impl::executor_play(ex);
    ImGui::SameLine();
    if (impl::icon_button(Icon::Step, "tp_step", "one step"))
        impl::executor_advance(ex, 1);
    ImGui::SameLine();

    // "Run N": the jump vklib could not have, because only an Executor
    // that counts can promise EXACTLY N.
    static int run_n = 100;
    ImGui::SetNextItemWidth(72.0f);
    ImGui::InputInt("##n", &run_n, 0, 0);
    ImGui::SameLine();
    if (impl::icon_button(Icon::Forward, "tp_run", "run that many steps") &&
        run_n > 0)
        impl::executor_advance(ex, std::uint64_t(run_n));
    ImGui::SameLine();
    if (impl::icon_button(Icon::Restart, "tp_restart", "restart"))
        impl::executor_restart(ex);

    {
        const Tick t = impl::executor_tick(ex);
        const Numerals face(mono);
        ImGui::Text("n = %llu   t = %.4g   %.1f /s",
                    static_cast<unsigned long long>(t.n), t.time,
                    impl::executor_rate(ex));
    }

    // Speed: vklib's four presets, PLUS the slider it never exposed —
    // a preset list was the thing users could not get past.
    static constexpr struct {
        const char *label;
        std::uint64_t ns;
    } kPresets[] = {{"uncapped", 0},
                    {"60 /s", 16'666'667},
                    {"10 /s", 100'000'000},
                    {"1 /s", 1'000'000'000}};
    const std::uint64_t cur = impl::executor_delay_ns(ex);
    int idx = -1;
    for (int i = 0; i < 4; ++i)
        if (kPresets[i].ns == cur)
            idx = i;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::BeginCombo("##speed",
                          idx >= 0 ? kPresets[idx].label : "custom")) {
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable(kPresets[i].label, i == idx))
                impl::executor_set_delay_ns(ex, kPresets[i].ns);
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    // Log slider over 0..1 s: a delay is a ratio, not a sum.
    float ms = float(cur) / 1e6f;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##delay", &ms, 0.0f, 1000.0f, "%.1f ms",
                           ImGuiSliderFlags_Logarithmic))
        impl::executor_set_delay_ns(ex, std::uint64_t(ms * 1e6f));
    ImGui::PopID();
}

void choice_draw(impl::WidgetState &w) {
    int *sel = static_cast<int *>(w.target);
    const int n = int(w.options.size());
    *sel = std::clamp(*sel, 0, n - 1);
    if (!ImGui::BeginCombo(w.label.c_str(),
                           w.options[std::size_t(*sel)].c_str()))
        return;
    for (int i = 0; i < n; ++i)
        if (ImGui::Selectable(w.options[std::size_t(i)].c_str(), i == *sel))
            *sel = i;
    ImGui::EndCombo();
}

void value_draw(impl::WidgetState &w, ::ImFont *mono) {
    // Bound or pulled, the same duality a series has.
    const double v =
        w.value ? w.value(w.user) : double(*static_cast<float *>(w.target));
    char buf[64];
    std::snprintf(buf, sizeof buf, w.fmt.c_str(), v);
    const Numerals face(mono);
    ImGui::LabelText(w.label.c_str(), "%s", buf);
}

void progress_draw(impl::WidgetState &w, ::ImFont *mono) {
    const double v = w.value(w.user);
    const float span = w.max - w.min;
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.3g", v);
    const Numerals face(mono);
    ImGui::ProgressBar(span != 0.0f ? float((v - w.min) / span) : 0.0f,
                       ImVec2(0.0f, 0.0f), buf);
    ImGui::SameLine();
    ImGui::TextUnformatted(w.label.c_str());
}

void widget_draw(impl::WidgetState &w, ::ImFont *mono) {
    switch (w.kind) {
    case impl::WidgetKind::Text:
        ImGui::TextUnformatted(w.label.c_str());
        break;
    case impl::WidgetKind::Separator:
        w.label.empty() ? ImGui::Separator()
                        : ImGui::SeparatorText(w.label.c_str());
        break;
    case impl::WidgetKind::Help:
        // Pinned to the widget before it, and hover-only: a panel that
        // spells every parameter out in full is a panel nobody reads.
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", w.label.c_str());
        break;
    case impl::WidgetKind::Button:
        // The click runs while the UI frame is being BUILT, which is
        // CPU-only — no command buffer is open, so a callback that
        // touches the GPU cannot stomp one.
        if (ImGui::Button(w.label.c_str()) && w.on_click)
            w.on_click(w.user);
        break;
    case impl::WidgetKind::Slider:
        ImGui::SliderFloat(w.label.c_str(), static_cast<float *>(w.target),
                           w.min, w.max, "%.4g",
                           w.scale == Scale::Log ? ImGuiSliderFlags_Logarithmic
                                                 : ImGuiSliderFlags_None);
        break;
    case impl::WidgetKind::SliderInt:
        ImGui::SliderInt(w.label.c_str(), static_cast<int *>(w.target),
                         int(w.min), int(w.max));
        break;
    case impl::WidgetKind::SliderVec3:
        ImGui::SliderFloat3(w.label.c_str(), static_cast<float *>(w.target),
                            w.min, w.max);
        break;
    case impl::WidgetKind::Drag:
        ImGui::DragFloat(w.label.c_str(), static_cast<float *>(w.target),
                         w.speed, w.min, w.max);
        break;
    case impl::WidgetKind::InputFloat:
        ImGui::InputFloat(w.label.c_str(), static_cast<float *>(w.target));
        break;
    case impl::WidgetKind::InputInt:
        ImGui::InputInt(w.label.c_str(), static_cast<int *>(w.target));
        break;
    case impl::WidgetKind::Choice:
        choice_draw(w);
        break;
    case impl::WidgetKind::Color:
        ImGui::ColorEdit3(w.label.c_str(), static_cast<float *>(w.target));
        break;
    case impl::WidgetKind::Checkbox:
        ImGui::Checkbox(w.label.c_str(), static_cast<bool *>(w.target));
        break;
    case impl::WidgetKind::Value:
        value_draw(w, mono);
        break;
    case impl::WidgetKind::Progress:
        progress_draw(w, mono);
        break;
    case impl::WidgetKind::IconButton:
        if (impl::icon_button(w.icon, w.id.c_str(), w.label.c_str()) &&
            w.on_click)
            w.on_click(w.user);
        break;
    case impl::WidgetKind::IconToggle: {
        bool &on = *static_cast<bool *>(w.target);
        if (impl::icon_button(w.icon, w.id.c_str(), w.label.c_str(), on))
            on = !on;
        break;
    }
    case impl::WidgetKind::Transport:
        transport_draw(w.target, mono);
        break;
    case impl::WidgetKind::GroupBegin:
    case impl::WidgetKind::GroupEnd:
        break;
    }
}

bool group_on(const impl::WidgetState &w) {
    return w.value ? w.value(w.user) != 0.0 : *static_cast<bool *>(w.target);
}

bool group_begin(impl::WidgetState &w) {
    switch (w.group) {
    case impl::Group::Section:
        return ImGui::CollapsingHeader(w.label.c_str(),
                                       ImGuiTreeNodeFlags_DefaultOpen);
    case impl::Group::Tabs:
        return ImGui::BeginTabBar(w.id.c_str());
    case impl::Group::Tab:
        return ImGui::BeginTabItem(w.label.c_str());
    case impl::Group::Enabled:
        // Disabled, not hidden: a greyed control still says what it
        // would do, which is how a reader learns why it cannot.
        ImGui::BeginDisabled(!group_on(w));
        return true;
    case impl::Group::Row:
        break;
    }
    return true;
}

void group_end(const impl::WidgetState &w) {
    switch (w.group) {
    case impl::Group::Tabs:
        ImGui::EndTabBar();
        break;
    case impl::Group::Tab:
        ImGui::EndTabItem();
        break;
    case impl::Group::Enabled:
        ImGui::EndDisabled();
        break;
    default:
        break;
    }
}

} // namespace

// A FLAT list with Begin/End markers, walked with a stack. A group
// that is not showing must not draw its children AND must not emit its
// own End, so `on` is remembered rather than asked twice.
void panel_body(impl::PanelState &p, bool inline_row) {
    struct Frame {
        impl::Group group;
        bool on;
        bool first;
    };
    std::vector<Frame> stack;
    int hidden = 0;
    bool first = true;

    for (impl::WidgetState &w : p.widgets) {
        if (w.kind == impl::WidgetKind::GroupBegin) {
            const bool on = hidden == 0 && group_begin(w);
            stack.push_back({w.group, on, true});
            if (!on)
                ++hidden;
            continue;
        }
        if (w.kind == impl::WidgetKind::GroupEnd) {
            if (stack.empty())
                continue;
            const Frame f = stack.back();
            stack.pop_back();
            f.on ? group_end(w) : void(--hidden);
            continue;
        }
        if (hidden)
            continue;

        // A row lays its widgets side by side. Help is exempt: it pins
        // itself to whatever came before, row or not.
        if (!stack.empty() && stack.back().group == impl::Group::Row) {
            if (!stack.back().first && w.kind != impl::WidgetKind::Help)
                ImGui::SameLine();
            stack.back().first = false;
        } else if (inline_row && !first) {
            // A tool strip is a row the caller did not have to ask
            // for.
            ImGui::SameLine();
        }
        first = false;
        widget_draw(w, p.mono);
    }
}

void panel_draw(impl::PanelState &p) {
    // Width only: a label sits to the RIGHT of its control, so an
    // auto-fitted panel sizes itself to the widest CONTROL and clips
    // every name. The height stays auto.
    place_window(p.app, p.slot, 400.0f, 0.0f);
    if (!ImGui::Begin(p.title.c_str())) {
        ImGui::End();
        return;
    }
    panel_body(p);
    ImGui::End();
}

} // namespace sv
