#pragma once

#include "../scene/Target.h"
#include "../world/World.h"

#include <cstdint>
#include <memory>
#include <string>

namespace sv {
namespace impl {

struct View {
    std::string title;
    RenderTarget target;
    // A view holds one or the other: `world` null is the 2D case, and
    // the panel, the target and the resize path are shared by both.
    SceneState scene;
    std::unique_ptr<WorldState> world;
    App *app = nullptr;          // for the sampler the panel's image bakes
    void *imgui_tex = nullptr;   // VkDescriptorSet from AddTexture
    std::uint32_t bound_gen = 0; // target.gen imgui_tex was made for
};

} // namespace impl

// The panel a view lives in — it shows the texture and reports how
// much room it had, which is what sizes the next frame's texture.
void view_draw(impl::View &);

} // namespace sv
