// The three axes at the origin: +X red, +Y green, +Z blue.
//
// A real line list of six vertices, not a wireframe over triangles —
// asking the rasterizer for line-mode polygons draws the triangulation
// diagonals too, and the fix is always to give it lines in the first
// place.
//
// Regenerate the committed bytecode with shaders/regen.sh — never edit
// it by hand.

#include "world_view.slang"

struct AParams {
    float length; // world units
    float pad0, pad1, pad2;
};

[[vk::push_constant]] ConstantBuffer<AParams> ap;

struct A2F {
    float4 pos : SV_Position;
    float4 color : TEXCOORD0;
};

[shader("vertex")]
A2F vsmain(uint vI : SV_VertexID) {
    const uint axis = vI / 2u;
    const bool tip = (vI % 2u) == 1u;

    float3 dir = float3(0.0f, 0.0f, 0.0f);
    float3 rgb = float3(0.0f, 0.0f, 0.0f);
    if (axis == 0u) {
        dir = float3(1.0f, 0.0f, 0.0f);
        rgb = float3(0.90f, 0.25f, 0.25f);
    } else if (axis == 1u) {
        dir = float3(0.0f, 1.0f, 0.0f);
        rgb = float3(0.30f, 0.85f, 0.35f);
    } else {
        dir = float3(0.0f, 0.0f, 1.0f);
        rgb = float3(0.35f, 0.55f, 0.95f);
    }

    A2F o;
    o.pos = mul(vp.world_to_clip,
                float4(tip ? dir * ap.length : float3(0.0f, 0.0f, 0.0f), 1.0f));
    o.color = float4(rgb, 1.0f);
    return o;
}

[shader("fragment")]
float4 fsmain(A2F v) : SV_Target { return v.color; }
