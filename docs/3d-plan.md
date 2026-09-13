# The world stratum

A world is the 3D sibling of a scene: a camera, a list of items, and a
draw that orders before it records. It is a second stratum rather than
a fourth scene kind because the things a 3D renderer must vary — depth
state, pass membership, draw order — are exactly the things the 2D
kind contract fixes, and widening that contract would spend its
simplicity on a case it does not have.

What W1 landed is the ARCHITECTURE plus the smallest feature set that
proves it: three item kinds through one contract, one pass table, one
sort, reverse-Z depth, an orbital camera, and clouds through all three
data doors. Lighting, colormaps, meshes and shadows are later tickets
against an unchanged contract.

## The laws

**1. Items submit; the world draws.** A 2D kind records its draw where
it is built. A world item cannot: where its geometry lands depends on
the camera, so it emits a `DrawCmd` and the world decides the order.
That single indirection is the optimization surface — sorting lives
behind it now, culling and batching would land behind it without an
item noticing.

**2. Passes are a fixed table** (`src/world/Passes.h`), in execution
order, `constexpr`. A render graph buys automatic barriers (the
renderer already inserts those), transient aliasing (there are two
targets), pass culling (that is one bool) and decoupled authoring
(there are no user passes). It would cost a scheduler and buy nothing.
What a graph would need is declared anyway — each row says what it
clears, how it treats depth and how its draws are ordered — so the day
a scheduler is wanted, only the scheduler is missing.

| pass | enabled | clears | depth | order |
| --- | --- | --- | --- | --- |
| shadow | no (W5) | depth | test, write | state, then near |
| opaque | yes | colour + depth | test, write, `GreaterOrEqual` | state, then near |
| transparent | yes | — | test, no write, `Greater` | far, then state |
| overlay | yes | — | test, no write | submission |

The shadow row exists and is disabled rather than absent: a row
appended later renumbers every row after it. Transparent tests
strictly greater so a translucent surface coplanar with the opaque one
that wrote the depth cannot blend itself twice. Overlay is
depth-AWARE: the grid is occluded by geometry but never occludes.

**3. One sort, and the pass is the outer loop.** Every item's commands
go into one vector, sorted once by `(pass, key, seq)`; a pass is then
a contiguous range rather than a filter. The pass never enters the key
— encoding four values known at compile time would make the top bits a
constant.

- opaque: `pipeline << 48 | item << 32 | (65535 − depth) << 16`.
  State leads, because a pipeline change costs more than the overdraw
  the depth test would have saved; depth only breaks ties, nearest
  first, so early-Z discards the rest.
- transparent: `depth << 48 | pipeline << 32`. Depth leads, because
  there the order IS the result.
- overlay: key 0; the submission index alone orders it.

`seq` is the trailing tie-break, which is why a plain `std::sort` is
enough — no stable sort, and no radix sort at this scale (a few
hundred commands sort in microseconds; the scratch buffer and the pass
count would cost more than they save).

The depth field is `znear / z_view` quantized to 16 bits — the very
number the depth buffer will hold, so the order the sort produces is
the order the test enforces.

**4. Reverse-Z, in five agreeing parts.** A float `D32` attachment;
the depth cleared to **0**; `GreaterOrEqual` set EXPLICITLY on every
pipeline (the renderer's default is `Less`, which under a reversed
buffer keeps exactly the geometry it should discard); the viewport's
depth range left at 0..1; and an infinite-far projection whose z row
is `[0, 0, 0, n]`, so depth is `znear / z_view` — 1 at the near plane,
approaching 0 forever. Precision then comes from the exponent, and it
is the NEAR distance that governs it, so `znear` follows the orbit
distance rather than sitting at a fixed 0.01.

The projection's `m[1][1]` is POSITIVE. The renderer flips the
viewport height itself (`VKViewportWithDXCoords` negates it
unconditionally), so a matrix that also flipped would render the world
upside down.

**5. State control is a cache key.** The world's pipeline cache is
keyed `(ops, pass, colour format, depth format)`. Blend, topology and
the binding shape are data on the ops; depth state comes from the pass
row. The 2D cache is left alone: it is keyed `(kind, format)` and says
why, and a target with a depth attachment is not pipeline-compatible
with one without in any case.

