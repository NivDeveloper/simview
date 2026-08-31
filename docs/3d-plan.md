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
src/world/Math.h    Vec3, Quat, Mat4, Camera3, the projection, the keys
src/world/Passes.h  the pass table
src/world/Items.h   WorldItemOps, WorldItem, DrawCmd, WorldView
src/world/World.h   WorldState + the draw
src/world/World.cpp constants, submit, sort, the pass loop
src/world/Pipelines.cpp  the world's pipeline cache
src/world/Cloud.cpp      the cloud item, three ops rows, three doors
src/world/GridAxes.cpp   the grid and the axes, as ordinary items
src/ui/World.cpp    world_create and the camera controller
shaders/{world_view,cloud,grid3,axes3}.slang
```

Math is a 250-line internal header rather than a dependency: ten
functions do not justify pinning a library, and being header-only is
what lets `world_math_check` prove the projection and the camera with
no device in the room.

## The camera

A turntable. `Camera3` carries a focus, a distance, a world-from-camera
quaternion and a field of view; the position is always re-derived from
`focus + q·(0,0,1)·distance`, because two copies of one fact drift.
Yaw is about the WORLD up axis (+Z — the world is Z-up) so the horizon
stays level however far the view has tilted; pitch is about the
camera's own right. Pan moves the focus in the screen plane at a rate
proportional to distance; dolly is multiplicative and clamped.

The controller (`src/ui/World.cpp`) is the only mouse reader in the
engine, and it reads ImGui's state, never SDL. One gesture function
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
- **The grid's EXTENT follows the camera; its CELL does not.** A grid
  is a ruler, so the finest cell is a world length and the
  level-of-detail picks which decade of it to draw. The quad and its
  fade are the opposite: twelve orbit distances wide, fading over the
  last two thirds. Both fixed, as they were at first, is wrong in both
  directions — zoomed out past the extent the whole grid sits beyond
  its own fade and disappears, and zoomed in the fade never engages,
  so the far field grazes the plane and turns to moire.
- **The overlay pass draws after transparency**, so the grid paints
  over a translucent cloud rather than under it. It is what
  "depth-aware overlay" means with a pass that does not write depth,
  and it reads acceptably; a grid that should sit UNDER transparency
  wants its own row between opaque and transparent, which is a one
  line change to the table and a W2 decision.
- **A colormap's scale is a number the caller supplies**, not one
  derived from the data: deriving it would need a reduction over the
  buffer every frame, and a scale that moves under you is worse than
  one you set.
- **No meshes and no shadows** (W3, W5), and no multisampling
  anywhere — sphere silhouettes and grid lines alias, which is its own
  decision because it touches the target, the pipelines and the pass
  table at once. The colour target is 8-bit UNORM, so tone mapping
  would want a format change first.

## Named follow-ups

- **The `render/` hoist.** `Gpu`, `RenderTarget` and `Stats` are
  shared by both strata today through `scene/Target.h`. When a second
  consumer appears — the W5 shadow target is the likely one — hoist
  them into their own layer, behaviour-preserving, under the pixel
  checks. Doing it now would churn three 2D kinds, the lint DAG and
  CMake for no behaviour.
- **Per-pass GPU timestamps.** The passes carry debug markers today;
  timing sections do not nest and a world already draws inside the
  frame's "views" section, so attribution per pass wants its own
  timestamp pair.
- **A bounds-driven near plane.** `WorldItemOps::bounds` is declared
  and null: the near plane follows the orbit distance for now, and a
  world of very different scale would rather it followed the geometry.
- ~~A headless check for the controller~~ — **closed** by
  `input_check` and `tests/harness/Input.h`.

## What the checks prove

`world_math_check` (pure, no device): the projection's fixed points
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
