#pragma once

// Gestures, for checks that want to test an interaction rather than a
// picture.
//
// A drag is not one event: it is a position, then a press, then a
// motion, then a release, and the layer under test only sees the
// difference between consecutive FRAMES. Spelling that out in every
// check is how a check ends up asserting something other than what it
// meant to. Here a drag is one call, and the frames it takes are this
// file's business.
//
// What this covers: the UI's hit-testing and everything above it —
// which panel owns the pointer, whether a drag was latched, what the
// camera did about it. What it does NOT cover is the platform's
// translation of real events into UI events; that is the backend's
// own, and the windowed showcases under `make validate` are where it
// is exercised for real.

#include "Check.h"

#include "probe/Probe.h"

#include <simview/simview.h>

#include <cmath>

namespace input {

enum Button { Left = 0, Right = 1, Middle = 2 };

// Put the pointer somewhere and let a frame see it.
inline void move(sv::App &app, float x, float y) {
    sv::probe::mouse_move(app.Raw(), x, y);
    app.Step();
}

inline void press(sv::App &app, Button b = Left) {
    sv::probe::mouse_button(app.Raw(), int(b), true);
    app.Step();
}

inline void release(sv::App &app, Button b = Left) {
    sv::probe::mouse_button(app.Raw(), int(b), false);
    app.Step();
}

inline void shift(sv::App &app, bool down) {
    sv::probe::mouse_modifier_shift(app.Raw(), down);
}

// A drag, whole: the pointer arrives, the button goes down THERE, the
// pointer moves, the button comes up. The arrival is its own frame
// because a press is only "on" what the previous frame saw under the
// cursor.
inline void drag(sv::App &app, float x0, float y0, float x1, float y1,
                 Button b = Left) {
    move(app, x0, y0);
    press(app, b);
    move(app, x1, y1);
    release(app, b);
}

// A click: the pointer arrives, the button goes down and comes up with
// no motion between. A double-click is two, within ImGui's time.
inline void click(sv::App &app, float x, float y, Button b = Left) {
    move(app, x, y);
    press(app, b);
    release(app, b);
}

inline void double_click(sv::App &app, float x, float y) {
    click(app, x, y);
    press(app);
    release(app);
}

// The wheel turns where the pointer already is.
inline void wheel(sv::App &app, float x, float y, float dy) {
    move(app, x, y);
    sv::probe::mouse_wheel(app.Raw(), dy);
    app.Step();
}

// A key, posted: it lands where SDL's own do, and a frame sees it.
inline void key(sv::App &app, sv::Key k, bool down) {
    app.PostEvent(down ? sv::KeyDown(k) : sv::KeyUp(k));
    app.Step();
}

inline void tap(sv::App &app, sv::Key k) {
    key(app, k, true);
    key(app, k, false);
}

// The captured pointer moves. Only a flight reads it.
inline void look(sv::App &app, float dx, float dy) {
    sv::probe::look(app.Raw(), dx, dy);
    app.Step();
}

// How far a camera turned, as the angle between two forward vectors in
// degrees — one number, and the one a person would describe.
inline float turned(const sv::probe::CameraState &a,
                    const sv::probe::CameraState &b) {
    float d = 0.0f;
    for (int i = 0; i < 3; ++i)
        d += a.forward[i] * b.forward[i];
    d = d > 1.0f ? 1.0f : (d < -1.0f ? -1.0f : d);
    return std::acos(d) * 57.2957795f;
}

inline float moved(const sv::probe::CameraState &a,
                   const sv::probe::CameraState &b) {
    float s = 0.0f;
    for (int i = 0; i < 3; ++i)
        s += (a.focus[i] - b.focus[i]) * (a.focus[i] - b.focus[i]);
    return std::sqrt(s);
}

} // namespace input
