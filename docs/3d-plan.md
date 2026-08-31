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
