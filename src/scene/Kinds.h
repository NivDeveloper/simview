#pragma once

// Internal to src/ — what a scene kind IS.
//
// A kind is DATA: one static KindOps per kind, carrying its four
// functions, its two shaders, its blend state and its binding shape.
// The scene walks items and calls through the table; nothing switches
// on a kind, and nothing enumerates them. A new kind is one file that
// defines its own KindOps and is named by nobody.
//
// Each kind's file includes its OWN shader bytecode. That is what
// keeps the generated headers to one includer each: they declare
// non-const, non-inline arrays, so two includers would be a multiple
// definition.

#include <nvrhi/nvrhi.h>
#include <simview/Scene.h>
#include <simview/Types.h>

#include <cstdint>

namespace sv {

namespace impl {
struct SceneItem;
} // namespace impl

// One stage's program: committed SPIR-V plus the entry point slangc
// kept (-fvk-use-entrypoint-name — NVRHI passes it to pipeline
// creation, and a mismatch dies inside the driver's SPIRV consumer).
struct Shader {
    const unsigned char *code = nullptr;
    unsigned len = 0;
    const char *entry = nullptr;
};

// What every kind needs to place itself on the target: the range the
// scene maps into, the ONE aspect-fit every kind shares (which is what
// makes a point land on the cell it belongs to), and the target it is
// going to.
struct Placement {
    std::uint32_t tw = 0, th = 0;
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    Range2 range{};
    float sx = 1.0f, sy = 1.0f;
};

struct KindOps {
    const char *name = nullptr;

    // With no render pass open: ask a pull source where the data is
    // now, and upload whatever the host changed.
    void (*prepare)(impl::SceneItem &, nvrhi::ICommandList *) = nullptr;

    // Record one draw against the framebuffer. State, bindings and
    // push constants are the kind's own; the pipeline comes from the
    // shared cache.
    void (*draw)(impl::SceneItem &, nvrhi::ICommandList *,
                 nvrhi::IFramebuffer *, const Placement &) = nullptr;

    // Free the state. GPU objects are refcounted handles inside it —
    // dropping them IS the release.
    void (*release)(impl::SceneItem &) = nullptr;

    // Does this kind have a natural pixel grid? A field does — its
    // extent in cells — and that answers two questions at once: the
    // scene's default range, and how big a shot of it should be. A
    // kind without one leaves the question to the next item.
    bool (*grid)(const impl::SceneItem &, Extent2 *) = nullptr;

    Shader vs, fs;
    nvrhi::BlendState::RenderTarget blend;

    // The one set every kind binds: binding 0 is a storage buffer read
    // by THIS stage, and the params ride the push-constant block with
    // the same visibility.
    nvrhi::ShaderType storage_stage = nvrhi::ShaderType::Vertex;
    std::uint32_t push_bytes = 0;
};

extern const KindOps kFieldOps;
extern const KindOps kParticlesOps;
extern const KindOps kLinesOps;

} // namespace sv
