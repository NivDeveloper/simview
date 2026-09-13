#include "Actions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sv {
namespace impl {

namespace {

constexpr float kHalf = 0.5f;

bool is_pad_button(std::int32_t code) {
    return code >= 0 && code < kPadControls && code != int(Pad::LT) &&
           code != int(Pad::RT) && code != int(Pad::LS) && code != int(Pad::RS);
}

bool is_stick(Control c) {
    return c.device == Device::Pad &&
           (c.code == int(Pad::LS) || c.code == int(Pad::RS));
}

bool is_trigger(Control c) {
    return c.device == Device::Pad &&
           (c.code == int(Pad::LT) || c.code == int(Pad::RT));
}

// A control that reports a level, not a press.
bool is_analog(Control c) {
    if (c.device == Device::Mouse)
        return c.code == int(Mouse::Move) || c.code == int(Mouse::Wheel);
    return is_stick(c) || is_trigger(c);
}

// A control with a press: a key, a mouse button, a pad button — and a
// trigger, which is down past a half and edges like a button.
bool is_button(Control c) {
    switch (c.device) {
    case Device::Keyboard:
        return c.code >= 0 && c.code < 512;
    case Device::Mouse:
        return c.code == int(Mouse::Left) || c.code == int(Mouse::Right) ||
               c.code == int(Mouse::Middle) ||
               c.code == int(Mouse::DoubleClick);
    case Device::Pad:
        return is_pad_button(c.code) || is_trigger(c);
    default:
        return false;
    }
}

bool pad_presses(std::int32_t code) {
    return code >= 0 && code < kPadControls && code != int(Pad::LS) &&
           code != int(Pad::RS);
}

bool down(const Snapshot &s, Control c) {
    switch (c.device) {
    case Device::Keyboard:
        return c.code >= 0 && c.code < 512 &&
               s.key_down.test(std::size_t(c.code));
    case Device::Mouse:
        return c.code >= 0 && c.code < 3 && s.mouse_down[c.code];
    case Device::Pad:
        if (is_pad_button(c.code))
            return s.pad_down[c.code];
        if (c.code == int(Pad::LT))
            return s.lt > kHalf;
        if (c.code == int(Pad::RT))
            return s.rt > kHalf;
        if (c.code == int(Pad::LS))
            return std::hypot(s.lx, s.ly) > kHalf;
        if (c.code == int(Pad::RS))
            return std::hypot(s.rx, s.ry) > kHalf;
        return false;
    default:
        return false;
    }
}

bool pressed(const Snapshot &s, Control c) {
    switch (c.device) {
    case Device::Keyboard:
        return c.code >= 0 && c.code < 512 &&
               s.key_pressed.test(std::size_t(c.code));
    case Device::Mouse:
        if (c.code == int(Mouse::DoubleClick))
            return s.double_click;
        return c.code >= 0 && c.code < 3 && s.mouse_pressed[c.code];
    case Device::Pad:
        return pad_presses(c.code) && s.pad_pressed[c.code];
    default:
        return false;
    }
}

bool released(const Snapshot &s, Control c) {
    switch (c.device) {
    case Device::Keyboard:
        return c.code >= 0 && c.code < 512 &&
               s.key_released.test(std::size_t(c.code));
    case Device::Mouse:
        return c.code >= 0 && c.code < 3 && s.mouse_released[c.code];
    case Device::Pad:
        return pad_presses(c.code) && s.pad_released[c.code];
    default:
        return false;
    }
}

// An analog control's level: a stick as two, a trigger as one, the
// wheel as notches, the mouse's step as pixels.
void level(const Snapshot &s, Control c, float *x, float *y) {
    *x = *y = 0.0f;
    if (c.device == Device::Pad) {
        if (c.code == int(Pad::LS)) {
            *x = s.lx;
            *y = s.ly;
        } else if (c.code == int(Pad::RS)) {
            *x = s.rx;
            *y = s.ry;
        } else if (c.code == int(Pad::LT)) {
            *x = s.lt;
        } else if (c.code == int(Pad::RT)) {
            *x = s.rt;
        }
    } else if (c.device == Device::Mouse) {
        if (c.code == int(Mouse::Move)) {
            *x = s.move_dx;
            *y = s.move_dy;
        } else if (c.code == int(Mouse::Wheel)) {
            *x = s.wheel;
        }
    } else if (c == kPointer) {
        *x = s.pointer_dx;
        *y = s.pointer_dy;
    }
}

int control_count(Shape sh) {
    switch (sh) {
    case Shape::Plain:
    case Shape::Drag:
        return 1;
    case Shape::Axis:
        return 2;
    case Shape::Axis2:
        return 4;
    default:
        return 0;
    }
}

enum class Role : std::uint8_t { Button, Gate, Motion };

struct Claim {
    Control control;
    Role role;
};

// A drag holds its button whether or not it is dragging, and the
// pointer's motion only while it is.
void claims_of(const Binding &b, const Snapshot &s, std::vector<Claim> &out) {
    const int n = control_count(b.shape);
    if (b.shape == Shape::Drag) {
        out.push_back({b.controls[0], Role::Gate});
        if (down(s, b.controls[0]))
            out.push_back({kPointer, Role::Motion});
        return;
    }
    for (int i = 0; i < n; ++i)
        out.push_back({b.controls[i],
                       is_analog(b.controls[i]) ? Role::Motion : Role::Button});
}

bool taken(const std::vector<Claim> &set, const std::vector<Claim> &want) {
    for (const Claim &w : want)
        for (const Claim &t : set)
            if (t.control == w.control && t.role == w.role)
                return true;
    return false;
}

// The thing a binding reads, so two bindings on one source count once:
// a drag from either device is the same pointer step.
std::uint64_t source_of(const Binding &b) {
    if (b.shape == Shape::Drag)
        return 1;
    std::uint64_t h = std::uint64_t(b.shape) << 56;
    for (int i = 0; i < control_count(b.shape); ++i)
        h ^= (std::uint64_t(std::uint32_t(b.controls[i].device)) << 40 |
              std::uint64_t(std::uint32_t(b.controls[i].code)))
             << (i * 4);
    return h;
}

struct Read {
    ActionValue v;
    bool spoke = false;
};

// A binding's value for a row of a kind. A button as an axis is one
// while held; an axis as a button is past a half.
Read read(const Binding &b, const Snapshot &s, ActionKind kind) {
    Read r;
    ActionValue &v = r.v;
    switch (b.shape) {
    case Shape::Plain: {
        const Control c = b.controls[0];
        if (is_analog(c)) {
            float x, y;
            level(s, c, &x, &y);
            if (kind == ActionKind::Button) {
                v.down = std::hypot(x, y) > kHalf;
                v.pressed = pressed(s, c);
                v.released = released(s, c);
            } else if (c.device == Device::Mouse) {
                v.x = x;
                v.y = y;
            } else {
                v.rx = x;
                v.ry = kind == ActionKind::Axis2 ? y : 0.0f;
            }
        } else {
            v.down = down(s, c);
            v.pressed = pressed(s, c);
            v.released = released(s, c);
            if (kind != ActionKind::Button)
                v.rx = v.down ? 1.0f : 0.0f;
        }
        break;
    }
    case Shape::Axis: {
        const Control n = b.controls[0], p = b.controls[1];
        float neg = 0.0f, pos = 0.0f, dummy = 0.0f;
        if (is_analog(n))
            level(s, n, &neg, &dummy);
        else
            neg = down(s, n) ? 1.0f : 0.0f;
        if (is_analog(p))
            level(s, p, &pos, &dummy);
        else
            pos = down(s, p) ? 1.0f : 0.0f;
        v.rx = pos - neg;
        v.down = v.rx != 0.0f;
        break;
    }
    case Shape::Axis2: {
        const float l = down(s, b.controls[0]) ? 1.0f : 0.0f;
        const float rr = down(s, b.controls[1]) ? 1.0f : 0.0f;
        const float d = down(s, b.controls[2]) ? 1.0f : 0.0f;
        const float u = down(s, b.controls[3]) ? 1.0f : 0.0f;
        v.rx = rr - l;
        v.ry = d - u;
        v.down = v.rx != 0.0f || v.ry != 0.0f;
        break;
    }
    case Shape::Drag: {
        const Control g = b.controls[0];
        v.down = down(s, g);
        v.pressed = pressed(s, g);
        v.released = released(s, g);
        if (v.down) {
            v.x = s.pointer_dx;
            v.y = s.pointer_dy;
        }
        break;
    }
    default:
        break;
    }
    r.spoke = v.any();
    return r;
}

bool mouse_bound(const Binding &b) {
    for (int i = 0; i < control_count(b.shape); ++i)
        if (b.controls[i].device == Device::Mouse)
            return true;
    return false;
}

} // namespace

// ── the table ────────────────────────────────────────────────────────

int ActionTable::find_context(const char *name) const {
    for (std::size_t i = 0; i < contexts.size(); ++i)
        if (contexts[i].name == name)
            return int(i);
    return -1;
}

int ActionTable::context(const char *name, int priority) {
    const int have = find_context(name);
    if (have >= 0)
        return have;
    contexts.push_back({name, priority, false});
    return int(contexts.size()) - 1;
}

int ActionTable::find(int context, const char *id) const {
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].context == context && rows[i].id == id)
            return int(i);
    return -1;
}

