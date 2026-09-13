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
    default:
        return -1;
    }
}

bool dpad(Control c) {
    return c.device == Device::Pad && c.code >= int(Pad::Up) &&
           c.code <= int(Pad::Right);
}

} // namespace

Chip chip_for(const Binding &b, const char *label, bool lit) {
    Chip c;
    c.label = label ? label : "";
    c.lit = lit;
    c.round = impl::device_of(b) == Device::Pad;
    if (b.modifier.device != Device::None)
        c.hold = impl::control_word(b.modifier);
    const auto word = [&](Control k) { return impl::control_word(k); };
    switch (b.shape) {
    case Shape::Plain: {
        const Control k = b.controls[0];
        if (k.device == Device::Mouse && mouse_icon(k.code) >= 0) {
            c.icon = mouse_icon(k.code);
            c.caps = {word(k)};
            if (k.code == int(Mouse::DoubleClick))
                c.hold = "2×";
        } else if (k.device == Device::Mouse) {
            c.caps = {"mouse"};
        } else if (dpad(k)) {
            c.caps = {"D-pad"};
        } else {
            c.caps = {word(k)};
        }
        break;
    }
    case Shape::Axis:
        if (dpad(b.controls[0]))
            c.caps = {"D-pad"};
        else
            c.caps = {word(b.controls[0]), word(b.controls[1])};
        break;
    case Shape::Axis2:
        if (dpad(b.controls[0]))
            c.caps = {"D-pad"};
        else // up, left, down, right: the shape a hand knows
            c.caps = {word(b.controls[3]), word(b.controls[0]),
                      word(b.controls[2]), word(b.controls[1])};
        break;
    case Shape::Drag: {
        const Control k = b.controls[0];
        if (k.device == Device::Mouse) {
            c.icon = mouse_icon(k.code);
            c.caps = {k.code == int(Mouse::Left)    ? "drag"
                      : k.code == int(Mouse::Right) ? "right-drag"
                                                    : "middle-drag"};
        } else {
            c.caps = {word(k), "RS"};
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

void keycap(const char *word, bool round, bool lit) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float h = ImGui::GetTextLineHeight() + 4.0f;
    const ImVec2 tw = ImGui::CalcTextSize(word);
    const float w = std::max(h, tw.x + 10.0f);
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

        if (!c.hold.empty()) {
            keycap(c.hold.c_str(), false, false);
            ImGui::SameLine(0.0f, gap * 0.5f);
        }
        if (c.icon >= 0) {
            const float h = ImGui::GetTextLineHeight() + 4.0f;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            icon_draw(ImGui::GetWindowDrawList(), Icon(c.icon), at, h,
                      ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::Dummy({h, h});
        } else {
            for (std::size_t i = 0; i < c.caps.size(); ++i) {
                if (i) {
                    if (c.joiner == " + ") {
                        ImGui::SameLine(0.0f, gap * 0.5f);
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
                        ImGui::TextDisabled("+");
                    }
                    ImGui::SameLine(0.0f, gap * 0.5f);
                }
                keycap(c.caps[i].c_str(), c.round, c.lit);
            }
        }

        if (c.label.empty())
            continue;
        ImGui::SameLine(0.0f, gap);
        // Text sits on the cap's own centre line.
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
        if (c.lit)
            ImGui::TextUnformatted(c.label.c_str());
        else
            ImGui::TextDisabled("%s", c.label.c_str());
    }
}

} // namespace impl
} // namespace sv
