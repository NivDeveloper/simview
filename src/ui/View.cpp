// A view: a scene whose target is a texture, shown by a panel. The
// panel's draw lives in Context.cpp beside the other ImGui code; what
// is here is the registration, which is what makes a view an item in
// the UI's title namespace like a plot or a panel.

#include "View.h"

#include "../core/App.h"
#include "Ui.h"

#include <simview/simview.h>

#include <string>

namespace sv {
namespace impl {

Scene view_create(App *a, const ViewDesc &d) {
    if (!a)
        return {};
    if (!d.title || !*d.title)
        return set_error("a view needs a title — it names the panel it "
                         "is shown in"),
               Scene{};
    if (title_taken(a, d.title))
        return set_error(std::string("\"") + d.title +
                         "\" is already the title of a plot, panel or "
                         "view, and two windows of one name draw into "
                         "each other"),
               Scene{};

    View &v = a->views.emplace_back();
    v.title = d.title;
    v.app = a;
    v.scene.gpu = a->scene.gpu;
    v.scene.stats = &a->stats;
    v.scene.pipelines = &a->pipelines;
    v.scene.gates = &a->gates;
    a->ui.cbs.push_front(
        {[](void *u) { view_draw(*static_cast<View *>(u)); }, &v});
    return Scene{&v.scene};
}

} // namespace impl
} // namespace sv
