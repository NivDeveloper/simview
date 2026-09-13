#include "Chips.h"

#include "../core/Actions.h"
#include "Icons.h"

#include <imgui.h>

#include <algorithm>

namespace sv {

namespace {

int mouse_icon(std::int32_t code) {
    switch (Mouse(code)) {
    case Mouse::Left:
    case Mouse::DoubleClick:
        return int(Icon::MouseLeft);
    case Mouse::Right:
        return int(Icon::MouseRight);
    case Mouse::Middle:
        return int(Icon::MouseMiddle);
    case Mouse::Wheel:
        return int(Icon::MouseWheel);
    case Mouse::Move:
        return int(Icon::Mouse);
    default:
        return -1;
    }
}

bool dpad(Control c) {
    return c.device == Device::Pad && c.code >= int(Pad::Up) &&
           c.code <= int(Pad::Right);
}

int pad_icon(std::int32_t code) {
    switch (Pad(code)) {
    case Pad::A:
        return int(Icon::PadA);
    case Pad::B:
        return int(Icon::PadB);
    case Pad::X:
        return int(Icon::PadX);
    case Pad::Y:
        return int(Icon::PadY);
    case Pad::LB:
        return int(Icon::PadLB);
    case Pad::RB:
        return int(Icon::PadRB);
    case Pad::LT:
        return int(Icon::PadLT);
    case Pad::RT:
        return int(Icon::PadRT);
    case Pad::L3:
        return int(Icon::PadL3);
    case Pad::R3:
        return int(Icon::PadR3);
    case Pad::Start:
        return int(Icon::PadStart);
    case Pad::Back:
        return int(Icon::PadBack);
    case Pad::Up:
        return int(Icon::DpadUp);
    case Pad::Down:
        return int(Icon::DpadDown);
    case Pad::Left:
        return int(Icon::DpadLeft);
    case Pad::Right:
        return int(Icon::DpadRight);
    case Pad::LS:
        return int(Icon::PadLS);
    case Pad::RS:
        return int(Icon::PadRS);
    default:
        return -1;
    }
}

// A control's icon, or -1 for a key, which is a cap.
int icon_of(Control k) {
    if (k.device == Device::Mouse)
        return mouse_icon(k.code);
    if (k.device == Device::Pad)
        return pad_icon(k.code);
    return -1;
}

// One control into a chip: its word for the reader of text, its icon
// for the reader of the bar.
void put(Chip &c, Control k) {
    c.caps.push_back(impl::control_word(k));
    const int ic = icon_of(k);
    if (ic >= 0)
        c.icons.push_back(ic);
}

} // namespace

Chip chip_for(const Binding &b, const char *label, bool lit) {
    Chip c;
    c.label = label ? label : "";
    c.lit = lit;
    if (b.modifier.device != Device::None) {
        c.hold = impl::control_word(b.modifier);
        c.hold_icon = icon_of(b.modifier);
    }
    switch (b.shape) {
    case Shape::Plain:
        put(c, b.controls[0]);
        if (b.controls[0] == ControlOf(Mouse::DoubleClick))
            c.hold = "2×";
        break;
    case Shape::Axis:
        put(c, b.controls[0]);
        put(c, b.controls[1]);
        break;
    case Shape::Axis2:
        if (dpad(b.controls[0])) {
            c.caps = {"D-pad"};
            c.icons = {int(Icon::Dpad)};
        } else { // up, left, down, right: the shape a hand knows
            put(c, b.controls[3]);
            put(c, b.controls[0]);
            put(c, b.controls[2]);
            put(c, b.controls[1]);
        }
        break;
    case Shape::Drag: {
        const Control k = b.controls[0];
        if (k.device == Device::Mouse) {
            c.icons = {mouse_icon(k.code)};
            c.caps = {k.code == int(Mouse::Left)    ? "drag"
                      : k.code == int(Mouse::Right) ? "right-drag"
                                                    : "middle-drag"};
        } else {
            put(c, k);
            put(c, ControlOf(Pad::RS));
            c.joiner = " + ";
        }
        break;
    }
    default:
        break;
    }
    return c;
}

std::string chip_text(const Chip &c) {
    std::string s;
    if (!c.hold.empty())
        s += c.hold + " ";
    for (std::size_t i = 0; i < c.caps.size(); ++i)
        s += (i ? c.joiner : "") + c.caps[i];
    if (!c.label.empty())
        s += " " + c.label;
    return c.lit ? "[" + s + "]" : s;
}

namespace impl {

// The bar's row height: a keycap's, and a glyph's, so the two sit on
// one line.
float chip_height() { return ImGui::GetTextLineHeight() + 8.0f; }

void keycap(const char *word, bool round, bool lit) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float h = chip_height();
    const ImVec2 tw = ImGui::CalcTextSize(word);
    const float w = std::max(h, tw.x + 12.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1{p0.x + w, p0.y + h};
    const ImU32 face =
        ImGui::GetColorU32(lit ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg);
    const ImU32 edge = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 ink =
        ImGui::GetColorU32(lit ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const float r = round ? h * 0.5f : 4.0f;
    dl->AddRectFilled(p0, p1, face, r);
    dl->AddRect(p0, p1, edge, r);
    dl->AddText({p0.x + (w - tw.x) * 0.5f, p0.y + (h - tw.y) * 0.5f}, ink,
                word);
    ImGui::Dummy({w, h});
}

void draw_chips(const std::vector<Chip> &chips) {
    const float gap = ImGui::GetFontSize() * 0.4f;
    bool first = true;
    for (const Chip &c : chips) {
        if (c.blank()) {
            first = true;
            continue;
        }
        if (!first)
            ImGui::SameLine(0.0f, gap * 4.0f);
        first = false;

        const float h = chip_height();
        const auto glyph = [&](int ic) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            icon_draw(ImGui::GetWindowDrawList(), Icon(ic), at, h,
                      ImGui::GetColorU32(c.lit ? ImGuiCol_Text
                                               : ImGuiCol_TextDisabled));
            ImGui::Dummy({h, h});
        };
        const auto plus = [&] {
            ImGui::SameLine(0.0f, gap * 0.5f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
            ImGui::TextDisabled("+");
            ImGui::SameLine(0.0f, gap * 0.5f);
        };
        if (!c.hold.empty()) {
            if (c.hold_icon >= 0)
                glyph(c.hold_icon);
            else
                keycap(c.hold.c_str(), false, false);
            ImGui::SameLine(0.0f, gap * 0.5f);
        }
        if (!c.icons.empty()) {
            for (std::size_t i = 0; i < c.icons.size(); ++i) {
                if (i && c.joiner == " + ")
                    plus();
                else if (i)
                    ImGui::SameLine(0.0f, gap * 0.25f);
                glyph(c.icons[i]);
            }
        } else {
            for (std::size_t i = 0; i < c.caps.size(); ++i) {
                if (i && c.joiner == " + ")
                    plus();
                else if (i)
                    ImGui::SameLine(0.0f, gap * 0.5f);
                keycap(c.caps[i].c_str(), false, c.lit);
            }
        }

        if (c.label.empty())
            continue;
        ImGui::SameLine(0.0f, gap);
        // Text sits on the cap's own centre line.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        if (c.lit)
            ImGui::TextUnformatted(c.label.c_str());
        else
            ImGui::TextDisabled("%s", c.label.c_str());
    }
}

} // namespace impl
} // namespace sv