**6. A world is the window, or a panel.** `app.World()` with NO title
draws into the swapchain itself, with the ImGui panels floating over
it — what a program whose subject IS the 3D scene wants, and the
default. A titled one lives on a `View` (`unique_ptr<WorldState>`,
null meaning the 2D case) and is a panel among panels, for a layout
with several views. They differ in nothing but the framebuffer they
end up in: the same items, the same passes, the same camera. The
window's framebuffers grow a depth image the moment a world asks for
one, and so does a headless shot's target — without that a `Shot()` of
a 3D program would come back empty.

## Where things are

```
include/simview/World.h        WorldDesc, CameraDesc, the World builder
include/simview/world/Cloud.h  CloudMode/CloudDesc, the Cloud handle
include/simview/world/Wire.h   WireDesc, the Wire handle
src/core/Math.h     Vec3, Quat, Mat4, Aabb, Plane, Frustum — pure
                    functions and nothing else
src/world/Camera.h  Camera3, the reverse-Z projection, the near plane
                    and the ordering keys: the DECISIONS about how this
                    engine looks at a scene, which a file of pure
                    functions should not be carrying
src/world/Passes.h  the pass table
src/world/Items.h   WorldItemOps, WorldItem, DrawCmd, WorldView
src/world/World.h   WorldState + the draw
src/world/World.cpp constants, submit, sort, the pass loop
src/world/Pipelines.cpp  the world's pipeline cache
src/world/Channel.h      a channel: host, Sync or device buffer, and
                         the readback that gives the last a host copy
src/world/Cloud.cpp      the cloud item, three ops rows, three doors
src/world/Wire.cpp       the wire item: edges over a channel, a mask
src/world/GridAxes.cpp   the grid and the axes, as ordinary items
src/ui/World.cpp    world_create, the camera controller, the stroke
shaders/{world_view,cloud,mesh,wire,grid3,axes3}.slang
```

Math is a 250-line internal header rather than a dependency: ten
functions do not justify pinning a library, and being header-only is
what lets `math_check` prove the projection and the camera with
no device in the room.

## The camera

A turntable. `Camera3` carries a focus, a distance, a world-from-camera
quaternion and a field of view; the position is always re-derived from
`focus + q·(0,0,1)·distance`, because two copies of one fact drift.
Yaw is about the WORLD up axis (+Z — the world is Z-up) so the horizon
stays level however far the view has tilted; pitch is about the
camera's own right. Pan moves the focus in the screen plane at a rate
proportional to distance; dolly is multiplicative and clamped.

The controller (`src/ui/World.cpp`) is the only input reader in the
engine, and it reads ImGui's state, never SDL — except in flight,
below, where ImGui is blind by design. One gesture function
takes the two facts it needs — is the pointer over this world, is a
drag on it under way — and the caller establishes them: a world in a
panel from the item ImGui latches, a world in the window from whether
any panel claimed the pointer. Because ImGui hands over per-frame
deltas there is no remembered cursor of ours to seed, so the
first-frame jump a remembered position causes cannot happen; and
because whoever the press landed on owns the drag until the release,
no latch of our own is needed (one was written and then removed, when
a drill proved nothing could tell it apart).

Three things about that path were wrong until a check could speak
mouse, and none of them was visible in a picture:

- **A world in the window read no pointer at all.** Its input hangs
  off the UI frame, and the UI frame ran only when something had
  registered a panel — a world registers none. `ui_on` now counts a
  world in the window as a reason to build one.
- **A world in a panel never latched a drag.** Its rect was an
  `ImGui::Image`, which can be hovered and can never become ACTIVE,
  because nothing about an image responds to a press. The rect is now
  an `InvisibleButton` with the texture drawn under it — the idiom for
  a viewport, and the thing that makes a drag a drag.
- **Neither was true when W1 claimed both were.** The gap was named in
  W1 as "the controller has no headless check"; this is what was
  behind it.

## Flight

Tab captures the world under the pointer, or the window's, and the
camera becomes a first-person one: the pointer's motion turns it about
the EYE, W A S D move it along its own right and forward, Q and E
along world up, Shift is faster and the wheel scales the speed. Tab or
Escape releases it, and so does the window losing focus — a window
that cannot show its own cursor must not keep it hidden. The world
menu has a `fly` entry naming the key, and in flight the corner
button gives way to a one-line hint, since there is no pointer to
press a button with.

