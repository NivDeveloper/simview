#pragma once

// Internal to src/ — drawing a SCENE into a render target. The target
// itself, and the two devices behind it, are render/'s: both strata
// use them and neither owns them.

#include "../render/Target.h"
#include "Scene.h"

namespace sv {

// Draw a scene into the target, if it has one.
void target_draw(impl::SceneState &, nvrhi::ICommandList *,
                 impl::RenderTarget &);

} // namespace sv
