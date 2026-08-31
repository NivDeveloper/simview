// The world's pipeline cache. It knows nothing about any item: the
// shaders, the binding shape, the blend and the topology arrive as
// data on the WorldItemOps, and the depth state arrives from the pass
// row. Keyed on (ops, pass, colour format, depth format) — the same
// item drawn in two passes wants two depth states, and a target with a
// depth attachment is not pipeline-compatible with one without.
//
// Separate from the 2D cache on purpose. That one is keyed (kind,
// format) and says so, because every 2D target is one colour
// attachment with no depth; widening it to carry a pass and a depth
// format would spend that exactness for nothing, since the renderer
// keys pipelines on the framebuffer anyway.

#include "../core/Error.h"
#include "Items.h"

#include <string>

namespace sv {

const WorldPipelineEntry *
world_pipeline_for(const impl::Gpu &gpu, std::vector<WorldPipelineEntry> &cache,
                   Stats *stats, const WorldItemOps *ops, PassId pass,
                   nvrhi::IFramebuffer *fb) {
    const auto &fbd = fb->getDesc();
    const nvrhi::Format cf = fbd.colorAttachments[0].texture->getDesc().format;
    const nvrhi::Format df = fbd.depthAttachment.texture
                                 ? fbd.depthAttachment.texture->getDesc().format
                                 : nvrhi::Format::UNKNOWN;
    for (const auto &e : cache)
        if (e.ops == ops && e.pass == pass && e.color == cf && e.depth == df)
            return &e;

    auto vs =
        gpu.dev->createShader(nvrhi::ShaderDesc()
                                  .setShaderType(nvrhi::ShaderType::Vertex)
                                  .setEntryName(ops->vs.entry),
                              ops->vs.code, ops->vs.len);
    auto fs = gpu.dev->createShader(nvrhi::ShaderDesc()
                                        .setShaderType(nvrhi::ShaderType::Pixel)
                                        .setEntryName(ops->fs.entry),
                                    ops->fs.code, ops->fs.len);
    if (!vs || !fs)
        return set_error(std::string("shader creation failed for ") +
                         ops->name),
               nullptr;

    // One set, visible to both stages: the view block at binding 0, the
    // item's own storage at binding 1 when it has any. The two cannot
    // share a slot — the binding offsets are zeroed, so a constant
    // buffer and a buffer view at 0 would land on the same descriptor.
    auto ld = nvrhi::BindingLayoutDesc()
                  .setVisibility(nvrhi::ShaderType::All)
                  .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
                  .addItem(nvrhi::BindingLayoutItem::PushConstants(
                      1, ops->push_bytes))
                  .setBindingOffsets(nvrhi::VulkanBindingOffsets()
                                         .setShaderResourceOffset(0)
                                         .setSamplerOffset(0)
                                         .setConstantBufferOffset(0)
                                         .setUnorderedAccessViewOffset(0));
    if (ops->has_storage)
        ld.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1));
    auto layout = gpu.dev->createBindingLayout(ld);
    if (!layout)
        return set_error(std::string("binding layout failed for ") + ops->name),
               nullptr;

    // The depth state is the pass's, and the comparison is spelled out
    // every time: the default is Less, which under a reverse-Z depth
    // buffer keeps exactly the geometry it should have discarded.
    const PassDesc &pd = pass_of(pass);
    nvrhi::RenderState rstate;
    rstate.depthStencilState.depthTestEnable = pd.depth_test;
    rstate.depthStencilState.depthWriteEnable = pd.depth_write;
    rstate.depthStencilState.depthFunc = pd.depth_func;
    rstate.depthStencilState.stencilEnable = false;
    rstate.rasterState.setCullNone();
    rstate.blendState.targets[0] = ops->blend;

    auto pipeline =
        gpu.dev->createGraphicsPipeline(nvrhi::GraphicsPipelineDesc()
                                            .setVertexShader(vs)
                                            .setPixelShader(fs)
                                            .setPrimType(ops->topology)
                                            .setRenderState(rstate)
                                            .addBindingLayout(layout),
                                        fb->getFramebufferInfo());
    if (!pipeline)
        return set_error(std::string("pipeline creation failed for ") +
                         ops->name),
               nullptr;

    cache.push_back({.ops = ops,
                     .pass = pass,
                     .color = cf,
                     .depth = df,
                     .pipeline = pipeline,
                     .layout = layout});
    ++stats->pipelines;
    return &cache.back();
}

std::uint32_t world_pipeline_id(const std::vector<WorldPipelineEntry> &cache,
                                const WorldPipelineEntry *e) {
    return std::uint32_t(e - cache.data());
}

void world_pipelines_release(std::vector<WorldPipelineEntry> &cache) {
    cache.clear(); // handles are refcounted; dropping them IS the release
}

} // namespace sv
