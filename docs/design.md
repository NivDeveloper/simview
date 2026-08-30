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
pacing, triple-buffered state/transfer/draw with POINTER swaps, sim
depth capped at one in flight so render interleaves, lazy first-touch
registration. One channel flavour shipped: `HostChannel`
(triple-buffered host memory — the portable default, works for any
sim). The zero-copy half was never built as a channel: the pull model
(gpud's `BufferSource`, below) answered that question instead, and
answered it without simview owning a second transfer path.

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
  points into the producer object).

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
- **A new scene kind costs its three sites PLUS a pipeline and a
  shader** — irreducible, and worth saying so nobody expects the
  series-level cheapness a plot kind has. The three sites are one
  `impl::*_create(Scene, …)`, one `case` in `item_draw`, and one
  method on `sv::Scene`. **`sv::Scene` is the one place a kind is
  spelled for the user**: `App` and `View` both hand one out, so
  neither grows a method per kind.

## Views

A view is a scene whose target is a texture instead of the swapchain,
shown by a panel that docks, tabs and tears out like any other. It is
the SECOND arrangement, not a replacement: `app.Field(…)` still draws
straight to the window with panels floating over it (ising-cpu), and
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
  buffer wrote, so the main buffer has to be on the queue first.
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
  single line turns `DType` into `T`. A new kind costs one enum value,
  one case and one builder method; supporting float AND double costs
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
  engine's counters, the sync layer's tearing stress, and SHOT CHECKS
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

macOS (Metal), Linux (Vulkan), Windows (D3D12) — wherever SDL_GPU
runs. Primary development on macOS; CI builds the library and every
example on all three from the first push (the vklib lesson: its CI
never built the library, and six of eighteen examples were dead with a
green badge). Note for device creators: SDL's Vulkan driver dlopens
the loader by bare name and misses /usr/local/lib on macOS — whoever
creates the device owns the `SDL_VULKAN_LIBRARY` hint probe.

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
| unit | the sync layer under a fake clock; builder recording |
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
