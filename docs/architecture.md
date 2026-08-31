# Architecture

`design.md` records decisions as they were made. This file records the
shape those decisions add up to, the patterns behind it, and where it
is going. When the two disagree, this one is the architecture and that
one is the history.

## The forces

Four properties make simview what it is. A pattern that does not serve
one of them is decoration.

1. **A retained scene and an immediate UI share one frame.** Scene
   items are registered once and drawn forever; ImGui is rebuilt every
   frame from scratch. Two lifetime models, one command buffer, and the
   ordering between them is a correctness property — not a detail. It
   has already produced one shipped bug.
2. **The public surface must stay ABI-plain.** Exported functions take
   and return handles, PODs, enums, `const char *`, primitives and
   fn-ptr + `void *` pairs — never a template, never a std type. The
   sugar above it is templated and header-only.
3. **Three extension axes, different shapes.** Plot series and panel
   widgets have uniform state and differ only in how they are emitted.
   Scene kinds have genuinely different state per kind. The same
   solution does not fit both, and pretending it does is what made
   `SceneItem` carry every kind at once.
4. **Foreign SDKs must not leak** — not through headers, not through
   include paths, not through the installed archive.

## The patterns, and why each one

### Opaque handle + free functions + inline sugar

The two-strata surface: `sv::impl` is exported symbols over opaque
handles; `sv` is inline sugar in the same headers, lowering onto impl
calls and owning no logic. This is the *C core with a C++ veneer*
pattern, and simview is squarely in its family — Vulkan/`vulkan.hpp`,
sokol_gfx, bgfx and libgit2 are all the same species of library
(GPU/windowing, opaque handles, a stable ABI, a header-only ergonomic
layer). Their answers to later problems are worth stealing.

This is the best thing in the design. It is what makes
`installed_surface` possible: a consumer compiles against the prefix
with no SDL, no ImGui, no gpud on any path.

### Ports and adapters

The core must not know whether its output goes to a swapchain, an
offscreen texture, or a fake. Where that is not true, the same logic
gets written twice and only one copy is reachable by a test — which is
exactly how the torn-out-panel bug survived: `app_run` and `app_shot`
each carried their own copy of the frame order, and the harness could
only reach one.

A **port** is a small struct of function pointers plus a `void *self`.
An **adapter** implements it. The frame's port is the presenter:

```cpp
struct Target { nvrhi::IFramebuffer *fb; nvrhi::ITexture *tex;
                std::uint32_t w, h; nvrhi::Format format; };

struct Presenter {
    bool (*acquire)(void *self, nvrhi::ICommandList *, Target *out);
    void (*finish )(void *self, nvrhi::ICommandList *, bool acquired);
    bool  composites;
    void *self;
};
```

`frame_render` is written once against it. A window is one adapter, a
shot texture another, and a test may supply a third. ImGui already
works this way — its platform and renderer backends *are* adapters,
which is why `tests/fakes/Viewports.h` could supply a display-free
multi-viewport backend at all. That file is prior art from inside our
own dependency; the library should be as adaptable as the thing it
embeds.

### Type Object (an ops table) for scene kinds

Measured cost of adding Particles as the second scene kind: **13
`ItemKind::` branch sites across 4 files**, four `points ? … : …`
ternaries inside `pipeline_for`, and a `ParticlesState` member on every
`SceneItem` — 160 bytes carrying both kinds, half of it dead per item,
and worse for every existing item when a third kind arrives.

The shape that scales is a static table of function pointers per kind,
with the kind's state behind a `void *`:

```cpp
struct KindOps {
    const char *name;
    void (*prepare)(SceneItem &, nvrhi::ICommandList *);
    void (*draw)   (SceneItem &, nvrhi::ICommandList *,
                    nvrhi::IFramebuffer *, const Placement &);
    void (*release)(SceneItem &);
    bool (*grid)(const SceneItem &, Extent2 *);
    Shader vs, fs;                       // committed SPIR-V + entry
    nvrhi::BlendState::RenderTarget blend;
    nvrhi::ShaderType storage_stage;     // who reads binding 0
    std::uint32_t push_bytes;            // the kind's Params size
};
struct SceneItem { App *app; const KindOps *ops; void *state; };
```

A new kind becomes one file plus one table entry plus a shader, with
zero edits to existing engine functions. **That claim was measured,
not asserted**: `Lines` was added after the table, and nothing in
`Scene.cpp`, `Pipelines.cpp`, `Field.cpp`, `Particles.cpp`, `Frame.cpp`
or `Device.cpp` changed. The honest other half: a kind still costs
about 55 lines of PUBLIC header across three files — its `Desc`, its
handle class, two `sv::Scene` methods and two `App` forwarders —
because `sv::Scene` is the one place a kind is spelled for the user.
The engine claim holds exactly; the surface claim was never made and
does not. The table is chosen over virtual
inheritance because it is **data**: it carries the shader bytecode and
blend state too, which is what removes the branching from
`pipeline_for` that virtuals alone would leave behind. The virtual-call
cost is per item per frame, which is nothing beside the draw.

