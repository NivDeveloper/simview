// A channel, whole: the upload from a host shadow, the pull from a
// Sync, the wrap of a producer's buffer, and the readback that gives a
// device buffer a host copy one frame late.

#include "Channel.h"

#include "../core/Error.h"
#include "World.h"

#include <gpud/Vulkan.h>

#include <string>

namespace sv {
namespace impl {

namespace {

// Matches readback.slang's RParams.
struct ReadbackParams {
    std::uint32_t count;
    std::uint32_t pad0, pad1, pad2;
};

void channel_pull_host(Channel &ch, const Gpu &gpu, const char *name) {
    std::size_t bytes = 0;
    std::uint64_t gen = 0;
    const void *data = ch.host.fn(ch.host.user, &bytes, &gen);
    if (!data || gen == ch.host_gen)
        return;
    ch.host_gen = gen;
    const std::size_t stride = ch.width * sizeof(float);
    if (bytes % stride) {
        const std::string n = std::to_string(bytes / sizeof(float));
        return set_error(
            ch.width == 3
                ? std::string(name) + " are xyz triples, and " + n +
                      " floats were published — not a multiple of three"
                : std::string(name) + " are " + std::to_string(ch.width) +
                      " floats each, and " + n + " floats were published");
    }
    channel_upload(ch, gpu, name, static_cast<const float *>(data),
                   bytes / stride);
}

} // namespace

// The byte count IS the element count: `width` floats each, whichever
// source they came from.
bool channel_upload(Channel &ch, const Gpu &gpu, const char *name,
                    const float *data, std::size_t count) {
    const std::size_t stride = ch.width * sizeof(float);
    if (!count) {
        ch.count = 0; // an empty channel is not an error
        return true;
    }
    if (count > ch.capacity) {
        ch.buf = gpu.dev->createBuffer(
            nvrhi::BufferDesc()
                .setByteSize(count * stride)
                .setStructStride(4)
                .setInitialState(nvrhi::ResourceStates::ShaderResource)
                .setKeepInitialState(true)
                .setDebugName(name));
        if (!ch.buf) {
            ch.capacity = ch.count = 0;
            return set_error(std::string(name) + ": buffer creation failed"),
                   false;
        }
        ch.capacity = count;
    }
    ch.shadow.assign(data, data + count * ch.width);
    ch.count = count;
    ch.dirty = true;
    return true;
}

bool channel_prepare(Channel &ch, const Gpu &gpu, nvrhi::ICommandList *cl,
                     Stats *stats, const char *name) {
    if (ch.host)
        channel_pull_host(ch, gpu, name);
    if (!ch.dirty || ch.external || !ch.buf || !ch.count)
        return false;
    cl->writeBuffer(ch.buf, ch.shadow.data(),
                    ch.count * ch.width * sizeof(float));
    ch.dirty = false;
    ++stats->uploads;
    return true;
}

// Ask the producer where its data is NOW and wrap it. The size is
// where the count comes from, so nothing sits between asking and using.
void channel_resolve(Channel &ch, const Gpu &gpu, const char *name) {
    if (!ch.external)
        return;
    gpud::Buffer *b = ch.src.current();
    const std::uint64_t native = b ? gpud::vulkan::native_buffer(*b) : 0;
    ch.count = b ? b->bytes() / (ch.width * sizeof(float)) : 0;
    if (!native || !ch.count) {
        ch.wrapped = nullptr;
        return;
    }
    ch.wrapped = gpu.dev->createHandleForNativeBuffer(
        nvrhi::ObjectTypes::VK_Buffer,
        nvrhi::Object(reinterpret_cast<void *>(native)),
        nvrhi::BufferDesc()
            .setByteSize(b->bytes())
            .setStructStride(4)
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            .setDebugName(name));
}

// A device buffer's host copy, ONE FRAME LATE: recorded now, mapped at
// the next prepare once its frame has been waited for, so the map never
// blocks. The producer's buffer has no transfer usage: a shader reads it.
void channel_readback(Channel &ch, WorldItem &it, nvrhi::ICommandList *cl,
                      const char *name) {
    const Gpu &gpu = it.gpu;
    if (!ch.external || !ch.wrapped || !ch.count || !it.owner)
        return;
    const std::size_t floats = ch.count * ch.width;
    if (ch.staged && ch.staging) {
        if (const void *p =
                gpu.dev->mapBuffer(ch.staging, nvrhi::CpuAccessMode::Read)) {
            const float *f = static_cast<const float *>(p);
            ch.shadow.assign(f, f + ch.staged_count * ch.width);
            gpu.dev->unmapBuffer(ch.staging);
        }
        ch.staged = false;
    }

    nvrhi::IComputePipeline *pipe = world_readback(*it.owner);
    if (!pipe)
        return;
    const std::size_t bytes = floats * sizeof(float);
    if (!ch.staging || ch.staging_bytes < bytes) {
        ch.scratch = gpu.dev->createBuffer(
            nvrhi::BufferDesc()
                .setByteSize(bytes)
                .setStructStride(4)
                .setCanHaveUAVs(true)
                .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                .setKeepInitialState(true)
                .setDebugName(name));
        ch.staging =
            gpu.dev->createBuffer(nvrhi::BufferDesc()
                                      .setByteSize(bytes)
                                      .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                      .setDebugName(name));
        ch.staging_bytes = bytes;
        if (!ch.scratch || !ch.staging)
            return set_error(std::string(name) + ": buffer creation failed");
    }

    // The wrapped handle is this frame's, so the set is too.
    nvrhi::BindingSetHandle bset = gpu.dev->createBindingSet(
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, ch.wrapped))
            .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(1, ch.scratch))
            .addItem(nvrhi::BindingSetItem::PushConstants(
                2, sizeof(ReadbackParams))),
        it.owner->readback_layout);
    if (!bset)
        return;
    const ReadbackParams rp{std::uint32_t(floats), 0, 0, 0};
    cl->setComputeState(
        nvrhi::ComputeState().setPipeline(pipe).addBindingSet(bset));
    cl->setPushConstants(&rp, sizeof rp);
    cl->dispatch(std::uint32_t((floats + 255) / 256));
    cl->copyBuffer(ch.staging, 0, ch.scratch, 0, bytes);
    ch.staged = true;
    ch.staged_count = ch.count;
}

} // namespace impl
} // namespace sv
