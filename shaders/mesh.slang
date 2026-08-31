// A cloud drawn as real geometry: one instance of a built-in mesh per
// point, in one indexed draw.
//
// Instanced here and vertex-pulled in cloud.slang, for the reason each
// is faster at what it does: a billboard is six vertices and the
// per-instance step costs more than it saves, while a sphere is a
// hundred and the instance step is amortized over all of them.
//
// The mesh is a unit shape scaled by the radius and moved to the
// point, which is a uniform scale and a translation — so a normal
// survives it untouched, and there is no normal matrix here or
// anywhere. Rotating it into view space is the view matrix's rotation,
// which is orthonormal, so that survives too.
//
// Regenerate the committed bytecode with shaders/regen.sh — never edit
// it by hand.

#include "world_view.slang"

// MUST MATCH src/world/Cloud.cpp's CloudParams, field for field —
// ONE push block serves both this and cloud.slang, so `mode` is here
// even though geometry has no use for it. Dropping it moved every
// field after it up four bytes and the colormap silently read the
// mode instead, which renders as a flat colour and looks like a
// missing buffer.
struct MParams {
    float4 color;
    float radius; // world units
    uint count;
    uint mode;  // cloud.slang's; unused here
    uint map;   // 0 uniform, 1 magnitude, 2 direction, 3 components
    float map_scale;
    float pad0, pad1, pad2;
};

[[vk::binding(1, 0)]] StructuredBuffer<float> positions; // xyz per instance
[[vk::binding(2, 0)]] StructuredBuffer<float> values;    // xyz per instance
[[vk::binding(3, 0)]] StructuredBuffer<float> mesh;      // xyz nx ny nz
[[vk::push_constant]] ConstantBuffer<MParams> mp;

float3 turbo(float t) {
    t = saturate(t);
    const float t2 = t * t, t3 = t2 * t, t4 = t2 * t2, t5 = t4 * t;
    const float r = 0.13572138f + 4.61539260f * t - 42.66032258f * t2 +
                    132.13108234f * t3 - 152.94239396f * t4 + 59.28637943f * t5;
    const float g = 0.09140261f + 2.19418839f * t + 4.84296658f * t2 -
                    14.18503333f * t3 + 4.27729857f * t4 + 2.82956604f * t5;
    const float b = 0.10667330f + 12.64194608f * t - 60.58204836f * t2 +
                    110.36276771f * t3 - 89.90310912f * t4 + 27.34824973f * t5;
    return saturate(float3(r, g, b));
}

float4 mapped_color(uint i) {
    if (mp.map == 0u)
        return mp.color;
    const float3 v = float3(values[i * 3u + 0u], values[i * 3u + 1u],
                            values[i * 3u + 2u]);
    const float s = max(mp.map_scale, 1e-6f);
    if (mp.map == 1u)
        return float4(turbo(length(v) / s), mp.color.a);
    if (mp.map == 2u) {
        const float l = length(v);
        const float3 n = l > 1e-6f ? v / l : float3(0.0f, 0.0f, 0.0f);
        return float4(n * 0.5f + 0.5f, mp.color.a);
    }
    return float4(saturate(v / s * 0.5f + 0.5f), mp.color.a);
}

struct M2F {
    float3 wpos : TEXCOORD2; // where the shadow is asked about
    float4 pos : SV_Position;
    float3 normal : TEXCOORD0; // view space, where the lights are
    float4 color : TEXCOORD1;
};

[shader("vertex")]
M2F vsmain(uint vI : SV_VertexID, uint iI : SV_InstanceID) {
    M2F o;
    if (iI >= mp.count) {
        o.pos = float4(0.0f, 0.0f, 0.0f, 0.0f);
        o.normal = float3(0.0f, 0.0f, 1.0f);
        o.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return o;
    }

    const float3 v = float3(mesh[vI * 6u + 0u], mesh[vI * 6u + 1u],
                            mesh[vI * 6u + 2u]);
    const float3 n = float3(mesh[vI * 6u + 3u], mesh[vI * 6u + 4u],
                            mesh[vI * 6u + 5u]);
    const float3 centre =
        float3(positions[iI * 3u + 0u], positions[iI * 3u + 1u],
               positions[iI * 3u + 2u]);

    const float3 world = centre + v * (mp.radius * 2.0f);
    o.wpos = world;
    o.pos = mul(vp.world_to_clip, float4(world, 1.0f));
    o.normal = mul(vp.world_to_view, float4(n, 0.0f)).xyz;
    o.color = mapped_color(iI);
    return o;
}

[shader("fragment")]
float4 fsmain(M2F v) : SV_Target {
    return float4(v.color.rgb * lit_by(normalize(v.normal), shadow_at(v.wpos)),
                  v.color.a);
}
