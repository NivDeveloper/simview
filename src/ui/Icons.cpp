#include "Icons.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace sv {
namespace impl {

namespace {

constexpr float kTau = 6.283185307179586f;

// Every icon is drawn in the unit square and scaled on the way out,
// so a shape is described once, in the proportions it was designed
// at, and never in pixels.
struct Pen {
    ImDrawList *dl;
    ImVec2 o;
    float s;
    ImU32 c;
    float t;

    ImVec2 at(float x, float y) const { return {o.x + x * s, o.y + y * s}; }

    ImVec2 polar(float turn, float r) const {
        return at(0.5f + r * std::cos(turn * kTau),
                  0.5f - r * std::sin(turn * kTau));
    }

    void line(float x0, float y0, float x1, float y1) const {
        dl->AddLine(at(x0, y0), at(x1, y1), c, t);
    }

    // Points as x, y pairs; `closed` joins the last back to the first.
    void poly(std::initializer_list<float> xy, bool closed = false) const {
        ImVec2 p[32];
        int n = 0;
        for (const float *f = xy.begin(); f + 1 < xy.end() && n < 32; f += 2)
            p[n++] = at(f[0], f[1]);
        dl->AddPolyline(p, n, c, closed ? ImDrawFlags_Closed : 0, t);
    }

    void fill(std::initializer_list<float> xy) const {
        ImVec2 p[32];
        int n = 0;
        for (const float *f = xy.begin(); f + 1 < xy.end() && n < 32; f += 2)
            p[n++] = at(f[0], f[1]);
        dl->AddConvexPolyFilled(p, n, c);
    }

    void box(float x0, float y0, float x1, float y1, float round = 0.0f) const {
        dl->AddRect(at(x0, y0), at(x1, y1), c, round * s, 0, t);
    }

    void slab(float x0, float y0, float x1, float y1,
              float round = 0.0f) const {
        dl->AddRectFilled(at(x0, y0), at(x1, y1), c, round * s);
    }

    void ring(float cx, float cy, float r) const {
        dl->AddCircle(at(cx, cy), r * s, c, 0, t);
    }

    void disc(float cx, float cy, float r) const {
        dl->AddCircleFilled(at(cx, cy), r * s, c);
    }

    void arc(float x0, float y0, float cx0, float cy0, float cx1, float cy1,
             float x1, float y1) const {
        dl->AddBezierCubic(at(x0, y0), at(cx0, cy0), at(cx1, cy1), at(x1, y1),
                           c, t, 0);
    }

