# simview — design

A lightweight, cross-platform library for watching numerical
simulations run: fields, particles, and live plots in an SDL window,
with a sim/render sync layer that keeps the two honest.

What simview is NOT — the scope fence, stated first because its
predecessor (vklib) grew past it: not a renderer abstraction (SDL_GPU
is), not a GUI toolkit (Dear ImGui is), not a compute layer (the sim
owns its compute — tensor, gpud, plain loops), not a shader DSL, and
no 3-D scene system in v1 (camera/mesh/material stay out; a 3-D slice
view is a future decision, not a founding one).

## The two-strata public surface

The public surface (`include/simview/`) has two strata; the split is
between what the LINKER sees and what the READER sees.

**The impl** is the set of symbols exported from libsimview — the
compiled boundary, the ABI. Its signatures are deliberately boring:
opaque handles (`struct App { void *p; }`-shaped), POD structs, enums,
`const char *`, primitives, and function-pointer + `void *` pairs.
Never a template (they cannot be compiled into a library ahead of
time); never a std type in a signature (a compiler-coupling hazard
across a shared-library boundary). The impl is what must stay stable,
what a future .so exports, what tests mock, and what an ABI review
reads.

**The sugar** is inline code in the same public headers — methods on
the handle types, builders, span/lambda overloads. It compiles into
the CONSUMER's binary, never into libsimview, so it may use std freely
and be reshaped at any time. Its one law: it lowers ONLY onto impl
calls; it holds no logic of its own.

```cpp
// the impl: exported from libsimview
namespace sv::impl {
Field field_create(App *, const FieldDesc &);
bool  field_update(Field, const void *data, DType, size_t n);
}

// the sugar: inline, compiled into the consumer
inline bool sv::Field::Update(std::span<const float> v) {
    return impl::field_update(f_, v.data(), DType::f32, v.size());
}
```

The reader convention, uniform everywhere: **the impl is `sv::impl`,
snake_case free functions over handles; the sugar is `sv`, PascalCase
classes and methods owning the clean names** (`sv::App`, `sv::Field`
— no `Handle` suffix anywhere). One glance at any spelling says which
side it is on, and a consumer never types an underscore or the word
`impl`.

Why both strata exist: the impl buys ABI stability, mockability, and a
cheap future C-ABI; the sugar buys vklib-grade ergonomics (its ising
example was 205 lines with ~20 of ceremony — the bar). vklib had only
the sugar with no deliberate ABI stratum beneath, which is how its
public headers accumulated five boundary mechanisms and its internals;
gpud is nearly all impl. simview takes both on purpose.

On C interop: the impl is C-SHAPED (POD/handle/fn-ptr arguments
only), not C-LINKAGE — its symbols are C++-mangled and its functions
take references. That is deliberate: C has no namespaces, so a C API
was always going to be a separate flat `extern "C"` file of `sv_*`
wrappers, and the POD-only law is what makes generating it
mechanical. Nesting the impl in `sv::impl` changes nothing about
that path.

## Layers

**The layer DAG, the patterns behind it and where it is going live in
`architecture.md`.** This file records decisions as they were made;
that one records the shape they add up to. When the two disagree, that
one is the architecture and this one is the history.

The sync layer inherits vklib's `SyncBuffer`/`Executor` design — its
best-factored piece (138 public lines, 269 impl lines, 3 lines of
consumer cost): Play/Pause/Step safe from any thread, `iterDelayNs`
pacing, three slots with POINTER swaps, sim depth capped at one in
flight so render interleaves.

**`sv::Sync<T>` is the handoff as a type.** Three slots of `T` with
roles `next` / `current` / `shown`, the roles as INDICES under one
mutex, no `T` ever moved between slots. The sim writes `Next()`, reads
`Current()`, calls `Publish()`; the frame flips `shown := current`
once; every consumer reads `Shown()`. `next` is never `current` or
`shown`, so the sim writes what nobody reads and the frame reads what
nobody writes — the property a bare pointer handed across threads
cannot have, and the race `examples/ising` shipped with before this
type existed: its Executor re-parked a tensor on its thread while the
frame read the same slot. A sim that produces a fresh value per tick
(every tensor eval) pays no copy; an in-place sim copies `Current()`
into `Next()` itself, the price it chose (`ising-cpu` pays one
memcpy a sweep). The `T` is anything: a device-resident tensor, a
`std::vector<float>`, a gpud buffer.

