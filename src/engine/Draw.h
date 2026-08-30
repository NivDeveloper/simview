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

// The particle uniform block, laid out to match particles.slang's
// PParams: 4-byte scalars and a float4 on a 16-byte boundary.
struct PointParams {
    float x0, y0, x1, y1;
    float fit[2];
    float viewport[2];
    float color[4];
    float radius;
    float pad0, pad1, pad2;
};

// The pipeline for a kind AND a target format, created on demand and
// cached on the App. Keyed on both: matching format alone would hand
// a particles draw the field's pipeline.
SDL_GPUGraphicsPipeline *pipeline_for(impl::App *, impl::App::ItemKind,
                                      SDL_GPUTextureFormat);

} // namespace sv
