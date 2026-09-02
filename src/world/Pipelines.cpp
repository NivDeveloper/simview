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
    // Part of the key: a pipeline compiled against one sample count
    // cannot be used with another.
    const std::uint32_t ms = fb->getFramebufferInfo().sampleCount;
    for (const auto &e : cache)
        if (e.ops == ops && e.pass == pass && e.color == cf && e.depth == df &&
            e.samples == ms)
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

    // The two cannot share a slot: one set, view block at binding 0,
    // the item's storage at binding 1.
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
    for (std::uint32_t i = 0; i < ops->storage_count; ++i)
        ld.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1 + i));
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
                     .samples = ms,
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
