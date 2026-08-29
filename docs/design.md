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

```
App shell    window(s), the run loop, input, ImGui frame, headless mode
Views        FieldView; PlotView/WidgetView builders; custom-pass hook
Data plane   the sync layer: Executor + channels (versioned snapshots)
Draw         internal pipelines from committed bytecode; ImGui render
SDL3         the only floor
```

The sync layer inherits vklib's `SyncBuffer`/`Executor` design — its
best-factored piece (138 public lines, 269 impl lines, 3 lines of
consumer cost): Play/Pause/Step safe from any thread, `iterDelayNs`
pacing, triple-buffered state/transfer/draw with POINTER swaps, sim
depth capped at one in flight so render interleaves, lazy first-touch
registration. Two channel flavors: `HostChannel` (triple-buffered host
memory — the portable default, works for any sim) and `GpuChannel`
(SDL_GPUBuffer handoff, zero-copy; single-threaded first, threaded
only after SDL's cross-thread command-buffer rules are pinned).

## Dependencies

| dep | policy |
| --- | --- |
| SDL3 | PRIVATE (engine-only); no public header names it at all |
| Dear ImGui (+ ImPlot later) | FetchContent, pinned by hash in the root CMakeLists exactly as gpud is; built as `simview_imgui` with warnings silenced at the target. The UI layer is not optional to the engine; the opt-in door is how a CONSUMER reaches it |
| tensor | absent, forever — its data arrives, its types never do |
| gpud | the substrate: the ENGINE builds on it (gpud::sdl owns device bring-up; linked PRIVATE, gpud::gpud PUBLIC for the door's include dirs, FetchContent-pinned by hash). The opt-in door `gpud.h` speaks its vocabulary; the core never names it |
| shader toolchain | absent for consumers: internal shaders ship as committed bytecode (SPIR-V/MSL/DXIL); `shaders/regen.sh` is dev-only |
| everything else | std, C++20, the SYSTEM compiler (this is not tensor's g++-16/reflection world) |

## Integration with sims (tensor/gpud as the worked case)

The composition principle: gpud is the GPU interchange the sibling
projects share — one device, opened by simview THROUGH gpud, computed
on by producers (tensor) that export gpud vocabulary, and NOBODY
imports tensor. The interchange type is gpud's BufferSource: a
producer implements `source_of(const P &)` (found by ADL), a field
binds it once, and the engine PULLS current() at every draw — the
moving residency of a value-semantics producer costs no per-frame
glue and no API that scales with vis types.

- **Mode A (default): host memory.** `field.update(state.data())` —
  works with every backend and every language; two devices may exist
  and it does not matter (a 256² float field is ~15 MB/s at 60 fps).
- **Mode B (opt-in): zero-copy, one shared device.** simview owns
  the gpud device (`sv::Device(app)` hands it to the compute
  runtime); `app.Field(producer, desc)` registers the pull source
  once and no per-frame call exists. Opting into `gpud.h` IS opting
  into gpud, explicitly. Producers die before the App — the one
  lifetime rule of the glue (the field's source points into the
  producer object).

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

A new feature adds: one `tests/headless/<x>_check.cpp` (harness +
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
| pixel | offscreen goldens per view type, small, committed |
| examples | every in-tree example builds on every platform on every push — build-only: they are showcases, and tests/headless carries the verification they must not |
| interaction | SDL_PushEvent-driven scripted input (later) |

Every gate must be BROKEN once when added, to prove it fires.

## Roadmap

- **Move 1** (this): constitution, gates, minimal impl, CI matrix.
- **Move 2** (done): the walking skeleton — window + run loop,
  Event/Key, host-path Field with the colormap pipeline (committed
  SPIR-V bytecode; native MSL/DXIL emission is a named follow-up),
  step/shot as headless API, hello-window.
- **Move 3** (done): the founding examples — `ising-cpu` (plain
  arrays + the sync layer; the canary) and `xy-gpu` (Mode B,
  standalone subproject, the cross-compiler ABI proof) — plus the
  Executor/Channel port and the zero-copy field. Controls are keys
  until the widget move.
- Then: plots over ImPlot behind the one UI boundary, aux windows,
  particles, threaded GpuChannel. v1.0 is not a feature: it is the CI
  matrix green on all three platforms with the docs matching reality.
