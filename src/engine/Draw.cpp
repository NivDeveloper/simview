#include "Draw.h"

#include "../draw/bytecode/display_fsmain_msl.h"
#include "../draw/bytecode/display_fsmain_spirv.h"
#include "../draw/bytecode/display_vsmain_msl.h"
#include "../draw/bytecode/display_vsmain_spirv.h"

namespace simview {
namespace {

SDL_GPUShader *make_shader(SDL_GPUDevice *dev, SDL_GPUShaderStage stage,
                           const unsigned char *code, unsigned len,
                           SDL_GPUShaderFormat fmt, const char *entry,
                           Uint32 storage, Uint32 uniforms) {
    SDL_GPUShaderCreateInfo ci{};
    ci.code_size = len;
    ci.code = code;
    ci.entrypoint = entry;
    ci.format = fmt;
    ci.stage = stage;
    ci.num_storage_buffers = storage;
    ci.num_uniform_buffers = uniforms;
    return SDL_CreateGPUShader(dev, &ci);
}

} // namespace

SDL_GPUGraphicsPipeline *display_pipeline(App *a, SDL_GPUTextureFormat tf) {
    for (const auto &e : a->pipelines)
        if (e.format == tf) return e.pipeline;

    const SDL_GPUShaderFormat have = SDL_GetGPUShaderFormats(a->dev);
    SDL_GPUShader *vs = nullptr;
    SDL_GPUShader *fs = nullptr;
    if (have & SDL_GPU_SHADERFORMAT_SPIRV) {
        vs = make_shader(a->dev, SDL_GPU_SHADERSTAGE_VERTEX,
                         display_vsmain_spirv, display_vsmain_spirv_len,
                         SDL_GPU_SHADERFORMAT_SPIRV, "vsmain", 0, 0);
        fs = make_shader(a->dev, SDL_GPU_SHADERSTAGE_FRAGMENT,
                         display_fsmain_spirv, display_fsmain_spirv_len,
                         SDL_GPU_SHADERFORMAT_SPIRV, "fsmain", 1, 1);
    } else if (have & SDL_GPU_SHADERFORMAT_MSL) {
        vs = make_shader(a->dev, SDL_GPU_SHADERSTAGE_VERTEX,
                         display_vsmain_msl, display_vsmain_msl_len,
                         SDL_GPU_SHADERFORMAT_MSL, "vsmain", 0, 0);
        fs = make_shader(a->dev, SDL_GPU_SHADERSTAGE_FRAGMENT,
                         display_fsmain_msl, display_fsmain_msl_len,
                         SDL_GPU_SHADERFORMAT_MSL, "fsmain", 1, 1);
    } else {
        set_error("this driver takes neither SPIR-V nor MSL shaders — "
                  "DXIL bytecode is a planned addition");
        return nullptr;
    }
    if (!vs || !fs) {
        set_error(SDL_GetError());
        if (vs) SDL_ReleaseGPUShader(a->dev, vs);
        if (fs) SDL_ReleaseGPUShader(a->dev, fs);
        return nullptr;
    }

    SDL_GPUColorTargetDescription ctd{};
    ctd.format = tf;
    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader = vs;
    pci.fragment_shader = fs;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions = &ctd;
    pci.target_info.num_color_targets = 1;
    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(a->dev, &pci);
    SDL_ReleaseGPUShader(a->dev, vs);
    SDL_ReleaseGPUShader(a->dev, fs);
    if (!p) return set_error(SDL_GetError()), nullptr;
    a->pipelines.push_back({tf, p});
    return p;
}

} // namespace simview
