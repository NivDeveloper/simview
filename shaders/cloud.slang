// A cloud: N points in world space, drawn as camera-facing quads.
//
// No vertex buffer and no instancing: one draw of 6*N vertices, where
// SV_VertexID / 6 picks the point and % 6 the corner. Very small
// per-instance vertex counts starve the vertex front end, and the
// arithmetic here is two integer ops.
//
// The quad is built in VIEW space, where camera-facing means
// axis-aligned — no basis extracted from the matrix, so there is no
// transpose to get wrong. The radius is in WORLD units, so a cloud
// shrinks as the camera pulls away, which is what makes it read as
// three-dimensional at all.
//
// Regenerate the committed bytecode with shaders/regen.sh — never edit
// it by hand.

#include "world_view.slang"

struct CParams {
    float4 color;
    float radius; // world units
    uint count;
    uint mode; // 0 = solid impostor, 1 = flat disc
    float pad0;
};

[[vk::binding(1, 0)]] StructuredBuffer<float> positions; // xyz per point
[[vk::push_constant]] ConstantBuffer<CParams> cp;

struct C2F {
    float4 pos : SV_Position;
    float2 local : TEXCOORD0; // -1..1 across the quad
    float4 color : TEXCOORD1;
    float mode : TEXCOORD2;
};

[shader("vertex")]
C2F vsmain(uint vI : SV_VertexID) {
    const uint i = vI / 6u;
    const uint v = vI % 6u;

    C2F o;
    // A stale count would otherwise draw a triangle over whatever the
    // buffer happens to hold. w = 0 puts it outside clip space for good.
    if (i >= cp.count) {
        o.pos = float4(0.0f, 0.0f, 0.0f, 0.0f);
        o.local = float2(0.0f, 0.0f);
        o.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
        o.mode = 0.0f;
        return o;
    }

    // Two triangles: 0,1,2, 2,1,3 over the unit square's corners.
    const uint c = (v < 3u) ? v : (v == 3u ? 2u : (v == 4u ? 1u : 3u));
    const float2 corner = float2(float(c & 1u), float((c >> 1u) & 1u)) * 2.0f - 1.0f;

    // Scalar indexing, not StructuredBuffer<float3>: a three-float
    // element in a storage buffer is padded to four by the layout
    // rules, and a packed xyz array would then read every point wrong.
    const float3 centre = float3(positions[i * 3u + 0u],
                                 positions[i * 3u + 1u],
                                 positions[i * 3u + 2u]);

    float4 pv = mul(vp.world_to_view, float4(centre, 1.0f));
    pv.xy += corner * cp.radius;

    o.pos = mul(vp.view_to_clip, pv);
    o.local = corner;
    o.color = cp.color;
    o.mode = float(cp.mode);
    return o;
}

[shader("fragment")]
float4 fsmain(C2F v) : SV_Target {
    const float d2 = dot(v.local, v.local);
    if (d2 > 1.0f)
        discard;

    // Mode 1: a flat disc with a soft edge, for the blended passes
    // where a lit sphere would read as a hard-edged bead.
    if (v.mode > 0.5f) {
        const float a = v.color.a * saturate((1.0f - sqrt(d2)) * 4.0f);
        return float4(v.color.rgb, a);
    }

    // Mode 0: the sphere the quad stands in for. The normal is
    // analytic — the point of the disc is that the depth it writes is
    // the quad's, which keeps early-Z alive; at a radius small against
    // the scene the difference never shows.
    const float3 n = float3(v.local, sqrt(saturate(1.0f - d2)));
    const float lambert = saturate(n.z); // a light at the camera
    return float4(v.color.rgb * (0.3f + 0.7f * lambert), 1.0f);
}
