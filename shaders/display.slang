// simview's display pair: a fullscreen triangle from the vertex index
// and the field colormap. NVRHI layout: one set, binding 0 the
// fragment's storage buffer, params in the push-constant block.
// Regenerate the committed bytecode with shaders/regen.sh — never
// edit it by hand.

struct Params {
    uint w; // field extent
    uint h;
    int cmap; // Colormap enum value
    uint pad0;
    float lo; // value range -> colormap ends
    float hi;
    float2 uvscale; // letterbox: window uv -> field uv
    float2 uvoff;
};

[[vk::binding(0, 0)]] StructuredBuffer<float> field;
[[vk::push_constant]] ConstantBuffer<Params> params;

struct V2F {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")] V2F vsmain(uint vI : SV_VertexID) {
    float2 t = float2((vI << 1) & 2, vI & 2);
    V2F o;
    o.uv = t;
    o.pos = float4(t * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

// h in [0,1) -> the wheel; the cyclic map for cyclic quantities.
float3 hue(float x) {
    float r = abs(x * 6.0f - 3.0f) - 1.0f;
    float g = 2.0f - abs(x * 6.0f - 2.0f);
    float b = 2.0f - abs(x * 6.0f - 4.0f);
    return clamp(float3(r, g, b), 0.0f, 1.0f);
}

// Viridis by polynomial fit (degree 6, per the widely used
// approximation of the matplotlib colormap).
float3 viridis(float t) {
    const float3 c0 = float3(0.2777273f, 0.0054077f, 0.3340998f);
    const float3 c1 = float3(0.1050930f, 1.4046131f, 1.3845258f);
    const float3 c2 = float3(-0.3308618f, 0.2148179f, 0.0950944f);
    const float3 c3 = float3(-4.6342630f, -5.7991854f, -19.3324233f);
    const float3 c4 = float3(6.2282465f, 14.1799931f, 56.6905593f);
    const float3 c5 = float3(4.7763755f, -13.7451336f, -65.3529416f);
    const float3 c6 = float3(-5.4354569f, 4.6458543f, 26.3124352f);
    return c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
}

[shader("fragment")] float4 fsmain(V2F v) : SV_Target {
    float2 fuv = v.uv * params.uvscale + params.uvoff;
    if (any(fuv < 0.0f) || any(fuv >= 1.0f))
        return float4(0.09f, 0.09f, 0.10f, 1.0f); // the letterbox bars
    uint x = min(uint(fuv.x * params.w), params.w - 1);
    uint y = min(uint(fuv.y * params.h), params.h - 1);
    float t = (field[y * params.w + x] - params.lo) / (params.hi - params.lo);
    float3 c;
    if (params.cmap == 1)
        c = hue(frac(t));
    else if (params.cmap == 2)
        c = viridis(clamp(t, 0.0f, 1.0f));
    else
        c = float3(clamp(t, 0.0f, 1.0f).xxx);
    return float4(c, 1.0f);
}
