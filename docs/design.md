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

**The seam** is the set of symbols exported from libsimview — the
compiled boundary, the ABI. Its signatures are deliberately boring:
opaque handles (`struct App { void *p; }`-shaped), POD structs, enums,
`const char *`, primitives, and function-pointer + `void *` pairs.
Never a template (they cannot be compiled into a library ahead of
time); never a std type in a signature (a compiler-coupling hazard
across a shared-library boundary). The seam is what must stay stable,
what a future .so exports, what tests mock, and what an ABI review
reads.

**The sugar** is inline code in the same public headers — methods on
the handle types, builders, span/lambda overloads. It compiles into
the CONSUMER's binary, never into libsimview, so it may use std freely
and be reshaped at any time. Its one law: it lowers ONLY onto seam
calls; it holds no logic of its own.

```cpp
// the seam: exported from libsimview
Field field_create(App &, const FieldDesc &);
bool  field_update(Field, const void *data, DType, size_t n, size_t stride);

// the sugar: inline, compiled into the consumer
inline bool Field::update(std::span<const float> v) {
    return field_update(*this, v.data(), DType::f32, v.size(), sizeof(float));
}
```

The reader convention, uniform everywhere: **seam = free functions
over handles; sugar = inline methods on the handles.** One glance at
any declaration says which side it is on.

Why both strata exist: the seam buys ABI stability, mockability, and a
cheap future C-ABI; the sugar buys vklib-grade ergonomics (its ising
example was 205 lines with ~20 of ceremony — the bar). vklib had only
the sugar with no deliberate ABI stratum beneath, which is how its
public headers accumulated five boundary mechanisms and its internals;
gpud is nearly all seam. simview takes both on purpose.

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
| SDL3 | the ONE library dependency, PRIVATE (engine-only). Public headers never include it; `native.h` may FORWARD-DECLARE its handle types — the one carve-out |
| Dear ImGui + ImPlot | vendored, pinned, PRIVATE (arrive in Move 3); one opt-in escape hatch for custom panels, explicitly version-locking |
| tensor / gpud | absent, forever. Data arrives as pointer+stride or (native.h) `SDL_GPUBuffer *` |
| shader toolchain | absent for consumers: internal shaders ship as committed bytecode (SPIR-V/MSL/DXIL); `shaders/regen.sh` is dev-only |
| everything else | std, C++20, the SYSTEM compiler (this is not tensor's g++-16/reflection world) |

## Integration with sims (tensor/gpud as the worked case)

The composition principle: the three libraries never learn about each
other; THE APP is the only place they meet, and handles flow through a
chain of ADJACENT seams — tensor exports `gpud::Buffer *`
(`resident_buffer`), gpud exports `SDL_GPUBuffer *`
(`gpud::sdl::native_buffer`), simview imports `SDL_GPUBuffer *`
(`native.h`). No library skips a level.

- **Mode A (default): host memory.** `field.update(state.data())` —
  works with every backend and every language; two devices may exist
  and it does not matter (a 256² float field is ~15 MB/s at 60 fps).
- **Mode B (opt-in): zero-copy, one shared device.** simview creates
  the device; the compute runtime ADOPTS it (`gpud::sdl::try_open_on`)
  — or the reverse; per-frame `field.rebind(handle)` because resident
  buffers ping-pong. Opting into `native.h` IS opting into SDL,
  explicitly. A parked tensor holds a device handle and must die
  before the device — the one lifetime rule of the glue.

The seam accepts behaviors (callables as fn-ptr+void*), handles
(pointers), and integers (generation counters) — never foreign types.
The future threaded GpuChannel synchronizes on a caller-supplied
generation source (an integer callback the app wires to its runtime's
`completed()`), so even that needs no dependency.

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
  its loop; it would be seam-level, with run() as sugar over it.
- **Errors: the seam never throws.** Constructors that fail return a
  null handle; operations return bool; `simview::last_error()` gives
  the sentence (SDL's own convention — the engine wraps it anyway).
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
| examples | every example builds on every platform on every push; each carries a headless self-check |
| interaction | SDL_PushEvent-driven scripted input (later) |

Every gate must be BROKEN once when added, to prove it fires.

## Roadmap

- **Move 1** (this): constitution, gates, minimal seam, CI matrix.
- **Move 2**: the walking skeleton — window + run loop, Event/Key,
  host-path Field with the colormap pipeline (committed bytecode),
  hello-window on all platforms.
- **Move 3**: the founding examples — `ising-cpu` (plain arrays; the
  independence proof and permanent canary) and `xy-gpu` (Mode B on the
  named seams) — plus the Executor/HostChannel port and controls
  widget.
- Then: plots over ImPlot behind the one UI seam, aux windows,
  particles, threaded GpuChannel. v1.0 is not a feature: it is the CI
  matrix green on all three platforms with the docs matching reality.
