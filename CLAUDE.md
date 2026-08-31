# simview — conventions

Lightweight cross-platform sim visualization over SDL3. C++20, the
SYSTEM compiler (AppleClang/gcc/MSVC — deliberately NOT tensor's
g++-16/reflection world). Structure and patterns: docs/architecture.md;
how it got there: docs/design.md; what is next: docs/roadmap.md.
Current state: the APP owns the Vulkan stack (src/platform/Vk.cpp:
one instance, one device, two queues) and both halves adopt it —
NVRHI renders on the graphics queue, gpud computes on the
compute queue (try_open_on; buffers CONCURRENT-shared) — with an
app-managed swapchain (FIFO on a portability driver: MoltenVK's
IMMEDIATE spins in nextDrawable inside vkQueueSubmit and starves the
compute queue, 650 against 3363 sweeps/s measured; IMMEDIATE-first
elsewhere; SIMVIEW_PRESENT overrides) and ImGui on the upstream sdl3
+ vulkan backends. Vk.cpp also switches MoltenVK's Metal argument
buffers OFF through VK_EXT_layer_settings (4x on device-address
compute — 2876 to 10875 sweeps/s; bindless would want them back). The frame orders itself GPU-SIDE on gpud's
timeline: Publish stamps its Sync slot with submitted(), the frame
waits (native_timeline, max shown stamp) on the graphics queue, and
frames-in-flight = 1 (an event query waited before the flips) closes
the reverse edge — the sim and the display are DECOUPLED. The one
opt-in door — include/simview/gpud.h — carries sv::Device(app) and
the pull-model field (app.Field(producer, desc) resolves gpud's
source_of protocol by ADL; the engine re-asks the source at every
draw, so rebind does not exist — `app.Particles(producer)` is the
same protocol for a point cloud, whose count comes from the buffer's
size; a bare pull with no Sync behind it makes the frame wait on
everything submitted instead of a stamp). The UI is ImGui + ImPlot, hash-pinned through FetchContent
exactly as gpud is (third_party/ stays EMPTY — its README says why),
behind one boundary that no public header names and whose include
directories are PRIVATE to this build: plots, panels and views are
builders. Examples:
ising-cpu (plain C++ arrays through sv::Sync on the Executor's thread
— the independence canary), gas (the scene in the window AND a second
scene in a view), and two standalone tensor-on-gpud subprojects, xy-gpu
(the bare pull, stepped on the render thread) and ising (the same
lattice as ising-cpu through sv::Sync over a device-resident tensor).
Next: 3-D scene kinds, and more than one window.

## Invariants (do not break)

- **The two-strata surface.** The IMPL is every exported symbol: free
  functions over opaque handles, taking/returning only handles, PODs,
  enums, `const char *`, primitives, and fn-ptr+`void *` pairs — never
  a template, never a std type in a signature. The SUGAR is inline
  code in the same headers (methods on handles, builders): it may use
  std freely and must lower ONLY onto impl calls, no logic of its own.
  The strata are told apart by NAMESPACE and by NAME: the impl lives
  in `sv::impl`, the sugar in `sv`, and a user never spells `impl`.
- **The include graph:**

  | who | may include |
  | --- | --- |
  | `include/simview/**/*.h` (core) | each other + std. Nothing else, ever. Subfolders (`scene/`, `sync/`) are part of the surface and lint walks them |
  | the one opt-in door, `gpud.h` | core + std + `<gpud/Device.h>`. No public header names ImGui or ImPlot. Door consumers take simview via add_subdirectory/FetchContent (the installed prefix carries no gpud) |
  | `src/**` | anything (SDL, later ImGui/ImPlot) — never installed |
  | a core consumer | `<simview/simview.h>` + libsimview; no SDL anywhere |

- **SDL3 is PRIVATE — in NO public header now**, not even as a
  forward declaration: gpud vocabulary replaced the SDL handle door.
