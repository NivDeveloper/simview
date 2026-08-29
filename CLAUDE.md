# simview — conventions

Lightweight cross-platform sim visualization over SDL3. C++20, the
SYSTEM compiler (AppleClang/gcc/MSVC — deliberately NOT tensor's
g++-16/reflection world). Design rationale and roadmap: docs/design.md.
Current state: Move 1 — constitution, gates, minimal seam; no features
yet (no Field, no window loop; they arrive in Move 2).

## Invariants (do not break)

- **The two-strata surface.** The SEAM is every exported symbol: free
  functions over opaque handles, taking/returning only handles, PODs,
  enums, `const char *`, primitives, and fn-ptr+`void *` pairs — never
  a template, never a std type in a signature. The SUGAR is inline
  code in the same headers (methods on handles, builders): it may use
  std freely and must lower ONLY onto seam calls, no logic of its own.
  Reader convention: seam = free functions, sugar = inline methods.
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
  seam as pointer+stride or (native.h) `SDL_GPUBuffer *`. Internal
  shaders are committed bytecode for all three formats; regeneration
  is a dev-only script.
- **The seam never throws.** Null handle / `false` + `last_error()`.
- **Headless is first-class.** Every view must render offscreen
  byte-identically to onscreen.
- **A gate must be broken once when added** — watch it go red, then
  restore. A check that never fired is a comment.
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

Handle and POD types PascalCase (`App`, `FieldDesc`); seam free
functions snake_case, handle-first (`app_run(App)`, `field_update`);
sugar methods snake_case. The umbrella is `<simview/simview.h>`;
`native.h` is opt-in and never included by the umbrella.

## Layout

```
include/simview/   the installed surface: simview.h (umbrella),
                   Types.h, App.h, native.h (the carve-out)
src/seam/          exported function definitions, one .cpp per header
src/engine/        the SDL side — internal headers live here, never
                   installed
examples/hello/    init headless, report, quit; doubles as the
                   install-check consumer
tests/installed_surface/   the clean-surface gate
tools/lint.sh      the hygiene gate
attic/             out of the build, kept in history (the v0 demo)
docs/design.md     the architecture record
```
