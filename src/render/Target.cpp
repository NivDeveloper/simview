// Creating and recreating a drawable texture. The only decisions here
// are which formats, and that a failure leaves the target measurably
// empty (w = h = 0) rather than half-built.

#include "Target.h"

#include "../core/Error.h"

namespace sv {

void target_resize(const impl::Gpu &gpu, impl::RenderTarget &t) {
    if ((t.tex || (t.depth_only && t.depth)) && t.w == t.want_w &&
        t.h == t.want_h)
        return;
    t.fb = nullptr;
    t.tex = nullptr;
    t.depth = nullptr;

    if (!t.depth_only) {
        t.tex = gpu.dev->createTexture(
            nvrhi::TextureDesc()
                .setWidth(t.want_w)
                .setHeight(t.want_h)
                .setFormat(kTargetFormat)
                .setIsRenderTarget(true)
                .setInitialState(nvrhi::ResourceStates::ShaderResource)
                .setKeepInitialState(true)
                .setDebugName("view target"));
        if (!t.tex) {
            set_error("view texture: creation failed");
            t.w = t.h = 0;
            return;
        }
    }

    if (t.want_depth || t.depth_only) {
        t.depth = gpu.dev->createTexture(
            nvrhi::TextureDesc()
                .setWidth(t.want_w)
                .setHeight(t.want_h)
                .setFormat(kDepthFormat)
                .setIsRenderTarget(true)
                .setInitialState(nvrhi::ResourceStates::DepthWrite)
                .setKeepInitialState(true)
                .setDebugName(t.depth_only ? "shadow map" : "view depth"));
        if (!t.depth) {
            set_error("view depth texture: creation failed");
            t.tex = nullptr;
            t.w = t.h = 0;
            return;
        }
    }

    nvrhi::FramebufferDesc fbd;
    if (t.tex)
        fbd.addColorAttachment(t.tex);
    if (t.depth)
        fbd.setDepthAttachment(t.depth);
    t.fb = gpu.dev->createFramebuffer(fbd);
    t.w = t.want_w;
    t.h = t.want_h;
    ++t.gen;
}

void target_release(impl::RenderTarget &t) {
    t.fb = nullptr;
    t.tex = nullptr;
    t.depth = nullptr;
    t.w = t.h = 0;
}

} // namespace sv
