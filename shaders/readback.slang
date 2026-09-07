// A device buffer the renderer may only READ through a shader, copied
// into one it may copy: a compute library's buffers carry no transfer
// usage, so the host copy a pick needs starts here.
//
// Regenerate the committed bytecode with shaders/regen.sh — never edit
// it by hand.

struct RParams {
    uint count; // floats
    uint pad0, pad1, pad2;
};

[[vk::binding(0, 0)]] StructuredBuffer<float> src;
[[vk::binding(1, 0)]] RWStructuredBuffer<float> dst;
[[vk::push_constant]] ConstantBuffer<RParams> rp;

[shader("compute")]
[numthreads(256, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    if (tid.x < rp.count)
        dst[tid.x] = src[tid.x];
}
