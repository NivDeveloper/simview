# simview roadmap

What ships next, why it is shaped that way, and what the predecessor
(vklib) taught. `docs/design.md` is the architecture record; this file
is the sequence.

## The lesson that shapes everything

vklib hand-built ~600 lines of parallel window machinery (`AuxWindow`,
`WindowKind`, an "active window" global mode mirrored into the Vulkan
context) to stand in for one ImGui feature it could not enable. The
diagnosis, from reading the source:

- **Docking was never the problem.** vklib pinned `v1.92.7-docking`,
  set `ImGuiConfigFlags_DockingEnable`, and submitted real dockspaces.
- **Multi-viewport was.** It had no ImGui *platform* backend at all —
  both official backends were dropped when a custom renderer landed —
  yet it still advertised `PlatformHasViewports | RendererHasViewports`.
  Those flags bypass ImGui's own capability guard, so enabling
  viewports dove straight into null function pointers: a mysterious
  crash rather than a clean refusal.
- **Then docking was smothered anyway**: every panel it drew set
  `NoDocking` and pinned itself fullscreen over the dockspace.

simview is on SDL_GPU, where `imgui_impl_sdl3` and
`imgui_impl_sdlgpu3` are official, maintained, and support
multi-viewport (the SDL_GPU backend creates secondary swapchains
itself). **So docking, tear-out into real OS windows, per-monitor DPI
and input routing arrive as configuration, not as code we own.**

Three asks collapse into one mechanism: multi-window becomes "drag a
panel out"; plot windows become "a plot panel dragged out"; tabbed
plot groups become "dock two panels into one node". The placement of a
view stops being a build-time decision (vklib's fatal `app->Plot` vs
`ctx.Plot` split — same spelling, two lifetimes, one silently
discarding the work) and becomes a runtime drag that persists in the
ini.

## Decisions taken from vklib's evidence

| vklib | what it cost | simview |
| --- | --- | --- |
| Two plot renderers (main window, aux window) | Types silently render as NOTHING in the wrong one — a `break;` per gap | ONE draw path for a panel, wherever the panel lives |
| Adding a plot type = 18 edit sites (+8 for settings, so Heatmap has none) | Extension stopped happening | A series is a POD + one enum + one case + one sugar method: THREE sites |
| A tagged union with a reference member | Three parallel side-tables, keyed by index | Plain PODs; behaviour arrives as a source callback |
| Plot identity = a frame ordinal, reset per frame | Conditionally emitting a plot swapped every later plot's saved config | Identity is the NAME |
| Data = a raw pointer read at draw | Stale pointers; a real data race (a background thread called the user's function while the main thread mutated its captures) | The PULL idiom already used for fields: the source is asked at draw time, on the render thread |
| Five per-type style structs; axis policy hardcoded per type | Every `LineData` got axis labels "time" and "μ"; log scales never existed | One axis struct, one style struct, shared by every series kind |
| 281 lines of hand-written sscanf/appendf persistence | Two lines to hand-write per new field, so fields stopped being added | ImGui's own ini for layout; a field table if per-series settings ever need saving |
| Per-window ImGui context | Fonts and atlas re-loaded per window; input routed by "whoever called setCurrent last"; `WantCaptureMouse` read from the wrong window | ONE context, always |

## The sequence

**1. The ImGui foundation** (next; plan below in this file's sibling
notes). ImGui + the two official backends, one context, the field
rendered into a texture and shown as a dockable panel, a dockspace,
and viewports enabled — proven by tearing a panel into its own OS
window. The field stops owning the swapchain; `Shot(field)` keeps the
pixel tests chrome-free.

**2. Plots, and the first type.** The series architecture — `Series`
POD (kind + source + style), one axis struct, retained handles that
work in a loop with no destructor tricks — plus ImPlot and `Line`.

**3. The second and third plot types.** Scatter and Heatmap, as the
PROOF that a type costs three sites. If either costs more, the
architecture is wrong and this is where we learn it, not after ten
types.

**4. Panels the user writes.** The opt-in door (the `gpud.h` pattern)
exposing raw ImGui for custom panels — vklib had no escape hatch at
all, so anything the wrapper did not model was unreachable.

**5. More than one field.** N panels rather than one field per App;
the refusal that exists today becomes a list.

**6. 3D.** ImPlot3D for plot-shaped 3D, and — for particle/volume
work — the one genuinely excellent piece of vklib worth stealing: its
BDA-addressed particle path with Slang shaders, zero readback.

## Non-goals

- **We do not build multi-window.** Viewports own secondary OS
  windows. If viewports prove unworkable on a platform, the fallback
  is docking-only inside one window — not a hand-rolled window stack.
- **No second draw path** for anything. vklib's defining failure was
  two renderers drifting apart, and it happened three separate times
  (plots, shader views, controls).
- **No build-time placement choice.** A view is a panel; where it
  lives is the user's drag, saved in the ini.
- **No hand-written persistence per field.**

## Dependency policy note

`third_party/` was set up expecting vendored sources. After reading
vklib — which began with a vendored ImGui tree and deliberately moved
to a pinned FetchContent — ImGui and ImPlot arrive the same way gpud
does: FetchContent, pinned by hash, one place. The `third_party/`
contract (PIN + LICENSE per directory, lint rule (h)) stands for
anything genuinely copied in, and stays empty until something is.