The mode is ONE fact: `App::flying`, the world being flown or null,
written by `world_fly_begin` and `world_fly_end` and nowhere else.
Beginning does four things together — relative mouse mode on the SDL
window, ImGui made blind to mouse and keyboard, the held-key set
cleared, the pointer set — and ending undoes the three that persist,
so the mode cannot be half-entered. That is
where its predecessor spent its bugs: vklib flipped a bool on the
camera from inside a key callback and patched the cursor, the UI's
event acceptance and a stored velocity around it, and needed a
first-frame flag to swallow the jump that computing deltas from
absolute positions produced across the switch.

Three consequences of that one fact:

- **Keys are a held SET, read once a frame, not a velocity set on
  press.** A flight begins by clearing it, because a release can land
  in another window and a press remembered from before take-off would
  move the camera forever. The step is the state of the set times the
  frame time; nothing is remembered between frames. Clearing it at
  landing too was written and removed: nothing reads the set out of
  flight, and the drill could not tell the two apart.
- **In flight the mouse is the platform's, not ImGui's.** SDL's
  relative motion and wheel are summed in `poll` into `Input`. ImGui's
  own delta cannot serve: SDL3's relative mode still moves the reported
  position inside the window, and ImGui would keep hovering panels
  under a hidden cursor. `NoMouse` and `NoKeyboard` are what stop that,
  and the orbit gesture is gated besides — a press latched before the
  flight would otherwise still be active under it.
- **The flight keeps exactly its own keys.** W A S D Q E Shift Tab
  Escape stop at the engine; Space and R still reach the sim, so a
  pause or a restart works in the air. Out of flight the engine
  consumes one key, Tab, and only while the app has a world. One
  dispatch function serves SDL's events and `PostEvent`'s, which is
  what makes a posted W a real W.

Which world Tab takes is `App::pointed`, rewritten every UI frame by
whoever is hovered — a panel world by its WINDOW, since an
overlap-allowed item learns its hover a frame late, the window's world
when no panel claimed the pointer — and read by the next Tab.

**A gamepad steers without a flight.** The left stick walks, the
right stick looks, the triggers lift, the left stick pressed is its
Shift and B its Escape. It needs no mode: a pad has no hotkeys to
collide with and no pointer to hide, so it steers the world under the
pointer, else the window's, whenever it is touched, and in a flight
it steers the flown world beside the keys and the mouse. Both devices
are live at once and the hand-off is seamless: a key on top of a
stick is still ONE full deflection, never twice the speed, and the
in-flight hint names whichever device spoke last. The pad is the
platform's, six axes and two buttons polled once a frame in `poll`
into `Input`, and ImGui never sees it — its own copy would be cleared
by `NoKeyboard` in flight and would open the device a second time.
The subsystem is optional: a machine that cannot enumerate pads runs
and says so in the log. `pad_check` pushes raw axes through the same
dead zone a device does, because a stick at rest never reads exactly
zero.

The camera stays the turntable and grows two mutators. `turn` is
orbit's rotation applied about the eye, re-deriving the focus, so
ending a flight leaves an orbit whose pivot is what was straight
ahead. `move` shifts eye and focus together, at a rate scaled by the
orbit distance as `pan` is. Neither clamps the pitch, as orbit does
not. Under orthographic projection turning works and moving ahead is
invisible, and that is left as it is.

`fly_check` proves it headless — relative mode needs a window and is
a no-op without one; every other fact flips the same either way. W
moves eye and focus by one vector along forward; E rises along world Z
only; a look holds the eye and keeps forward's height; a drag in
flight moves nothing; Shift and the wheel scale the step; Escape
releases, and a key held into the release moves nothing after, nor
does one whose release was lost before the next flight; W never
reaches the sim while Space does, and both are the sim's again
afterwards; the panel world under the pointer is the one captured and
the window's does not turn; a press latched before the flight does not
orbit under it; over a plain panel Tab falls back to the window's
world. Two things it cannot reach: a text field keeping Tab, which is
the OS-event typing gate that `PostEvent` bypasses by design, and the
release on focus loss, which is an SDL event.

## Picking

A click asks the world what is under the pointer. `Pick` is the
answer, a POD: the world point, the distance along the ray, and which
cloud and which of its points — or the GROUND, no cloud and no index,
when the ray met the grid's plane first. `world.OnPick(fn)` is how a
program hears it, `world.Follow(pick)` makes the camera's focus track
that point every frame until a pan, a flight, a `Camera` call or the
point's disappearance ends it, and a double-click makes the point the
pivot with distance and pose untouched.

