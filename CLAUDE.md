# simview — conventions

Lightweight cross-platform sim visualization over SDL3. C++20, the
SYSTEM compiler (AppleClang/gcc/MSVC — deliberately NOT tensor's
g++-16/reflection world). Design rationale and roadmap: docs/design.md.
Current state: the engine builds ON gpud (the sibling projects' GPU
interchange): gpud::sdl::try_open owns device bring-up, and the one
opt-in door — include/simview/gpud.h — carries sv::Device(app) and
the pull-model field (app.Field(producer, desc) resolves gpud's
source_of protocol by ADL; the engine re-asks the source at every
draw, so rebind does not exist). Examples: ising-cpu (plain arrays +
the sync layer; the independence canary) and xy-gpu (the pull model,
a standalone subproject). Next: widgets + plots over vendored
ImGui/ImPlot behind the one UI boundary.

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
  | `include/simview/*.h` (core) | each other + std. Nothing else, ever |
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
  in `emit_series<T>`, one builder method. The scalar is erased once,
  so float and double cost nothing per kind. If a kind ever costs
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
- **A test is assertions and nothing else.** `tests/headless/` gives
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
  tests/headless/. And a showcase never prints — no stdout, no logs
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
`sv::Field`, `sv::Executor`, `sv::Channel<T>`), their methods and the
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
                   Types.h, App.h, Plots.h, Panel.h, gpud.h (the door)
third_party/       vendored deps, one dir each, PIN + LICENSE
                   required (README.md is the contract; exempt from
                   the format gate, never from the warning flags)
src/impl/          exported function definitions, one .cpp per header
src/engine/        the SDL side — internal headers live here, never
                   installed
examples/hello/    init headless, report, quit; doubles as the
                   install-check consumer
tests/installed_surface/   the clean-surface gate
tools/lint.sh      the hygiene gate
docs/design.md     the architecture record
```
