// Segments over a point set — springs, bonds, trajectories — drawn as
// thin quads of a fixed pixel width: SV_VertexID / 6 picks the edge and
// % 6 the corner, and an edge is two indices into the positions. A
// masked-out edge collapses to nothing.
//
// The perpendicular is taken in PIXEL space so the width holds at any
// depth and on a non-square target, then put back in clip space scaled
// by w, which keeps the quad's depth the segment's.
//
// Regenerate the committed bytecode with shaders/regen.sh — never edit
// it by hand.

#include "world_view.slang"

// MUST MATCH src/world/Wire.cpp's WireParams, field for field.
struct WParams {
    float4 color;
    float width;    // pixels
    uint count;     // edges
    uint masked;    // 1 when `alive` holds one float per edge
    uint highlight; // the edge drawn brighter: hovered or followed, else ~0
};

[[vk::binding(1, 0)]] StructuredBuffer<float> positions; // xyz per point
[[vk::binding(2, 0)]] StructuredBuffer<uint> edges;      // two points per edge
[[vk::binding(3, 0)]] StructuredBuffer<float> alive;     // per edge, when masked
[[vk::push_constant]] ConstantBuffer<WParams> wp;

struct W2F {
    float4 pos : SV_Position;
    float across : TEXCOORD0; // -1..1 across the width, for a soft edge
    float4 color : TEXCOORD1;
};

float3 point_at(uint k) {
    return float3(positions[k * 3u + 0u], positions[k * 3u + 1u],
                  positions[k * 3u + 2u]);
}

[shader("vertex")]
W2F vsmain(uint vI : SV_VertexID) {
    const uint e = vI / 6u;
    const uint c6 = vI - e * 6u;
    W2F o;
    o.across = 0.0f;
    o.color = wp.color;
    if (e >= wp.count || (wp.masked != 0u && alive[e] <= 0.0f)) {
        o.pos = float4(0.0f, 0.0f, 0.0f, 0.0f); // w = 0: clipped away
        return o;
    }
    // Two triangles over the unit square: `along` runs down the
    // segment, `across` from one edge of the quad to the other.
    const uint c = (c6 < 3u) ? c6 : (c6 == 3u ? 2u : (c6 == 4u ? 1u : 3u));
    const float along = float(c & 1u);
    const float across = float((c >> 1u) & 1u) * 2.0f - 1.0f;

    const float4 a = mul(vp.world_to_clip,
                         float4(point_at(edges[e * 2u + 0u]), 1.0f));
    const float4 b = mul(vp.world_to_clip,
                         float4(point_at(edges[e * 2u + 1u]), 1.0f));
    const float2 an = a.xy / max(a.w, 1e-6f);
    const float2 bn = b.xy / max(b.w, 1e-6f);
    const float2 px = vp.viewport.xy * 0.5f;
    const float2 d = (bn - an) * px;
    const float len = max(length(d), 1e-6f);
    const float2 nrm = float2(-d.y, d.x) / len;
    const float2 offset = nrm * (wp.width * 0.5f) / px;

    float4 p = lerp(a, b, along);
    p.xy += offset * across * p.w;
    o.pos = p;
    o.across = across;
    if (e == wp.highlight)
        o.color = float4(lerp(wp.color.rgb, float3(1.0f, 1.0f, 1.0f), 0.55f),
                         wp.color.a);
    return o;
}

[shader("fragment")]
float4 fsmain(W2F v) : SV_Target {
    // A soft edge across the width, like the 2D line's.
    const float a = v.color.a * saturate((1.0f - abs(v.across)) * 3.0f);
    return float4(v.color.rgb, a);
}