**The world picks, not the item** — culling's argument again. Each
item gets a `pick` hook beside `bounds`, answers with its nearest hit
or with "I do not know", and the world keeps the nearest over all of
them. A cloud answers from its host copy: the nearest point whose
sphere the ray enters, at the radius it is drawn with. The grid
answers as the ground. `locate` is the hook's twin for following:
where element N is now.

**A device-resident cloud gets a host copy, one frame late, while it
is asked.** bgk's gas and flow's tracers never touch the host — a
Sync over a tensor resolves through `source_of`, so the engine pulls
the sim's device buffer every frame — and the first version answered
nothing for them, which is what "clicking a particle does nothing"
was. The copy is made only while something asks what is where (a pick
listener, a hover, a following) and costs the host nothing otherwise.
It cannot be `vkCmdCopyBuffer`: gpud creates its buffers with storage
and device-address usage only, so a transfer copy reads zeros. It
cannot be gpud's own `read()`: that is not in its thread-safe
carve-out, and bgk's sim is dispatching on another thread. What the
renderer may do with a gpud buffer is read it from a shader, so the
readback is one compute dispatch (`shaders/readback.slang`) into an
NVRHI scratch buffer, a copy from THAT into a CPU-readable one, and a
map at the next prepare — after the frame that carried the copy has
been waited for, so the map never blocks. The pipeline is the world's,
made on first use.

**Hovering brightens the point under the pointer**, and following
brightens the followed one: the hovered index goes to the cloud's
shaders in a push constant, and that instance is lerped 55% toward
white. The hover is a pick every frame the pointer is over the world
with no button down; on bgk's hundred thousand points that is well
under a millisecond.

**The ray is the picture's.** The world keeps the view its last draw
looked through, and a click looks back through it — a click lands on
what the reader saw, not on what the next frame will draw. Two depths
are unprojected through `clip_to_world`, the near plane and one behind
it, so one arithmetic serves both projections. A click is a release
whose drag distance ImGui reports as zero; the engine remembers no
press position of its own. **The picture's rect is in SCREEN
coordinates**, because that is where ImGui puts the pointer once
viewports are on, and a window is rarely at the screen's origin: the
first version measured from the window's corner and every click in a
windowed program landed a window's position away. The headless suite
sat at (0, 0) and could not see it; `pick_screen_check` now puts the
window at (100, 50) on the viewport fake's pretend screen.

**A click has six pixels of slop**, ImGui's own drag threshold. A
cloud's sphere is taken no smaller than that at the point's depth, so
bgk's gas, drawn at a radius under half a pixel, can be clicked at
all. What a click cannot resolve, a pick does not demand.

One thing the device section of `pick_check` taught about writing a
check against a Sync over a device buffer: create the cloud BEFORE the
first Publish. The cloud is what installs the stamper on the Sync's
gate, and an unstamped Publish makes the frame wait for nothing, so
the fill had not run when the frame read the buffer — a picture with
the point at the origin, and a readback faithfully copying zeros. The
check now asserts the point is DRAWN where it belongs before it asks
the pick, so data that never arrived can never again read as a
readback that failed.

vklib had the whole vocabulary of this, a cursor ray and six ray tests,
and in its entire history nothing called any of it. So the hook landed
with its first two callers in the same change: bgk tags the particle
you click, follows it and plots its momentum beside the ensemble's
spreads — one molecule's relaxation inside the gas's — and water sends
a jet up under the point you click, water or floor. Both keep their
data on the device; both work through the readback above.

`pick_check` posts the camera at +X looking at the origin, so screen
right is world +Y and up is +Z, and asks: the middle finds the point at
the focus, and the NEAR cloud, though a far one directly behind it was
registered first; a point one unit right and one up land where +Y and
+Z do; a click 0.45 units off a point of radius 0.3 misses and one 0.2
off hits; a click fires the callback and a drag does not; a
double-click moves the focus onto the point and nothing else; tilted,
the ground answers below the focus at z = 0 and the particle at the
focus still beats it; following moves the focus with an `Update`, a pan
ends it, and so does the point going away; a panel world picks through
its own rect and the window's does not hear it; a speck three pixels
off is picked and one twelve pixels off is not; hovering names the
point and the picture brightens it by half while its neighbour does
not change; a cloud filled by a gpud kernel is drawn where it belongs,
then picked and followed through the readback. Ten drills, each red:
first hit kept instead of nearest, the radius doubled, the drag gate
removed, following that never moves the focus, screen y flipped, the
slop removed, the rect measured from the window's corner, the
highlight never drawn, the hover never recorded, and the host copy
never wanted.

