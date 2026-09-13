# Input: controls → actions → contexts

How a key, a mouse gesture or a gamepad control becomes something the
engine or the app does, and why the mode is the app's state and never
the device's. The model lives in `src/core/Actions.*` with no device in
the room; the devices feed it from `src/platform/Input.cpp`; the frame
drives it from `src/ui/Actions.cpp`; the pointer and the gestures are
`src/ui/Pointer.cpp`; the bar and the settings page spell it through
`src/ui/Chips.cpp` and `src/ui/Settings.cpp`.

## The three layers

**Controls** are what a device has: a key by scancode, a mouse button,
the mouse's motion and wheel, a pad's buttons, its two sticks and two
triggers (`include/simview/Input.h`: `Key`, `Mouse`, `Pad`; a `Control`
is a device and a code). A **binding** shapes controls into a value:
`Plain` (one control), `Axis` (a negative and a positive, two buttons or
two triggers), `Axis2` (left, right, down, up as four buttons), `Drag`
(the pointer's motion while a button is held), any of them under a
held modifier (`With`). A binding is one hand: the pad's, or the
keyboard and mouse's.

**Actions** are what something wants: an id, a label, and a kind —
`Button` (down, pressed, released), `Axis` (a rate from buttons or
sticks plus a delta from the wheel), `Axis2` (the same in two). An
action is a row in a **context**.

**Contexts** are layers of rows with a priority. The engine has seven:

| context | priority | on when | rows |
| --- | --- | --- | --- |
| `base` | 0 | always | `settings`, `mode.<name>` per app mode, the app's own |
| `orbit` / `fly` | 1 | the camera mode | `mode.camera`, `camera.turn` (orbit only), `camera.slide`, `camera.depth`, `camera.frame`; fly adds `camera.fast`, `camera.speed` |
| `cursor` / `crosshair` | 2 | the pointer style | `mode.pointer`, `pointer.primary`; cursor adds `pointer.move` (passive) and `pointer.secondary`; crosshair adds `camera.turn` |
| `stroke` | 3 | a mode with `OnStroke` or `OnCarry` is on | `pointer.drag`, `pointer.depth` (only while carrying with the primary held) |
| `mode` | 4 | any mode is on | `mode.exit` |
| `mode:<name>` | 5 | that mode is on | the mode's own rows |

An app adds rows to `base` with `Bind`/`Axis`/`Axis2`, a mode with
`app.Mode({.name, .enter})` and its own `Bind`, and listens for the
pointer's gestures with `OnStroke` and `OnCarry`. Cut and drag are the
cloth's modes, not the library's.

## The resolve

Once a frame, one pure function (`resolve`, proved by `bindings_check`
with no device):

1. Of every id, the row of the highest active context is the one in
   play — `crosshair.camera.turn` replaces `orbit.camera.turn`.
2. Candidates are the rows' bindings; **chorded before bare, higher
   context before lower**, then registration order.
3. A candidate is live when its modifier is held. It **claims** its
   controls by role — a button as a Button, a stick or the wheel as
   Motion, a drag's button as a Gate and the pointer's step as Motion
   while the button is down — and a later candidate wanting a claimed
   (control, role) is shadowed. So a click picks and a drag orbits on
   one button, and a mode's `Drag(Left)` shadows the orbit's while the
   orbit's `Drag(Right)` still turns.
4. Values accumulate per action, **one source counted once** (a drag
   from the mouse and from the pad are the same pointer step) and
   **rates clamped to one deflection** (a key on top of a stick).
5. A panel that owns the pointer takes every mouse binding for the
   frame; they stay listed as alive, so the bar does not blink.

Edges are recorded as events arrive, so a press and a release inside
one frame are both seen; a key repeat is never an edge; a key press
while a panel is typing is dropped and its release always noted, so no
key is left held.

## Camera modes and pointer styles

The camera is **Orbit** (turn about the focus, pan, zoom) or **Fly**
(turn about the eye, walk, rise, at a speed the wheel or the D-pad
scales). The pointer is a **Cursor** on the picture or a **Crosshair**
at its centre with the picture turning under it. Fly and Cursor are
mutually exclusive: `Camera(Fly)` brings the crosshair, `Pointer(Cursor)`
brings the orbit. Neither is a property of a device.

| action | Orbit + Cursor | Orbit + Crosshair | Fly + Crosshair |
| --- | --- | --- | --- |
| turn | drag, right-drag · A + RS, B + RS | mouse · RS | mouse · RS |
| slide | Shift + drag, middle-drag · LS | Shift + drag, middle-drag · LS | W A S D · LS |
| depth | wheel · LT RT | wheel · LT RT | Q E · LT RT |
| fast / speed | | | Shift · L3 / wheel · D-pad |
| frame | 2× click | 2× click | 2× click |
| toggle camera | Tab · Back | Tab · Back | Tab · Back |
| toggle pointer | Ctrl+Tab · R3 | Ctrl+Tab, Esc · R3, B | Ctrl+Tab, Esc · R3, B |
| settings | F1 · Start | F1 · Start | F1 · Start |

Under the cursor the pad's right stick moves the SAME cursor the mouse
does, warped along in a window and fed to ImGui headless, and A is the
mouse's left button to ImGui and the resolver alike: a panel's checkbox,
a hover and a pick behave the same from either device (`cursor_check`).
Under the crosshair the mouse is captured, ImGui is blind, both devices
turn the camera, and a press acts at the centre; a stroke there runs
from where the aim point was last frame to the centre, so a turn sweeps
it. Focus loss ends the crosshair, as it must: a window that lost focus
cannot show its own hidden cursor again.

Esc and B peel one layer a press: a mode's exit, then the crosshair,
then whatever the app bound.

## Gestures

Hover (no button down), pick (a release that never dragged past six
pixels), the double-click that frames (two primary presses within 0.3 s
and six pixels, on any device, timed by the frame's dt), the stroke and
the carry (a drag with the primary, to the active mode) — all from the
pointer's values, on the world the pointer means: the one the press
landed on until its release, else the one under the pointer, else the
window's, and the aimed one under a crosshair. A `Stroke` is a frame's
worth: two picture points, the view they were drawn through, what the
press began on, and a depth — the wheel or the triggers while carrying,
which `Carry` applies along the point's own line of sight.

## The bar and the settings page

The bar on the picture is generated from the table for the device in
hand: the modes with the active one lit, the app's labelled actions,
the engine's toggles, and under them what the camera, the pointer and
the mode read on that hand — a row shows its first binding the last
resolve left alive. One spelling (`chip_for`) serves the bar and the
page: keycaps a word, the mouse's glyphs, pills for the pad.

F1 (Start) opens the page: every row of every context, its bindings on
both hands, a cell that captures the next control of that hand (a held
Shift/Ctrl/Alt or LB/RB wraps it; on an axis row two presses or a
trigger; on a two-axis row a stick, a button as a drag, or four
presses; Esc cancels, Backspace unbinds), a reset per row and for all,
and rows of one context binding one control the same way tinted with
the other's name. Changes go to `bindings.txt` beside `layout.ini`
under SDL's preference path, only the rows overridden, so untouched
rows follow the code:

```
# simview bindings — cloth
orbit.camera.turn = Drag(Mouse.Left), Drag(Mouse.Right) | Drag(Pad.A), Drag(Pad.B)
base.pause = Key.Return | -
fly.camera.slide = Axis2(Key.A,Key.D,Key.S,Key.W) | none
```

`context.action = keyboard side | pad side`; `-` leaves a side to the
code, `none` empties it; a line with no bar lands each binding on its
own hand; unknown lines are kept and rewritten. Precedence per row is
file over code (`Rebind`, or `Bind`'s own controls) over the engine's
defaults. Headless there is no file; the probe loads and saves the same
text.

## What changed, and why

Before this the camera mode was a property of the device (the mouse
orbited, Tab flew it, the pad flew always), `Tool::Cut/Drag` were the
cloth's words in the library, the pad was special-cased at every seam,
and dispatch was an if-chain. Now the mode is one state, every engine
action is bound on both hands, modes are the app's, and shadowing is
the rule — a table a check can enumerate rather than a chain it can
only exercise. Two consequences to know: an app's `Bind` callback runs
in the frame's build, one frame after the poll it used to run in; and
a stroke under a crosshair in orbit sweeps only past points off the
focus, since an orbit keeps its focus centred.
