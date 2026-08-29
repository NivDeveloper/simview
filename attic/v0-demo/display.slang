// The display pair: a fullscreen triangle spun from the vertex index
// (no vertex buffers), and a cyclic hue-wheel colormap of the field.
// Fragment-stage SPIR-V sets per SDL_CreateGPUShader's contract:
// storage buffers in set 2, uniforms in set 3.

struct Params {
    uint side; // the field is side x side, row-major
};

[[vk::binding(0, 2)]] StructuredBuffer<float> field;
[[vk::binding(0, 3)]] ConstantBuffer<Params> params;

struct V2F {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
V2F vsmain(uint vI : SV_VertexID) {
    float2 t = float2((vI << 1) & 2, vI & 2);
    V2F o;
    o.uv = t;
    o.pos = float4(t * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

// h in [0,1) -> the wheel; a cyclic map for a cyclic variable.
float3 hue(float h) {
    float r = abs(h * 6.0f - 3.0f) - 1.0f;
    float g = 2.0f - abs(h * 6.0f - 2.0f);
    float b = 2.0f - abs(h * 6.0f - 4.0f);
    return clamp(float3(r, g, b), 0.0f, 1.0f);
}

[shader("fragment")]
float4 fsmain(V2F v) : SV_Target {
    uint x = min(uint(v.uv.x * params.side), params.side - 1);
    uint y = min(uint(v.uv.y * params.side), params.side - 1);
    float theta = field[y * params.side + x];
    float h = frac(theta * (1.0f / 6.28318530718f));
    return float4(hue(h) * 0.92f, 1.0f);
}