One more thing the windowed path taught, not reachable headless: a
popup viewport closing is a focus loss too, so only the MAIN window's
counts as leaving a flight — the `fly` menu entry began and ended one
in the same frame until then.

What it cannot do yet: pick anything that is not a cloud or the
ground. A surface, when there is one, wants either a ray-triangle
test on host geometry or the depth readback the lighting work also
wants.

## What W2 added

**Orthographic projection.** `CameraDesc::projection`, and nothing
else changes: the pose, the turntable and the reverse-Z convention are
shared, so switching holds the subject still and removes only the
convergence. The orthographic box is built from the height the frustum
subtends AT THE FOCUS, which is what makes the switch continuous
rather than a jump. It is linear in view depth, so unlike the
perspective form it needs a real far plane — twenty orbits, spending
precision on the scene rather than on empty distance.

**Colormaps over a second channel.** A cloud may carry per-point
VALUES beside its positions — `Magnitude` (a turbo ramp over
|v|/scale), `Direction` (the unit vector as rgb), `Components` — and
that channel answers the same three doors the positions do: `Update`,
a Sync, or a device buffer re-resolved every frame. Both channels are
one `Channel` struct, because they are the same problem twice.

A cloud with no values of its own binds its POSITIONS in the value
slot. One binding layout then serves every cloud there is, at the
price of nothing, and the shader never reads the slot unless a map is
on. Fewer values than points degrades to the flat colour rather than
reading past the end of the buffer.

**A light set.** Up to four directional lights plus an ambient, fixed
in the view block so every shader's lighting is one loop with no
branch on which lights exist. Directions are rotated into VIEW space
on the CPU once a frame, because that is the space an impostor knows
its own normal in — the alternative is a normal matrix in every shader
that shades anything. An unlit world gets a single light at the
camera, which is exactly what W1 had, so nothing changed for a caller
who never asked.

## What W3 added

**Points drawn as geometry.** A shape is a field on the cloud, not a
kind of its own: `Billboard` is the impostor disc, `Sphere` and `Cube`
are real meshes drawn one instance a point in one indexed call.
Everything else — the three doors, the colormaps, the lights, the
sort — is the same code, which is what the shape being a field rather
than a kind buys.

The built-in shapes are generated, not shipped, and the sphere comes
in TIERS: 972 triangles below four thousand instances and 108 above,
chosen by the item from the count. That is a measured decision, not a
taste: across the boundary a sphere costs 0.33 us at the fine tier and
0.20 at the coarse, and `make bench` is where the number lives.
vklib learned the same thing the other way, at fifty thousand
instances of a three-and-a-half-thousand-triangle sphere.

Instancing keeps a mesh in the one binding shape everything else uses:
the vertices are a storage buffer read by index, because there is no
vertex input layout anywhere in this engine and a mesh is not where
one starts. A unit shape scaled and moved is a uniform scale and a
translation, so a normal survives it untouched and there is no normal
matrix — the view rotation is orthonormal, so lighting in view space
survives too.

**Multisampling**, four samples where the device allows both
attachments to carry them. A world draws into a multisampled pair of
its own and RESOLVES into whatever it was asked to fill, so a panel
world and a window world get it the same way and neither knows. The
pipeline cache keys on the sample count, because a pipeline is
compiled against one and cannot be used with another. It is the one
thing shading cannot fix: a silhouette is a hard edge, and the check
counts the partly-covered pixels along one — 84 with it, exactly 0
without.

**A ground pass**, between the opaque geometry and the translucent.
The grid is the floor of the scene, so solid things occlude it and
translucent things wash over it; in the overlay it painted over a
cloud that was in front of it.

`examples/shapes` is where the three are side by side — a lattice of
cubes, a shell of spheres coloured by direction, the same shell as
billboards — with a slider that walks the sphere count across the tier
boundary. `examples/orbit` is the same world in one panel-free window.

## What the wire and the stroke added

