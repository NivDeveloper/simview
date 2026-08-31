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
    uint map;  // 0 uniform, 1 magnitude, 2 direction, 3 components
    float map_scale;
    float pad0, pad1, pad2;
};

[[vk::binding(1, 0)]] StructuredBuffer<float> positions; // xyz per point
// The colour source: xyz per point, read only when `map` is not
// uniform. Bound ALWAYS — a cloud with no data of its own binds its
// positions here a second time, which costs nothing and keeps one
// binding layout for every cloud there is.
[[vk::binding(2, 0)]] StructuredBuffer<float> values;
[[vk::push_constant]] ConstantBuffer<CParams> cp;

// Turbo: a perceptually even blue-cyan-green-yellow-red ramp, as a
// fifth-order fit. Rainbow-shaped without rainbow's false edges.
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
    if (cp.map == 0u)
        return cp.color;
    const float3 v =
        float3(values[i * 3u + 0u], values[i * 3u + 1u], values[i * 3u + 2u]);
    const float s = max(cp.map_scale, 1e-6f);
    if (cp.map == 1u)
        return float4(turbo(length(v) / s), cp.color.a);
    if (cp.map == 2u) {
        const float l = length(v);
        const float3 n = l > 1e-6f ? v / l : float3(0.0f, 0.0f, 0.0f);
        return float4(n * 0.5f + 0.5f, cp.color.a);
    }
    return float4(saturate(v / s * 0.5f + 0.5f), cp.color.a);
}

struct C2F {
    float3 world : TEXCOORD3; // where the shadow is asked about
    float4 pos : SV_Position;
    float2 local : TEXCOORD0; // -1..1 across the quad
    float4 color : TEXCOORD1;
    float mode : TEXCOORD2;
};

[shader("vertex")] C2F vsmain(uint vI : SV_VertexID) {
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
    const float2 corner =
        float2(float(c & 1u), float((c >> 1u) & 1u)) * 2.0f - 1.0f;

    // Scalar indexing, not StructuredBuffer<float3>: a three-float
    // element in a storage buffer is padded to four by the layout
    // rules, and a packed xyz array would then read every point wrong.
    const float3 centre = float3(positions[i * 3u + 0u], positions[i * 3u + 1u],
                                 positions[i * 3u + 2u]);

    float4 pv = mul(vp.world_to_view, float4(centre, 1.0f));
    pv.xy += corner * cp.radius;

    // The same corner in WORLD units, for the shadow lookup. Rows 0
    // and 1 of the view matrix are the camera's right and up, and
    // they are orthonormal, so this is the quad's own geometry rather
    // than an approximation of it.
    o.world = centre + vp.world_to_view[0].xyz * (corner.x * cp.radius) +
              vp.world_to_view[1].xyz * (corner.y * cp.radius);
    o.pos = mul(vp.view_to_clip, pv);
    o.local = corner;
    o.color = mapped_color(i);
    o.mode = float(cp.mode);
    return o;
}

    [shader("fragment")] float4 fsmain(C2F v)
    : SV_Target {
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
    // the scene the difference never shows. It is a VIEW-space normal,
    // which is the space the lights arrive in.
    const float3 n = float3(v.local, sqrt(saturate(1.0f - d2)));

    // Back to world for the shadow lookup. The view matrix's rows are
    // the camera's axes and are orthonormal, so its transpose is its
    // inverse and this is three multiply-adds rather than a matrix.
    const float3 wn = n.x * vp.world_to_view[0].xyz +
                      n.y * vp.world_to_view[1].xyz +
                      n.z * vp.world_to_view[2].xyz;
    return float4(v.color.rgb * lit_by(n, shadow_at(v.world, wn)), 1.0f);
}
