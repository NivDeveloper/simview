// The settings page: every action of every context with its bindings
// on both hands, a cell that captures the next control pressed, a
// reset per row and for all, and the file the changes go to.

#include "../core/App.h"
#include "Chips.h"
#include "Icons.h"
#include "Ui.h"

#include <imgui.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace sv {

namespace {

using impl::ActionRow;
using impl::ActionTable;

constexpr float kWidth = 620.0f, kHeight = 540.0f;

// A context's name as the page heads it.
std::string heading(const std::string &name) {
    if (name == "base")
        return "always";
    if (name == "stroke")
        return "while a mode strokes";
    if (name == "mode")
        return "in any mode";
    if (name.rfind("mode:", 0) == 0)
        return "in " + name.substr(5);
    return name;
}

bool is_modifier(Control c) {
    if (c.device == Device::Keyboard)
        return c.code == int(Key::LeftShift) || c.code == int(Key::LeftCtrl) ||
               c.code == int(Key::LeftAlt);
    return c.device == Device::Pad &&
           (c.code == int(Pad::LB) || c.code == int(Pad::RB));
}

// The rows a device's bindings clash with: two rows of one context
// binding the same control the same way.
std::vector<int> clashes(const ActionTable &t, int row, Device d) {
    std::vector<int> out;
    const ActionRow &r = t.rows[std::size_t(row)];
    for (const Binding &a : impl::effective(r, d))
        for (std::size_t i = 0; i < t.rows.size(); ++i) {
            const ActionRow &o = t.rows[i];
            if (int(i) == row || o.context != r.context || !o.enabled)
                continue;
            for (const Binding &b : impl::effective(o, d))
                if (impl::binding_text(a) == impl::binding_text(b))
                    out.push_back(int(i));
        }
    return out;
}

// One cell: the bindings as chips over a selectable, the capture's
// prompt while it is this cell's turn.
void cell(impl::App *a, int row, Device d) {
    impl::UiState &ui = a->ui;
    ActionTable &t = a->input.table;
    const ActionRow &r = t.rows[std::size_t(row)];
    const bool mine =
        ui.capture.active && ui.capture.row == row && ui.capture.device == d;
    const std::vector<int> clash = clashes(t, row, d);

    ImGui::PushID(d == Device::Pad ? "pad" : "km");
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight() + 8.0f;
    if (!clash.empty())
        ImGui::PushStyleColor(ImGuiCol_Header,
                              ImGui::GetColorU32(ImGuiCol_ButtonActive, 0.35f));
    const bool clicked =
        ImGui::Selectable("##cell", mine || !clash.empty(),
                          ImGuiSelectableFlags_None, ImVec2(0.0f, h));
    if (!clash.empty())
        ImGui::PopStyleColor();
    if (!clash.empty() && ImGui::IsItemHovered()) {
        std::string who = "also bound by";
        for (int i : clash)
            who += " \"" + t.rows[std::size_t(i)].label + "\"";
        ImGui::SetTooltip("%s", who.c_str());
    }
    if (clicked && !mine) {
        ui.capture = {};
        ui.capture.active = true;
        ui.capture.row = row;
        ui.capture.device = d;
    }

    ImGui::SetCursorScreenPos(ImVec2(at.x + 4.0f, at.y + 2.0f));
    if (mine) {
        const char *what = r.kind == ActionKind::Axis
                               ? (ui.capture.n == 0 ? "press the negative…"
                                                    : "press the positive…")
                           : r.kind == ActionKind::Axis2
                               ? (ui.capture.n == 0 ? "a stick, or press left…"
                                  : ui.capture.n == 1 ? "press right…"
                                  : ui.capture.n == 2 ? "press down…"
                                                      : "press up…")
                               : "press a control…";
        ImGui::TextUnformatted(what);
    } else {
        std::vector<Chip> chips;
        for (const Binding &b : impl::effective(r, d))
            chips.push_back(chip_for(b, nullptr, false));
        if (chips.empty())
            ImGui::TextDisabled("none");
        else
            impl::draw_chips(chips);
    }
    ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + h));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::PopID();
}

} // namespace