- **No tensor — ever. gpud is the substrate.** gpud is the universal
  GPU abstraction the sibling projects share, and the ENGINE composes
  with it: simview owns Vulkan bring-up (Vk.cpp — the loader hint,
  ICD ranking, the two-queue policy) and hands gpud its compute queue
  through try_open_on; libsimview links gpud::vulkan, nvrhi and
  Vulkan::Headers PRIVATE and gpud::gpud PUBLIC (include dirs for the
  door). Consumers of the core surface still see none of them. Data crosses the impl as
  pointer+stride or gpud vocabulary (BufferSource is a POD) at the
  door; the pull model means the engine asks the source at draw time
  and per-frame rebinding does not exist. No shader toolchain:
  internal shaders are committed bytecode (gpud resolves slangc
  lazily, so device bring-up needs none); regeneration is dev-only.
- **The impl never throws — and it reports itself.** A refusal is a
  null handle / `false`, with its sentence logged at the refusal site
  (SDL's error log). `LastError()` is programmatic access for tests
  and tooling; a consumer never NEEDS it, and examples never call it.
- **Headless is first-class.** Every view must render offscreen
  byte-identically to onscreen.
- **A gate must be broken once when added** — watch it go red, then
  restore. A check that never fired is a comment. The same test
  applies to CODE: a drill that cannot tell a branch apart has found
  dead weight, not a passing gate (the camera's grab latch went that
  way — ImGui already owns a drag from its press).
- **A picture is asked what it is MADE OF, not only what colour it
  is.** `tests/harness/Bmp.h` carries the structure vocabulary —
  `lit_count`, `quadrants`, `runs_in_row`/`runs_in_col` (lines
  crossing a scan), `shades` (that a line was anti-aliased rather than
  stamped) — because the questions a colour probe can answer are not
  the ones that catch a grid with a quadrant missing or a family of
  lines gone. `grid_check` is the worked example: four quadrants
  compared, both line families demanded in each, the picture asserted
  IDENTICAL a decade of zoom apart, and the camera flown around
  through the input harness before asking again.
- **An interaction is testable, and therefore tested.**
  `tests/harness/Input.h` spells a gesture the way a person performs
  it — `input::drag(app, x0, y0, x1, y1)`, `wheel`, `press`,
  `release` — over `probe::mouse_*`, which puts synthetic events into
  ImGui's own queue. A headless app runs no backend, so nothing
  overwrites them. It covers hit-testing and everything above it:
  which panel owns the pointer, whether a drag latched, what the
  camera did. It does NOT cover the platform's translation of real
  events into UI events — that is the backend's, and the windowed
  showcases under `make validate` are where it is exercised for
  real.
- **The windowed path has no headless check — `make validate` is its
  gate.** The suite and the four windowed showcases run under the
  Khronos layer with synchronization validation, aborting on the
  first error (and on Darwin again under Metal's own API and shader
  validation). The four swap-era defects — swapchain usage, a nested
  rendering pass, a dangling view descriptor, the residency
  use-after-free — were all invisible to a green suite and all named
  by a layer. Two limits, measured: the layer cannot see gpud's
  compute writes (a device-address ABI has no descriptors), and on
  MoltenVK the graphics queue never overtook the compute queue even
  at 8 ms dispatches with the timeline wait deleted — so the
  cross-queue edge is currently UNOBSERVABLE here; `decouple_check`
  holds every frame to the value of the generation it claims to show
  (a stale pool buffer fails it), which is the gate a concurrent
  driver would trip. And the SDK's layer (1.4.304) INTERMITTENTLY
  segfaults in its own queue-batch tracking on the two-queue flagship
  under `sync` (2 of 3 runs) — `make validate` covers the suite and
  the in-tree showcases; the flagship under `sync` waits for the SDK
  update. Cost on the flagship: core −14%, sync −36% of sweeps/s.
- **A hang is a report.** Every host wait for the device — the
  previous frame, a shot's readback, the swapchain acquire, the drain
  at quit, and gpud's own waits, handed the same bound at adoption —
  is bounded by `SIMVIEW_WAIT_MS` when set, and every gate sets it
  (`make test`, `make validate`, CI: 20 s). Past the bound the
  sentence names what was waited for and where BOTH timelines stood,
  and the process exits with it; a lost device gets its own sentence.
  What the bound catches is not slow work but a wait on a value
  nothing will ever signal — a deleted pump, a stamp past what compute
  will reach — which is a freeze on lavapipe and, on MoltenVK,
  `VK_ERROR_DEVICE_LOST` after ~8 s of Metal's watchdog. `hang_check`
  stalls a frame on purpose and passes on the sentence. Unset, every
  wait is unbounded, as a long dispatch needs.
- **Where the time goes is measured on ONE clock, and nothing is
  measured that nobody asked for.** `SIMVIEW_TIMINGS=1` stamps every
  frame's graphics sections (views, ui, readback, and either "scene"
  for the 2D path or a section per PASS for a world) with timestamp
  queries on the frame's own command list and turns on gpud's batch
  stamps (`Options::profile`), both on the device clock; a
  once-a-second line reports frames, ms/frame, ms per section, and the
  compute queue's batches, dispatches and busy %. `timing_check` proves
  the stamps real (ordered, non-zero, every dispatch accounted for, one
  clock). The flame graph is the same data in Tracy: `make trace`
  builds `build-trace/` with the client linked (`SIMVIEW_TRACE=ON`),
  `SV_ZONE`/`SV_FRAME_MARK` (src/core/Trace.h) compile to nothing
  elsewhere, and platform/Timing.cpp feeds two GPU contexts — the
  graphics sections and gpud's batches — so both queues lie on the
  profiler's timeline beside the frame thread and the sim thread. The
  client is `TRACY_ON_DEMAND`: a traced showcase runs at its normal
  rate until a profiler connects. Debug labels (`VK_EXT_debug_utils`,
  on whenever the loader has it) name the queues, the timelines, every
  texture and every section, so a capture reads the same words.
