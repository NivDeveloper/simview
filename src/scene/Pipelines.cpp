// The pipeline cache. It knows nothing about any kind — the shaders,
// the binding shape and the blend state all arrive as data on the
// KindOps. The cache is still keyed (kind, format): every target here
// is a single color attachment with no depth, so framebuffer
// compatibility folds into the format and the Stats semantics stay
// exact — one format means one pipeline.

#include "Scene.h"

#include <string>

namespace sv {

const impl::PipelineEntry *pipeline_for(const impl::Gpu &gpu,
                                        std::vector<impl::PipelineEntry> &cache,
                                        Stats *stats, const KindOps *kind,
                                        nvrhi::IFramebuffer *fb) {
    const nvrhi::Format tf =
        fb->getDesc().colorAttachments[0].texture->getDesc().format;
    for (const auto &e : cache)
        if (e.kind == kind && e.format == tf)
            return &e;

    auto vs =
        gpu.dev->createShader(nvrhi::ShaderDesc()
                                  .setShaderType(nvrhi::ShaderType::Vertex)
                                  .setEntryName(kind->vs.entry),
                              kind->vs.code, kind->vs.len);
    auto fs = gpu.dev->createShader(nvrhi::ShaderDesc()
                                        .setShaderType(nvrhi::ShaderType::Pixel)
                                        .setEntryName(kind->fs.entry),
                                    kind->fs.code, kind->fs.len);
    if (!vs || !fs)
        return set_error(std::string("shader creation failed for ") +
                         kind->name),
               nullptr;

    // One set: the kind's storage buffer at binding 0 plus the
    // push-constant block, both visible to the stage the kind names.
    // Zeroed offsets: the shaders spell [[vk::binding(0,0)]].
    auto layout = gpu.dev->createBindingLayout(
        nvrhi::BindingLayoutDesc()
            .setVisibility(kind->storage_stage)
            .addItem(
                nvrhi::BindingLayoutItem::PushConstants(0, kind->push_bytes))
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0))
            .setBindingOffsets(nvrhi::VulkanBindingOffsets()
                                   .setShaderResourceOffset(0)
                                   .setSamplerOffset(0)
                                   .setConstantBufferOffset(0)
                                   .setUnorderedAccessViewOffset(0)));
    if (!layout)
        return set_error(std::string("binding layout failed for ") +
                         kind->name),
               nullptr;

    nvrhi::RenderState rstate;
    rstate.depthStencilState.depthTestEnable = false;
    rstate.depthStencilState.depthWriteEnable = false;
    rstate.depthStencilState.stencilEnable = false;
    rstate.rasterState.setCullNone();
    rstate.blendState.targets[0] = kind->blend;

    auto pipeline = gpu.dev->createGraphicsPipeline(
        nvrhi::GraphicsPipelineDesc()
            .setVertexShader(vs)
            .setPixelShader(fs)
            .setPrimType(nvrhi::PrimitiveType::TriangleList)
            .setRenderState(rstate)
            .addBindingLayout(layout),
        fb->getFramebufferInfo());
    if (!pipeline)
        return set_error(std::string("pipeline creation failed for ") +
                         kind->name),
               nullptr;

    cache.push_back(
        {.kind = kind, .format = tf, .pipeline = pipeline, .layout = layout});
    ++stats->pipelines;
    return &cache.back();
}

void pipelines_release(std::vector<impl::PipelineEntry> &cache) {
    cache.clear(); // handles are refcounted; dropping them IS the release
}

} // namespace sv
