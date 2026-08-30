#pragma once

// Internal to src/ — what a scene kind IS.
//
// A kind is DATA: one static KindOps per kind, carrying its four
// functions, its two shaders and its blend state. The scene walks
// items and calls through the table; nothing switches on a kind, and
// nothing enumerates them. A new kind is one file that defines its own
// KindOps and is named by nobody — which is the whole point, because
// the previous shape cost thirteen branch sites across four files and
// a state member on every item whether it was that kind or not.
//
// Each kind's file includes its OWN shader bytecode. That is what
// keeps the generated headers to one includer each: they declare
// non-const, non-inline arrays, so two includers would be a multiple
// definition.

#include <SDL3/SDL.h>
#include <simview/Scene.h>
#include <simview/Types.h>

namespace sv {

namespace impl {
struct App;
struct SceneItem;
} // namespace impl

// One stage's program. The counts are per stage and they differ per
// kind: a field reads its grid in the fragment stage, a cloud reads
// its points in the vertex stage.
struct Shader {
    const unsigned char *code = nullptr;
    unsigned len = 0;
    const char *entry = nullptr;
    Uint32 storage = 0;
    Uint32 uniforms = 0;
};

// Spelled out in full because zero-init means every factor INVALID,
// not "no blending" — `enabled` is what distinguishes the two.
struct Blend {
    bool enabled = false;
    SDL_GPUBlendFactor src_color = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    SDL_GPUBlendFactor dst_color = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    SDL_GPUBlendOp color_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUBlendFactor src_alpha = SDL_GPU_BLENDFACTOR_ONE;
    SDL_GPUBlendFactor dst_alpha = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    SDL_GPUBlendOp alpha_op = SDL_GPU_BLENDOP_ADD;
};

// What every kind needs to place itself on the target: the range the
// scene maps into, the ONE aspect-fit every kind shares (which is what
// makes a point land on the cell it belongs to), and the target it is
// going to.
struct Placement {
    Uint32 tw = 0, th = 0;
    SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
    Range2 range{};
    float sx = 1.0f, sy = 1.0f;
};

struct KindOps {
    const char *name = nullptr;

    // With no render pass open: ask a pull source where the data is
    // now, and upload whatever the host changed.
    void (*prepare)(impl::SceneItem &, SDL_GPUCommandBuffer *) = nullptr;

    // Into an already-open pass. The command buffer comes too, because
    // pushing uniforms is a per-STAGE call and only the kind knows
    // which stage its block lives in.
    void (*draw)(impl::SceneItem &, SDL_GPUCommandBuffer *, SDL_GPURenderPass *,
                 const Placement &) = nullptr;

    // Free the GPU objects AND the state. Both, because the state is
    // this kind's alone and nothing else knows its type.
    void (*release)(impl::SceneItem &, SDL_GPUDevice *) = nullptr;

    // Does this kind have a natural pixel grid? A field does — its
    // extent in cells — and that answers two questions at once: the
    // scene's default range, and how big a shot of it should be. A
    // kind without one leaves the question to the next item.
    bool (*grid)(const impl::SceneItem &, Extent2 *) = nullptr;

    Shader vs, fs;
    Blend blend;
};

extern const KindOps kFieldOps;
extern const KindOps kParticlesOps;

// The pipeline for a kind AND a target format, created on demand and
// cached on the App. Keyed on both: matching format alone would hand
// one kind another's pipeline — a picture, not an error.
SDL_GPUGraphicsPipeline *pipeline_for(impl::App *, const KindOps *,
                                      SDL_GPUTextureFormat);

} // namespace sv
