// The pipeline cache. It knows nothing about any kind — the shaders,
// the binding counts and the blend state all arrive as data on the
// KindOps, which is what removed the four ternaries this file used to
// carry.

#include "../engine/Engine.h"

#include <string>

namespace sv {
namespace {

SDL_GPUShader *make_shader(SDL_GPUDevice *dev, SDL_GPUShaderStage stage,
                           const Shader &s, SDL_GPUShaderFormat fmt) {
    SDL_GPUShaderCreateInfo ci{};
    ci.code_size = s.len;
    ci.code = s.code;
    ci.entrypoint = s.entry;
    ci.format = fmt;
    ci.stage = stage;
    ci.num_storage_buffers = s.storage;
    ci.num_uniform_buffers = s.uniforms;
    return SDL_CreateGPUShader(dev, &ci);
}

} // namespace

SDL_GPUGraphicsPipeline *pipeline_for(impl::App *a, const KindOps *kind,
                                      SDL_GPUTextureFormat tf) {
    for (const auto &e : a->pipelines)
        if (e.kind == kind && e.format == tf)
            return e.pipeline;

    if (!(SDL_GetGPUShaderFormats(a->dev) & SDL_GPU_SHADERFORMAT_SPIRV)) {
        set_error("this driver does not take SPIR-V shaders — native "
                  "MSL and DXIL bytecode are a planned addition");
        return nullptr;
    }
    SDL_GPUShader *vs = make_shader(a->dev, SDL_GPU_SHADERSTAGE_VERTEX,
                                    kind->vs, SDL_GPU_SHADERFORMAT_SPIRV);
    SDL_GPUShader *fs = make_shader(a->dev, SDL_GPU_SHADERSTAGE_FRAGMENT,
                                    kind->fs, SDL_GPU_SHADERFORMAT_SPIRV);
    if (!vs || !fs) {
        set_error(std::string("shader creation failed (") +
                  SDL_GetGPUDeviceDriver(a->dev) +
                  " driver): " + SDL_GetError());
        if (vs)
            SDL_ReleaseGPUShader(a->dev, vs);
        if (fs)
            SDL_ReleaseGPUShader(a->dev, fs);
        return nullptr;
    }

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = tf;
    if (kind->blend.enabled) {
        ctd.blend_state.enable_blend = true;
        ctd.blend_state.src_color_blendfactor = kind->blend.src_color;
        ctd.blend_state.dst_color_blendfactor = kind->blend.dst_color;
        ctd.blend_state.color_blend_op = kind->blend.color_op;
        ctd.blend_state.src_alpha_blendfactor = kind->blend.src_alpha;
        ctd.blend_state.dst_alpha_blendfactor = kind->blend.dst_alpha;
        ctd.blend_state.alpha_blend_op = kind->blend.alpha_op;
    }
    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vs;
    pci.fragment_shader = fs;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &ctd;
    pci.target_info.num_color_targets = 1;
    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(a->dev, &pci);
    SDL_ReleaseGPUShader(a->dev, vs);
    SDL_ReleaseGPUShader(a->dev, fs);
    if (!p) {
        set_error(std::string("pipeline creation failed: ") + SDL_GetError());
        return nullptr;
    }
    a->pipelines.push_back({.kind = kind, .format = tf, .pipeline = p});
    ++a->stats.pipelines;
    return p;
}

} // namespace sv
