// The Field kind, whole: its state, its uniform block, its shaders,
// its ops table, and the three exported functions a user reaches it
// through. Nothing outside this file knows a FieldState exists.

#include "Scene.h"

#include "bytecode/display_fsmain_spirv.h"
#include "bytecode/display_vsmain_spirv.h"

#include <gpud/Vulkan.h>

#include <cstring>
#include <string>
#include <vector>

namespace sv {
namespace {

struct FieldState {
    std::uint32_t w = 0, h = 0;
    std::int32_t cmap = 0;
    float lo = 0, hi = 1;
    nvrhi::BufferHandle buf;
    nvrhi::BindingSetHandle bset;
    std::vector<float> shadow; // what prepare() uploads when dirty
    bool dirty = false;        // shadow holds a newer grid than buf
    bool external = false;     // buf resolves from src: no shadow, no upload
    // The wrapped native buffer an external field re-binds when the
    // producer publishes a new one.
    std::uint64_t bound_native = 0;
    // The pull source an external field re-asks at every draw.
    gpud::BufferSource src{};
    // The host source a Sync-fed field asks once per frame, and the
    // generation it last staged.
    HostSource host{};
    std::uint64_t host_gen = 0;
};

// The push-constant block, std430: 4-byte scalars only, matching
// display.slang's Params.
struct DrawParams {
    std::uint32_t w, h;
    std::int32_t cmap;
    std::uint32_t pad0;
    float lo, hi;
    float uvscale[2];
    float uvoff[2];
};

FieldState &state_of(impl::SceneItem &it) {
    return *static_cast<FieldState *>(it.state);
}

const FieldState &state_of(const impl::SceneItem &it) {
    return *static_cast<const FieldState *>(it.state);
}

// A host-fed field stages a NEW generation the way an Update does. A
// size that disagrees with the extent is refused once per generation,
// by name, and the last good grid stays on screen.
void pull_host(FieldState &f) {
    std::size_t bytes = 0;
    std::uint64_t gen = 0;
    const void *data = f.host.fn(f.host.user, &bytes, &gen);
    if (!data || gen == f.host_gen)
        return;
    f.host_gen = gen;
    if (bytes != std::size_t(f.w) * f.h * 4) {
        set_error("a field of " + std::to_string(std::size_t(f.w) * f.h) +
                  " floats was published " + std::to_string(bytes / 4) +
                  " — the extent and the Sync disagree");
        return;
    }
    std::memcpy(f.shadow.data(), data, bytes);
    f.dirty = true;
}

void prepare(impl::SceneItem &it, nvrhi::ICommandList *cl) {
    FieldState &f = state_of(it);
    if (f.host)
        pull_host(f);
    if (f.w && f.dirty && !f.external) {
        cl->writeBuffer(f.buf, f.shadow.data(), f.shadow.size() * 4);
        f.dirty = false;
        ++it.stats->uploads;
    }
}

// (Re)make the binding set an external field draws with. The wrapped
// handle carries keepInitialState(ShaderResource): NVRHI then emits no
// barriers against memory the compute queue writes — cross-queue
// visibility is the semaphore's job, not a barrier's.
bool rebind_external(impl::SceneItem &it, FieldState &f,
                     const impl::PipelineEntry *pe) {
    gpud::Buffer *b = f.src.current();
    const std::uint64_t native = b ? gpud::vulkan::native_buffer(*b) : 0;
    if (!native) {
        f.bset = nullptr;
        f.bound_native = 0;
        return false;
    }
    if (native == f.bound_native && f.bset)
        return true;
    auto wrapped = it.gpu.dev->createHandleForNativeBuffer(
        nvrhi::ObjectTypes::VK_Buffer,
        nvrhi::Object(reinterpret_cast<void *>(native)),
        nvrhi::BufferDesc()
            .setByteSize(b->bytes())
            .setStructStride(4)
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setDebugName("field (external)"));
    if (!wrapped)
        return false;
    f.bset = it.gpu.dev->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(
                nvrhi::BindingSetItem::PushConstants(0, sizeof(DrawParams)))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, wrapped)),
        pe->layout);
    f.bound_native = native;
    return f.bset != nullptr;
}

void draw(impl::SceneItem &it, nvrhi::ICommandList *cl, nvrhi::IFramebuffer *fb,
          const Placement &at) {
    FieldState &f = state_of(it);
    if (!f.w)
        return;
    const impl::PipelineEntry *pe =
        pipeline_for(it.gpu, *it.pipelines, it.stats, &kFieldOps, fb);
    if (!pe)
        return;
    // Resolved AT the bind, not a phase earlier: the pointer a source
    // hands out is good for as long as the source says, and nothing
    // should sit between asking and using. An owned buffer binds once,
    // lazily, because the set needs the pipeline's layout.
    if (f.external) {
        if (!rebind_external(it, f, pe))
            return;
    } else if (!f.bset) {
        f.bset = it.gpu.dev->createBindingSet(
            nvrhi::BindingSetDesc()
                .addItem(
                    nvrhi::BindingSetItem::PushConstants(0, sizeof(DrawParams)))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, f.buf)),
            pe->layout);
    }
    if (!f.bset)
        return;

    DrawParams p{};
    p.w = f.w;
    p.h = f.h;
    p.cmap = f.cmap;
    p.lo = f.lo;
    p.hi = f.hi;
    p.uvscale[0] = at.sx;
    p.uvscale[1] = at.sy;
    p.uvoff[0] = (1.0f - at.sx) * 0.5f;
    p.uvoff[1] = (1.0f - at.sy) * 0.5f;
    cl->setGraphicsState(
        nvrhi::GraphicsState()
            .setPipeline(pe->pipeline)
            .setFramebuffer(fb)
            .addBindingSet(f.bset)
            .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
                nvrhi::Viewport(float(at.tw), float(at.th)))));
    cl->setPushConstants(&p, sizeof p);
    cl->draw(nvrhi::DrawArguments().setVertexCount(3));
    ++it.stats->draws;
}

