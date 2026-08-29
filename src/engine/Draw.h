#pragma once

// Internal to src/ — the display pipeline over the committed bytecode.

#include "Engine.h"

namespace sv {

// std140-compatible: 4-byte scalars only, matching the shader's Params.
struct DrawParams {
    Uint32 w, h;
    Sint32 cmap;
    Uint32 pad0;
    float lo, hi;
    float uvscale[2];
    float uvoff[2];
};

// The pipeline for a given target format, created on demand and cached
// on the App. Picks SPIR-V when the driver takes it, else MSL source;
// DXIL is a planned addition (the error names it).
SDL_GPUGraphicsPipeline *display_pipeline(seam::App *, SDL_GPUTextureFormat);

} // namespace sv
