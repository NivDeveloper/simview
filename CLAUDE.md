# simview — conventions

Lightweight cross-platform sim visualization over SDL3. C++20, the
SYSTEM compiler (AppleClang/gcc/MSVC — deliberately NOT tensor's
g++-16/reflection world). Design rationale and roadmap: docs/design.md.
Current state: Move 3 — the sync layer (Executor + Channel, the
inherited triple-buffer design) and the founding examples: ising-cpu
(plain arrays + the sync layer; the independence canary) and xy-gpu
(zero-copy Mode B, a standalone subproject). Field grew the external
mode (field_from_buffer/rebind in native.h). Next: widgets + plots
over vendored ImGui/ImPlot behind the one UI boundary.

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
  | `include/simview/native.h` | core + std; may FORWARD-DECLARE SDL types; includes nothing of SDL's |
  | `src/**` | anything (SDL, later ImGui/ImPlot) — never installed |
  | a core consumer | `<simview/simview.h>` + libsimview; no SDL anywhere |

- **SDL3 is PRIVATE.** It appears in exactly one public header
  (native.h) as forward declarations — the deliberate carve-out for
  zero-copy interop and device sharing. Including native.h is the
  consumer declaring "SDL is my dependency too".
- **No tensor, no gpud, no shader toolchain — ever.** Data crosses the
  impl as pointer+stride or (native.h) `SDL_GPUBuffer *`. Internal
  shaders are committed bytecode for all three formats; regeneration
  is a dev-only script.
- **The impl never throws — and it reports itself.** A refusal is a
  null handle / `false`, with its sentence logged at the refusal site
  (SDL's error log). `LastError()` is programmatic access for tests
  and tooling; a consumer never NEEDS it, and examples never call it.
- **Headless is first-class.** Every view must render offscreen
  byte-identically to onscreen.
- **A gate must be broken once when added** — watch it go red, then
  restore. A check that never fired is a comment.
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
| clean | `make clean` |

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
every example, run hello, lint, install-check). SDL3 is brewed on
macOS and source-built+cached elsewhere. The Windows leg is
continue-on-error until proven. CI building EVERY example on EVERY
push is the rule that keeps examples alive — vklib's never built the
library and six of its eighteen examples were silently dead.

## Naming

The namespace is `sv`. **Everything a user types is PascalCase with
no underscores** — sugar classes own the clean names (`sv::App`,
`sv::Field`, `sv::Executor`, `sv::Channel<T>`), their methods and the
free functions match (`OnFrame`, `Run`, `Update`, `NativeDevice`,
`LastError`), and no user-facing type wears a `Handle` suffix. The
impl lives in `sv::impl`: snake_case free functions over opaque
handles, handle-first (`app_run(App *)`, `field_update`), spelled
only by the sugar, the tests, and a future binding — never by a
consumer. PODs and enums shared by both strata (`Config`,
`FieldDesc`, `Key`) sit in `sv` and are PascalCase. The umbrella is
`<simview/simview.h>`; `native.h` is opt-in and never included by
the umbrella.

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
                   Types.h, App.h, native.h (the carve-out)
src/impl/          exported function definitions, one .cpp per header
src/engine/        the SDL side — internal headers live here, never
                   installed
examples/hello/    init headless, report, quit; doubles as the
                   install-check consumer
tests/installed_surface/   the clean-surface gate
tools/lint.sh      the hygiene gate
docs/design.md     the architecture record
```
