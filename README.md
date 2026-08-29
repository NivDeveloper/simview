# simview

A window on a running numerical simulation: fields, particles, and
live plots over SDL3, with a sim/render sync layer that keeps the two
honest. The public surface is dependency-free — any C++20 compiler,
`#include <simview/simview.h>`, link `libsimview` — and a grep-clean
seam keeps it that way (docs/design.md is the architecture record).

**Status: Move 1.** The constitution, the enforcement gates, and a
minimal seam (App init/quit, headless device bring-up). No window
loop, no views yet — they arrive in Move 2. The v0 zero-copy XY demo
lives in `attic/` until Move 3 rebuilds it as a proper example.

## Build

```sh
make            # needs SDL3 (system package, or
                #   CMAKE_PREFIX_PATH=~/Projects/toolchains/sdl3)
make test       # + make lint, make install-check — the gates
make run        # examples/hello: init headless, report, quit
```