ActionRow &ActionTable::add(int context, const ActionDesc &d) {
    int i = find(context, d.id ? d.id : "");
    if (i < 0) {
        rows.push_back({});
        i = int(rows.size()) - 1;
        rows[std::size_t(i)].context = context;
        rows[std::size_t(i)].id = d.id ? d.id : "";
    }
    ActionRow &r = rows[std::size_t(i)];
    r.label = d.label ? d.label : "";
    r.kind = d.kind;
    r.defaults = {};
    for (std::int32_t k = 0; k < d.count; ++k) {
        const Binding &b = d.controls[k];
        (device_of(b) == Device::Pad ? r.defaults.pad : r.defaults.km)
            .push_back(b);
    }
    r.defaults.km_set = r.defaults.pad_set = true;
    return r;
}

Device device_of(const Binding &b) {
    const int n = control_count(b.shape);
    Device d = Device::None;
    for (int i = 0; i < n; ++i)
        if (b.controls[i].device != Device::None)
            d = b.controls[i].device == Device::Pad ? Device::Pad
                                                    : Device::Keyboard;
    if (d == Device::None && b.modifier.device != Device::None)
        d = b.modifier.device == Device::Pad ? Device::Pad : Device::Keyboard;
    return d;
}

bool binding_valid(const Binding &b, std::string *why) {
    const auto refuse = [&](const char *s) {
        if (why)
            *why = s;
        return false;
    };
    const int n = control_count(b.shape);
    if (n == 0)
        return refuse("a binding needs a shape");
    bool pad = false, km = false;
    for (int i = 0; i < n; ++i) {
        const Control c = b.controls[i];
        if (c.device == Device::None)
            return refuse("a binding's controls are all named");
        (c.device == Device::Pad ? pad : km) = true;
    }
    if (b.modifier.device != Device::None)
        (b.modifier.device == Device::Pad ? pad : km) = true;
    if (pad && km)
        return refuse("a binding is one hand: the pad's, or the keyboard "
                      "and mouse's");
    if (b.shape == Shape::Drag && !is_button(b.controls[0]))
        return refuse("a drag is gated by a button");
    if (b.shape == Shape::Drag &&
        b.controls[0].code == int(Mouse::DoubleClick) &&
        b.controls[0].device == Device::Mouse)
        return refuse("a drag is gated by a button, not a double-click");
    if (b.shape == Shape::Axis2)
        for (int i = 0; i < 4; ++i)
            if (!is_button(b.controls[i]))
                return refuse("a four-way axis is four buttons");
    if (b.shape == Shape::Axis &&
        is_analog(b.controls[0]) != is_analog(b.controls[1]))
        return refuse("an axis pairs two buttons or two triggers");
    if (b.modifier.device != Device::None && !is_button(b.modifier))
        return refuse("a modifier is a held button");
    return true;
}

