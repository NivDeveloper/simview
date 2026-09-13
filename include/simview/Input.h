#pragma once

#include "Event.h"

#include <cstdint>
#include <vector>

namespace sv {

enum class Mouse : std::int32_t {
    Left = 0,
    Right = 1,
    Middle = 2,
    Move = 3,
    Wheel = 4,
    DoubleClick = 5,
};

enum class Pad : std::int32_t {
    A = 0,
    B = 1,
    X = 2,
    Y = 3,
    LB = 4,
    RB = 5,
    LT = 6,
    RT = 7,
    L3 = 8,
    R3 = 9,
    Start = 10,
    Back = 11,
    Up = 12,
    Down = 13,
    Left = 14,
    Right = 15,
    LS = 16,
    RS = 17,
};

enum class Shape : std::int32_t {
    None = 0,
    Plain = 1,
    Axis = 2,
    Axis2 = 3,
    Drag = 4,
};

struct Binding {
    Shape shape = Shape::None;
    Control controls[4] = {};
    Control modifier = {};
};

enum class ActionKind : std::int32_t {
    Button = 0,
    Axis = 1,
    Axis2 = 2,
};

enum class CameraMode : std::int32_t {
    Orbit = 0,
    Fly = 1,
};

enum class PointerStyle : std::int32_t {
    Cursor = 0,
    Crosshair = 1,
};

struct ActionDesc {
    const char *id = nullptr;
    const char *label = nullptr;
    ActionKind kind = ActionKind::Button;
    const Binding *controls = nullptr;
    std::int32_t count = 0;
};

struct ModeDesc {
    const char *name = nullptr;
    const Binding *enter = nullptr;
    std::int32_t count = 0;
};

inline Control ControlOf(Key k) {
    return {Device::Keyboard, static_cast<std::int32_t>(k)};
}

inline Control ControlOf(Mouse m) {
    return {Device::Mouse, static_cast<std::int32_t>(m)};
}

inline Control ControlOf(Pad p) {
    return {Device::Pad, static_cast<std::int32_t>(p)};
}

inline Binding Ctl(Key k) { return {Shape::Plain, {ControlOf(k)}, {}}; }
inline Binding Ctl(Mouse m) { return {Shape::Plain, {ControlOf(m)}, {}}; }
inline Binding Ctl(Pad p) { return {Shape::Plain, {ControlOf(p)}, {}}; }

inline Binding Axis(Key neg, Key pos) {
    return {Shape::Axis, {ControlOf(neg), ControlOf(pos)}, {}};
}

inline Binding Axis(Pad neg, Pad pos) {
    return {Shape::Axis, {ControlOf(neg), ControlOf(pos)}, {}};
}

inline Binding Axis2(Key l, Key r, Key d, Key u) {
    return {Shape::Axis2,
            {ControlOf(l), ControlOf(r), ControlOf(d), ControlOf(u)},
            {}};
}

inline Binding Axis2(Pad l, Pad r, Pad d, Pad u) {
    return {Shape::Axis2,
            {ControlOf(l), ControlOf(r), ControlOf(d), ControlOf(u)},
            {}};
}

inline Binding Drag(Mouse button) {
    return {Shape::Drag, {ControlOf(button)}, {}};
}

inline Binding Drag(Pad button) {
    return {Shape::Drag, {ControlOf(button)}, {}};
}

inline Binding With(Key modifier, Binding b) {
    b.modifier = ControlOf(modifier);
    return b;
}

inline Binding With(Pad modifier, Binding b) {
    b.modifier = ControlOf(modifier);
    return b;
}

inline Event MouseDown(Mouse m) {
    return {Event::Type::Down, ControlOf(m), 0.0f, 0.0f, false};
}

inline Event MouseUp(Mouse m) {
    return {Event::Type::Up, ControlOf(m), 0.0f, 0.0f, false};
}

inline Event MouseMove(float x, float y) {
    return {Event::Type::Move, ControlOf(Mouse::Move), x, y, false};
}

inline Event Look(float dx, float dy) {
    return {Event::Type::Delta, ControlOf(Mouse::Move), dx, dy, false};
}

inline Event MouseWheel(float dy) {
    return {Event::Type::Delta, ControlOf(Mouse::Wheel), 0.0f, dy, false};
}

inline Event PadDown(Pad p) {
    return {Event::Type::Down, ControlOf(p), 0.0f, 0.0f, false};
}

inline Event PadUp(Pad p) {
    return {Event::Type::Up, ControlOf(p), 0.0f, 0.0f, false};
}

inline Event PadAxis(Pad p, float x, float y = 0.0f) {
    return {Event::Type::Delta, ControlOf(p), x, y, false};
}

struct Action {
    const char *id = nullptr;
    const char *label = nullptr;
    std::vector<Binding> controls;
};

struct ModeSpec {
    const char *name = nullptr;
    std::vector<Binding> enter;
};

struct Stroke;

namespace impl {

struct App;

struct Mode {
    void *p = nullptr;
    explicit operator bool() const { return p != nullptr; }
};

void app_bind(App *, const ActionDesc &, void (*fn)(float, float, void *),
              void *user, void (*free)(void *));
void app_rebind(App *, const char *key, const Binding *, std::int32_t count);
void app_camera_mode(App *, CameraMode);
CameraMode app_camera_mode(App *);
void app_pointer_style(App *, PointerStyle);
PointerStyle app_pointer_style(App *);
bool app_enter_mode(App *, const char *name);
const char *app_active_mode(App *);
Mode mode_create(App *, const ModeDesc &);
const char *mode_name(Mode);
void mode_bind(Mode, const ActionDesc &, void (*fn)(float, float, void *),
               void *user, void (*free)(void *));
void mode_on_stroke(Mode, void (*fn)(const Stroke &, void *), void *user,
                    void (*free)(void *));
void mode_on_carry(Mode, void (*fn)(const Stroke &, void *), void *user,
                   void (*free)(void *));

}

class Mode {
  public:
    Mode() = default;
    explicit Mode(impl::Mode m) : m_(m) {}

