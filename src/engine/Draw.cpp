#include "Draw.h"

#include "../draw/bytecode/display_fsmain_spirv.h"
#include "../draw/bytecode/display_vsmain_spirv.h"

#include <simview/simview.h>

namespace sv {
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

SDL_GPUGraphicsPipeline *display_pipeline(seam::App *a, SDL_GPUTextureFormat tf) {
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
    } else {
        set_error("this driver does not take SPIR-V shaders — native "
                  "MSL and DXIL bytecode are a planned addition");
        SDL_Log("simview: %s", seam::last_error());
        return nullptr;
    }
    if (!vs || !fs) {
        set_error(SDL_GetError());
        SDL_Log("simview: shader creation failed (%s driver): %s",
                SDL_GetGPUDeviceDriver(a->dev), seam::last_error());
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
    if (!p) {
        set_error(SDL_GetError());
        SDL_Log("simview: pipeline creation failed: %s", seam::last_error());
        return nullptr;
    }
    a->pipelines.push_back({tf, p});
    return p;
}

} // namespace sv

namespace sv {

void render_field(seam::App *a, SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target,
                  Uint32 tw, Uint32 th, SDL_GPUTextureFormat tf) {
    seam::App::FieldState &f = a->field;
    if (f.w && f.dirty && !f.external) {
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
        const SDL_GPUTransferBufferLocation loc{f.staging, 0};
        const SDL_GPUBufferRegion reg{f.buf, 0, f.w * f.h * 4};
        SDL_UploadToGPUBuffer(cp, &loc, &reg, false);
        SDL_EndGPUCopyPass(cp);
        f.dirty = false;
    }

    SDL_GPUColorTargetInfo ct{};
    ct.texture = target;
    ct.clear_color = {0.09f, 0.09f, 0.10f, 1.0f};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    SDL_GPUGraphicsPipeline *pipe =
        f.w ? display_pipeline(a, tf) : nullptr;
    if (pipe) {
        // Aspect-fit: scale window uv so the field fills the largest
        // centered rectangle; outside samples paint the bar color.
        const float wa = float(tw) / float(th);
        const float fa = float(f.w) / float(f.h);
        DrawParams p{};
        p.w = f.w;
        p.h = f.h;
        p.cmap = f.cmap;
        p.lo = f.lo;
        p.hi = f.hi;
        float sx = 1.0f, sy = 1.0f;
        if (wa > fa)
            sx = wa / fa; // window wider: bars left/right
        else
            sy = fa / wa; // window taller: bars top/bottom
        p.uvscale[0] = sx;
        p.uvscale[1] = sy;
        p.uvoff[0] = (1.0f - sx) * 0.5f;
        p.uvoff[1] = (1.0f - sy) * 0.5f;
        SDL_PushGPUFragmentUniformData(cmd, 0, &p, sizeof p);
        SDL_BindGPUGraphicsPipeline(pass, pipe);
        SDL_BindGPUFragmentStorageBuffers(pass, 0, &f.buf, 1);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    }
    SDL_EndGPURenderPass(pass);
}

} // namespace sv