**Plot series deliberately do not get this.** A series is
`{kind, dtype, source, style}` — one shared struct differing only in
the emit call, so a switch is right, and its measured cost is one enum
value plus one `case` plus one forwarder — held across eleven 2D kinds.
Same-looking problem, different shape. And a plot FAMILY — a second
library with its own bracket — costs one enum value, one bracket arm,
one emitter and one builder, with the draw path staying one function.
That was the question ImPlot3D was brought in to ask.

### Builder

`Plot`, `Panel`, `Scene` and `View` each return a reference so
registration chains. Chaining is the idiomatic spelling; a handle is
assigned to a variable only when it is updated later.

## Patterns deliberately rejected

| pattern | why not |
| --- | --- |
| ECS for the scene | Tens of heterogeneous items, not thousands of homogeneous ones. The cost is all in the draws, not the iteration. |
| A full render graph | The frame has about four passes. A graph is more machinery than the thing it schedules; the presenter port buys the ordering guarantee for a fraction of the cost. |
| Observer / signals for events | The fn-ptr + `void *` list is *required* by the ABI rule. `std::function` in an impl signature would break the surface. |
| Pimpl per class | One App-owned arena beats N allocations, and the handles are already opaque. |
| A DI container | There is one object graph and it is built in `app_init`. |

## The layer DAG

Dependencies flow downward only. `ui` sits **above** `scene`, because a
View is a scene rendered for a panel — `ui/View.cpp` calls into scene,
never the reverse.

```
L0  core       Types, Error                        no SDK
L1  platform   Device, Window, Presenter port      SDL / gpud
L2  scene      SceneState, kinds via the ops table L0 L1 sync
L3  ui         Context, Panel, Plot, View          L0 L1 L2 sync + ImGui
L4  app        the frame over the Presenter port   L1 L2 L3
L5  doors      gpud.h                              public vocabulary
    sync       Executor, Sync, Tick                std + Types.h — a
                                                   leaf; scene and ui
                                                   may include it
```

`App` **composes** rather than contains — `src/core/App.h`, 40 lines:

```cpp
struct App {
    Platform platform;   Input input;   Stats stats;
    std::vector<PipelineEntry> pipelines;
    SceneState scene;    UiState ui;
    std::list<View> views;  std::list<PlotState> plots;  std::list<PanelState> panels;
};
```

Each member's type is declared in its own layer's header, so a layer
includes only what is below it and `App.h` is the one place that sees
all of them. It replaced a 228-line header holding 11 nested types
that every TU included. There was **no build-time argument** for the
change — touching that header cost 1.3 s against 1.2 s for one source
— and it was made anyway, because reading the old header for the plan
found it had been **hiding six layering violations**. A single opaque
`App *` lets any layer reach any state; the violations were invisible
until the types were separated, and two were structural:

- `views_resize`/`views_draw` lived in `scene/` but iterated a
  UI-owned `ViewState`, with `want_w`/`want_h` written by ui and read
  by scene — "ui calls into scene, never the reverse", inverted.
  Resolved by a split: a scene-owned `RenderTarget` (a texture resized
  to a requested size and drawn into, knowing nothing of panels) and a
  ui-owned `View`. ui writes `target.want_*`; scene never reads a ui
  type.
- `it.app->stats.draws` at six sites in three kind files forced
  `SceneItem` to hold the full `App`. It now holds a device, a
  `Stats *` and the pipeline cache — what a kind actually asks of the
  world — and `src/scene/` includes nothing from `ui/` or `App.h`.

Lint rule (j) enforces the DAG: `scene/` and the platform *state*
headers may name nothing from `ui/` and never `core/App.h`. The
`.cpp` files that orchestrate a frame or a lifecycle are L4 by
content and may.

Two things stayed deliberately small. `Platform` declares `gdev`
before `dev` because `dev` is borrowed from it and destruction is
reverse order — the one place member order is load-bearing, with the
comment above the two adjacent fields. And `headless` is gone: three
spellings of one fact became `win == nullptr`.

## The target `src/` layout

Folded by subsystem, because that is the axis of change. The two
strata are files *within* a subsystem, since that axis is already
carried by the namespace and enforced by lint — folding by it as well
re-encoded one axis while cutting across the other. It used to produce
`src/impl/Plots.cpp` and `src/engine/Plots.cpp`: two files with the
same name, three places to edit for one feature.

