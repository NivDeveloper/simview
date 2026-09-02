#pragma once

// What a TEST may reach that a consumer may not.
//
// These live in sv::probe, never sv::impl, and they are compiled into
// simview_probe — an archive built only under SIMVIEW_BUILD_TESTS and
// never installed. A release build does not contain them, which
// tests/installed_surface proves rather than assumes.
//
// The context types stay opaque here so a check can assert which
// context is current without any of it reaching the public surface.
// Tests are not consumers.

#include <simview/Types.h>

#include <cstddef>
#include <cstdint>

struct ImGuiContext;
struct ImPlotContext;
struct ImPlot3DContext;

namespace sv {
namespace probe {

ImGuiContext *ui_context(impl::App *);
ImPlotContext *plot_context(impl::App *);
ImPlot3DContext *plot3d_context(impl::App *);

// The size a view's texture currently holds — how a check asks
// whether the panel's room reached the render target.
Extent2 view_extent(impl::App *, const char *title);

// How many Syncs the frame flips — one per Sync however many scenes
// draw it.
std::size_t gate_count(impl::App *);

// The renderer device (an nvrhi::IDevice*), for fakes that draw
// offscreen the way a real backend would. Opaque here so the probe
// header stays SDK-free.
void *render_device(impl::App *);

// A test that submits on the graphics queue itself (the viewport fake)
// brackets exactly as the frame does: no-ops unless the driver gave
// the app and gpud one queue.
void queue_lock(impl::App *);
void queue_unlock(impl::App *);

// The validation tally — errors from the Khronos layer and NVRHI's
// wrapper, process-wide — and whether SIMVIEW_VVL turned them on.
bool validation_on(impl::App *);
std::size_t validation_errors(impl::App *);

// Where the time went (SIMVIEW_TIMINGS=1): the last collected frame's
// graphics sections, and the compute batches gpud had completed by
// that collect — both on the device clock. Plain records, so the probe
// stays SDK-free.
struct GpuSection {
    const char *name;
    std::uint64_t begin_ns, end_ns;
};
struct ComputeBatch {
    std::uint64_t first, last; // tickets
    std::uint32_t dispatches;
    std::uint64_t begin_ns, end_ns;
};
bool timings_on(impl::App *);
std::size_t gpu_sections(impl::App *, GpuSection *out, std::size_t cap);
std::size_t compute_batches(impl::App *, ComputeBatch *out, std::size_t cap);

// Synthetic pointer input, delivered where a backend delivers the
// real thing: into the UI's own event queue, to be consumed by the
// next frame the app builds. A headless app runs no backend, so
// nothing overwrites these — which is what makes a gesture testable
// without a window, a cursor or a person.
//
// One frame per call is the unit: press, then move, then release,
// each with a Step between, is what a drag IS to the layer under test.
void mouse_move(impl::App *, float x, float y);
void mouse_button(impl::App *, int button, bool down);
void mouse_wheel(impl::App *, float dy);
void mouse_modifier_shift(impl::App *, bool down);

// The built-in meshes a world has had to make, newest last, as their
// triangle counts. A tier is chosen from how many instances there
// are, so this is how a check asks whether that rule fired without
// timing anything.
void culling(impl::App *, bool on);

std::size_t item_triangles(impl::App *, const char *title, std::uint64_t *out,
                           std::size_t cap);

std::size_t mesh_tiers(impl::App *, const char *title, unsigned *triangles,
                       std::size_t cap);

// Where a world's camera is now — what an input check asserts against,
// since a picture can only say something moved. `title` names a
// world shown in a panel; null asks for the one in the window.
struct CameraState {
    float focus[3];
    float distance;
    float forward[3];
    float up[3];
    bool orthographic;
};
bool camera_of(impl::App *, const char *title, CameraState *out);

// A plot's toolbar as the plot itself sees it: whether a fit control
// was offered on the last draw, whether a click on it is still waiting
// to be consumed, and whether the window is showing at all. A picture
// can say a button is there; only this says which button it is.
struct PlotTools {
    bool fit_offered;
    bool fit_pending;
    bool open;
    // What the chrome left for the data on the last draw. A bar costs
    // height, a rail costs width, a menu costs neither — which makes
    // the arrangement something a check can read rather than admire.
    float canvas_w;
    float canvas_h;
    bool legend;
    bool grid;
};
bool plot_tools(impl::App *, const char *title, PlotTools *out);

// Flip a setting the CHROME owns, the way its menu does — so a check
// can prove the setting reaches the plot without hunting a popup entry
// whose position is a layout detail.
void plot_show_legend(impl::App *, const char *title, bool on);

// The world's corner controls, driven the way its menu drives them.
// `preset` indexes the SAME table the menu loops, so an entry that
// duplicated its neighbour fails a check about the table rather than
// about the check's idea of what the entries are. `what` is 0 for the
// grid and 1 for the axes. `title` names a world in a panel; null asks
// for the one in the window.
bool world_preset(impl::App *, const char *title, int preset);
bool world_show(impl::App *, const char *title, int what, bool on);
std::size_t world_preset_count();

// Make the next graphics submission wait GPU-side on a compute-timeline
// value nothing will ever signal — the hang a deleted pump or a stamp
// past what compute will reach would cause — so a check can prove the
// bounded wait turns it into a sentence. The App is unusable after.
void stall_frame(impl::App *);

} // namespace probe
} // namespace sv