**Segments over a point set.** A `Wire` is a cloud's positions channel
with an edge list — two indices each, fixed at creation — drawn as thin
quads of a pixel width (`shaders/wire.slang`): the perpendicular is
taken in pixel space and put back in clip space scaled by w, so the
width holds at any depth and the quad keeps the segment's depth. A mask,
one float an edge, hides the ones at or below zero, and it is a channel
like any other: uploaded, pulled from a Sync, or a producer's device
buffer through the door. The channel itself moved out of the cloud into
`Channel.h`, one `width` field wider, which is the whole of what the
second item cost the first — and moving it fixed a cloud fed by a Sync
never learning its bounds, since the prepare now says whether the host
copy changed. A wire picks by the nearest edge the ray passes within
the click's slop of, masked-out edges excluded; a followed edge is its
midpoint.

**A stroke is a drag with the cut tool on.** The tool is a field on the
world, offered in the on-picture menu only where something listens
(`OnStroke`). While it is on, a left drag hands each frame's motion on
as a `Stroke`: two picture-space points, the view it was drawn through,
and what the drag began on, picked once at the press. `Crosses(p, q)`
answers whether a world segment's projection crossed it — the whole of
what a cut needs — and `Carry(at, to)` moves a world point along it in
the picture's plane at that point's depth, which is the whole of what
dragging something is; `On(item)` says which of the two a stroke means.
Nothing the world has to know about the data. The right button orbits
meanwhile. A stroke through
a picture nobody has seen is nothing: headless, that means a shot must
have been drawn, which is what `stroke_check` learned first.

`examples/cloth` is the case: a spring lattice hung from its top edge,
position-based dynamics with every spring family a stencil over the
grid, two wires for the structural springs with the families' own masks
as their masks, cut with the pointer and tearing where a spring is
stretched past its limit. The sim runs on the host, which is what makes
the cut exact and immediate: the stroke callback tests every spring
against the frame's own copy of the positions and queues the crossings
for the next tick.

## Stated limitations

- **Transparency is sorted per ITEM, not per particle.** Two
  translucent clouds order correctly against each other; two points
  within one cloud do not. Additive mode is the order-independent
  escape.
- **A cloud's depth key is its centroid.** A cloud is one draw, so it
  takes one place in the order. A cloud that wraps around another is
  not ordered against it in any meaningful way.
- **A billboard writes the quad's depth, not the sphere's.** The
  fragment shader shades an impostor with an analytic normal but does
  not export depth, which keeps early-Z alive. At a radius small
  against the scene the difference does not show; where a sphere meets
  a surface it would.
- **A remainder that places a grid line must be POSITIVE.** `fmod`
  carries the sign of its dividend, so at negative world coordinates
  it returns a negative remainder, the saturate clamps it to zero and
  the line disappears: one quadrant of the plane with no grid at all
  and two with one family of lines each. `frac(x/n)*n` is the same
  quantity without the hole. Every colour probe in the suite passed
  the whole time it was broken — there were grey pixels, the content
  box was full, the picture changed when the camera moved — which is
  what `grid_check` and the structure vocabulary exist for.
- **The grid's EXTENT follows the camera; its CELL does not.** A grid
  is a ruler, so the finest cell is a world length and the
  level-of-detail picks which decade of it to draw. The quad and its
  fade are the opposite: twelve orbit distances wide, fading over the
  last two thirds. Both fixed, as they were at first, is wrong in both
  directions — zoomed out past the extent the whole grid sits beyond
  its own fade and disappears, and zoomed in the fade never engages,
  so the far field grazes the plane and turns to moire.
- **A colormap's scale is a number the caller supplies**, not one
  derived from the data: deriving it would need a reduction over the
  buffer every frame, and a scale that moves under you is worse than
  one you set.
- **Culling and the tier are per ITEM, and a cloud is one item.** A
  crowd half out of frame is drawn whole, and the tier comes from the
  CENTROID's distance, so a cloud that stretches far in depth gets one
  answer for all of it. Splitting an item into parts the view could
  reject separately is the next thing here, and it wants a reason
  first: the scenes that hurt are many crowds, which this already
  handles, not one crowd that is very long.
- **A cloud whose points only ever lived on the device reports no
  bounds**, so it is never culled and never moves the near plane. The
  host has nothing to walk and a box it invented could delete data
  nobody can look at. Declaring them from the producer's side is the
  escape hatch that would close this, and no workload has asked yet.
