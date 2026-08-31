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
    float4 camera_pos; // xyz, w unused
    float4 viewport;   // w, h, 1/w, 1/h
    float4 znear;      // znear, unused
};

[[vk::binding(0, 0)]] ConstantBuffer<ViewParams> vp;
