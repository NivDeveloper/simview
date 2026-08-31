// A cloud drawn as geometry, from the LIGHT's side: the same instanced
// mesh, one depth value per fragment.
//
// Nothing here needs the normal, the colour or the value buffer — a
// depth pass reads position and nothing else, which is most of why it
// is cheap.
//
// Regenerate the committed bytecode with shaders/regen.sh — never edit
// it by hand.

#define SV_SHADOW_PASS 1
#include "world_view.slang"

// MUST MATCH src/world/Cloud.cpp's CloudParams, field for field.
struct SParams {
    float4 color;
    float radius;
    uint count;
    uint mode;
    uint map;
    float map_scale;
    float pad0, pad1, pad2;
};

[[vk::binding(1, 0)]] StructuredBuffer<float> positions;
[[vk::binding(3, 0)]] StructuredBuffer<float> mesh; // xyz nx ny nz
[[vk::push_constant]] ConstantBuffer<SParams> mp;

struct S2F {
    float4 pos : SV_Position;
};

[shader("vertex")] S2F vsmain(uint vI : SV_VertexID, uint iI : SV_InstanceID) {
    S2F o;
    if (iI >= mp.count) {
        o.pos = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return o;
    }

    const float3 v =
        float3(mesh[vI * 6u + 0u], mesh[vI * 6u + 1u], mesh[vI * 6u + 2u]);
    const float3 centre =
        float3(positions[iI * 3u + 0u], positions[iI * 3u + 1u],
               positions[iI * 3u + 2u]);

    o.pos =
        mul(vp.light_to_clip, float4(centre + v * (mp.radius * 2.0f), 1.0f));
    return o;
}

    [shader("fragment")] void fsmain(S2F v) {}