- **No shadows and no occlusion term.** A directional shadow map
  shipped in W5 and was removed a day later; the section below says
  why, and what to try instead. No user meshes yet either: the
  built-ins cover particles, and geometry a caller computed is the
  next thing the registry grows. The colour target is 8-bit UNORM, so
  tone mapping would want a format change first.

## Named follow-ups

- ~~The `render/` hoist~~ — **done in W5**, on the trigger it was
  deferred with: the shadow map was a target with no colour
  attachment, a second consumer of the same resize-and-recreate
  discipline. `Gpu`, `RenderTarget` and `Shader` moved to
  `src/render/` and nothing else changed — the pixel checks were the
  proof. The shadow map has since gone; the hoist stays, because it
  cut the last `world/ -> scene/` edge and the two strata now share
  the bottom layer and nothing else, which is a stronger rule than
  the one-way arrow it replaced.
- ~~Per-pass GPU timestamps~~ — **closed in W4**. A world stamps a
  section per pass instead of drawing inside one called "scene"; the
  2D path keeps that name. Sections still do not nest, so a world's
  passes ARE the attribution rather than a level below one.
- ~~A bounds-driven near plane~~ — **closed in W4**. It only ever
  moves the plane CLOSER than the orbit-scale default, never further:
  items may report no bounds, and geometry nobody accounted for must
  not be sliced away by a plane derived from geometry somebody did.
- ~~A headless check for the controller~~ — **closed** by
  `input_check` and `tests/harness/Input.h`.

## What the checks prove

`math_check` (pure, no device): the projection's fixed points
and monotonicity, the pose against its own spherical definition, the
view matrix agreeing with the pose, the turntable's invariants, the
matrix inverse, and both key orderings.

`world_check` (device): that the camera the caller asked for is the
camera that drew — a point at the focus lands in the middle of the
panel, which is the assertion a transposed upload fails; that the near
sphere occludes the far one; that the transparent pass sorts back to
front even when the near cloud was registered first; that all three
doors reach the item; and that a world with no content of its own
still draws its grid and axes.

Three drills, each watched red: the transparent comparator flipped
(the composite inverts to red-over-blue), the depth test set to `Less`
(the far sphere wins), the depth cleared to 1 (nothing draws). Two
more on the lint: a world file including `../ui/`, a scene file
including `../world/`.

Every probe is restricted to its own panel's rectangle, pinned by the
check. The first version searched the whole shot for a colour and
found the OTHER panel's sphere — it passed, and it passed for the
wrong reason, which the sort drill is what exposed.

## W4 — what the view decides not to draw

Three decisions, all reading one hook the item contract had declared
and nobody filled: `WorldItemOps::bounds`.

**Culling belongs to the world, not the item.** The world asks each
item for its box, tests it against the frustum, and only then calls
`submit`. Putting it in the items would mean every future item has to
remember, and the one that forgot would be the one drawn wrong — the
same argument that put the ordering in the world in W1.

**`bounds` returning FALSE means "I do not know", not "empty".** A
cloud whose points live on the device cannot walk them, and a box
invented for it would delete geometry nobody can see. An item that
reports nothing is drawn, and contributes nothing to the near plane.

**The tier follows screen size, with a triangle budget beside it.**
Both halves are needed and each guards a failure the other cannot:
screen size alone hands a fine mesh to a crowd that cannot afford one,
and the count alone gives the cheap mesh to a dozen spheres filling
the frame — which is exactly what W3 shipped and what this replaces.
The budget is measured (about half a millisecond per million
triangles here, so twenty million is a frame's worth); the six-pixel
threshold is read off the mesh, whose cheap sphere carries twelve
segments around its silhouette.

Making a mesh needs the frame's command list and picking one needs the
camera, so `prepare` builds BOTH tiers of a shape and `submit` chooses
between them. That is why a world with one sphere shape holds three
meshes and not two.

**The near plane only ever moves closer.** Under the old rule an orbit
of a thousand units put it a full unit from the eye, and a subject
panned close to the camera was simply not in the picture. It now
follows the nearest bounded geometry — but never past the old default,
because an item is free to report no bounds and its geometry must not
be sliced away by a plane derived from somebody else's.

### The numbers

| what | measured |
| --- | --- |
| the cull, 64 crowds spread over 960 world units, camera on one corner, 60 off screen | 22.475 ms off, 1.173 ms on — **19.2x** |
| the fine tier | 0.488 ms per million triangles, so a 16 ms frame is about 33 million |
| 20000 spheres at 12.1 px of radius | 19.44 M triangles, 9.481 ms |
| the same 20000 at 5.4 px | 2.16 M triangles, 1.061 ms |

960x720, headless, 4x multisampled, best of 25 frames, on this
machine's integrated GPU. What moves them: the device, the window size
(fill is most of the first table), the sample count, and for the tier
rows the camera distance, which is what chooses the mesh.

### What the checks prove

`cull_check` asks the only question that can fail usefully: **culling
must not change the picture.** Each pose in the sweep is rendered
twice, with the test off and on, and the two must be pixel-identical.

The first version asserted instead that a culled item draws nothing —
which is a tautology, since culling is what removed the pixels, and it
passed a drill that threw away eleven thousand pixels of a sphere. The
pair-of-renders form catches that drill at once. The sweep pans the
subject out through a CORNER, which is where a plane-by-plane test is
weakest and is an exit a turntable cannot produce at all.

Around it: the camera standing inside the subject's own box (the case
a corner test gets wrong, and the frame the user is closest to); a
cloud with no host data left alone; and a sphere half a unit from an
eye orbiting a thousand, which the old near plane clipped away
entirely and which must still sort in front of the backdrop.