Three rules make it hold. **The flip is the FIRST thing a frame does**
— before the frame callbacks, so `OnFrame`, the panels and the scene
all read one generation; one site per entry path (run, step, a plain
shot), never inside `frame_build` or `frame_render`, where `app_run`
would flip twice and split a frame across generations. A composited
headless shot reuses the draw data the last `Step` built and shows
what that Step flipped to. **A Sync is tracked when a builder is given
it** — the App owns one list of gates, so a Sync drawn by the main
scene and a view flips once; a Sync no builder was given is never
flipped and `Shown()` stays at generation 0 (`app.Track` is the
roadmap's answer for a Sync only a plot reads). **The gate is
refcounted**: the Sync holds one reference, the registry another, so a
Sync destroyed mid-run leaves a dead gate the flip skips, never a
dangling handle — the user-facing rule stays "the producer outlives
the last frame that draws it", but breaking it is now a blank item.

Why the frame never waits ON THE HOST: Publish STAMPS its slot with
the compute device's submitted() ticket (taken on the producer's
thread — the stamper installs in scene_track), and the frame waits
GPU-side on gpud's timeline semaphore for the max shown stamp, after
a non-blocking submit() pump. The reverse edge is frames-in-flight
= 1: an event query set after the frame's execute and waited BEFORE
the next frame's flips, so a slot leaving `shown` has no frame still
reading it — which also answers the old in-place-writer question: a
kernel writing `Next()`'s buffer races nothing, because the only
frame that ever read that slot completed before the flip released
it. decouple_check runs the whole seam on a device.

Two spellings, kept on purpose. The BARE pull — `app.Field(tensor,
…)` with the sim stepping inside `OnFrame` — is the render-thread
shape: nothing re-parks the tensor while a frame reads it, and it
costs one lattice instead of three (`xy-gpu`). `Sync` is the
off-thread shape. The `Channel` that preceded both allocated slabs of
its own, unrelated to the sim's data — the user copied in, the item
copied out — and nothing used it.

**The Executor keeps the clock.** vklib's body received a command
recorder and nothing else — no iteration index, no time, no dt —
which is why it could not offer a step counter, a time readout or a
"run N": it did not know what one iteration meant. The body here may
receive a `Tick` (`n`, `time`, `dt`) the Executor advances and the
body never fabricates. From that one fact the transport is derivable:
`Advance(N)` plays until the count reaches a target and the worker
pauses ITSELF (not when the caller notices — with an uncapped body
that is 3 ticks against 3.8 million, measured); `Restart` is a STATE
the worker observes between ticks, so the callback runs on the worker
thread and can never overlap one — the race vklib's `FireOnRestart`
had; `Now()` and `Rate()` are read from the same counter. The sim
verb is `Advance`, because `App::Step` already means one FRAME.

**A restart has two halves.** The callback runs where the sim's state
lives (the worker), so it may touch sim state freely and must NOT
touch main-thread data. `Restarted()` is the main thread's half — a
consumable flag the frame polls to clear a trace or reset a plot.

**`app.Controls(sim)`** is a panel led by a transport widget that
reads the Executor: play/pause, advance one, advance N, restart, the
clock, the achieved rate, and the speed as presets PLUS a delay
slider. There is no shadow bool; the panel, the keys and the code
cannot disagree. It returns the panel, so sliders chain on.

## Dependencies

