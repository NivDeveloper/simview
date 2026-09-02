#pragma once

#include "../render/Shader.h"
#include <nvrhi/nvrhi.h>
#include <simview/Scene.h>
#include <simview/Types.h>

#include <cstdint>

namespace sv {

namespace impl {
struct SceneItem;
} // namespace impl

// The ONE aspect-fit every kind shares, which is what makes a point
// land on the cell it belongs to.
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

    // Answers the scene's default range and how big a shot should be.
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
