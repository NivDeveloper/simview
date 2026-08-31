// A cloud of billboards, from the LIGHT's side: the same points, the
// same radius, one depth value each.
//
// The quad faces the LIGHT here, not the camera. A view-facing quad
// seen from anywhere else is edge-on, and an edge-on billboard casts
// nothing at all — which looks exactly like a shadow pass that never
// ran. The light's axes come out of its own matrix rather than being
// passed separately, so the two cannot disagree.
//
// A fragment stage even though nothing is written: without the discard
// a sphere casts a square.
//
// Regenerate the committed bytecode with shaders/regen.sh — never edit
// it by hand.

#define SV_SHADOW_PASS 1
#include "world_view.slang"

// MUST MATCH src/world/Cloud.cpp's CloudParams, field for field — one
// push block serves the colour shaders and this one.
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
[[vk::push_constant]] ConstantBuffer<SParams> cp;

struct S2F {
    float4 pos : SV_Position;
    float2 local : TEXCOORD0;
};

[shader("vertex")] S2F vsmain(uint vI : SV_VertexID) {
    const uint i = vI / 6u;
    const uint v = vI % 6u;

    S2F o;
    if (i >= cp.count) {
        o.pos = float4(0.0f, 0.0f, 0.0f, 0.0f);
        o.local = float2(0.0f, 0.0f);
        return o;
    }

    const uint c = (v < 3u) ? v : (v == 3u ? 2u : (v == 4u ? 1u : 3u));
    const float2 corner =
        float2(float(c & 1u), float((c >> 1u) & 1u)) * 2.0f - 1.0f;

    const float3 centre = float3(positions[i * 3u + 0u], positions[i * 3u + 1u],
                                 positions[i * 3u + 2u]);

    // Rows 0 and 1 of the light's matrix are its right and up, scaled
    // by the orthographic fit; normalizing recovers the axes.
    const float3 lr = normalize(vp.light_to_clip[0].xyz);
    const float3 lu = normalize(vp.light_to_clip[1].xyz);
    const float3 world =
        centre + lr * (corner.x * cp.radius) + lu * (corner.y * cp.radius);

    o.pos = mul(vp.light_to_clip, float4(world, 1.0f));
    o.local = corner;
    return o;
}

    [shader("fragment")] void fsmain(S2F v) {
    if (dot(v.local, v.local) > 1.0f)
        discard;
}
