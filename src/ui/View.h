#pragma once

// Internal to src/ — a view: a scene rendered into a target, shown by
// a panel that docks and tears out like any other. The target is
// scene's; the title and the panel are ui's. This is the split that
// lets the scene layer never learn what a panel is.

#include "../scene/Target.h"

#include <cstdint>
#include <string>

namespace sv {
namespace impl {

struct View {
    std::string title;
    RenderTarget target;
    SceneState scene;
    App *app = nullptr;          // for the sampler the panel's image bakes
    void *imgui_tex = nullptr;   // VkDescriptorSet from AddTexture
    std::uint32_t bound_gen = 0; // target.gen imgui_tex was made for
};

} // namespace impl

// The panel a view lives in — it shows the texture and reports how
// much room it had, which is what sizes the next frame's texture.
void view_draw(impl::View &);

} // namespace sv
