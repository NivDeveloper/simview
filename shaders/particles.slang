// simview's particle pair: instanced quads, one per point. SDL warns
// that sized point topology is unsupported on D3D12 and to expect
// regret, so a point is two triangles: SV_VertexID picks the corner,
// SV_InstanceID picks the point.
//
// NVRHI layout: one set, binding 0 the vertex-stage storage buffer,
// params in the push-constant block. Regenerate the committed
// bytecode with shaders/regen.sh — never edit it by hand.

struct PParams {
    float x0, y0;      // the scene range these positions live in
    float x1, y1;
    float2 fit;        // the field's (sx, sy): what makes the two align
    float2 viewport;   // target pixels, to turn a radius into NDC
    float4 color;
    float radius;      // in pixels
    float pad0, pad1, pad2;
};

[[vk::binding(0, 0)]] StructuredBuffer<float2> points;
[[vk::push_constant]] ConstantBuffer<PParams> pp;

struct P2F {
    float4 pos : SV_Position;
    float2 local : TEXCOORD0; // -1..1 across the quad, for the disc
    float4 color : TEXCOORD1; // carried from the vertex stage on purpose
};

[shader("vertex")]
P2F vsmain(uint vI : SV_VertexID, uint iI : SV_InstanceID) {
    // Two triangles: 0,1,2, 2,1,3 over the unit square's corners.
    const uint c = (vI < 3) ? vI : (vI == 3 ? 2 : (vI == 4 ? 1 : 3));
    const float2 corner = float2(float(c & 1), float((c >> 1) & 1)) * 2.0f - 1.0f;

    const float2 p = points[iI];
    // Normalise into the range, then the SAME centred rect the field
    // occupies: ndc = (2t - 1) / fit, with y down like the field's.
    const float2 t = float2((p.x - pp.x0) / (pp.x1 - pp.x0),
                            (p.y - pp.y0) / (pp.y1 - pp.y0));
    float2 ndc = (t * 2.0f - 1.0f) / pp.fit;
    ndc.y = -ndc.y;

    // A pixel radius is a different NDC distance on each axis.
    const float2 r = float2(pp.radius * 2.0f / pp.viewport.x,
                            pp.radius * 2.0f / pp.viewport.y);

    P2F o;
    o.local = corner;
    o.color = pp.color;
    o.pos = float4(ndc + corner * r, 0.0f, 1.0f);
    return o;
}

// The colour rides in from the vertex stage: the push-constant block
// is declared with vertex visibility in the binding layout, so the
// fragment stage reads what the vertex stage carried.
[shader("fragment")]
float4 fsmain(P2F v) : SV_Target {
    // A disc, not a square, with a soft edge.
    const float d = length(v.local);
    if (d > 1.0f)
        discard;
    const float a = v.color.a * saturate((1.0f - d) * 4.0f);
    return float4(v.color.rgb, a);
}
