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

#include "../core/Engine.h"

#include "../../tests/probe/Probe.h"

namespace sv {
namespace probe {

ImGuiContext *ui_context(impl::App *a) { return a ? a->ui.ctx : nullptr; }

ImPlotContext *plot_context(impl::App *a) { return a ? a->ui.plot : nullptr; }

Extent2 view_extent(impl::App *a, const char *title) {
    if (!a || !title)
        return {};
    for (const impl::App::ViewState &v : a->views)
        if (v.title == title)
            return {v.w, v.h};
    return {};
}

} // namespace probe
} // namespace sv
