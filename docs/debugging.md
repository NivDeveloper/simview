# Debugging, validation and profiling — the recipe book

Every switch here is an environment variable, a Makefile target or a
tool outside the build; none reaches `include/simview/`. The table of
switches is in CLAUDE.md ("The dev surface"). This file is what to run
when something is wrong, each one a command that was actually used.

## Validation — is the API use correct?

| question | command |
| --- | --- |
| everything, to an exit code | `make validate` (suite + showcases, Khronos sync validation, then Metal's layers) |
| one check, validated | `SIMVIEW_VVL=sync,abort ./build/tests/viewport_check` |
| a showcase, validated, bounded | `SIMVIEW_VVL=sync,abort SIMVIEW_FRAMES=120 ./build/examples/gas/gas` |
| out-of-range device-address reads (kernels) | `SIMVIEW_VVL=gpuav,abort …` — 10× slower, exclusive with printf |
| shader printf | `SIMVIEW_VVL=printf …` — messages arrive as `validation[info]` |
| best-practices hints | `SIMVIEW_VVL=best …` — noisy; read once per change of shape |
| **a freeze** — the window stops, a check never returns | `SIMVIEW_WAIT_MS=5000 ./…` — the wait that never returns names itself (what it waited for, graphics instance and compute ticket waited vs completed) and the process exits; every gate already runs at 20 s. On MoltenVK a freeze is instead `VK_ERROR_DEVICE_LOST` after ~8 s, and its sentence names that too |
| **a GPU-side lifetime bug** (device lost, "Invalid Resource") | `MTL_DEBUG_LAYER=1 MTL_DEBUG_LAYER_ERROR_MODE=nslog ./…` — Metal names the object "destroyed while still required by the command buffer"; no Vulkan-level tool can |
| GPU-side bounds | `MTL_SHADER_VALIDATION=1 ./…` |
| MoltenVK's own diagnosis | `MVK_CONFIG_LOG_LEVEL=3 ./…` (errors + warnings), `MVK_CONFIG_TRACE_VULKAN_CALLS=1` |

What the Khronos layer cannot see: gpud's compute writes — its ABI
passes device addresses in push constants, and synchronization
validation tracks descriptors. The cross-queue edge is gated by
`decouple_check` (a frame showing anything but its generation's
value fails it), not by a layer — and on MoltenVK that edge did not
open even with the wait deleted and 8 ms dispatches; a concurrent
driver is where the check earns its keep. Known: VVL 1.4.304
intermittently crashes in `QueueBatchContext::RegisterAsyncContexts`
on the two-queue flagship under `sync` (2 of 3 runs); core validation
(`SIMVIEW_VVL=1`) runs it every time. Cost on the flagship, 8 s
windowed: none 25.7k, core 22.0k, sync 16.5k sweeps/s.

## Profiling — where does the time go?

| question | command |
| --- | --- |
| **the numbers, no tool**: ms/frame, ms per graphics section, compute batches and busy % | `SIMVIEW_TIMINGS=1 ./build/examples/gas/gas` — one line a second; `timing_check` is the same probe as a gate |
| **the flame graph** — frame thread, sim thread, graphics queue and compute queue on one timeline | `make trace`, run `./build-trace/examples/<x>/<x>`, then `~/Projects/toolchains/tracy/bin/tracy-profiler` → Connect (the client waits for it; nothing is recorded before). The flagship: `make -C examples/ising trace && ./examples/ising/build-trace/ising` |
| the same, to a file | `tracy-capture -o run.tracy -s 10` beside the running program, then `tracy-profiler run.tracy`; `tracy-csvexport -g run.tracy` lists the GPU zones as text |
| where a thread sits (what found the IMMEDIATE stall) | `sample <pid> 2 -file out.txt`, then read the thread's subtree |
| both GPU queues on one timeline | `xcrun xctrace record --template 'Metal System Trace' --launch -- ./build/examples/gas/gas` |
| a frame's GPU commands, in Xcode | `MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE=1 MVK_CONFIG_AUTO_GPU_CAPTURE_OUTPUT_FILE=frame.gputrace ./…` (1 = frame, 2 = device) |
| MoltenVK's per-activity cost (shader compile, pipeline, submit) | `MVK_CONFIG_PERFORMANCE_TRACKING=1 MVK_CONFIG_ACTIVITY_PERFORMANCE_LOGGING_STYLE=2 ./…` |
| the generated MSL | `MVK_CONFIG_SHADER_DUMP_DIR=/tmp/msl ./…` |
| the sim rate beside its window | the isingfps probe (scratch): `isingfps 0 shared` prints 500 ms buckets — repeat 3×, the spread is ±20% |

What Tracy shows here: the main thread's zones (poll, wait previous
frame, flip, frame callbacks, build, render → acquire, views, scene,
ui, finish, viewports), the sim thread's `step` (the Executor) and
`publish` (the Sync), the "graphics" GPU context with a zone per
section, and the "compute (gpud)" context with a zone per batch, named
by dispatch count. Sampling is not available on macOS, so the sim
thread's own work shows as the gap between its `step` zones — put a
`ZoneScopedN` inside a step to see more (the example must include
`tracy/Tracy.hpp` itself; the library's zones stay in the library).
The profiler binary must match the client's version, 0.13.1: brew
ships no bottle for macOS 27, so it was built from source into
`~/Projects/toolchains/tracy-0.13.1` (`git clone -b v0.13.1`, cmake
the `profiler`, `capture` and `csvexport` folders; glfw and freetype
from brew, capstone fetched).

## Capture and replay — hand a driver bug to someone

`VK_LOADER_LAYERS_ENABLE='*gfxreconstruct*' ./…` writes a `.gfxr`;
`gfxrecon-replay file.gfxr` replays it (installed with the SDK).
Traces are not portable across platforms; a MoltenVK trace replays on
MoltenVK.

## Not on this platform

RenderDoc has no macOS/MoltenVK target; it is the frame debugger for
the Linux and Windows legs. NVIDIA's Aftermath (NVRHI carries the
integration) and Nsight are for the CUDA/HPC path when it arrives.