void ui_settings_window(impl::App *a) {
    if (!a || !a->ui.settings_open)
        return;
    impl::UiState &ui = a->ui;
    ActionTable &t = a->input.table;
    if (ui.settings_slot < 0) {
        ui.settings_slot = a->windows++;
        place_window(a, ui.settings_slot, kWidth, kHeight);
    }
    if (!ImGui::Begin("settings###simview_settings", &ui.settings_open)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTable("bindings", 4,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed,
                                150.0f);
        ImGui::TableSetupColumn("keyboard + mouse");
        ImGui::TableSetupColumn("gamepad");
        ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed,
                                28.0f);
        ImGui::TableHeadersRow();
        for (std::size_t c = 0; c < t.contexts.size(); ++c) {
            bool any = false;
            for (const ActionRow &r : t.rows)
                any = any || (r.context == int(c) && r.claims);
            if (!any)
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", heading(t.contexts[c].name).c_str());
            for (std::size_t i = 0; i < t.rows.size(); ++i) {
                const ActionRow &r = t.rows[i];
                if (r.context != int(c) || !r.claims)
                    continue;
                ImGui::PushID(int(i));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
                ImGui::TextUnformatted(r.label.empty() ? r.id.c_str()
                                                       : r.label.c_str());
                ImGui::TableNextColumn();
                cell(a, int(i), Device::Keyboard);
                ImGui::TableNextColumn();
                cell(a, int(i), Device::Pad);
                ImGui::TableNextColumn();
                if ((r.file.km_set || r.file.pad_set) &&
                    impl::icon_button(Icon::Restart, "reset",
                                      "back to the "
                                      "app's own")) {
                    t.rows[i].file = {};
                    ui.bindings_dirty = true;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("reset all")) {
        for (ActionRow &r : t.rows)
            r.file = {};
        t.unknown.clear();
        ui.bindings_dirty = true;
    }
    ImGui::SameLine();
    if (ui.capture.active)
        ImGui::TextDisabled("Esc cancels, Backspace unbinds");
    else if (ui.bindings.empty())
        ImGui::TextDisabled("not saved: no window");
    else
        ImGui::TextDisabled("saved to %s", ui.bindings.c_str());
    ImGui::End();
}

// The capture's turn, before the frame's resolve: one control of the
// hand asked, shaped by the row's kind — a modifier held wraps it, a
// button on an axis row becomes a drag, Esc cancels, Backspace unbinds.
bool ui_settings_capture(impl::App *a, const impl::Snapshot &s) {
    impl::UiState &ui = a->ui;
    if (!ui.capture.active)
        return false;
    impl::Capture &cap = ui.capture;
    ActionTable &t = a->input.table;
    ActionRow &r = t.rows[std::size_t(cap.row)];
    const Control c = impl::capture_scan(s, cap.device);
    if (c.device == Device::None)
        return true;
    if (c == ControlOf(Key::Escape)) {
        cap = {};
        return true;
    }
    if (c == ControlOf(Key::Backspace)) {
        impl::set_layer(r.file, cap.device, {});
        ui.bindings_dirty = true;
        cap = {};
        return true;
    }
    if (is_modifier(c)) {
        cap.modifier = c;
        return true;
    }
    const bool analog =
        c.device == Device::Pad
            ? c.code >= int(Pad::LT)
            : c.device == Device::Mouse &&
                  (c.code == int(Mouse::Wheel) || c.code == int(Mouse::Move));
    Binding b{};
    bool done = false;
    switch (r.kind) {
    case ActionKind::Button:
        b = {Shape::Plain, {c}, {}};
        done = true;
        break;
    case ActionKind::Axis:
        if (analog && c.code != int(Pad::LS) && c.code != int(Pad::RS)) {
            b = {Shape::Plain, {c}, {}};
            done = true;
        } else {
            cap.got[cap.n++] = c;
            if (cap.n == 2) {
                b = {Shape::Axis, {cap.got[0], cap.got[1]}, {}};
                done = true;
            }
        }
        break;
    case ActionKind::Axis2:
        if (analog) {
            b = {Shape::Plain, {c}, {}};
            done = true;
        } else if (c.device != Device::Keyboard && cap.n == 0 &&
                   c.code < int(Pad::Up)) {
            b = {Shape::Drag, {c}, {}};
            done = true;
        } else {
            cap.got[cap.n++] = c;
            if (cap.n == 4) {
                b = {Shape::Axis2,
                     {cap.got[0], cap.got[1], cap.got[2], cap.got[3]},
                     {}};
                done = true;
            }
        }
        break;
    }
    if (!done)
        return true;
    b.modifier = cap.modifier;
    std::string why;
    if (impl::binding_valid(b, &why))
        impl::set_layer(r.file, cap.device, {b});
    else
        set_error("capture refused: " + why);
    ui.bindings_dirty = true;
    cap = {};
    return true;
}

// The file beside the layout: read at bring-up, written when dirty.
void ui_settings_load(impl::App *a, const char *title) {
    a->ui.bindings = pref_file(title, "bindings.txt");
    if (a->ui.bindings.empty())
        return;
    std::ifstream in(a->ui.bindings);
    if (!in)
        return;
    std::stringstream ss;
    ss << in.rdbuf();
    a->ui.bindings_text = ss.str();
}

void ui_settings_save(impl::App *a, const char *title) {
    if (!a->ui.bindings_dirty || a->ui.bindings.empty())
        return;
    std::ofstream out(a->ui.bindings);
    if (out)
        out << impl::table_save(a->input.table, title);
    a->ui.bindings_dirty = false;
}

} // namespace sv