- **3D is a stratum, not a kind.** `app.World()` is a panel with a
  camera, a fixed pass table (shadow reserved, opaque, transparent,
  overlay), items that SUBMIT draws for one `std::sort` to order, and
  reverse-Z depth whose five parts must agree — a D32 attachment,
  a 0 clear, an explicit `GreaterOrEqual`, the viewport range left
  alone, and a projection with a POSITIVE m[1][1] (the renderer flips
  the viewport itself). The world is Z-up. Ordering shipped with the
  check that fails without it, because vklib's equivalent sort was an
  identity function for its whole life. Limits are stated, not
  implied: transparency sorts per ITEM, a cloud keys on its centroid,
  a billboard writes the quad's depth. **The world culls, not the
  item** — an item that had to remember would forget — and an item
  reporting NO bounds is drawn, because "I do not know" is not
  "empty" and a box invented for device-resident data would delete
  it. A mesh tier follows how big a shape is ON SCREEN, with a
  triangle budget beside it: each guards a failure the other cannot.
  The near plane follows the geometry but only ever moves CLOSER than
  the orbit-scale default, since unbounded items must not be sliced
  away by a plane derived from bounded ones. **A world with no title IS the
  window** (the panels float over it); a titled one is a panel, for a
  layout with several views. A camera is perspective or orthographic
  and nothing else changes; a cloud may carry per-point VALUES beside
  its positions for a colormap, through the same three doors; up to
  four directional lights ride the view block, and an unlit world
  keeps the light at the camera it always had. Four-sample
  multisampling with a resolve, in the world stratum only.
  `docs/3d-plan.md`.
- **Dev, validation, debug and profiling facilities never touch the
  public surface.** They are environment variables read at bring-up,
  Makefile targets, or accessors in the test-only `sv::probe` archive
  — never a `Config` field, never a public function. The table is
  below under "The dev surface".