const std::vector<Binding> &effective(const ActionRow &r, Device d) {
    const bool pad = d == Device::Pad;
    if (pad ? r.file.pad_set : r.file.km_set)
        return pad ? r.file.pad : r.file.km;
    if (pad ? r.code.pad_set : r.code.km_set)
        return pad ? r.code.pad : r.code.km;
    return pad ? r.defaults.pad : r.defaults.km;
}

void set_layer(Layer &l, Device d, const std::vector<Binding> &b) {
    if (d == Device::Pad) {
        l.pad = b;
        l.pad_set = true;
    } else {
        l.km = b;
        l.km_set = true;
    }
}

// ── the resolve ──────────────────────────────────────────────────────

void resolve(ActionTable &t, const Snapshot &s) {
    for (ActionRow &r : t.rows) {
        r.value = {};
        r.alive_km = r.alive_pad = 0;
        r.live = -1;
        r.live_device = Device::None;
    }

    // The rows in play: of every id, the highest active context's.
    std::vector<int> eff;
    for (std::size_t i = 0; i < t.rows.size(); ++i) {
        const ActionRow &r = t.rows[i];
        if (!t.contexts[std::size_t(r.context)].active)
            continue;
        bool placed = false;
        for (int &e : eff) {
            const ActionRow &o = t.rows[std::size_t(e)];
            if (o.id != r.id)
                continue;
            if (t.contexts[std::size_t(r.context)].priority >
                t.contexts[std::size_t(o.context)].priority)
                e = int(i);
            placed = true;
            break;
        }
        if (!placed)
            eff.push_back(int(i));
    }

    struct Cand {
        int row, bind;
        Device dev;
        bool chorded;
        int prio, order;
    };
    std::vector<Cand> cands;
    int order = 0;
    for (int e : eff) {
        const ActionRow &r = t.rows[std::size_t(e)];
        if (!r.enabled)
            continue;
        const int prio = t.contexts[std::size_t(r.context)].priority;
        for (Device d : {Device::Keyboard, Device::Pad}) {
            const std::vector<Binding> &bs = effective(r, d);
            for (std::size_t k = 0; k < bs.size(); ++k)
                cands.push_back({e, int(k), d,
                                 bs[k].modifier.device != Device::None, prio,
                                 order++});
        }
    }
    std::stable_sort(cands.begin(), cands.end(),
                     [](const Cand &a, const Cand &b) {
                         if (a.chorded != b.chorded)
                             return a.chorded;
                         if (a.prio != b.prio)
                             return a.prio > b.prio;
                         return a.order < b.order;
                     });

    std::vector<Claim> set, want;
    std::vector<std::pair<int, std::uint64_t>> sources; // (row, source)
    for (const Cand &c : cands) {
        ActionRow &r = t.rows[std::size_t(c.row)];
        const Binding &b = effective(r, c.dev)[std::size_t(c.bind)];
        want.clear();
        claims_of(b, s, want);
        if (taken(set, want))
            continue;
        // Alive is not shadowed: a chord whose modifier is up is still
        // the bar's to list, it just says nothing this frame.
        if (c.dev == Device::Pad)
            r.alive_pad |= 1u << c.bind;
        else
            r.alive_km |= 1u << c.bind;
        if (b.modifier.device != Device::None && !down(s, b.modifier))
            continue;
        // A panel that owns the pointer takes the mouse's bindings for
        // the frame; they are still alive, so the bar still lists them.
        if (!s.mouse_free && mouse_bound(b))
            continue;
        if (r.claims)
            set.insert(set.end(), want.begin(), want.end());

        const std::uint64_t src = source_of(b);
        bool seen = false;
        for (const auto &p : sources)
            seen = seen || (p.first == c.row && p.second == src);
        if (seen)
            continue;
        const Read got = read(b, s, r.kind);
        if (!got.spoke)
            continue;
        sources.push_back({c.row, src});
        ActionValue &v = r.value;
        v.down = v.down || got.v.down;
        v.pressed = v.pressed || got.v.pressed;
        v.released = v.released || got.v.released;
        v.x += got.v.x;
        v.y += got.v.y;
        v.rx += got.v.rx;
        v.ry += got.v.ry;
        if (r.live < 0) {
            r.live = c.bind;
            r.live_device = c.dev;
        }
    }

    // A key on top of a stick is still ONE full deflection.
    for (ActionRow &r : t.rows) {
        r.value.rx = std::clamp(r.value.rx, -1.0f, 1.0f);
        r.value.ry = std::clamp(r.value.ry, -1.0f, 1.0f);
    }
}