| dep | policy |
| --- | --- |
| SDL3 | PRIVATE (engine-only); no public header names it at all |
| Dear ImGui + ImPlot | FetchContent, pinned by hash exactly as gpud is — nothing lands in `third_party/`, which stays empty and says why; built as `simview_imgui`/`simview_implot` with warnings silenced at the target. PRIVATE in every sense that matters: no public header names them, and **their include directories are PRIVATE too**, so `#include <imgui.h>` does not compile in a consumer. The archives still link PUBLIC — a static consumer needs their symbols — which is why the include side has to be denied separately from the link side |
| tensor | absent, forever — its data arrives, its types never do |
| gpud | the substrate: the ENGINE builds on it (gpud::sdl owns device bring-up; linked PRIVATE, gpud::gpud PUBLIC for the door's include dirs, FetchContent-pinned by hash). The opt-in door `gpud.h` speaks its vocabulary; the core never names it |
| shader toolchain | absent for consumers: internal shaders ship as committed bytecode (SPIR-V/MSL/DXIL); `shaders/regen.sh` is dev-only |
| everything else | std, C++20, the SYSTEM compiler (this is not tensor's g++-16/reflection world) |

## Integration with sims (tensor/gpud as the worked case)

The composition principle: gpud is the GPU interchange the sibling
projects share — one device, opened by simview THROUGH gpud, computed
on by producers (tensor) that export gpud vocabulary, and NOBODY
imports tensor. The interchange type is gpud's BufferSource: a
producer implements `source_of(const P &)` (found by ADL), a scene
item binds it once, and the engine PULLS current() at every draw — the
moving residency of a value-semantics producer costs no per-frame
glue and no API that scales with vis types.

- **Mode A (default): host memory.** `field.update(state.data())` —
  works with every backend and every language; two devices may exist
  and it does not matter (a 256² float field is ~15 MB/s at 60 fps).
- **Mode B (opt-in): zero-copy, one shared device.** simview owns
  the gpud device (`sv::Device(app)` hands it to the compute
  runtime); `app.Field(producer, desc)` and `app.Particles(producer)`
  register the pull source once and no per-frame call exists. Opting
  into `gpud.h` IS opting into gpud, explicitly. Producers die before
  the App — the one lifetime rule of the glue (the item's source
  points into the producer object). **The producer must be driven on
  the render thread**: `current()` is a borrowed pointer with no
  roles, and a producer re-parking on another thread races every
  reader. A producer on its own thread wraps its state in `Sync<T>`
  — `app.Field(sync, desc)` resolves the SHOWN slot's source at every
  bind, and the door's `source_of(const Sync<P>&)` forwards to `P`'s
  own. Mode A has the same twin: `Sync<std::vector<float>>` is fed to
  the item through `host_of`, and the frame stages a NEW generation
  the way an `Update` does, and only a new one.

  **A pulled item takes its size from the buffer**: a cloud is
  interleaved xy pairs, so `bytes() / 8` IS the point count. Nothing
  is passed alongside that could disagree with the memory — the same
  reason a pushed item refuses `Update` rather than keeping a second
  copy of the data.

The core impl accepts behaviors (callables as fn-ptr+void*), handles
(pointers), and integers (generation counters) — foreign vocabulary is
the interop doors' job, and there are two of them: gpud's and the UI's.

## The UI layer

A window has a SCENE (drawn to the swapchain — today a field, later a
shader, particles, 3-D) and a UI layer above it. ImGui composites in a
second render pass with `LOADOP_LOAD`, so the scene's own pass is
untouched and there is no second renderer to drift. The dockspace uses
`PassthruCentralNode`: its centre is a hole the scene shows through.
Where a panel lives — floating over the scene, docked to an edge,
tabbed with another, or torn out into its own OS window — is the
user's drag, saved in the layout file, never a build-time mode.

Three laws, each learned from the predecessor:

- **We never advertise a capability the backends have not claimed.**
  Viewports turn on only when both backend flags are set by the
  backends themselves. Setting them by hand is what made vklib's
  missing platform backend crash instead of refuse.
- **No panel we draw sets `NoDocking`.** vklib's did, fullscreen, over
  its own dockspace — which is how a working dockspace became
  decorative.
- **ImGui capture gates the OS's events, never the sim's own.** A
  panel wanting the keyboard swallows key events before hotkeys see
  them; `PostEvent` bypasses capture entirely, because the automation
  seam addresses the app and must not depend on invisible UI state.

## The scene

What is drawn to the swapchain is a LIST of items, not one field.
`scene_draw` prepares every item (pulls sources, uploads what the host
changed), begins ONE render pass, and draws each item through
`item_draw`'s switch on kind.

- **The clear belongs to the scene**, never to an item — an item that
  cleared would erase whatever the item before it drew. That single
  move is what makes compositing possible.
- **A scene has a 2-D range**, defaulting to the first field's grid in
  CELLS, so points over a lattice are placed in cell coordinates. One
  aspect-fit is computed from that range and used by every kind, so
  alignment between a field and the points over it is structural
  rather than coincidental. `SceneRange` names it explicitly when
  there is no field to inherit from.
- **A new scene kind is one file plus a shader, and nothing in the
  engine learns it arrived** — measured by adding `Lines` after the
  kinds became data (`docs/architecture.md`). What it still costs is
  the public surface, ~55 lines: `sv::Scene` is the one place a kind
  is spelled for the user. Before the ops table the three sites were one
  `impl::*_create(Scene, …)`, one `case` in `item_draw`, and one
  method on `sv::Scene`. **`sv::Scene` is the one place a kind is
  spelled for the user**: `App` and `View` both hand one out, so
  neither grows a method per kind.

## Views

A view is a scene whose target is a texture instead of the swapchain,
shown by a panel that docks, tabs and tears out like any other. It is
the SECOND arrangement, not a replacement: `app.Field(…)` still draws
straight to the window with panels floating over it (ising, ising-cpu,
gas), and
`app.View({…})` puts one in a panel. `gas` does both at once — real
space in the window, the same particles in phase space in a view —
which is the arrangement that earns the passthru central node: a
window whose scene is empty is a window doing nothing.

Each view's scene carries **its own range**, so the two need not share
coordinates: gas draws the same points at (x, y) in one and at
(y, v_y) in the other. A range may reach below zero, which is what
makes a phase-space axis expressible, and `field_check` pins that —
a scene that fell back to the unit square would put every point off
the target.

- **The texture is sized from what the panel had room for LAST
  frame**, and resized before the next UI frame is built — because a
  draw list records the texture handle, and releasing a texture the
  built frame points at is a use-after-free with a picture on the
  other side of it.
- **A first size is mandatory.** An ImGui window fits itself to its
  content on its first frame, and the content is an image sized from
  the window: without `SetNextWindowSize(…, FirstUseEver)` the two
  settle at the smallest window ImGui will draw. Measured: 32x13.
- **The room is asked for in PIXELS** — the content region is in
  ImGui's points, and on a retina display a texture sized in points
  would be drawn at half resolution.
- **The image is sampled NEAREST**, through the backend's render
  state and a pair of draw callbacks. A view's texture is a lattice,
  not a photograph; the default linear filter smears cell edges the
  moment the panel is not an exact multiple of the grid. The callback
  puts the sampler back, so text stays smooth.
- **A view is two things, in two layers.** The texture is scene's
  (`RenderTarget`: resized to a requested size, drawn into, knows
  nothing of panels); the title and the panel are ui's (`View`). The
  want/resize channel runs one way — ui writes the request, scene
  honours it next frame — which is what lets the scene layer never
  learn what a panel is. Found while dissolving the god header: the
  old `ViewState` was one struct read by four layers.
- **The pipeline cache is keyed on (kind, format)**. Format alone would
  hand one kind another's pipeline: a picture rather than an error.
- **Uniforms are per STAGE.** A block bound in the vertex set is not
  readable from a fragment shader — SDL binds nothing there, and the
  draw silently produces nothing while every counter says it happened.
  Values a fragment stage needs travel through the varying.

## Torn-out panels

A panel dragged off the window becomes an ImGui *secondary viewport*:
its own OS window, its own swapchain, and — this is the part that
matters — **its own command buffer, recorded and submitted inside
`Renderer_RenderWindow`**.

- **The frame's main command buffer is submitted BEFORE the viewports
  render.** A secondary viewport samples the view textures the main
  buffer wrote, so the main buffer has to be on the queue first. That
  order now lives in exactly one function, `frame_render`, which every
  path drives through the Presenter port — breaking it there is the
  only way to break it, and `viewport_check` catches that.
  Upstream's example renders viewports before submitting, and is
  right to for its own case: its secondary windows sample nothing the
  main buffer produced. Ours do, which makes the ordering ours to
  get right.
- **The symptom this produces is invisible except on resize.** On an
  ordinary frame the view's texture still holds the previous frame's
  content, so sampling it early gives a stale image nobody notices.
  On the frame after a resize the texture was just created and holds
  nothing, so the panel shows undefined memory — magenta, on the
  driver where it was found.
- `views_resize` runs before the UI frame is BUILT, for the matching
  reason: a draw list records a texture handle, and releasing a
  texture the built frame points at is a use-after-free.

`tests/checks/viewport_check.cpp` covers the arrangements a panel
passes through; `tests/fakes/Viewports.h` is what makes them reachable
without a display, and `tests/harness/Palette.h` is the assertion.

## Plots

A plot is a panel: `app.Plot({.title = …})` returns a retained handle
and registers a ui callback, so it docks, tabs and tears out like any
other. **One panel, one plot** — ImPlot keys item identity by the
label under the *window's* ID scope, so one plot per window is what
keeps series names from colliding.

- **A series is a POD** (kind, element type, source, style) and **the
  scalar is erased once**: `emit_series<T>` switches on kind, and a
  single line turns `DType` into `T`. The erasure is one struct wide
  enough for every kind — four value slots, an index channel, two
  counts, positional BY KIND — widened once before any kind needed it,
  and one shared `param[4]` carries every per-kind scalar so a Bars
  width or a Stems reference costs no descriptor field. Eleven 2D
  kinds ship: a kind costs one enum value, one case and one builder
  method per data shape; supporting float AND double costs
  nothing per kind.
- **Data is borrowed or pulled**, one impl path. A span is zero-copy
  and must outlive the plot; a callable is asked at draw time, on the
  render thread, exactly once per series per frame — and **not at all
  while the panel is collapsed**. Anything that grows or relocates
  wants the callable: that is the predecessor's stale-pointer failure,
  and its data race (its sampler thread called the user's function
  while the main thread mutated the captures) is why the pull happens
  on the render thread and nowhere else.
- **Four axis modes, because ImPlot has four behaviours.** `Fit::Data`
  fits once then hands panning to the user (the default); `Fit::Start`
  opens at a range you name and still pans; `Fit::Fixed` locks it;
  `Fit::Stream` refits every frame, which disables panning and costs a
  full extra pass over every point. Log and symlog scales are one
  field, not a missing feature.
- **Identity is the name.** A duplicate series name is refused: ImPlot
  would merge them into one legend entry, colour and visibility. A
  duplicate window title is refused: ImGui appends into the existing
  window.
- **A second plot FAMILY costs one bracket arm, not a second draw
  path.** `Plot3D` is a sibling builder over the same impl: ImPlot3D
  is a separate library with its own context and its own `BeginPlot`,
  so `plot_draw` stays ONE function and switches the family in ONE
  place — the bracket. The window, the title namespace, the source
  pull, the dtype erasure and the list addressing are shared. The
  family is on the PLOT, the kind on the SERIES, and a mismatch is
  refused by name before either emitter sees it. Four 3D kinds: Line
  and Scatter lifted to three arrays, Surface (three coordinate grids
  — NOT Heatmap's z-over-bounds shape; the two are deliberately not
  one vocabulary), Mesh (vertices plus an index buffer). `Fit` lifts
  to 3D unchanged; `AxisScale` is a no-op there, and says so.
- **Removal is a later move, not an oversight.** ImPlot's item pool is
  immortal within a context, so re-adding a name resurrects its old
  colour and visibility, and the colormap cursor never rewinds. The
  only honest reset also wipes every legend toggle in that plot.
- Panel widgets bind by reference and their button callbacks run while
  the UI frame is being BUILT — CPU-only, with no command buffer open,
  so a callback that touches the GPU cannot stomp one. (The
  predecessor's did, and it needed a deferral queue to survive.)
- ImPlot has no ini of its own: the panel layout persists, a panned
  axis does not. And the library still owns no clock — a streaming x
  axis is the caller's sample index.

## Verification, and what runs where

Three rungs, deliberately split by where the signal lives:

- **Local, every change**: `make test` + `make lint`. The headless
  checks cover refusals, the loop contract, posted-event delivery, the
  engine's counters, the Sync gate's roles and tearing stress, and SHOT CHECKS
  THAT READ PIXELS — a ramp's equal-value corners must agree and its
  ends must not, so "it drew something" is an assertion, not a file
  size. Image assertions are statistics and geometry (mean, distinct
  colours, content box, tolerant compare), never byte equality: three
  drivers never round alike, and a golden that fails on a driver
  difference teaches nothing.
- **Local, when it matters**: `make flagship` after anything the gpud
  door touches — no runner has tensor's compiler, so that example can
  only be gated here. (`make san`/`make tsan` exist and are correct,
  but the AppleClang sanitizer runtime cannot start on macOS 26+, so
  their real home is the weekly Linux workflow.)
- **CI, per push**: three platforms build the library and every in-tree
  example, run lint and the ctest gates. The Linux runner has a
  software Vulkan device (lavapipe), so its shot checks draw for real.
  Pushes are BATCHED — compute minutes are finite and the local rungs
  are what catch mistakes.
- **CI, weekly**: ASan+UBSan and TSan on Linux (`weekly.yml`), where
  those runtimes work; timeouts everywhere so a hang is bounded.

A new feature adds: one `tests/checks/<x>_check.cpp` (harness +
assertions), its line in the foreach with a `device`/`pure` label,
and — if it adds a rule — one lint clause, broken once to watch it go
red.

Two decisions this infrastructure encodes, both worth keeping:

- **The library owns no clock.** Frame time belongs to the sim, so a
  rendered frame is a function of what the caller did, and every shot
  is reproducible without special support. The day simview drives an
  animation itself (a plot with a time axis), that clock must be
  SETTABLE from a test on the same day it is written — retrofitting
  determinism costs far more than building it in.
- **Driving the App is public API, not a test hook.** `PostEvent`
  delivers through the ordinary callbacks; anything a test can do to
  an App, a scripted demo or a bug report can do too. A private test
  backdoor would have tested a path users never take.

## Platform

Vulkan 1.3 everywhere — MoltenVK on macOS, native drivers or
lavapipe on Linux, native on Windows; SDL3 stays for windowing and
input only. Primary development on macOS; CI builds the library and
every example on all three from the first push (the vklib lesson:
its CI never built the library, and six of eighteen examples were
dead with a green badge). Note for device creators: SDL dlopens the
loader by bare name and misses /usr/local/lib on macOS, and a stale
SDK's ICD can shadow a newer one — simview's Vk.cpp owns the hint
probe and RANKS physical devices (floor, discrete, apiVersion)
because the same silicon can enumerate twice through two ICDs.

Present mode is FIFO on a portability driver and IMMEDIATE-first
elsewhere, and that split is a measurement, not a preference:
examples/ising on MoltenVK 1.3.0 runs 3363 sweeps/s beside a 60 Hz
window under FIFO and 650 under IMMEDIATE, because MoltenVK's
IMMEDIATE spins in [CAMetalLayer nextDrawable] inside vkQueueSubmit
and the compute queue starves; a single shared queue gives 625 under
either. The same probe put gpud's standalone rate at 2876 on
MoltenVK 1.3.0 and 9126 on 1.2.11, and the cause is one MoltenVK
default: 1.3 turned Metal argument buffers ON, and with a
device-address compute ABI every dispatch then re-binds every
addressable buffer (57 µs a dispatch against 21). Vk.cpp turns them
back off through the standard VK_EXT_layer_settings extension at
instance creation — no environment variable, ignored by any driver
that is not MoltenVK. The same chain carries the Khronos validation
layer's features when `SIMVIEW_VVL` names them (`sync`, `gpuav`,
`printf`, `best`), with `debug_action` pointed at the app's own
debug-utils messenger so a validation error is a counted event — and,
with `abort`, an exit code — rather than a line on stdout; that is
what `make validate` and CI's Linux leg gate on. The shared path reads 10875 sweeps/s on
1.3.0 and 9798 on 1.4.2. Bindless (descriptorIndexing) would want
them back on; that is the trade to revisit then. gpud 0.8's eager
submission and buffer pool then took the same path to 24.2k and
24.5k beside a 60 Hz window — the standalone rate, with no empty
half-second bucket in any run; at 120 Hz the frame loop's own
submits cost the sim about a third (15.4k), driver contention rather
than a design coupling.

## Conventions decided up front

- **Frame loop: register-then-Run.** `app.on_frame(cb); app.run();` —
  the library drives; run() blocks until quit. A consumer-owned
  `frame()` escape hatch may be added later if a real sim needs to own
  its loop; it would be impl-level, with run() as sugar over it.
- **Pause does not abort a tick in flight.** A user's callback is not
  interruptible, so one more tick may complete after `Pause()` returns;
  what the executor promises is that no NEW tick starts. Anything
  measuring tick counts across a pause must let the in-flight one land.
- **Errors: the impl never throws — and it reports itself.**
  Constructors that fail return a null handle; operations return
  bool; the sentence is logged at the refusal site, so checking the
  bool is a consumer's whole error handling. `sv::LastError()` is
  the programmatic twin for tests and tooling, never a requirement.
- **Headless is first-class.** Every view renders offscreen
  byte-identically to onscreen; `--shot`-style offscreen capture is
  API, not a debug hack — the pixel tests and eyeless verification
  depend on it.
- **Input vocabulary**: `Key` names only the ~20 keys sims bind, with
  numeric values that are the USB-HID/SDL scancodes (a standard
  adopted, not a mirror that drifts); unnamed keys pass through as the
  integer. (Arrives in Move 2.)
- Names are admitted to the view/plot vocabulary only if they hide a
  decision the caller would otherwise get wrong.

## Testing

| layer | what |
| --- | --- |
| gates | `make lint` (dependency hygiene of the installed surface) and `make install-check` (install to a scratch prefix; compile a consumer against ONLY that, SDL absent) — both from day one |
| unit | the sync layer: the Executor's clock, the Sync gate's roles over random interleavings, the fresh-buffer rotation on gpud's mock under TSan |
| pixel | **measured palettes, not committed goldens** — a check captures the colours the working arrangement produces and fails on any colour outside them. It needs no golden to maintain, survives a driver that rounds differently, and catches a flash of a colour nobody predicted |
| examples | every in-tree example builds on every platform on every push — build-only: they are showcases, and the checks carry the verification they must not |
| interaction | `PostEvent` delivers keyboard events to the sim's own callbacks; `tests/fakes/` supplies the backends a display would otherwise be needed for, so torn-out panels are reachable headlessly |
| boundary | test-only exports live in `sv::probe`, in their own never-installed archive; `installed_surface` proves it absent from the prefix |

Every gate must be BROKEN once when added, to prove it fires.

## Roadmap

**`roadmap.md` is the feature list; `architecture.md` is the
structural sequence.** Both were duplicated here once and drifted,
which is why neither is repeated now.

v1.0 is not a feature: it is the CI matrix green on all three
platforms with the docs matching reality.
