// A view: a scene whose target is a texture, shown by a panel. The
// panel's draw lives in Context.cpp beside the other ImGui code; what
// is here is the registration, which is what makes a view an item in
// the UI's title namespace like a plot or a panel.

#include "../core/Engine.h"

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

    App::ViewState &v = a->views.emplace_back();
    v.app = a;
    v.title = d.title;
    v.scene.app = a;
    a->ui_cbs.push_front(
        {[](void *u) { view_draw(*static_cast<App::ViewState *>(u)); }, &v});
    return Scene{&v.scene};
}

} // namespace impl
} // namespace sv