const ActionRow *effective_row(const ActionTable &t, const char *id) {
    const ActionRow *best = nullptr;
    for (const ActionRow &r : t.rows) {
        if (r.id != id || !t.contexts[std::size_t(r.context)].active)
            continue;
        if (!best || t.contexts[std::size_t(r.context)].priority >
                         t.contexts[std::size_t(best->context)].priority)
            best = &r;
    }
    return best;
}

// ── names ────────────────────────────────────────────────────────────

namespace {

struct KeyName {
    Key key;
    const char *name;
};

constexpr KeyName kKeyNames[] = {
    {Key::Return, "Return"},
    {Key::Escape, "Esc"},
    {Key::Backspace, "Backspace"},
    {Key::Tab, "Tab"},
    {Key::Space, "Space"},
    {Key::Grave, "`"},
    {Key::F1, "F1"},
    {Key::Right, "Right"},
    {Key::Left, "Left"},
    {Key::Down, "Down"},
    {Key::Up, "Up"},
    {Key::LeftCtrl, "Ctrl"},
    {Key::LeftShift, "Shift"},
    {Key::LeftAlt, "Alt"},
};

constexpr const char *kMouseNames[] = {"Left", "Right", "Middle",
                                       "Move", "Wheel", "DoubleClick"};
constexpr const char *kMouseWords[] = {"click", "right-click", "middle-click",
                                       "mouse", "wheel",       "double-click"};
constexpr const char *kPadNames[] = {
    "A",  "B",     "X",    "Y",  "LB",   "RB",   "LT",    "RT", "L3",
    "R3", "Start", "Back", "Up", "Down", "Left", "Right", "LS", "RS"};
constexpr const char *kPadWords[] = {
    "A",        "B",          "X",          "Y",           "LB",    "RB",
    "LT",       "RT",         "L3",         "R3",          "Start", "Back",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right", "LS",    "RS"};

const char *letter_or_digit(std::int32_t c) {
    static const char *const letters[] = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
    static const char *const digits[] = {"1", "2", "3", "4", "5",
                                         "6", "7", "8", "9", "0"};
    if (c >= int(Key::A) && c <= int(Key::Z))
        return letters[c - int(Key::A)];
    if (c >= int(Key::N1) && c <= int(Key::N0))
        return digits[c - int(Key::N1)];
    return nullptr;
}

std::string trim(const std::string &s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

// Split at top-level commas: an Axis2(...)'s own commas stay inside.
std::vector<std::string> split_list(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char ch : s) {
        if (ch == '(')
            ++depth;
        if (ch == ')')
            --depth;
        if (ch == ',' && depth == 0) {
            out.push_back(trim(cur));
            cur.clear();
            continue;
        }
        cur += ch;
    }
    if (!trim(cur).empty())
        out.push_back(trim(cur));
    return out;
}

} // namespace

const char *key_name(Key k) {
    if (const char *s = letter_or_digit(int(k)))
        return s;
    for (const KeyName &n : kKeyNames)
        if (n.key == k)
            return n.name;
    return nullptr;
}

const char *mouse_name(Mouse m) {
    const int i = int(m);
    return i >= 0 && i < 6 ? kMouseNames[i] : nullptr;
}

const char *pad_name(Pad p) {
    const int i = int(p);
    return i >= 0 && i < kPadControls ? kPadNames[i] : nullptr;
}

std::string control_word(Control c) {
    char buf[32];
    switch (c.device) {
    case Device::Keyboard:
        if (const char *s = key_name(Key(c.code)))
            return s;
        std::snprintf(buf, sizeof buf, "#%d", int(c.code));
        return buf;
    case Device::Mouse:
        return c.code >= 0 && c.code < 6 ? kMouseWords[c.code] : "?";
    case Device::Pad:
        return c.code >= 0 && c.code < kPadControls ? kPadWords[c.code] : "?";
    default:
        return "?";
    }
}

std::string control_name(Control c) {
    char buf[32];
    switch (c.device) {
    case Device::Keyboard:
        if (const char *s = key_name(Key(c.code))) {
            // The file spells the modifiers as their aliases.
            if (c.code == int(Key::LeftShift))
                return "Shift";
            if (c.code == int(Key::LeftCtrl))
                return "Ctrl";
            if (c.code == int(Key::LeftAlt))
                return "Alt";
            return std::string("Key.") +
                   (c.code == int(Key::Grave) ? "Grave" : s);
        }
        std::snprintf(buf, sizeof buf, "Key.#%d", int(c.code));
        return buf;
    case Device::Mouse:
        return std::string("Mouse.") +
               (c.code >= 0 && c.code < 6 ? kMouseNames[c.code] : "?");
    case Device::Pad:
        return std::string("Pad.") +
               (c.code >= 0 && c.code < kPadControls ? kPadNames[c.code] : "?");
    default:
        return "?";
    }
}

bool control_parse(const std::string &raw, Control *out) {
    const std::string s = trim(raw);
    if (s == "Shift" || s == "Key.Shift" || s == "Key.LeftShift")
        return *out = ControlOf(Key::LeftShift), true;
    if (s == "Ctrl" || s == "Key.Ctrl" || s == "Key.LeftCtrl")
        return *out = ControlOf(Key::LeftCtrl), true;
    if (s == "Alt" || s == "Key.Alt" || s == "Key.LeftAlt")
        return *out = ControlOf(Key::LeftAlt), true;
    if (s.rfind("Key.", 0) == 0) {
        const std::string n = s.substr(4);
        if (n.size() > 1 && n[0] == '#')
            return *out = Control{Device::Keyboard, std::atoi(n.c_str() + 1)},
                   true;
        if (n == "Grave")
            return *out = ControlOf(Key::Grave), true;
        for (int c = 0; c < 512; ++c)
            if (const char *k = key_name(Key(c)))
                if (n == k)
                    return *out = Control{Device::Keyboard, c}, true;
        return false;
    }
    if (s.rfind("Mouse.", 0) == 0) {
        const std::string n = s.substr(6);
        for (int i = 0; i < 6; ++i)
            if (n == kMouseNames[i])
                return *out = Control{Device::Mouse, i}, true;
        return false;
    }
    if (s.rfind("Pad.", 0) == 0) {
        const std::string n = s.substr(4);
        for (int i = 0; i < kPadControls; ++i)
            if (n == kPadNames[i])
                return *out = Control{Device::Pad, i}, true;
        return false;
    }
    return false;
}

std::string binding_text(const Binding &b) {
    std::string s;
    if (b.modifier.device != Device::None)
        s += control_name(b.modifier) + "+";
    switch (b.shape) {
    case Shape::Plain:
        s += control_name(b.controls[0]);
        break;
    case Shape::Axis:
        s += "Axis(" + control_name(b.controls[0]) + "," +
             control_name(b.controls[1]) + ")";
        break;
    case Shape::Axis2:
        s += "Axis2(" + control_name(b.controls[0]) + "," +
             control_name(b.controls[1]) + "," + control_name(b.controls[2]) +
             "," + control_name(b.controls[3]) + ")";
        break;
    case Shape::Drag:
        s += "Drag(" + control_name(b.controls[0]) + ")";
        break;
    default:
        s += "none";
        break;
    }
    return s;
}

bool binding_parse(const std::string &raw, Binding *out, std::string *why) {
    const auto refuse = [&](const std::string &s) {
        if (why)
            *why = s;
        return false;
    };
    std::string s = trim(raw);
    Binding b{};
    // A modifier is the part before the LAST '+' outside parentheses.
    int depth = 0;
    std::size_t plus = std::string::npos;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(')
            ++depth;
        if (s[i] == ')')
            --depth;
        if (s[i] == '+' && depth == 0)
            plus = i;
    }
    if (plus != std::string::npos) {
        if (!control_parse(s.substr(0, plus), &b.modifier))
            return refuse("unknown modifier in \"" + s + "\"");
        s = trim(s.substr(plus + 1));
    }
    const auto args = [&](const std::string &head, int n) {
        if (s.rfind(head + "(", 0) != 0 || s.back() != ')')
            return false;
        const std::vector<std::string> parts =
            split_list(s.substr(head.size() + 1, s.size() - head.size() - 2));
        if (int(parts.size()) != n)
            return refuse(head + " takes " + std::to_string(n) + " controls");
        for (int i = 0; i < n; ++i)
            if (!control_parse(parts[std::size_t(i)], &b.controls[i]))
                return refuse("unknown control \"" + parts[std::size_t(i)] +
                              "\"");
        return true;
    };
    if (s.rfind("Axis2(", 0) == 0) {
        b.shape = Shape::Axis2;
        if (!args("Axis2", 4))
            return false;
    } else if (s.rfind("Axis(", 0) == 0) {
        b.shape = Shape::Axis;
        if (!args("Axis", 2))
            return false;
    } else if (s.rfind("Drag(", 0) == 0) {
        b.shape = Shape::Drag;
        if (!args("Drag", 1))
            return false;
    } else {
        b.shape = Shape::Plain;
        if (!control_parse(s, &b.controls[0]))
            return refuse("unknown control \"" + s + "\"");
    }
    std::string bad;
    if (!binding_valid(b, &bad))
        return refuse(bad);
    *out = b;
    return true;
}

// ── the file ─────────────────────────────────────────────────────────

namespace {

std::string side_text(const Layer &l, Device d) {
    const bool set = d == Device::Pad ? l.pad_set : l.km_set;
    if (!set)
        return "-";
    const std::vector<Binding> &bs = d == Device::Pad ? l.pad : l.km;
    if (bs.empty())
        return "none";
    std::string s;
    for (std::size_t i = 0; i < bs.size(); ++i)
        s += (i ? ", " : "") + binding_text(bs[i]);
    return s;
}

// One side of a line into a layer: `-` leaves it, `none` empties it.
void side_load(Layer &l, Device d, const std::string &text) {
    const std::string s = trim(text);
    if (s == "-")
        return;
    std::vector<Binding> bs;
    if (s != "none")
        for (const std::string &part : split_list(s)) {
            Binding b;
            if (binding_parse(part, &b, nullptr))
                bs.push_back(b);
        }
    set_layer(l, d, bs);
}

} // namespace

std::string table_save(const ActionTable &t, const char *title) {
    std::string out = "# simview bindings";
    if (title && *title)
        out += std::string(" — ") + title;
    out += "\n";
    for (const ActionRow &r : t.rows) {
        if (!r.file.km_set && !r.file.pad_set)
            continue;
        out += t.contexts[std::size_t(r.context)].name + "." + r.id + " = " +
               side_text(r.file, Device::Keyboard) + " | " +
               side_text(r.file, Device::Pad) + "\n";
    }
    for (const std::string &u : t.unknown)
        out += u + "\n";
    return out;
}

void table_load(ActionTable &t, const std::string &text) {
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos)
            nl = text.size();
        const std::string line = trim(text.substr(at, nl - at));
        at = nl + 1;
        if (line.empty() || line[0] == '#')
            continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            t.unknown.push_back(line);
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::size_t dot = key.find('.');
        const int ctx = dot == std::string::npos
                            ? -1
                            : t.find_context(key.substr(0, dot).c_str());
        const int row = ctx < 0 ? -1 : t.find(ctx, key.substr(dot + 1).c_str());
        if (row < 0) {
            t.unknown.push_back(line);
            continue;
        }
        Layer &l = t.rows[std::size_t(row)].file;
        const std::string rhs = line.substr(eq + 1);
        const std::size_t bar = rhs.find('|');
        if (bar != std::string::npos) {
            side_load(l, Device::Keyboard, rhs.substr(0, bar));
            side_load(l, Device::Pad, rhs.substr(bar + 1));
            continue;
        }
        // No bar: each binding lands on the hand it belongs to.
        std::vector<Binding> km, pad;
        const std::string s = trim(rhs);
        if (s == "none") {
            set_layer(l, Device::Keyboard, {});
            continue;
        }
        for (const std::string &part : split_list(s)) {
            Binding b;
            if (binding_parse(part, &b, nullptr))
                (device_of(b) == Device::Pad ? pad : km).push_back(b);
        }
        if (!km.empty())
            set_layer(l, Device::Keyboard, km);
        if (!pad.empty())
            set_layer(l, Device::Pad, pad);
    }
}

// ── capture ──────────────────────────────────────────────────────────

Control capture_scan(const Snapshot &s, Device d) {
    if (d == Device::Pad) {
        for (int i = 0; i < kPadControls; ++i)
            if (pad_presses(i) && s.pad_pressed[i])
                return {Device::Pad, i};
        if (std::hypot(s.lx, s.ly) > kHalf)
            return ControlOf(Pad::LS);
        if (std::hypot(s.rx, s.ry) > kHalf)
            return ControlOf(Pad::RS);
        return {};
    }
    for (int i = 0; i < 512; ++i)
        if (s.key_pressed.test(std::size_t(i)))
            return {Device::Keyboard, i};
    for (int i = 0; i < 3; ++i)
        if (s.mouse_pressed[i])
            return {Device::Mouse, i};
    return {};
}

} // namespace impl
} // namespace sv