    explicit operator bool() const { return bool(m_); }
    impl::Mode Raw() const { return m_; }
    const char *Name() const { return impl::mode_name(m_); }

    template <class F> Mode &Bind(const Action &a, F fn) {
        impl::mode_bind(
            m_, Desc(a, ActionKind::Button),
            [](float, float, void *u) { (*static_cast<F *>(u))(); },
            new F(std::move(fn)), [](void *u) { delete static_cast<F *>(u); });
        return *this;
    }

    template <class F> Mode &Axis(const Action &a, F fn) {
        impl::mode_bind(
            m_, Desc(a, ActionKind::Axis),
            [](float x, float, void *u) { (*static_cast<F *>(u))(x); },
            new F(std::move(fn)), [](void *u) { delete static_cast<F *>(u); });
        return *this;
    }

    template <class F> Mode &Axis2(const Action &a, F fn) {
        impl::mode_bind(
            m_, Desc(a, ActionKind::Axis2),
            [](float x, float y, void *u) { (*static_cast<F *>(u))(x, y); },
            new F(std::move(fn)), [](void *u) { delete static_cast<F *>(u); });
        return *this;
    }

    template <class F> Mode &OnStroke(F fn) {
        impl::mode_on_stroke(
            m_, [](const Stroke &s, void *u) { (*static_cast<F *>(u))(s); },
            new F(std::move(fn)), [](void *u) { delete static_cast<F *>(u); });
        return *this;
    }

    template <class F> Mode &OnCarry(F fn) {
        impl::mode_on_carry(
            m_, [](const Stroke &s, void *u) { (*static_cast<F *>(u))(s); },
            new F(std::move(fn)), [](void *u) { delete static_cast<F *>(u); });
        return *this;
    }

    static ActionDesc Desc(const Action &a, ActionKind kind) {
        return {a.id, a.label, kind, a.controls.data(),
                std::int32_t(a.controls.size())};
    }

  private:
    impl::Mode m_;
};

class Modes {
  public:
    explicit Modes(impl::App *a) : a_(a) {}

    Modes &Camera(CameraMode m) {
        impl::app_camera_mode(a_, m);
        return *this;
    }

    CameraMode Camera() const { return impl::app_camera_mode(a_); }

    Modes &Pointer(PointerStyle s) {
        impl::app_pointer_style(a_, s);
        return *this;
    }

    PointerStyle Pointer() const { return impl::app_pointer_style(a_); }

    Modes &Enter(const char *name) {
        impl::app_enter_mode(a_, name);
        return *this;
    }

    Modes &Exit() {
        impl::app_enter_mode(a_, nullptr);
        return *this;
    }

    const char *Active() const { return impl::app_active_mode(a_); }

  private:
    impl::App *a_;
};

}
