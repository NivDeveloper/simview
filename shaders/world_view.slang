// The per-view block every world shader binds at set 0, binding 0.
//
// MUST MATCH src/world/World.cpp's ViewConstants, field for field:
// slangc compiles each shader standalone, so this file is included by
// the three world shaders and nothing checks the C++ side for us. The
// matrices are uploaded column-major and declared column_major here —
// the transpose bug this prevents renders a scene that is almost right,
// which is the worst kind.

struct ViewParams {
    column_major float4x4 world_to_clip;
    column_major float4x4 world_to_view;
    column_major float4x4 view_to_clip;
    column_major float4x4 clip_to_world;
    float4 camera_pos;    // xyz, w unused
    float4 viewport;      // w, h, 1/w, 1/h
    float4 depth;         // znear, zfar, 1 if orthographic, unused
    float4 light_dir[4];  // xyz toward the light IN VIEW SPACE, w intensity
    float4 light_rgb[4];  // rgb, w unused
    float4 ambient;       // rgb, w = how many lights are set
    column_major float4x4 light_to_clip; // world -> the caster's clip
    float4 shadow;        // sample the map, 1/map px, bias, ground is lit
};

[[vk::binding(0, 0)]] ConstantBuffer<ViewParams> vp;

#ifndef SV_SHADOW_PASS
// The caster's depth map, and a COMPARISON sampler: the hardware runs
// the depth test per texel and filters the results, so four taps give
// a soft edge where four ordinary taps would give four hard ones.
//
// Declared only outside the shadow pass. A shader drawing INTO the map
// must not have it in its binding layout, and an unused declaration
// that survives into the SPIR-V would put it there.
[[vk::binding(8, 0)]] Texture2D<float> shadow_map;
[[vk::binding(9, 0)]] SamplerComparisonState shadow_sampler;

// How much of the light reaches this world point: 1 lit, 0 fully
// shadowed, in between at an edge.
//
// Two conventions have to agree here and neither is visible in the
// result when it is wrong — a mirrored shadow still looks like a
// shadow. The viewport is flipped by the renderer, so +y in clip space
// is the TOP row of the map and v runs the other way; and the shadow
// map is FORWARD-Z, unlike every other depth buffer here, because the
// comparison sampler's test is hardcoded to LESS. Nearer the light is
// therefore SMALLER, and a fragment is lit when its own depth is
// under what the map holds.
float shadow_at(float3 world_pos) {
    if (vp.shadow.x < 0.5f)
        return 1.0f;

    const float4 lc = mul(vp.light_to_clip, float4(world_pos, 1.0f));
    if (lc.w <= 0.0f)
        return 1.0f;
    const float3 ndc = lc.xyz / lc.w;
    const float2 uv = float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);

    // Outside the fitted box is outside what the map knows, and the
    // honest answer there is "lit" — the border colour says the same,
    // this is the case where the projection put us off the map
    // entirely rather than a texel past its edge.
    if (any(uv < 0.0f) || any(uv > 1.0f) || ndc.z < 0.0f || ndc.z > 1.0f)
        return 1.0f;

    const float d = ndc.z - vp.shadow.z;
    const float t = vp.shadow.y;

    // Four taps on a half-texel diagonal. The comparison sampler
    // filters each one across 2x2, so this is a 3x3-ish kernel for
    // four samples rather than nine.
    float sum = 0.0f;
    sum += shadow_map.SampleCmpLevelZero(shadow_sampler,
                                         uv + float2(-0.5f, -0.5f) * t, d);
    sum += shadow_map.SampleCmpLevelZero(shadow_sampler,
                                         uv + float2(0.5f, -0.5f) * t, d);
    sum += shadow_map.SampleCmpLevelZero(shadow_sampler,
                                         uv + float2(-0.5f, 0.5f) * t, d);
    sum += shadow_map.SampleCmpLevelZero(shadow_sampler,
                                         uv + float2(0.5f, 0.5f) * t, d);
    return sum * 0.25f;
}

// Directions arrive already in view space, which is where an impostor
// knows its own normal — the alternative is a normal matrix in every
// shader that shades anything.
//
// The shadow scales the DIRECTIONAL terms only: ambient is what
// reaches a surface the caster cannot see, so shadowing it would make
// a shadow black rather than dark.
float3 lit_by(float3 n, float shade) {
    float3 sum = vp.ambient.rgb;
    const uint count = uint(vp.ambient.w);
    for (uint i = 0u; i < count; ++i)
        sum += vp.light_rgb[i].rgb *
               (vp.light_dir[i].w * saturate(dot(n, vp.light_dir[i].xyz))) *
               shade;
    return sum;
}

float3 lit_by(float3 n) { return lit_by(n, 1.0f); }
#endif