```
src/
  core/      App.h                     the composed App — the one union
             Callbacks.h Error.h       shared by two layers, so below both
             Error.cpp                 set_error, last_error, version
  platform/  Device.cpp                gpud/SDL bring-up, window, lifecycle
             Frame.cpp                 frame_build, frame_render, the presenters,
                                       and app_run / app_step / app_shot
             Input.cpp                 poll, posted events, delivery
  scene/     Kinds.h                   KindOps, Shader, Blend, Placement
             Scene.cpp                 scene_draw, the range, views, release
             Field.cpp Particles.cpp   one file per kind: state, uniforms,
                                       shaders, ops, exported functions
             Pipelines.cpp             the (kind, format) cache, kind-agnostic
             bytecode/                 generated, beside its only includers
  ui/        Ui.h                      the ImGui layer's internal surface
             Context.cpp               lifecycle, dockspace, viewports, view panel
             Plot.cpp PlotDraw.cpp     plots and panels: registration / drawing
             View.cpp                  view registration
  door/      Gpud.cpp
  sync/      Sync.cpp
  testing/   Probe.cpp                 never installed — see below
```

`shaders/*.slang` stays at the repo root as a source; its generated
bytecode lives beside the code that includes it.

**`include/` has two subfolders.** `sync/Sync.h` — the Executor and
`Sync<T>`, a concurrency layer over std and `Types.h` alone; the
scene consumes a Sync through its gate handle and the frame flips it,
and neither knows the `T`, so the folder says what it is. And `scene/`, one
header per kind: a kind's public half (its `Desc`, its handle, its
sugar class) is one file there, and `Scene.h` holds only what every
kind shares — `Range2`, the `Scene` builder, `no_door`. The kind
headers depend on `Types.h` alone; `impl::Scene`, the ADL anchor that
finds `*_from_source` at instantiation, lives there and not in
`Scene.h`, which is why the split has no cycle.

Teaching `tools/lint.sh` about subfolders was the real work. Its
include whitelist was a flat basename alternation (one hard failure)
and both its header loop and its no-comments rule globbed
`include/simview/*.h` non-recursively — so a subfolder header was
never visited and the gate kept saying "clean". Two silent holes,
both proven closed by breaking them.

## The test boundary

**Test-only code is absent from a release build, not merely
undeclared.** Three accessors currently ship as real exported symbols
in `libsimview.a`, hidden only by not being written down in a public
header — the weakest form of hiding there is.

The rule:

- The probe's accessors: the UI contexts, a view's extent, the gate
  count, the renderer device, the shared-queue bracket, and the
  validation tally (`validation_on`, `validation_errors`) — what a
  check asks that a consumer never may.
- Test-only exports live in `sv::probe`, never `sv::impl`. One grep
  answers "what exists only for tests?".
- They live in one file, `src/testing/Probe.cpp`, declared in a header
  that is **not installed**.
- They are compiled into **their own static library**, `simview_probe`,
  built only under `SIMVIEW_BUILD_TESTS` and **never added to
  `install(TARGETS …)`**. The boundary is a *link* boundary, which is
  why proving absence needs no `nm` and no second build.
- `installed_surface` asserts the archive is absent from the prefix,
  and — when `nm` is found — that the installed `libsimview.a` contains
  no `sv::probe` symbol. A missing `nm` is a **named skip**, never a
  silent pass.
- `tools/lint.sh` refuses `sv::probe` outside `src/testing/` and
  `tests/`, and refuses it in any installed header.

`tests/` folds the same way `src/` does, because it is four kinds of
thing in one folder:

```
tests/
  harness/   Check.h Harness.h Bmp.h Palette.h   assertions, image tools
  fakes/     Viewports.h                         adapters for ports
  probe/     Probe.h                             the declared test surface
  checks/    *_check.cpp
  installed_surface/
```

`Palette.h` is the reusable half of the assertion that caught the
torn-out-panel bug: **arrangement invariance** — the same scene looks
the same however the panels are arranged. The expected palette is
*measured* from the arrangement that works, and any colour outside it
fails, whatever that colour turns out to be. That property is worth a
name because it generalises: it catches a flash of any colour, a stale
target, and undefined memory, without a golden image to maintain or a
symptom to hardcode.

## Sequence

| pass | change | why then |
| --- | --- | --- |
| 1 | `tests/` fold + `Palette.h`; `sv::probe` + its gate; the Presenter port and one frame order | **Shipped.** Clears the ground, then puts the loop a user runs under the harness |
| 2 | Scene kinds → the ops table; `src/` folded by layer; `Lines` as the measurement | **Shipped.** 13 branch sites became 0, `SceneItem` 160 → 24 B; the third kind cost zero engine edits and ~55 surface lines |
| 3 | Fold `src/` by layer; split `App.cpp` along the seams already in it | **Shipped** in pass 2 |
| 4 | Compose `impl::App` from subsystem states | **Shipped.** 228 → 40 lines; six hidden violations found, two structural, all resolved; lint rule (j) enforces the DAG |
| 5 | `Sync` to `include/simview/sync/`; `scene/` per kind; lint learns subfolders | **Shipped.** Two silent lint holes found and closed |
| 6 | `include/` subfolders | Folded into 5 |

Passes 1, 3 and 4 are behaviour-preserving refactors. The suite can now
police them by pixels, which it could not before the harness existed —
that is what makes this sequence safe to attempt at all.
