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

**1. The ImGui layer** (shipped). ImGui + both official backends, one
context per App, and a dockspace with a PASSTHRU central node — so the
scene never leaves the swapchain and ImGui composites over it in a
second pass. No panels means a window identical to before; floating
panels are overlays; docked panels frame the scene. Panels the user
writes came forward from item 4, because without them there is nothing
to dock. Viewports enabled only when the backends themselves claim the
capability.

What this move does NOT do, deliberately: render a scene into a
texture to put it inside a dockable panel. That is only needed to tear
a VIEW out or show two at once, and it is also FORCED to wait —
ImGui's SDL_GPU backend builds one pipeline for one colour format,
while `app_shot` renders into its own, so UI can never reach a shot
until that move lands. Which is why the UI's verification is honest
about being manual: no runner has a display, and no shot can contain a
panel.

**2. Plots and panels** (shipped). The series architecture — a POD
carrying kind + source + style, one axis struct, retained handles that
work in a loop with no destructor tricks — plus ImPlot and `Line`. And
the widget builder beside it (`Text`/`Separator`/`Slider`/`Checkbox`/
`Button`, binding by reference), so **no caller writes ImGui**: the
builder is the API, exactly as the predecessor's was, and the raw door
is only the escape hatch for what the vocabulary does not model.

**3. The array-shaped plot kinds** (shipped). The prediction was
recorded BEFORE the work: Scatter and Stairs at three sites each,
Heatmap at about five. The measurement: Stairs cost three. Heatmap
cost the colourbar bracket plus one `Range2` field — five. Every
scalar-tailed kind (Bars, Stems, Shaded) rode a shared `param[0]` and
paid nothing in the descriptor. Eleven 2D kinds in four files, and the
one deviation named rather than absorbed: `apply_flags` is a second
switch, because horizontal is a flag on the SPEC, not a data slot.
Five for structural reasons rather than eighteen for architectural
ones is the architecture vindicated.

**4. The scene is a list** (shipped). What is drawn to the swapchain is
N items, not one welded-in field: `scene_draw` prepares them, begins
ONE pass, and `item_draw` switches on kind. The clear moved from the
item to the scene, because an item that cleared would erase whatever
came before it. `Particles` is the second kind — instanced quads,
sharing the scene's 2-D range and its single aspect-fit so a point
lands on the cell it belongs to.

**5. Views in panels** (shipped). A scene whose target is a texture,
shown by ImGui::Image — a view docks, tabs and tears out like any
other panel, and `gas` is the example that arranges a whole window
out of them.

 N panels rather than one field per App;
the refusal that exists today becomes a list.

**6. 3D.** ImPlot3D for plot-shaped 3D (shipped: `app.Plot3D()` as a
sibling builder over the same impl — one draw path, the family
switched at the bracket; Line, Scatter, Surface, Mesh, each proven by
geometry and the family mismatch refused by name). Still to come, for
particle/volume work — the one genuinely excellent piece of vklib
worth stealing: its BDA-addressed particle path with Slang shaders,
zero readback.

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