- **The builder is the API, and ImGui is not on the surface AT ALL.**
  Users write `app.Plot(…).Line(…)` and `app.Panel(…).Slider(…)`; no
  public header names ImGui or ImPlot, and lint fails on the token in
  any of them. The UI stack is an implementation detail of the
  builder, not a door. If raw access is ever needed it comes back as a
  deliberate decision with a reason, not as a leftover.
- **A new series kind costs three sites**: one enum value, one `case`
  in `emit_series<T>`, one builder method per data shape. The scalar
  is erased once, so float and double cost nothing per kind, and the
  erasure (`SeriesData`: four slots, an index channel, two counts,
  positional BY KIND) plus a shared `param[4]` mean a new kind adds
  no descriptor field. A new plot FAMILY is one bracket arm in the
  one `plot_draw`, never a second draw path. If a kind ever costs
  more, say so in the commit rather than quietly paying it.
- **The UI layer is ImGui, and the scene stays on the swapchain.**
  ImGui composites over it in a second LOAD pass — ui_draw is the
  app's ONE raw-Vulkan seam (dynamic rendering around the backend's
  RenderDrawData, then clearState) — and the dockspace's central node
  is passthru. `ui_on()` is false until a panel is
  registered, so an app that asks for no UI builds no ImGui frame at
  all. Viewports are enabled only when the BACKENDS set their own
  capability flags — never by us. No panel sets `NoDocking`. Capture
  gates OS events, never `PostEvent`. The layout lives per-app under
  `SDL_GetPrefPath`, saved on `WantSaveIniSettings` and again at quit
  BUT only when a frame was built (else a good layout is truncated).
