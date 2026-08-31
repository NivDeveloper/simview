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
};

// Directions arrive already in view space, which is where an impostor
// knows its own normal — the alternative is a normal matrix in every
// shader that shades anything.
float3 lit_by(float3 n) {
    float3 sum = vp.ambient.rgb;
    const uint count = uint(vp.ambient.w);
    for (uint i = 0u; i < count; ++i)
        sum += vp.light_rgb[i].rgb *
               (vp.light_dir[i].w * saturate(dot(n, vp.light_dir[i].xyz)));
    return sum;
}

[[vk::binding(0, 0)]] ConstantBuffer<ViewParams> vp;
