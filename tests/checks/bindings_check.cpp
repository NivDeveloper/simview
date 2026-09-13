// The resolver, with no device in the room: a table of contexts and
// rows, a frame's device state, and the rules that turn the one into
// action values through the other. Every rule the engine relies on is
// stated here first, because a picture can only say the camera moved:
// which binding moved it, and which was shadowed, takes the table.

#include "harness/Check.h"

#include "core/Actions.h"

#include <cstdio>
#include <string>

using namespace sv;
using namespace sv::impl;

namespace {

Binding parse(const char *text) {
    Binding b;
    std::string why;
    if (!binding_parse(text, &b, &why))
        std::printf("  (refused \"%s\": %s)\n", text, why.c_str());
    return b;
}

ActionDesc desc(const char *id, ActionKind kind, const Binding *bs, int n) {
    return {id, id, kind, bs, n};
}

// A table shaped like the engine's: base under orbit under cursor
// under a mode's stroke layer.
struct Table {
    ActionTable t;
    int base, orbit, cursor, stroke;
    Table() {
        base = t.context("base", 0);
        orbit = t.context("orbit", 1);
        cursor = t.context("cursor", 2);
        stroke = t.context("stroke", 3);
        t.contexts[std::size_t(base)].active = true;
        t.contexts[std::size_t(orbit)].active = true;
        t.contexts[std::size_t(cursor)].active = true;
    }
    ActionRow &add(int ctx, const char *id, ActionKind kind,
                   std::initializer_list<Binding> bs) {
        std::vector<Binding> v(bs);
        return t.add(ctx, desc(id, kind, v.data(), int(v.size())));
    }
    const ActionValue &value(int ctx, const char *id) {
        return t.rows[std::size_t(t.find(ctx, id))].value;
    }
};

Snapshot frame() {
    Snapshot s;
    s.mouse_free = true;
    return s;
}

void key(Snapshot &s, Key k, bool down, bool edge = true) {
    const std::size_t i = std::size_t(k);
    s.key_down.set(i, down);
    if (edge)
        (down ? s.key_pressed : s.key_released).set(i, true);
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // ── spellings go to the file and come back the same ──────────────
    const char *spelled[] = {
        "Key.Space",           "Shift+Drag(Mouse.Left)",
        "Drag(Mouse.Right)",   "Axis(Key.Q,Key.E)",
        "Axis(Pad.LT,Pad.RT)", "Axis2(Key.A,Key.D,Key.S,Key.W)",
        "Pad.LB+Pad.RS",       "Mouse.Wheel",
        "Mouse.DoubleClick",   "Key.#100",
        "Ctrl+Key.Tab",
    };
    for (const char *s : spelled) {
        const Binding b = parse(s);
        CHECK(b.shape != Shape::None);
        CHECK_EQ(binding_text(b), std::string(s));
    }
    CHECK_EQ(binding_text(With(Key::LeftShift, Drag(Mouse::Left))),
             std::string("Shift+Drag(Mouse.Left)"));
    CHECK_EQ(binding_text(Ctl(Key::Grave)), std::string("Key.Grave"));
    CHECK_EQ(binding_text(parse("Key.LeftShift+Mouse.Left")),
             std::string("Shift+Mouse.Left"));
    CHECK_EQ(control_word(ControlOf(Key::Escape)), std::string("Esc"));
    CHECK_EQ(control_word(ControlOf(Pad::RS)), std::string("RS"));
    CHECK_EQ(control_word(ControlOf(Mouse::Left)), std::string("click"));

    // ── the shapes that are refused, with a sentence ─────────────────
    {
        Binding b;
        std::string why;
        CHECK(!binding_parse("Pad.LB+Mouse.Left", &b, &why));
        CHECK(!why.empty());
        CHECK(!binding_parse("Drag(Pad.LS)", &b, &why));
        CHECK(!binding_parse("Axis(Key.Q,Pad.RT)", &b, &why));
        CHECK(!binding_parse("Axis2(Key.A,Key.D,Key.S)", &b, &why));
        CHECK(!binding_parse("Key.Nope", &b, &why));
        CHECK(!binding_parse("Drag(Mouse.DoubleClick)", &b, &why));
        CHECK(device_of(Ctl(Pad::A)) == Device::Pad);
        CHECK(device_of(With(Key::LeftShift, Drag(Mouse::Left))) ==
              Device::Keyboard);
    }

    // ── a click and a drag share a button and both live ──────────────
    {
        Table tb;
        tb.add(tb.cursor, "pointer.primary", ActionKind::Button,
               {Ctl(Mouse::Left), Ctl(Pad::A)});
        tb.add(tb.orbit, "camera.turn", ActionKind::Axis2,
               {Drag(Mouse::Left), Drag(Mouse::Right), Drag(Pad::A),
                Drag(Pad::B)});
        Snapshot s = frame();
        s.mouse_down[0] = s.mouse_pressed[0] = true;
        s.pointer_dx = 4.0f;
        s.pointer_dy = -2.0f;
        resolve(tb.t, s);
        CHECK(tb.value(tb.cursor, "pointer.primary").pressed);
        CHECK_EQ(tb.value(tb.orbit, "camera.turn").x, 4.0f);
        CHECK_EQ(tb.value(tb.orbit, "camera.turn").y, -2.0f);

        // A drag from the pad's A is the same pointer step: counted once.
        s.pad_down[int(Pad::A)] = true;
        resolve(tb.t, s);
        CHECK_EQ(tb.value(tb.orbit, "camera.turn").x, 4.0f);

        // A mode's drag on the same button shadows the orbit's, and the
        // orbit's OTHER button still turns.
        tb.add(tb.stroke, "pointer.drag", ActionKind::Axis2,
               {Drag(Mouse::Left), Drag(Pad::A)});
        tb.t.contexts[std::size_t(tb.stroke)].active = true;
        resolve(tb.t, s);
        CHECK_EQ(tb.value(tb.stroke, "pointer.drag").x, 4.0f);
        CHECK_EQ(tb.value(tb.orbit, "camera.turn").x, 0.0f);
        CHECK(tb.value(tb.cursor, "pointer.primary").down);
        s.mouse_down[0] = false;
        s.pad_down[int(Pad::A)] = false;
        s.mouse_down[1] = true;
        resolve(tb.t, s);
        CHECK_EQ(tb.value(tb.orbit, "camera.turn").x, 4.0f);
        CHECK_EQ(tb.value(tb.stroke, "pointer.drag").x, 0.0f);
        // The bar asks which bindings are alive: left is shadowed,
        // right is not.
        const ActionRow &turn =
            tb.t.rows[std::size_t(tb.t.find(tb.orbit, "camera.turn"))];
        CHECK_EQ(int(turn.alive_km & 1u), 0);
        CHECK_EQ(int(turn.alive_km & 2u), 2);
        CHECK_EQ(int(turn.alive_pad & 1u), 0);
        CHECK_EQ(int(turn.alive_pad & 2u), 2);
        // Inactive: nothing is shadowed and left drags again.
        tb.t.contexts[std::size_t(tb.stroke)].active = false;
        s.mouse_down[1] = false;
        s.mouse_down[0] = true;
        resolve(tb.t, s);
        CHECK_EQ(tb.value(tb.orbit, "camera.turn").x, 4.0f);
    }

    // ── a chord beats a bare binding, whatever their contexts ────────
    {
        Table tb;
        tb.add(tb.orbit, "camera.turn", ActionKind::Axis2, {Drag(Mouse::Left)});
        tb.add(tb.orbit, "camera.slide", ActionKind::Axis2,
               {With(Key::LeftShift, Drag(Mouse::Left))});
        tb.add(tb.stroke, "pointer.drag", ActionKind::Axis2,
               {Drag(Mouse::Left)});
        tb.t.contexts[std::size_t(tb.stroke)].active = true;
        Snapshot s = frame();
        s.mouse_down[0] = true;
        s.pointer_dx = 3.0f;
        resolve(tb.t, s);
        CHECK_EQ(tb.value(tb.stroke, "pointer.drag").x, 3.0f);
        CHECK_EQ(tb.value(tb.orbit, "camera.slide").x, 0.0f);
        key(s, Key::LeftShift, true);
        resolve(tb.t, s);
        CHECK_EQ(tb.value(tb.orbit, "camera.slide").x, 3.0f);
        CHECK_EQ(tb.value(tb.stroke, "pointer.drag").x, 0.0f);
        CHECK_EQ(tb.value(tb.orbit, "camera.turn").x, 0.0f);

        // Ctrl+Tab and Tab: the chord takes the key while Ctrl is held.
        tb.add(tb.base, "mode.camera", ActionKind::Button, {Ctl(Key::Tab)});
        tb.add(tb.base, "mode.pointer", ActionKind::Button,
               {With(Key::LeftCtrl, Ctl(Key::Tab))});
        Snapshot k = frame();
        key(k, Key::Tab, true);
        resolve(tb.t, k);
        CHECK(tb.value(tb.base, "mode.camera").pressed);
        CHECK(!tb.value(tb.base, "mode.pointer").pressed);
        key(k, Key::LeftCtrl, true);
        resolve(tb.t, k);
        CHECK(!tb.value(tb.base, "mode.camera").pressed);
        CHECK(tb.value(tb.base, "mode.pointer").pressed);
    }

    // ── a higher context's row for an id replaces the lower one ──────
    {
        Table tb;
        tb.add(tb.orbit, "camera.turn", ActionKind::Axis2, {Ctl(Pad::RS)});
        tb.add(tb.cursor, "camera.turn", ActionKind::Axis2, {Ctl(Mouse::Move)});
        Snapshot s = frame();
        s.rx = 0.5f;
        s.move_dx = 7.0f;
        resolve(tb.t, s);
        CHECK_EQ(tb.value(tb.cursor, "camera.turn").x, 7.0f);
        CHECK_EQ(tb.value(tb.cursor, "camera.turn").rx, 0.0f);
        CHECK_EQ(tb.value(tb.orbit, "camera.turn").rx, 0.0f);
        // And a lower context's plain button on a key the higher one
        // claims is shadowed.
        tb.add(tb.base, "fire", ActionKind::Button, {Ctl(Mouse::Left)});
        tb.add(tb.cursor, "pointer.primary", ActionKind::Button,
               {Ctl(Mouse::Left)});
        s.mouse_pressed[0] = s.mouse_down[0] = true;
        resolve(tb.t, s);
        CHECK(tb.value(tb.cursor, "pointer.primary").pressed);
        CHECK(!tb.value(tb.base, "fire").pressed);
    }

    // ── a key on top of a stick is still ONE deflection ──────────────
    {
        Table tb;
        tb.add(tb.orbit, "camera.slide", ActionKind::Axis2,
               {Axis2(Key::A, Key::D, Key::S, Key::W), Ctl(Pad::LS)});
        Snapshot s = frame();
        key(s, Key::D, true);
        s.lx = 0.8f;
        s.ly = -0.3f;
        resolve(tb.t, s);
        CHECK_EQ(tb.value(tb.orbit, "camera.slide").rx, 1.0f);
        CHECK_EQ(tb.value(tb.orbit, "camera.slide").ry, -0.3f);
        // W is up: the stick's sign, y negative.
        Snapshot w = frame();
        key(w, Key::W, true);
        resolve(tb.t, w);
        CHECK_EQ(tb.value(tb.orbit, "camera.slide").ry, -1.0f);
    }

    // ── an axis is a rate from buttons or triggers, a delta from the
    // wheel; a button as an axis is one while held ───────────────────
    {
        Table tb;
        tb.add(tb.orbit, "camera.depth", ActionKind::Axis,
               {Ctl(Mouse::Wheel), Axis(Pad::LT, Pad::RT)});
        tb.add(tb.orbit, "camera.rise", ActionKind::Axis,
               {Axis(Key::Q, Key::E), Ctl(Pad::RB)});
        Snapshot s = frame();
        s.wheel = 2.0f;
        s.lt = 0.25f;
        s.rt = 1.0f;
        key(s, Key::Q, true);
        s.pad_down[int(Pad::RB)] = true;
        resolve(tb.t, s);
        CHECK_EQ(tb.value(tb.orbit, "camera.depth").x, 2.0f);
        CHECK_EQ(tb.value(tb.orbit, "camera.depth").rx, 0.75f);
        CHECK_EQ(tb.value(tb.orbit, "camera.rise").rx, 0.0f); // -1 + 1
    }

    // ── a disabled row claims nothing; a passive row shadows nothing ─
    {
        Table tb;
        tb.add(tb.orbit, "camera.depth", ActionKind::Axis, {Ctl(Mouse::Wheel)});
        ActionRow &depth = tb.add(tb.stroke, "pointer.depth", ActionKind::Axis,
                                  {Ctl(Mouse::Wheel)});
        tb.t.contexts[std::size_t(tb.stroke)].active = true;
        Snapshot s = frame();
        s.wheel = 1.0f;
        resolve(tb.t, s);
        CHECK_EQ(tb.value(tb.stroke, "pointer.depth").x, 1.0f);
        CHECK_EQ(tb.value(tb.orbit, "camera.depth").x, 0.0f);
        depth.enabled = false;
        resolve(tb.t, s);
        CHECK_EQ(tb.value(tb.stroke, "pointer.depth").x, 0.0f);
        CHECK_EQ(tb.value(tb.orbit, "camera.depth").x, 1.0f);

        ActionRow &move = tb.add(tb.cursor, "pointer.move", ActionKind::Axis2,
                                 {Ctl(Mouse::Move), Ctl(Pad::RS)});
        move.claims = false;
        tb.add(tb.orbit, "camera.turn", ActionKind::Axis2, {Ctl(Pad::RS)});
        Snapshot p = frame();
        p.rx = 0.4f;
        resolve(tb.t, p);
        CHECK_EQ(tb.value(tb.cursor, "pointer.move").rx, 0.4f);
        CHECK_EQ(tb.value(tb.orbit, "camera.turn").rx, 0.4f);
    }

    // ── a panel that owns the pointer takes every mouse binding, and
    // no other ───────────────────────────────────────────────────────
    {
        Table tb;
        tb.add(tb.orbit, "camera.turn", ActionKind::Axis2,
               {Drag(Mouse::Left), Ctl(Pad::RS)});
        tb.add(tb.orbit, "camera.slide", ActionKind::Axis2,
               {With(Key::LeftShift, Drag(Mouse::Left))});
        tb.add(tb.base, "pause", ActionKind::Button, {Ctl(Key::Space)});
        Snapshot s = frame();
        s.mouse_free = false;
        s.mouse_down[0] = true;
        s.pointer_dx = 5.0f;
        s.rx = 0.3f;
        key(s, Key::Space, true);
        key(s, Key::LeftShift, true);
        resolve(tb.t, s);
        CHECK_EQ(tb.value(tb.orbit, "camera.turn").x, 0.0f);
        CHECK_EQ(tb.value(tb.orbit, "camera.turn").rx, 0.3f);
        CHECK_EQ(tb.value(tb.orbit, "camera.slide").x, 0.0f);
        CHECK(tb.value(tb.base, "pause").pressed);
    }

    // ── a press and a release inside one frame are both edges; a held
    // key with no edge is neither; a double-click is a press ─────────
    {
        Table tb;
        tb.add(tb.base, "pause", ActionKind::Button, {Ctl(Key::Space)});
        tb.add(tb.orbit, "camera.frame", ActionKind::Button,
               {Ctl(Mouse::DoubleClick)});
        Snapshot s = frame();
        s.key_pressed.set(std::size_t(Key::Space));
        s.key_released.set(std::size_t(Key::Space));
        resolve(tb.t, s);
        CHECK(tb.value(tb.base, "pause").pressed);
        CHECK(tb.value(tb.base, "pause").released);
        CHECK(!tb.value(tb.base, "pause").down);
        Snapshot h = frame();
        key(h, Key::Space, true, false);
        resolve(tb.t, h);
        CHECK(tb.value(tb.base, "pause").down);
        CHECK(!tb.value(tb.base, "pause").pressed);
        h.double_click = true;
        resolve(tb.t, h);
        CHECK(tb.value(tb.orbit, "camera.frame").pressed);
    }

    // ── the file: layers, sides, unknown lines, and the round trip ───
    {
        Table tb;
        tb.add(tb.orbit, "camera.turn", ActionKind::Axis2,
               {Drag(Mouse::Left), Ctl(Pad::RS)});
        tb.add(tb.base, "pause", ActionKind::Button,
               {Ctl(Key::Space), Ctl(Pad::RB)});
        ActionRow &pause = tb.t.rows[std::size_t(tb.t.find(tb.base, "pause"))];
        // Code beats defaults, on the side it sets.
        set_layer(pause.code, Device::Keyboard, {Ctl(Key::P)});
        CHECK_EQ(binding_text(effective(pause, Device::Keyboard)[0]),
                 std::string("Key.P"));
        CHECK_EQ(binding_text(effective(pause, Device::Pad)[0]),
                 std::string("Pad.RB"));
        // The file beats code; `-` leaves a side; `none` empties one.
        table_load(tb.t, "# a comment\n"
                         "base.pause = Key.Return | -\n"
                         "orbit.camera.turn = - | none\n"
                         "orbit.nothing.here = Key.A\n"
                         "garbage line\n");
        CHECK_EQ(binding_text(effective(pause, Device::Keyboard)[0]),
                 std::string("Key.Return"));
        CHECK_EQ(binding_text(effective(pause, Device::Pad)[0]),
                 std::string("Pad.RB"));
        const ActionRow &turn =
            tb.t.rows[std::size_t(tb.t.find(tb.orbit, "camera.turn"))];
        CHECK_EQ(effective(turn, Device::Pad).size(), std::size_t(0));
        CHECK_EQ(effective(turn, Device::Keyboard).size(), std::size_t(1));
        CHECK_EQ(tb.t.unknown.size(), std::size_t(2));
        const std::string text = table_save(tb.t, "check");
        std::printf("%s", text.c_str());
        CHECK(text.find("base.pause = Key.Return | -") != std::string::npos);
        CHECK(text.find("orbit.camera.turn = - | none") != std::string::npos);
        CHECK(text.find("orbit.nothing.here = Key.A") != std::string::npos);
        // Loaded back into a fresh table: the same effective bindings.
        Table again;
        again.add(again.orbit, "camera.turn", ActionKind::Axis2,
                  {Drag(Mouse::Left), Ctl(Pad::RS)});
        again.add(again.base, "pause", ActionKind::Button,
                  {Ctl(Key::Space), Ctl(Pad::RB)});
        table_load(again.t, text);
        CHECK_EQ(table_save(again.t, "check"), text);
        // A line with no bar lands each binding on its own hand.
        table_load(again.t, "base.pause = Key.Space, Pad.X\n");
        const ActionRow &p2 =
            again.t.rows[std::size_t(again.t.find(again.base, "pause"))];
        CHECK_EQ(binding_text(effective(p2, Device::Keyboard)[0]),
                 std::string("Key.Space"));
        CHECK_EQ(binding_text(effective(p2, Device::Pad)[0]),
                 std::string("Pad.X"));
    }

    // ── a capture reads the first press of the hand asked ────────────
    {
        Snapshot s = frame();
        key(s, Key::F1, true);
        s.pad_pressed[int(Pad::Y)] = s.pad_down[int(Pad::Y)] = true;
        s.rt = 0.9f;
        CHECK(capture_scan(s, Device::Keyboard) == ControlOf(Key::F1));
        CHECK(capture_scan(s, Device::Pad) == ControlOf(Pad::Y));
        // A trigger edges like a button: past a half is its press.
        Snapshot t = frame();
        t.rt = 0.9f;
        t.pad_pressed[int(Pad::RT)] = t.pad_down[int(Pad::RT)] = true;
        CHECK(capture_scan(t, Device::Pad) == ControlOf(Pad::RT));
        CHECK(capture_scan(t, Device::Keyboard).device == Device::None);
        t.mouse_pressed[1] = true;
        CHECK(capture_scan(t, Device::Keyboard) == ControlOf(Mouse::Right));
    }

    return check::summary("bindings");
}
