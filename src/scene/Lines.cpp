// The Lines kind, whole: segments as instanced quads. Same shape as
// Field.cpp: state, push block, shaders, ops table, and the exported
// functions.

#include "Scene.h"

#include "bytecode/lines_fsmain_spirv.h"
#include "bytecode/lines_vsmain_spirv.h"

#include <gpud/Vulkan.h>

#include <cstring>
#include <string>
#include <vector>

namespace sv {
namespace {

struct LinesState {
    nvrhi::BufferHandle buf;
    nvrhi::BindingSetHandle bset;
    std::vector<float> shadow;
    std::size_t count = 0;    // segments the host last wrote
    std::size_t capacity = 0; // segments the buffer holds
    bool dirty = false;
    bool external = false;
    gpud::BufferSource src{};
    HostSource host{};
    std::uint64_t host_gen = 0;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float width = 1.5f;
};

// Matches lines.slang's LParams (std430 push block).
struct LineParams {
    float x0, y0, x1, y1;
    float fit[2];
    float viewport[2];
    float color[4];
    float width;
    float pad0, pad1, pad2;
};

LinesState &state_of(impl::SceneItem &it) {
    return *static_cast<LinesState *>(it.state);
}

// Stage the segments: grow rather than refuse, then fill the shadow.
// Shared by Update and the host pull. Growth re-creates the buffer, so
// the binding set is dropped and remade at the next draw.
bool upload(LinesState &ls, const impl::Gpu &gpu, const float *xyxy,
            std::size_t count) {
    if (!count) {
        ls.count = 0;
        return true;
    }
    if (count > ls.capacity) {
        ls.buf = gpu.dev->createBuffer(
            nvrhi::BufferDesc()
                .setByteSize(count * 16)
                .setStructStride(16)
                .setInitialState(nvrhi::ResourceStates::ShaderResource)
                .setKeepInitialState(true)
                .setDebugName("lines"));
        ls.bset = nullptr;
        if (!ls.buf) {
            ls.capacity = ls.count = 0;
            return set_error("lines buffer: creation failed"), false;
        }
        ls.capacity = count;
    }
    ls.shadow.assign(xyxy, xyxy + count * 4);
    ls.count = count;
    ls.dirty = true;
    return true;
}

// The buffer IS the set of segments: four floats each, whichever
// source it came from.
void pull_host(LinesState &ls, const impl::Gpu &gpu) {
    std::size_t bytes = 0;
    std::uint64_t gen = 0;
    const void *data = ls.host.fn(ls.host.user, &bytes, &gen);
    if (!data || gen == ls.host_gen)
        return;
    ls.host_gen = gen;
    if (bytes % 16)
        return set_error("a segment is four floats, and " +
                         std::to_string(bytes / 4) +
                         " floats were published — not a multiple of four");
    upload(ls, gpu, static_cast<const float *>(data), bytes / 16);
}

void prepare(impl::SceneItem &it, nvrhi::ICommandList *cl) {
    LinesState &ls = state_of(it);
    if (ls.host)
        pull_host(ls, it.gpu);
    if (ls.dirty && !ls.external && ls.buf && ls.count) {
        cl->writeBuffer(ls.buf, ls.shadow.data(), ls.count * 16);
        ls.dirty = false;
        ++it.stats->uploads;
    }
}

bool rebind_external(impl::SceneItem &it, LinesState &ls,
                     const impl::PipelineEntry *pe) {
    gpud::Buffer *b = ls.src.current();
    const std::uint64_t native = b ? gpud::vulkan::native_buffer(*b) : 0;
    ls.count = b ? b->bytes() / 16 : 0;
    if (!native || !ls.count) {
        ls.bset = nullptr;
        return false;
    }
    auto wrapped = it.gpu.dev->createHandleForNativeBuffer(
        nvrhi::ObjectTypes::VK_Buffer,
        nvrhi::Object(reinterpret_cast<void *>(native)),
        nvrhi::BufferDesc()
            .setByteSize(b->bytes())
            .setStructStride(16)
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setDebugName("lines (external)"));
    if (!wrapped)
        return false;
    ls.bset = it.gpu.dev->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(
                nvrhi::BindingSetItem::PushConstants(0, sizeof(LineParams)))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, wrapped)),
        pe->layout);
    return ls.bset != nullptr;
}

