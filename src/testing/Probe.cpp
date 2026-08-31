// The test surface, and nothing else.
//
// Everything here exists so a check can ask a question a consumer has
// no business asking. It is compiled into its OWN archive
// (simview_probe), built only under SIMVIEW_BUILD_TESTS and never
// installed — so the boundary is a link boundary, not a naming
// convention, and "is any of this in a release build?" is answered by
// looking for a file rather than by trusting a header not to declare
// it.
//
// The rule that keeps it honest: test-only exports live in sv::probe,
// never sv::impl. One grep answers what exists only for tests, and
// tools/lint.sh refuses the namespace anywhere but here and tests/.

#include "../core/App.h"

#include "../../tests/probe/Probe.h"

namespace sv {
namespace probe {

ImGuiContext *ui_context(impl::App *a) { return a ? a->ui.ctx : nullptr; }

ImPlotContext *plot_context(impl::App *a) { return a ? a->ui.plot : nullptr; }

ImPlot3DContext *plot3d_context(impl::App *a) {
    return a ? a->ui.plot3d : nullptr;
}

std::size_t gate_count(impl::App *a) { return a ? a->gates.size() : 0; }

void *render_device(impl::App *a) {
    return a ? static_cast<void *>(a->platform.ndev.Get()) : nullptr;
}

Extent2 view_extent(impl::App *a, const char *title) {
    if (!a || !title)
        return {};
    for (const impl::View &v : a->views)
        if (v.title == title)
            return {v.target.w, v.target.h};
    return {};
}

} // namespace probe
} // namespace sv
