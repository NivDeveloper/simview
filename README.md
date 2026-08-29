# simview

A window on a running numerical simulation: fields, particles, and
live plots over SDL3, with a sim/render sync layer that keeps the two
honest. The public surface is dependency-free — any C++20 compiler,
`#include <simview/simview.h>`, link `libsimview` — and a grep-clean
seam keeps it that way (docs/design.md is the architecture record).

**Status: Move 2.** The walking skeleton: a real window loop
(register-then-run), the Event/Key vocabulary (USB-HID numbering),
the host-path `Field` (a 2-D grid colormapped by committed-bytecode
shaders — Gray/Hue/Viridis), and headless capture as API (`step()` +
`shot()` — what CI runs). One field per App, f32, SPIR-V shaders (SDL's
Vulkan driver on macOS; native MSL/DXIL are one named follow-up). Next,
Move 3: the founding examples and the sim/render sync layer. The v0
zero-copy XY demo lives in `attic/` until then.

## Build

```sh
make            # needs SDL3 (system package, or
                #   CMAKE_PREFIX_PATH=~/Projects/toolchains/sdl3)
make test       # + make lint, make install-check — the gates
make run        # examples/hello: init headless, report, quit
./build/examples/hello-window/hello-window            # the plasma, windowed
./build/examples/hello-window/hello-window --frames 240 out.bmp  # headless
```
