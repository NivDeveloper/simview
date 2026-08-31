// simview's segment pair: instanced quads, one per segment. Each
// instance is a thin rectangle from p0 to p1, WIDTH pixels across:
// SV_VertexID picks the corner, SV_InstanceID picks the segment. The
// same reason particles are quads applies — line topology is a
// portability trap, a quad is not.
//
// NVRHI layout: one set, binding 0 the vertex-stage storage buffer,
// params in the push-constant block. Regenerate the committed
// bytecode with shaders/regen.sh — never edit it by hand.

struct LParams {
    float x0, y0; // the scene range these positions live in
    float x1, y1;
    float2 fit;      // the field's (sx, sy): what makes the two align
    float2 viewport; // target pixels, to turn a width into NDC
    float4 color;
    float width; // in pixels
    float pad0, pad1, pad2;
};

// A segment is (x0, y0, x1, y1): a float4, 16-byte stride, matching
// four packed floats on the host.
[[vk::binding(0, 0)]] StructuredBuffer<float4> segments;
[[vk::push_constant]] ConstantBuffer<LParams> lp;

struct L2F {
    float4 pos : SV_Position;
    float across : TEXCOORD0; // -1..1 across the width, for a soft edge
    float4 color : TEXCOORD1; // carried from the vertex stage on purpose
};

float2 to_ndc(float2 p) {
    const float2 t = float2((p.x - lp.x0) / (lp.x1 - lp.x0),
                            (p.y - lp.y0) / (lp.y1 - lp.y0));
    float2 ndc = (t * 2.0f - 1.0f) / lp.fit;
    ndc.y = -ndc.y;
    return ndc;
}

[shader("vertex")] L2F vsmain(uint vI : SV_VertexID, uint iI : SV_InstanceID) {
    // Two triangles over the unit square: corner.x runs along the
    // segment, corner.y across it.
    const uint c = (vI < 3) ? vI : (vI == 3 ? 2 : (vI == 4 ? 1 : 3));
    const float along = float(c & 1);
    const float across = float((c >> 1) & 1) * 2.0f - 1.0f;

    const float4 s = segments[iI];
    const float2 a = to_ndc(s.xy);
    const float2 b = to_ndc(s.zw);

    // The perpendicular, in PIXEL space so the width is uniform on a
    // non-square target, then back to NDC per axis.
    const float2 px = float2(lp.viewport.x, lp.viewport.y) * 0.5f;
    const float2 d = (b - a) * px;
    const float len = max(length(d), 1e-6f);
    const float2 n = float2(-d.y, d.x) / len;
    const float2 offset = n * (lp.width * 0.5f) / px;

    L2F o;
    o.across = across;
    o.color = lp.color;
    o.pos = float4(lerp(a, b, along) + offset * across, 0.0f, 1.0f);
    return o;
}

    [shader("fragment")] float4 fsmain(L2F v)
    : SV_Target {
    // A soft edge across the width, like the disc's.
    const float a = v.color.a * saturate((1.0f - abs(v.across)) * 3.0f);
    return float4(v.color.rgb, a);
}
