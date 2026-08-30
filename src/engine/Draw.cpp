#include "Draw.h"

#include "../draw/bytecode/display_fsmain_spirv.h"
#include "../draw/bytecode/display_vsmain_spirv.h"
#include "../draw/bytecode/particles_fsmain_spirv.h"
#include "../draw/bytecode/particles_vsmain_spirv.h"

#include <string>

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

SDL_GPUGraphicsPipeline *pipeline_for(impl::App *a, impl::App::ItemKind kind,
                                      SDL_GPUTextureFormat tf) {
    for (const auto &e : a->pipelines)
        if (e.kind == kind && e.format == tf)
            return e.pipeline;

    const bool points = kind == impl::App::ItemKind::Particles;
    const SDL_GPUShaderFormat have = SDL_GetGPUShaderFormats(a->dev);
    SDL_GPUShader *vs = nullptr;
    SDL_GPUShader *fs = nullptr;
    if (have & SDL_GPU_SHADERFORMAT_SPIRV) {
        // The counts are per stage: a particle vertex shader reads one
        // storage buffer and one uniform block, the field's reads none.
        vs = points
                 ? make_shader(a->dev, SDL_GPU_SHADERSTAGE_VERTEX,
                               particles_vsmain_spirv,
                               particles_vsmain_spirv_len,
                               SDL_GPU_SHADERFORMAT_SPIRV, "vsmain", 1, 1)
                 : make_shader(a->dev, SDL_GPU_SHADERSTAGE_VERTEX,
                               display_vsmain_spirv, display_vsmain_spirv_len,
                               SDL_GPU_SHADERFORMAT_SPIRV, "vsmain", 0, 0);
        fs = points
                 ? make_shader(a->dev, SDL_GPU_SHADERSTAGE_FRAGMENT,
                               particles_fsmain_spirv,
                               particles_fsmain_spirv_len,
                               SDL_GPU_SHADERFORMAT_SPIRV, "fsmain", 0, 0)
                 : make_shader(a->dev, SDL_GPU_SHADERSTAGE_FRAGMENT,
                               display_fsmain_spirv, display_fsmain_spirv_len,
                               SDL_GPU_SHADERFORMAT_SPIRV, "fsmain", 1, 1);
    } else {
        set_error("this driver does not take SPIR-V shaders — native "
                  "MSL and DXIL bytecode are a planned addition");
        return nullptr;
    }
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
    if (points) {
        // Zero-init means opaque and every factor INVALID, so straight
        // alpha has to be spelled out in full.
        ctd.blend_state.enable_blend = true;
        ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ctd.blend_state.dst_color_blendfactor =
            SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ctd.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        ctd.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ctd.blend_state.dst_alpha_blendfactor =
            SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ctd.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
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

namespace {

// Everything an item needs done with no render pass open: the pull
// that asks a source which buffer holds the data now, and the upload
// of anything the host has changed.
void item_prepare(impl::App::SceneItem &it, SDL_GPUCommandBuffer *cmd) {
    if (it.kind == impl::App::ItemKind::Particles) {
        impl::App::ParticlesState &ps = it.particles;
        if (ps.external) {
            gpud::Buffer *b = ps.src.current();
            ps.buf = b ? gpud::sdl::native_buffer(*b) : nullptr;
        }
        if (ps.dirty && !ps.external && ps.buf && ps.count) {
            SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
            const SDL_GPUTransferBufferLocation loc{ps.staging, 0};
            const SDL_GPUBufferRegion reg{ps.buf, 0, Uint32(ps.count * 8)};
            SDL_UploadToGPUBuffer(cp, &loc, &reg, false);
            SDL_EndGPUCopyPass(cp);
            ps.dirty = false;
            ++it.app->stats.uploads;
        }
        return;
    }

    impl::App::FieldState &f = it.field;
    if (f.external) {
        gpud::Buffer *b = f.src.current();
        f.buf = b ? gpud::sdl::native_buffer(*b) : nullptr;
    }
    if (f.w && f.dirty && !f.external) {
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
        const SDL_GPUTransferBufferLocation loc{f.staging, 0};
        const SDL_GPUBufferRegion reg{f.buf, 0, f.w * f.h * 4};
        SDL_UploadToGPUBuffer(cp, &loc, &reg, false);
        SDL_EndGPUCopyPass(cp);
        f.dirty = false;
        ++it.app->stats.uploads;
    }
}

// One item into an already-open pass. The switch is the whole cost of
// a new scene kind, beside its pipeline and shader.
void item_draw(impl::App::SceneItem &it, SDL_GPUCommandBuffer *cmd,
               SDL_GPURenderPass *pass, Uint32 tw, Uint32 th,
               SDL_GPUTextureFormat tf, const Range2 &rng, float sx, float sy) {
    impl::App *a = it.app;
    switch (it.kind) {
    case impl::App::ItemKind::Field: {
        impl::App::FieldState &f = it.field;
        if (!f.w || !f.buf)
            return;
        SDL_GPUGraphicsPipeline *pipe =
            pipeline_for(a, impl::App::ItemKind::Field, tf);
        if (!pipe)
            return;

        DrawParams p{};
        p.w = f.w;
        p.h = f.h;
        p.cmap = f.cmap;
        p.lo = f.lo;
        p.hi = f.hi;
        p.uvscale[0] = sx;
        p.uvscale[1] = sy;
        p.uvoff[0] = (1.0f - sx) * 0.5f;
        p.uvoff[1] = (1.0f - sy) * 0.5f;
        SDL_PushGPUFragmentUniformData(cmd, 0, &p, sizeof p);
        SDL_BindGPUGraphicsPipeline(pass, pipe);
        SDL_BindGPUFragmentStorageBuffers(pass, 0, &f.buf, 1);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        ++a->stats.draws;
        break;
    }
    case impl::App::ItemKind::Particles: {
        impl::App::ParticlesState &ps = it.particles;
        if (!ps.buf || !ps.count)
            return;
        SDL_GPUGraphicsPipeline *pipe =
            pipeline_for(a, impl::App::ItemKind::Particles, tf);
        if (!pipe)
            return;

        PointParams p{};
        p.x0 = float(rng.x0);
        p.y0 = float(rng.y0);
        p.x1 = float(rng.x1);
        p.y1 = float(rng.y1);
        p.fit[0] = sx;
        p.fit[1] = sy;
        p.viewport[0] = float(tw);
        p.viewport[1] = float(th);
        for (int c = 0; c < 4; ++c)
            p.color[c] = ps.color[c];
        p.radius = ps.radius;
        SDL_PushGPUVertexUniformData(cmd, 0, &p, sizeof p);
        SDL_BindGPUGraphicsPipeline(pass, pipe);
        SDL_BindGPUVertexStorageBuffers(pass, 0, &ps.buf, 1);
        // Six corners, one instance per point. first_* must stay 0:
        // SDL says the built-in IDs are not compatible with them.
        SDL_DrawGPUPrimitives(pass, 6, Uint32(ps.count), 0, 0);
        ++a->stats.draws;
        break;
    }
    }
}

// The range every item maps into, and — with the fit below — the ONE
// rect they all land on, which is what puts a point on the cell it
// belongs to.
Range2 effective_range(const impl::App::SceneState &sc) {
    if (sc.range.x1 > sc.range.x0 && sc.range.y1 > sc.range.y0)
        return sc.range;
    for (const impl::App::SceneItem &it : sc.items)
        if (it.kind == impl::App::ItemKind::Field && it.field.w)
            return {0.0, 0.0, double(it.field.w), double(it.field.h)};
    return {0.0, 0.0, 1.0, 1.0};
}

} // namespace

void scene_draw(impl::App::SceneState &sc, SDL_GPUCommandBuffer *cmd,
                SDL_GPUTexture *target, Uint32 tw, Uint32 th,
                SDL_GPUTextureFormat tf) {
    for (impl::App::SceneItem &it : sc.items)
        item_prepare(it, cmd);

    const Range2 rng = effective_range(sc);
    const float wa = float(tw) / float(th);
    const float ra = float((rng.x1 - rng.x0) / (rng.y1 - rng.y0));
    float sx = 1.0f, sy = 1.0f;
    if (wa > ra)
        sx = wa / ra; // target wider: bars left and right
    else
        sy = ra / wa; // target taller: bars above and below

    // The clear belongs to the SCENE. An item that cleared would erase
    // whatever the item before it drew.
    SDL_GPUColorTargetInfo ct{};
    ct.texture = target;
    ct.clear_color = {0.09f, 0.09f, 0.10f, 1.0f};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
    for (impl::App::SceneItem &it : sc.items)
        item_draw(it, cmd, pass, tw, th, tf, rng, sx, sy);
    SDL_EndGPURenderPass(pass);
}

} // namespace sv