`mesh_check` pins the tier claim as the count-based rule cannot: the
SAME eight thousand spheres, far away and flown in, get 108 and 972
triangles. It reads what the item ASKED FOR — `WorldItem::triangles`,
the drill-down from `Stats::triangles` — because both tiers are now
resident and residency stopped being an answer.

Five drills, each watched red then restored: the frustum testing the
nearest corner instead of the furthest; the near plane ignoring the
bounds; the tier reading the count again; bounds guessed for device
data; and a box too tight by the radius, which is the one the first
version of the check could not see.

## W5 — the shadow pass, and why it was removed

A directional shadow map shipped, worked, and came out again a day
later. The removal is the useful record, so it is kept here rather
than dropped with the code.

**What it did.** One light could cast (`LightDesc{.shadow = true}`,
a second refused by name); an orthographic map fitted to the scene
bounds W4 made available; four-tap PCF through a comparison sampler;
the grid, the instanced meshes and the billboard impostors all
receiving. About 470 lines, 1.1-3.5 ms a frame in the examples.

**Why it went.** Two reasons, and only the first is about shadows.

A directional shadow map answers "what is the silhouette of this
thing, seen from the light". For a lattice that is informative — the
cube grid's shadow reads AS a lattice. For a particle cloud it is a
dappled blob, and the question a cluster study actually asks is about
LOCAL occlusion: which particles are buried, where the cavities are.
That is a different measurement, not a better-tuned version of this
one.

The second reason is worse and is the one to remember. **To have
somewhere to fall, the shadow made the ground a lit surface.** The
grid was lines over a transparent quad; a shadow on it was invisible;
so the quad started painting a tone wherever the casting light reached
it. That is a lighting feature reaching out and changing what the
SCENE is — the floor stopped being a ruler and became an object. No
amount of tuning fixes that, because it is a scope error rather than a
bug.

**What it cost to remove.** Almost nothing, and the reason is worth
recording. The shadow-specific pieces were self-contained — 154 lines
of math with exactly one caller, two whole shader files, one field on
the ops table — and the only place shadows reached into general code
was two binding slots present in every colour pipeline and every
item's binding set. A screen-space method needs none of that: it reads
a depth texture after the fact and never touches an item shader.

**What to try instead**, in the order I would try it:

- **Eye-dome lighting**, the filter point-cloud viewers use: darken a
  pixel by how much nearer its neighbours are. Depth buffer only, one
  full-screen pass, nothing to tune, stable under rotation.
- **SSAO/GTAO** if surfaces rather than points become the subject.
- **An occlusion term from a density field** — bin the particles,
  shade by local density. The only one of the three that is
  view-independent and means something quantitatively, and it reuses
  the compute path rather than the raster one.

Each needs one thing that does not exist yet and that shadows did not
build: **a readable depth texture.** The world resolves colour only —
the depth attachment is multisampled and never resolved, so nothing
can sample it.

**What survived**, and would have whatever came next: the `render/`
hoist, per-pass GPU timing sections, W4's scene bounds, and the
`probe::`-switch pattern the checks are built on — render the same
frame with the feature off and on, and subtract.
