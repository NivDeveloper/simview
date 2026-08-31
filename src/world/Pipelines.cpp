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
    // A shadow map has NO colour attachment, which is itself part of
    // the key: a depth-only pipeline is not compatible with one that
    // writes colour, and UNKNOWN is how that says so.
    const nvrhi::Format cf =
        fbd.colorAttachments.empty()
            ? nvrhi::Format::UNKNOWN
            : fbd.colorAttachments[0].texture->getDesc().format;
    const nvrhi::Format df = fbd.depthAttachment.texture
                                 ? fbd.depthAttachment.texture->getDesc().format
                                 : nvrhi::Format::UNKNOWN;
    // The sample count is part of the key: a pipeline is compiled
    // against a framebuffer's sample count and cannot be used with
    // another, and two targets can agree on every format and differ
    // only here.
    const std::uint32_t ms = fb->getFramebufferInfo().sampleCount;
    for (const auto &e : cache)
        if (e.ops == ops && e.pass == pass && e.color == cf && e.depth == df &&
            e.samples == ms)
            return &e;

    // The shadow pass draws the same item from the light's side, so
    // it is the same ops with different stages — and a different
    // binding shape, since the map it would sample is the one it is
    // writing.
    const bool shadow = pass == PassId::Shadow;
    const Shader &vsrc = shadow ? ops->shadow_vs : ops->vs;
    const Shader &fsrc = shadow ? ops->shadow_fs : ops->fs;
    if (!vsrc.code || !fsrc.code)
        return set_error(std::string("no shaders for ") + ops->name +
                         (shadow ? " in the shadow pass" : "")),
               nullptr;

    auto vs =
        gpu.dev->createShader(nvrhi::ShaderDesc()
                                  .setShaderType(nvrhi::ShaderType::Vertex)
                                  .setEntryName(vsrc.entry),
                              vsrc.code, vsrc.len);
    auto fs = gpu.dev->createShader(nvrhi::ShaderDesc()
                                        .setShaderType(nvrhi::ShaderType::Pixel)
                                        .setEntryName(fsrc.entry),
                                    fsrc.code, fsrc.len);
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
    for (std::uint32_t i = 0; i < ops->storage_count; ++i)
        ld.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1 + i));
    // Every colour-pass item can read the map, whether its shader
    // does or not: one layout for all of them keeps the cache keyed
    // on the item and the pass, and not on how a world is lit.
    if (!shadow) {
        ld.addItem(nvrhi::BindingLayoutItem::Texture_SRV(kShadowMapBinding));
        ld.addItem(nvrhi::BindingLayoutItem::Sampler(kShadowSamplerBinding));
    }
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
    if (!shadow)
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
