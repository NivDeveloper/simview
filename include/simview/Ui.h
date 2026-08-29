#pragma once

#include "App.h"

#include <imgui.h>

namespace sv {

namespace impl {
ImGuiContext *app_ui_context(App *);
}

inline ImGuiContext &UiContext(const App &a) {
    return *impl::app_ui_context(a.Raw());
}

}
