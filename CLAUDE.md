# simview — conventions

Lightweight cross-platform sim visualization over SDL3. C++20, the
SYSTEM compiler (AppleClang/gcc/MSVC — deliberately NOT tensor's
g++-16/reflection world). Structure and patterns: docs/architecture.md;
how it got there: docs/design.md; what is next: docs/roadmap.md.
Current state: the engine builds ON gpud (the sibling projects' GPU
interchange): gpud::sdl::try_open owns device bring-up, and the one
opt-in door — include/simview/gpud.h — carries sv::Device(app) and
the pull-model field (app.Field(producer, desc) resolves gpud's
source_of protocol by ADL; the engine re-asks the source at every
draw, so rebind does not exist — `app.Particles(producer)` is the
same protocol for a point cloud, whose count comes from the buffer's
size). The UI is ImGui + ImPlot, hash-pinned through FetchContent
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
  GPU abstraction the sibling projects share, and the ENGINE builds
  on it: gpud::sdl owns device bring-up (the Vulkan-loader hint, the
  SPIR-V format policy), libsimview links gpud::sdl PRIVATE and
  gpud::gpud PUBLIC (include dirs for the door). Consumers of the
  core surface still see neither library. Data crosses the impl as
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
  restore. A check that never fired is a comment.
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
  ImGui composites over it in a second LOAD pass; the dockspace's
  central node is passthru. `ui_on()` is false until a panel is
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
  pipelines, draws. Behaviour a timing test could only guess at
  becomes an exact assertion: an unchanged field uploads ONCE, a
  second shot re-uploads nothing, one target format makes one
  pipeline.
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
| **sanitizers** | `make san` / `make tsan` — ASan+UBSan over the suite, TSan over sync_check. **Neither runs on macOS 26+**: the AppleClang sanitizer runtime spins in `get_dyld_hdr()` during its own startup, before `main` — a runtime-vs-OS break, nothing to do with this code. `.github/workflows/weekly.yml` runs both on Linux |
| **the flagship** | `make flagship` — builds examples/xy-gpu (needs g++-16); NO runner reaches it, so this is the local gate that keeps the door from rotting |
| clean | `make clean` (build, build-san, build-tsan) |

**Configure through `make`, never `cmake -B build` by hand.** SDL3 is
found from `CMAKE_PREFIX_PATH=$(PREFIX)`, which only the Makefile
sets; a bare `cmake -B build` fails the `find_package(SDL3 REQUIRED)`
and leaves a `CMakeCache.txt` with no generated `Makefile` behind it.
Make's rule keys on the cache file, so it then reports the tree as
configured and every target dies with `No rule to make target
'Makefile'`. **`rm -rf build` is the fix** — a half-configured tree
cannot be repaired by rebuilding.

**The pre-push rung is `make test && make lint && make flagship`.**
The flagship is deliberately out of CI — no runner has tensor's
compiler — so this local gate is the only thing that keeps the gpud
door from rotting.

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
`sv::Field`, `sv::Executor`, `sv::Channel<T>`, `sv::Tick`), their methods and the
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
                   — a kind's public half is ONE file here; sync/Sync.h
third_party/       vendored deps, one dir each, PIN + LICENSE
                   required (README.md is the contract; exempt from
                   the format gate, never from the warning flags)
src/               folded by LAYER, not by stratum (the namespace
                   already carries that): core/ platform/ scene/ ui/
                   door/ sync/ testing/. A feature is one folder;
                   its exported and internal halves are files in it
src/core/App.h     the composed App: each member's type from its own
                   layer's header. Lint rule (j) is the DAG: scene/
                   and the platform state headers never name ui/
src/scene/         a kind is ONE file: state, uniform block, shaders,
                   KindOps, and its exported functions. Nothing else
                   names a kind
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