void release(impl::SceneItem &it) {
    delete static_cast<FieldState *>(it.state);
    it.state = nullptr;
}

bool grid(const impl::SceneItem &it, Extent2 *out) {
    const FieldState &f = state_of(it);
    if (!f.w)
        return false;
    *out = {f.w, f.h};
    return true;
}

} // namespace

// clang-format off: the table reads as a table
const KindOps kFieldOps{
    .name = "field",
    .prepare = prepare,
    .draw = draw,
    .release = release,
    .grid = grid,
    .vs = {display_vsmain_spirv, display_vsmain_spirv_len, "vsmain"},
    .fs = {display_fsmain_spirv, display_fsmain_spirv_len, "fsmain"},
    .blend = {},
    .storage_stage = nvrhi::ShaderType::Pixel,
    .push_bytes = sizeof(DrawParams),
};
// clang-format on

namespace impl {

namespace {

// A field that owns its buffer: pushed by Update, or fed by a host
// source the frame asks. The one difference is who fills the shadow.
Field field_new(Scene s, const FieldDesc &d, HostSource host) {
    SceneState *sc = static_cast<SceneState *>(s.p);
    if (!sc)
        return {};
    if (d.dtype != DType::f32)
        return set_error("fields hold f32 values only for now"), Field{};
    if (!d.extent.w || !d.extent.h)
        return set_error("a field needs a non-zero extent"), Field{};

    const std::uint32_t bytes = d.extent.w * d.extent.h * 4;
    nvrhi::BufferHandle buf = sc->gpu.dev->createBuffer(
        nvrhi::BufferDesc()
            .setByteSize(bytes)
            .setStructStride(4)
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setDebugName("field"));
    if (!buf)
        return set_error("field buffer: creation failed"), Field{};

    SceneItem &it = sc->items.emplace_back();
    it.gpu = sc->gpu;
    it.stats = sc->stats;
    it.pipelines = sc->pipelines;
    it.ops = &kFieldOps;
    auto *f = new FieldState;
    f->w = d.extent.w;
    f->h = d.extent.h;
    f->cmap = std::int32_t(d.map);
    f->lo = d.lo;
    f->hi = d.hi;
    f->buf = buf;
    f->host = host;
    f->shadow.assign(std::size_t(d.extent.w) * d.extent.h, 0.0f);
    it.state = f;
    return Field{&it};
}

} // namespace

Field field_create(Scene s, const FieldDesc &d) {
    return field_new(s, d, HostSource{});
}

Field field_from_host(Scene s, HostSource src, const FieldDesc &d) {
    if (!src)
        return set_error("a field needs a source that can answer — the "
                         "HostSource's fn is null"),
               Field{};
    return field_new(s, d, src);
}

bool field_update(Field f, const void *data, DType t, std::size_t count) {
    SceneItem *it = static_cast<SceneItem *>(f.p);
    // The kind check comes FIRST and is a pointer comparison, so a
    // handle that is not a field's is refused before anything is
    // dereferenced through it.
    if (!it || it->ops != &kFieldOps)
        return set_error("field_update: this is not a field handle"), false;
    if (!data)
        return set_error("field_update: null"), false;
    FieldState &fs = state_of(*it);
    if (fs.external)
        return set_error("this field reads a caller-owned source, "
                         "re-resolved each frame — update the producer, "
                         "not the field"),
               false;
    if (fs.host)
        return set_error("this field reads a Sync — publish to it, not "
                         "the field"),
               false;
    if (t != DType::f32)
        return set_error("fields hold f32 values only for now"), false;
    if (count != std::size_t(fs.w) * fs.h)
        return set_error("field_update: count must equal w*h"), false;
    std::memcpy(fs.shadow.data(), data, count * 4);
    fs.dirty = true;
    return true;
}

Field field_from_source(Scene s, gpud::BufferSource src, const FieldDesc &d) {
    SceneState *sc = static_cast<SceneState *>(s.p);
    if (!sc)
        return {};
    if (!src)
        return set_error("a field needs a source that can answer — the "
                         "BufferSource's fn is null"),
               Field{};
    if (d.dtype != DType::f32)
        return set_error("fields hold f32 values only for now"), Field{};
    if (!d.extent.w || !d.extent.h)
        return set_error("a field needs a non-zero extent"), Field{};

    SceneItem &it = sc->items.emplace_back();
    it.gpu = sc->gpu;
    it.stats = sc->stats;
    it.pipelines = sc->pipelines;
    it.ops = &kFieldOps;
    auto *f = new FieldState;
    f->w = d.extent.w;
    f->h = d.extent.h;
    f->cmap = std::int32_t(d.map);
    f->lo = d.lo;
    f->hi = d.hi;
    f->external = true;
    f->src = src;
    it.state = f;
    return Field{&it};
}

} // namespace impl
} // namespace sv