void draw(impl::SceneItem &it, nvrhi::ICommandList *cl, nvrhi::IFramebuffer *fb,
          const Placement &at) {
    LinesState &ls = state_of(it);
    const impl::PipelineEntry *pe =
        pipeline_for(it.gpu, *it.pipelines, it.stats, &kLinesOps, fb);
    if (!pe)
        return;
    // Resolved at the bind, count included.
    if (ls.external) {
        if (!rebind_external(it, ls, pe))
            return;
    } else if (!ls.bset && ls.buf) {
        ls.bset = it.gpu.dev->createBindingSet(
            nvrhi::BindingSetDesc()
                .addItem(
                    nvrhi::BindingSetItem::PushConstants(0, sizeof(LineParams)))
                .addItem(
                    nvrhi::BindingSetItem::StructuredBuffer_SRV(0, ls.buf)),
            pe->layout);
    }
    if (!ls.bset || !ls.count)
        return;

    LineParams p{};
    p.x0 = float(at.range.x0);
    p.y0 = float(at.range.y0);
    p.x1 = float(at.range.x1);
    p.y1 = float(at.range.y1);
    p.fit[0] = at.sx;
    p.fit[1] = at.sy;
    p.viewport[0] = float(at.tw);
    p.viewport[1] = float(at.th);
    for (int c = 0; c < 4; ++c)
        p.color[c] = ls.color[c];
    p.width = ls.width;
    cl->setGraphicsState(
        nvrhi::GraphicsState()
            .setPipeline(pe->pipeline)
            .setFramebuffer(fb)
            .addBindingSet(ls.bset)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(float(at.tw), float(at.th)))));
    cl->setPushConstants(&p, sizeof p);
    // Six corners, one instance per segment.
    cl->draw(nvrhi::DrawArguments().setVertexCount(6).setInstanceCount(
        std::uint32_t(ls.count)));
    ++it.stats->draws;
}

void release(impl::SceneItem &it) {
    delete static_cast<LinesState *>(it.state);
    it.state = nullptr;
}

} // namespace

// clang-format off: the table reads as a table
const KindOps kLinesOps{
    .name = "lines",
    .prepare = prepare,
    .draw = draw,
    .release = release,
    .grid = nullptr,
    .vs = {lines_vsmain_spirv, lines_vsmain_spirv_len, "vsmain"},
    .fs = {lines_fsmain_spirv, lines_fsmain_spirv_len, "fsmain"},
    .blend = nvrhi::BlendState::RenderTarget()
                 .enableBlend()
                 .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                 .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                 .setSrcBlendAlpha(nvrhi::BlendFactor::One)
                 .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha),
    .storage_stage = nvrhi::ShaderType::Vertex,
    .push_bytes = sizeof(LineParams),
};
// clang-format on

namespace impl {

namespace {

Lines lines_new(Scene s, const LinesDesc &d, HostSource host) {
    SceneState *sc = static_cast<SceneState *>(s.p);
    if (!sc)
        return {};
    if (!(d.width > 0.0f))
        return set_error("lines need a width above zero — they are drawn "
                         "as quads, not as line primitives"),
               Lines{};

    SceneItem &it = sc->items.emplace_back();
    it.gpu = sc->gpu;
    it.stats = sc->stats;
    it.pipelines = sc->pipelines;
    it.ops = &kLinesOps;
    LinesState *ls = new LinesState{};
    ls->host = host;
    for (int c = 0; c < 4; ++c)
        ls->color[c] = d.color[c];
    ls->width = d.width;
    it.state = ls;
    return Lines{&it};
}

} // namespace

Lines lines_create(Scene s, const LinesDesc &d) {
    return lines_new(s, d, HostSource{});
}

Lines lines_from_host(Scene s, HostSource src, const LinesDesc &d) {
    if (!src)
        return set_error("lines need a source that can answer — the "
                         "HostSource's fn is null"),
               Lines{};
    return lines_new(s, d, src);
}

bool lines_update(Lines l, const float *xyxy, std::size_t count) {
    SceneItem *it = static_cast<SceneItem *>(l.p);
    if (!it || it->ops != &kLinesOps)
        return set_error("lines_update: this is not a lines handle"), false;
    if (!xyxy && count)
        return set_error("lines_update: null"), false;
    LinesState &ls = state_of(*it);
    if (ls.external)
        return set_error("these lines read a caller-owned source, "
                         "re-resolved each frame — update the producer, "
                         "not the item"),
               false;
    if (ls.host)
        return set_error("these lines read a Sync — publish to it, not "
                         "the item"),
               false;
    return upload(ls, it->gpu, xyxy, count);
}

Lines lines_from_source(Scene s, gpud::BufferSource src, const LinesDesc &d) {
    SceneState *sc = static_cast<SceneState *>(s.p);
    if (!sc)
        return {};
    if (!src)
        return set_error("lines need a source that can answer — the "
                         "BufferSource's fn is null"),
               Lines{};
    if (!(d.width > 0.0f))
        return set_error("lines need a width above zero — they are drawn "
                         "as quads, not as line primitives"),
               Lines{};

    SceneItem &it = sc->items.emplace_back();
    it.gpu = sc->gpu;
    it.stats = sc->stats;
    it.pipelines = sc->pipelines;
    it.ops = &kLinesOps;
    LinesState *ls = new LinesState{};
    ls->external = true;
    ls->src = src;
    for (int c = 0; c < 4; ++c)
        ls->color[c] = d.color[c];
    ls->width = d.width;
    it.state = ls;
    return Lines{&it};
}

} // namespace impl
} // namespace sv
