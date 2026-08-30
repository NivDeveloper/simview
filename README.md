# simview

A window on a running numerical simulation: fields, particles, and
live plots over SDL3, with a sim/render sync layer that keeps the two
honest. The public surface is dependency-free — any C++20 compiler,
`#include <simview/simview.h>`, link `libsimview` — and a grep-clean
impl/sugar split keeps it that way (docs/architecture.md is the
structure; docs/design.md is how it got there).

**Status: Move 3.** The walking skeleton: a real window loop
(register-then-run), the Event/Key vocabulary (USB-HID numbering),
the host-path `Field` (a 2-D grid colormapped by committed-bytecode
shaders — Gray/Hue/Viridis), and headless capture as API (`step()` +
`shot()` — what CI runs). One field per App, f32, SPIR-V shaders (SDL's
Vulkan driver on macOS; native MSL/DXIL are one named follow-up).

The examples are the showcase: `hello-window` (an animated field in
~45 lines), `ising-cpu` (a threaded sim through the Executor/Channel
sync layer, plain C++ arrays — simview's independence proof, with the
lattice on the window and panels floating over it), `gas` (2000
particles in the window itself, the same particles in phase space in
a panel, a live histogram and the controls — both arrangements at
once), and `xy-gpu` (a GPU sim drawn zero-copy through the
gpud door, a standalone subproject: one device, a pull-model field,
no per-frame glue), and `plots` (the gallery: one panel per series
kind, all fifteen — eleven 2D and the four 3D — so each can be
looked at). Verification lives in tests/, never in examples.

Next: 3-D scene kinds, and more than one window.

## Build

```sh
make            # needs SDL3 (system package, or
                #   CMAKE_PREFIX_PATH=~/Projects/toolchains/sdl3)
make test       # + make lint, make install-check — the gates
make run        # examples/hello: init headless, report, quit
./build/examples/hello-window/hello-window            # the plasma, windowed
./build/examples/hello-window/hello-window --frames 240 out.bmp  # headless
```