- **A test is assertions and nothing else.** `tests/harness/` gives
  every check a `Harness.h` (device-or-SKIP, temp paths, shot-read-back),
  a `Check.h` (CHECK/CHECK_EQ/CHECK_GT keep going and print BOTH
  values; REQUIRE leaves; `check::summary` is main's return) and a
  `Bmp.h` (pixels, plus `mean`/`distinct`/`content`/`similar` — image
  STATISTICS, never byte equality, because lavapipe, Metal and D3D12
  never round alike). A new feature's check should be its assertions
  and one `harness::` line.
- **ctest labels split what needs a GPU from what does not**: `-L
  device` is the drawing half, `-L pure` the device-free half the
  sanitizer job runs, and `-LE device` is how a platform that cannot
  host one opts out BY NAME.
- **Input is testable because the App can be driven.**
  `app.PostEvent(KeyDown(Key::Space))` queues an event that the next
  `Step` (or loop iteration) delivers through the same callbacks SDL's
  own events use — the automation seam. It is public API, not test
  scaffolding: scripted demos and reproducible bug reports want it too.
- **`app.Stats()` counts what the engine did** — frames, uploads,
  pipelines, draws, and what the view decided: `culled` items and
  `triangles` submitted, the pair a level-of-detail rule is judged by
  (`WorldItem::triangles` is the per-item drill-down, for a check
  that needs to know what ONE item chose). Behaviour a timing test
  could only guess at becomes an exact assertion: an unchanged field
  uploads ONCE, a second shot re-uploads nothing, one target format
  makes one pipeline.
- **Examples are showcases, tests carry the verification.** No argv
  test modes, no probes in examples/ — that machinery lives in
  tests/checks/. And a showcase never prints — no stdout, no logs
  (`tools/lint.sh` rule (f)); its error handling is checking the
  bool, because the library already said why. An example with extra toolchain needs (xy-gpu:
  g++-16 -freflection) is a STANDALONE subproject that gates itself
  loudly; CI builds every in-tree example.
- No hardcoded toolchain paths; no `-march=native`; no globals — an
  App owns everything and dies with it.

## Commands

| task | command |
| --- | --- |
| build | `make` |
| tests | `make test` |
| surface hygiene | `make lint` — tools/lint.sh over the installed headers |
| the clean-surface proof | `make install-check` — install to a scratch prefix, compile examples/hello against ONLY it |
| run the hello | `make run` |
| **stepping through it** | `make debug` (the checks + in-tree examples, `build-debug/`) and `make -C examples/ising debug` (the GPU stack — simview, gpud, tensor, the example — one g++-16 tree at -O0 -g). `.zed/debug.json` and `.vscode/launch.json` launch them; a breakpoint in `sweep` hits on the Executor's thread at once, since the sim plays before `Run`. **The debugger is Homebrew LLVM's lldb/lldb-dap, never Xcode's** — Xcode's lldb SIGILLs at `target create` on this OS, on AppleClang binaries too — and a g++-16 binary needs two init commands (`settings set target.enable-synthetic-value false`, plus a user summary shadowing `^std::unique_ptr<`): lldb's hardcoded unique_ptr formatter crashes on a GCC DWARF type it cannot parse |
| **sanitizers** | `make san` / `make tsan` — ASan+UBSan over the suite, TSan over sync_check. **Neither runs on macOS 26+**: the AppleClang sanitizer runtime spins in `get_dyld_hdr()` during its own startup, before `main` — a runtime-vs-OS break, nothing to do with this code. `.github/workflows/weekly.yml` runs both on Linux |
| **the flagship** | `make flagship` — builds examples/xy-gpu and examples/ising (need g++-16); NO runner reaches them, so this is the local gate that keeps the door from rotting |
| **validation** | `make validate` — the suite and the four windowed showcases under `SIMVIEW_VVL=sync,abort`, then (Darwin) under Metal's API + shader validation; an error is an exit code, never a grep. CI's Linux leg runs its gates the same way on every push |
| **bench** | `make bench` — what a crowd of instanced geometry costs by shape and count, what the mesh tier is worth at a fixed crowd, and what the view cull saves with the camera held still and only the test switched. A REPORT: it prints numbers and judges none of them. Both constants in `choose_tier` come from it |
| clean | `make clean` (build, build-san, build-tsan) |

**Configure through `make`, never `cmake -B build` by hand.** SDL3 is
found from `CMAKE_PREFIX_PATH=$(PREFIX)`, which only the Makefile
sets; a bare `cmake -B build` fails the `find_package(SDL3 REQUIRED)`
and leaves a `CMakeCache.txt` with no generated `Makefile` behind it.
Make's rule keys on the cache file, so it then reports the tree as
configured and every target dies with `No rule to make target
'Makefile'`. **`rm -rf build` is the fix** — a half-configured tree
cannot be repaired by rebuilding.

**The pre-push rung is `make test && make lint && make validate &&
make flagship`.**
The flagship is deliberately out of CI — no runner has tensor's
compiler — so this local gate is the only thing that keeps the gpud
door from rotting.

## The dev surface

Hidden by construction — environment variables read once at bring-up,
Makefile targets, `sv::probe` — and listed here so nothing is hidden
from the developer too.

| switch | reads as | who reads it |
| --- | --- | --- |
| `SIMVIEW_VVL` | comma list of `1`/`core`, `sync`, `gpuav`, `printf`, `best`, `abort` — the Khronos layer's features by `VK_EXT_layer_settings`, NVRHI's validation wrapper, a debug-utils messenger; `abort` = `std::abort()` on the first validation error from either | `src/platform/Vk.cpp`, `Device.cpp` |
| `SIMVIEW_FRAMES` | quit `Run()` after N loop iterations — a showcase to an exit code | `src/platform/Frame.cpp` |
| `SIMVIEW_WAIT_MS` | bound on every host wait for the device, gpud's included; past it a sentence and exit 1. Every gate runs at 20000; unset = unbounded | `src/platform/Vk.cpp`, `Device.cpp`, `Swapchain.cpp` |
| `SIMVIEW_TIMINGS` | `1`: stamp the frame's graphics sections and gpud's batches (device clock), log a line a second; the probe's `gpu_sections`/`compute_batches` read the last frame | `src/platform/Timing.cpp` |
| `SIMVIEW_TRACE` (CMake) | `ON`: Tracy's client linked, zones and both GPU contexts live — `make trace` → `build-trace/`; `make -C examples/ising trace` for the flagship | `CMakeLists.txt`, `src/core/Trace.h` |
| `GPUD_PROFILE` | `1`: gpud stamps and labels its batches even where the app did not ask (`SIMVIEW_TIMINGS` asks) | gpud |
| `SIMVIEW_PRESENT` | `fifo` / `immediate`, overriding the portability-driver policy | `src/platform/Swapchain.cpp` |
| `SIMVIEW_ONE_QUEUE` | `1`: force the one-shared-queue path, for measuring what a second queue buys | `src/platform/Vk.cpp` |
| `GPUD_LOG` | `1`: gpud says why a device did not come up | gpud |
| `GPUD_SLANGC` | pins (or poisons) the kernel compiler | gpud |
| `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS` | `0`: what Vk.cpp sets through layer settings — needed by hand only for gpud STANDALONE on MoltenVK (4x) | MoltenVK |
| `MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=assert`, `MTL_SHADER_VALIDATION=1` | Metal's own validation — the lifetime oracle; `make validate` runs them | Metal |

`docs/debugging.md` is the recipe book: captures, traces, profiles.


Editor tooling: `.clangd` reads the exported compile DB, and — plain
C++20 — its diagnostics are trustworthy here, unlike the tensor repo;
`examples/xy-gpu/.clangd` is the carve-out (it consumes tensor's
reflection headers, so it suppresses like tensor does). `.clang-format`
is the shared house style (LLVM, 4-space, west const); lint rule (g)
gates format drift, pinned to clang-format major 20 with a NAMED SKIP
otherwise, and `include/.clang-format` turns namespace closers off so
the formatter and the no-comment law (e) cannot fight.

SDL3 comes from a system package or `CMAKE_PREFIX_PATH` (this machine:
`~/Projects/toolchains/sdl3`; the Makefile defaults it). Configure is
cached — option changes need `make clean` first.

## CI

`.github/workflows/ci.yml`: {macOS, Linux, Windows} × (build library +
every in-tree example, lint, the ctest gates). SDL3 is brewed on
macOS and source-built+cached elsewhere; gpud arrives by FetchContent
at configure (network needed there).

**Push in batches, not per commit — GitHub compute minutes are a real
budget.** A three-platform run costs several minutes of Windows and
Linux time each, and the Windows leg is the expensive one (SDL and
gpud both build from source there). So: commit freely and locally,
push when a piece of work is COMPLETE, and let the local rungs — `make
test`, `make lint`, `make flagship`, `make san` — be what catches
mistakes. The concurrency group means a newer push cancels an older
run, so batching also stops paying twice for superseded work. Never
push a series of small commits one at a time just to watch CI.

**The Linux leg draws.** Lavapipe (software Vulkan, from
mesa-vulkan-drivers) gives that runner a real device, so the shot
checks assert pixels there instead of skipping — and where no device
comes up they still SKIP by name.

**Tier 2, `weekly.yml`**: ASan+UBSan and TSan on Linux, scheduled
weekly and runnable on demand — the sanitizer signal without a
per-push bill, and the only place these runtimes work at all given
the dev machine's OS. Every job carries `timeout-minutes`, and ctest
a `--timeout`, so a hang costs minutes and NAMES the test rather than
stalling for the six-hour default.

CI building EVERY example on EVERY push is the rule that keeps
examples alive — vklib's never built the
library and six of its eighteen examples were silently dead.

## Naming

The namespace is `sv`. **Everything a user types is PascalCase with
no underscores** — sugar classes own the clean names (`sv::App`,
`sv::Field`, `sv::Executor`, `sv::Sync<T>`, `sv::Tick`), their methods and the
free functions match (`OnFrame`, `Run`, `Update`, `Device`,
`LastError`), and no user-facing type wears a `Handle` suffix.
`sv::Device(app)` and the producer overload `app.Field(p, desc)`
require a TRUE App — check it first, as with `Raw()`. The
impl lives in `sv::impl`: snake_case free functions over opaque
handles, handle-first (`app_run(App *)`, `field_update`), spelled
only by the sugar, the tests, and a future binding — never by a
consumer. PODs and enums shared by both strata (`Config`,
`FieldDesc`, `Key`) sit in `sv` and are PascalCase. The umbrella is
`<simview/simview.h>`; `gpud.h` is opt-in and never included by
the umbrella.

**Chaining is the idiom.** Every registration returns its handle —
`app.OnKey(...).OnKey(...).OnFrame(...)`,
`app.Panel("controls").Slider(...).Checkbox(...).Button(...)`,
`app.Plot({...}).Line(...)` — so setting up reads as one statement per
subject rather than one per call. Keep a named handle only when
something arrives later: a field updated every frame, or series added
in a loop.

**Build a descriptor with designated initialisers**, never a default
construction followed by a run of assignments: `Desc{.a = x, .b = y}`
says what the value IS in one expression, and a field that is meant to
keep its default is then visibly absent rather than merely forgotten.

**Sugar breathes**: between inline definitions longer than one line
there is always a blank line; one-liners may pack together.
**Examples read in chunks**: blank lines separate the logical stages
(open, state, frame callback, keys, run) — a showcase is read top to
bottom.

**`include/` carries no comments — not one.** The public surface must
explain itself; a header that needs prose needs renaming instead.
Design rationale lives in docs/, implementation commentary in `src/`
(where comments are fine). `tools/lint.sh` rule (e) gates this.

## Layout

```
include/simview/   the installed surface: simview.h (umbrella),
                   Types.h, App.h, Scene.h, Plots.h, Panel.h, Event.h,
                   gpud.h (the door); scene/{Field,Particles,Lines}.h
                   — a kind's public half is ONE file here;
                   World.h + world/Cloud.h the 3D stratum's;
                   sync/Sync.h
third_party/       vendored deps, one dir each, PIN + LICENSE
                   required (README.md is the contract; exempt from
                   the format gate, never from the warning flags)
src/               folded by LAYER, not by stratum (the namespace
                   already carries that): core/ render/ platform/
                   scene/ world/ ui/ door/ sync/ testing/. A feature
                   is one folder; its exported and internal halves are
                   files in it
src/core/App.h     the composed App: each member's type from its own
                   layer's header. Lint rule (j) is the DAG: scene/
                   and the platform state headers never name ui/
src/render/        the bottom of the drawing stack, under BOTH
                   strata and owned by neither: the two devices, a
                   resizable target, how a shader is named. It
                   reaches nothing but core (lint). It exists
                   because a second consumer appeared — the world's
                   shadow map, which is a target with no colour
src/scene/         a kind is ONE file: state, uniform block, shaders,
                   KindOps, and its exported functions. Nothing else
                   names a kind
src/world/         the 3D stratum BESIDE scene, not inside it: a pass
                   table, items that submit draws, one sort, reverse-Z
                   depth, the camera. An item is ONE file, the same
                   shape as a kind. It shares render/ with scene/
                   and NOTHING else — neither stratum may name the
                   other (lint). A pass is also a TIMING section, so
                   a frame says where its milliseconds went.
                   docs/3d-plan.md is the decision record
src/testing/       sv::probe — the ONLY test-only code, in its own
                   never-installed archive (lint rule (i))
examples/hello/    init headless, report, quit; doubles as the
                   install-check consumer
tests/harness/     the assertion and image vocabulary (Check, Bmp,
                   Harness, Palette)
tests/fakes/       backends a display would otherwise be needed for
tests/probe/       the declared test surface
tests/checks/      the checks themselves
tests/installed_surface/   the clean-surface gate
tools/lint.sh      the hygiene gate
docs/architecture.md   the structure: layers, patterns, the test
                   boundary, and the refactor sequence
docs/design.md     the decision record — history, not architecture
```