    // A tooth ring: `n` teeth alternating between two radii, stroked as
    // one closed outline. Six and not eight, because a tooth narrower
    // than the stroke is a smudge at 16px.
    void teeth(int n, float inner, float outer) const {
        ImVec2 p[32];
        int k = 0;
        const float w = 1.0f / float(n);
        for (int i = 0; i < n && k + 4 <= 32; ++i) {
            const float u = float(i) * w;
            p[k++] = polar(u, outer);
            p[k++] = polar(u + 0.30f * w, outer);
            p[k++] = polar(u + 0.50f * w, inner);
            p[k++] = polar(u + 0.80f * w, inner);
        }
        dl->AddPolyline(p, k, c, ImDrawFlags_Closed, t);
    }
};

} // namespace

void icon_draw(ImDrawList *dl, Icon ic, ImVec2 at, float size, ImU32 col) {
    if (!dl || size <= 0.0f)
        return;

    const Pen p{dl, at, size, col, std::max(1.0f, size * 0.085f)};

    switch (ic) {
    case Icon::Home:
        p.poly({0.09f, 0.48f, 0.50f, 0.12f, 0.91f, 0.48f});
        p.poly({0.20f, 0.42f, 0.20f, 0.88f, 0.80f, 0.88f, 0.80f, 0.42f});
        p.poly({0.41f, 0.88f, 0.41f, 0.62f, 0.59f, 0.62f, 0.59f, 0.88f});
        return;

    case Icon::Fit:
        p.poly({0.36f, 0.11f, 0.11f, 0.11f, 0.11f, 0.36f});
        p.poly({0.64f, 0.11f, 0.89f, 0.11f, 0.89f, 0.36f});
        p.poly({0.36f, 0.89f, 0.11f, 0.89f, 0.11f, 0.64f});
        p.poly({0.64f, 0.89f, 0.89f, 0.89f, 0.89f, 0.64f});
        p.slab(0.40f, 0.40f, 0.60f, 0.60f, 0.03f);
        return;

    case Icon::Grid:
        p.box(0.11f, 0.11f, 0.89f, 0.89f, 0.04f);
        p.line(0.37f, 0.11f, 0.37f, 0.89f);
        p.line(0.63f, 0.11f, 0.63f, 0.89f);
        p.line(0.11f, 0.37f, 0.89f, 0.37f);
        p.line(0.11f, 0.63f, 0.89f, 0.63f);
        return;

    case Icon::Axes:
        p.line(0.36f, 0.64f, 0.36f, 0.20f);
        p.line(0.36f, 0.64f, 0.84f, 0.64f);
        p.line(0.36f, 0.64f, 0.14f, 0.86f);
        p.fill({0.36f, 0.08f, 0.28f, 0.24f, 0.44f, 0.24f});
        p.fill({0.96f, 0.64f, 0.80f, 0.56f, 0.80f, 0.72f});
        p.fill({0.06f, 0.94f, 0.20f, 0.90f, 0.10f, 0.80f});
        return;

    case Icon::Cube:
        p.poly({0.50f, 0.09f, 0.855f, 0.295f, 0.855f, 0.705f, 0.50f, 0.91f,
                0.145f, 0.705f, 0.145f, 0.295f},
               true);
        p.line(0.50f, 0.50f, 0.145f, 0.295f);
        p.line(0.50f, 0.50f, 0.855f, 0.295f);
        p.line(0.50f, 0.50f, 0.50f, 0.91f);
        return;

    // The pair is one contrast and nothing else: rails that converge
    // toward a horizon, and the same rails that do not. A frustum
    // drawn side-on reads as a loudspeaker, which is how these two
    // started out.
    case Icon::Perspective:
        p.line(0.13f, 0.90f, 0.41f, 0.14f);
        p.line(0.87f, 0.90f, 0.59f, 0.14f);
        p.line(0.155f, 0.86f, 0.845f, 0.86f);
        p.line(0.269f, 0.55f, 0.731f, 0.55f);
        p.line(0.383f, 0.24f, 0.617f, 0.24f);
        return;

    case Icon::Orthographic:
        p.line(0.22f, 0.90f, 0.22f, 0.14f);
        p.line(0.78f, 0.90f, 0.78f, 0.14f);
        p.line(0.22f, 0.86f, 0.78f, 0.86f);
        p.line(0.22f, 0.55f, 0.78f, 0.55f);
        p.line(0.22f, 0.24f, 0.78f, 0.24f);
        return;

    case Icon::Camera:
        p.poly({0.33f, 0.31f, 0.40f, 0.17f, 0.60f, 0.17f, 0.67f, 0.31f});
        p.box(0.09f, 0.30f, 0.91f, 0.83f, 0.09f);
        p.ring(0.50f, 0.565f, 0.17f);
        return;

    case Icon::Light:
        p.ring(0.50f, 0.50f, 0.20f);
        for (int k = 0; k < 8; ++k) {
            const float u = float(k) / 8.0f;
            const ImVec2 a = p.polar(u, 0.30f), b = p.polar(u, 0.44f);
            dl->AddLine(a, b, col, p.t);
        }
        return;

    case Icon::Eye:
        p.arc(0.08f, 0.50f, 0.30f, 0.14f, 0.70f, 0.14f, 0.92f, 0.50f);
        p.arc(0.08f, 0.50f, 0.30f, 0.86f, 0.70f, 0.86f, 0.92f, 0.50f);
        p.ring(0.50f, 0.50f, 0.15f);
        p.disc(0.50f, 0.50f, 0.06f);
        return;

    case Icon::Gear:
        p.teeth(6, 0.30f, 0.45f);
        p.ring(0.50f, 0.50f, 0.13f);
        return;

    // What this plot can BECOME, as the thing it would become: a
    // chart. A funnel was tried and reads as "filter", which is a
    // different promise.
    case Icon::Chart:
        p.line(0.13f, 0.12f, 0.13f, 0.88f);
        p.line(0.13f, 0.88f, 0.90f, 0.88f);
        p.slab(0.26f, 0.55f, 0.42f, 0.86f);
        p.slab(0.48f, 0.30f, 0.64f, 0.86f);
        p.slab(0.70f, 0.44f, 0.86f, 0.86f);
        return;

    // A key: one swatch and the two lines of names beside it. Not an
    // eye — an eye is visibility in general, and this switch is about
    // one specific thing on the plot.
    case Icon::Legend:
        p.box(0.10f, 0.20f, 0.90f, 0.80f, 0.06f);
        p.slab(0.20f, 0.34f, 0.36f, 0.46f, 0.02f);
        p.line(0.44f, 0.40f, 0.80f, 0.40f);
        p.slab(0.20f, 0.56f, 0.36f, 0.68f, 0.02f);
        p.line(0.44f, 0.62f, 0.80f, 0.62f);
        return;

    // The three reductions, drawn as what each one PRODUCES. They sit
    // in a menu beside their names, so the job is to be told apart at
    // a glance rather than to be readable alone — a distribution, a
    // field of cells, a line with its uncertainty.
    case Icon::Histogram:
        p.slab(0.08f, 0.62f, 0.24f, 0.90f);
        p.slab(0.27f, 0.38f, 0.43f, 0.90f);
        p.slab(0.46f, 0.16f, 0.62f, 0.90f);
        p.slab(0.65f, 0.44f, 0.81f, 0.90f);
        p.line(0.06f, 0.94f, 0.94f, 0.94f);
        return;

    // Four cells and not nine: at this size the outline of a 3x3
    // outweighs everything inside it, and two lit against two unlit
    // says "some cells are busier" in a quarter of the ink.
    case Icon::Density:
        p.slab(0.08f, 0.08f, 0.46f, 0.46f, 0.04f);
        p.box(0.54f, 0.08f, 0.92f, 0.46f, 0.04f);
        p.box(0.08f, 0.54f, 0.46f, 0.92f, 0.04f);
        p.slab(0.54f, 0.54f, 0.92f, 0.92f, 0.04f);
        return;

    case Icon::Profile:
        p.poly({0.10f, 0.74f, 0.36f, 0.50f, 0.62f, 0.56f, 0.90f, 0.26f});
        p.line(0.36f, 0.34f, 0.36f, 0.66f);
        p.line(0.28f, 0.34f, 0.44f, 0.34f);
        p.line(0.28f, 0.66f, 0.44f, 0.66f);
        p.line(0.90f, 0.12f, 0.90f, 0.40f);
        p.line(0.82f, 0.12f, 0.98f, 0.12f);
        p.line(0.82f, 0.40f, 0.98f, 0.40f);
        return;

    // An open ring with a head on it, not an ellipse: a flattened
    // ring is a second Eye, and the two sit next to each other.
    case Icon::Restart: {
        ImVec2 arc[22];
        for (int k = 0; k < 22; ++k)
            arc[k] = p.polar(0.10f + 0.82f * float(k) / 21.0f, 0.33f);
        dl->AddPolyline(arc, 22, col, 0, p.t);
        const ImVec2 head[3] = {p.polar(0.00f, 0.33f), p.polar(0.12f, 0.22f),
                                p.polar(0.12f, 0.44f)};
        dl->AddConvexPolyFilled(head, 3, col);
        return;
    }

    case Icon::Forward:
        p.fill({0.10f, 0.16f, 0.50f, 0.50f, 0.10f, 0.84f});
        p.fill({0.50f, 0.16f, 0.90f, 0.50f, 0.50f, 0.84f});
        return;

    case Icon::Play:
        p.fill({0.28f, 0.13f, 0.85f, 0.50f, 0.28f, 0.87f});
        return;

    case Icon::Pause:
        p.slab(0.27f, 0.15f, 0.44f, 0.85f, 0.04f);
        p.slab(0.56f, 0.15f, 0.73f, 0.85f, 0.04f);
        return;

    case Icon::Step:
        p.fill({0.19f, 0.15f, 0.68f, 0.50f, 0.19f, 0.85f});
        p.slab(0.73f, 0.15f, 0.86f, 0.85f, 0.04f);
        return;
    }
}

// The button is sized from the FONT, not from a constant: a control
// carrying an icon must line up with one carrying text, and the frame
// height is what every other widget in the row is already using.
bool icon_button(Icon ic, const char *id, const char *tip, bool on) {
    const float h = ImGui::GetFrameHeight();
    const ImVec2 at = ImGui::GetCursorScreenPos();

    // "##" so ImGui identifies the button without drawing the id as
    // its label — the icon IS the label.
    char tag[64];
    std::snprintf(tag, sizeof tag, "##%s", id ? id : "icon");

    const ImGuiCol face = on ? ImGuiCol_ButtonActive : ImGuiCol_Button;
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(face));
    const bool hit = ImGui::Button(tag, {h, h});
    ImGui::PopStyleColor();

    // Drawn after the button, over its face, and greyed with it: a
    // disabled group fades the frame and the glyph must go with it.
    const float glyph = h * 0.62f;
    const float pad = (h - glyph) * 0.5f;
    icon_draw(ImGui::GetWindowDrawList(), ic, {at.x + pad, at.y + pad}, glyph,
              ImGui::GetColorU32(ImGuiCol_Text));

    if (tip && *tip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tip);
    return hit;
}

} // namespace impl
} // namespace sv
