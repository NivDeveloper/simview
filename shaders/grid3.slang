// The ground grid: one quad in the world's XY plane, recentred on the
// camera every frame so it never runs out, with lines that stay about
// two pixels wide at any zoom.
//
// Three numbers in here are the result of getting it wrong first, and
// changing any of them brings the failure back:
//   - the two tiers share ONE rgb and differ only in ALPHA. Two greys
//     look identical over a light background;
//   - the crossfade happens in the last fifth of a level, not across
//     it, or the hierarchy dissolves at most zooms;
//   - a cell must stay at least four line-widths wide (8 px against a
//     2 px line) or the transition moires.
//
// Regenerate the committed bytecode with shaders/regen.sh — never edit
// it by hand.

#include "world_view.slang"

struct GParams {
    float4 color; // rgb shared by both tiers; a is the thin tier's
    float size;   // half-extent of the quad, world units
    float cell;   // the finest cell, world units
    float thick_alpha;
    float pad0;
};

[[vk::push_constant]] ConstantBuffer<GParams> gp;

struct G2F {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;  // world xy, for the line math
    float2 cam : TEXCOORD1; // camera xy, for the falloff
};

[shader("vertex")] G2F vsmain(uint vI : SV_VertexID) {
    const uint c = (vI < 3u) ? vI : (vI == 3u ? 2u : (vI == 4u ? 1u : 3u));
    const float2 corner =
        float2(float(c & 1u), float((c >> 1u) & 1u)) * 2.0f - 1.0f;

    // Z-up: the ground is the XY plane, and the quad follows the
    // camera so its edge is never reached.
    const float2 xy = corner * gp.size + vp.camera_pos.xy;

    G2F o;
    o.pos = mul(vp.world_to_clip, float4(xy, 0.0f, 1.0f));
    o.uv = xy;
    o.cam = vp.camera_pos.xy;
    return o;
}

    [shader("fragment")] float4 fsmain(G2F v)
    : SV_Target {
    // How much world one pixel covers, from the screen-space
    // derivatives — this is what makes the line width independent of
    // the distance and the angle.
    float2 dudv = float2(length(float2(ddx(v.uv.x), ddy(v.uv.x))),
                         length(float2(ddx(v.uv.y), ddy(v.uv.y))));

    const float min_px = 11.0f; // >= 4x the line width, or it moires
    const float lod = max(0.0f, log10(length(dudv) * min_px / gp.cell) + 1.0f);
    const float fade = frac(lod);

    const float lod0 = gp.cell * pow(10.0f, floor(lod));
    const float lod1 = lod0 * 10.0f;
    const float lod2 = lod1 * 10.0f;

    dudv *= 2.0f; // a line is about two pixels
    float2 uv = v.uv + dudv * 0.5f;

    // Distance past the last line, ALWAYS POSITIVE. fmod carries the
    // sign of its dividend, so at negative world coordinates it
    // returns a negative remainder, saturate clamps that to zero and
    // the line vanishes — which is one whole quadrant of the plane
    // with no grid on it and two with only half of one. frac is
    // x - floor(x), so this is the same quantity without the hole.
    const float2 m0 = frac(uv / lod0) * lod0;
    const float2 m1 = frac(uv / lod1) * lod1;
    const float2 m2 = frac(uv / lod2) * lod2;

    // Coverage of each tier's nearest line, anti-aliased by the same
    // derivative that set the width.
    const float2 a0 = float2(1.0f, 1.0f) -
                      abs(saturate(m0 / dudv) * 2.0f - float2(1.0f, 1.0f));
    const float2 a1 = float2(1.0f, 1.0f) -
                      abs(saturate(m1 / dudv) * 2.0f - float2(1.0f, 1.0f));
    const float2 a2 = float2(1.0f, 1.0f) -
                      abs(saturate(m2 / dudv) * 2.0f - float2(1.0f, 1.0f));
    const float c0 = max(a0.x, a0.y);
    const float c1 = max(a1.x, a1.y);
    const float c2 = max(a2.x, a2.y);

    const float blend = smoothstep(0.45f, 1.0f, fade);
    float alpha = (c2 > 0.0f)
                      ? gp.thick_alpha
                      : ((c1 > 0.0f) ? lerp(gp.thick_alpha, gp.color.a, blend)
                                     : gp.color.a);
    const float cover =
        (c2 > 0.0f) ? c2 : ((c1 > 0.0f) ? c1 : c0 * (1.0f - blend));

    // Fade out well before the quad's edge, so the grid ends in
    // nothing rather than in a rim — and so the grazing far field,
    // where a cell is thinner than a pixel and no amount of
    // anti-aliasing saves it, is gone before it can shimmer.
    const float dist = length(v.uv - v.cam) / gp.size;
    const float falloff = 1.0f - smoothstep(0.33f, 1.0f, dist);
    alpha *= cover * falloff;

    // A shadow can only be seen on something LIT, and this quad is
    // the only ground the engine draws. Left transparent between its
    // lines there is nothing for a shadow to fall on: the pass runs,
    // the map fills, and the picture is unchanged.
    //
    // So where a light shines on the ground, the ground takes a tone —
    // and the shadow is that tone's ABSENCE rather than a darkening
    // laid over it, which is both what a shadow is and what keeps a
    // world with no casting light looking exactly as it did.
    const float shade = shadow_at(float3(v.uv, 0.0f));
    const float ga = vp.shadow.w * 0.55f * falloff * shade;

    // Union of the two coverages, each contributing its own colour —
    // the line over the ground, not instead of it. A line crossing a
    // shadow dims with it, or it reads as floating above.
    const float a = alpha + ga * (1.0f - alpha);
    if (a <= 0.0f)
        discard;
    const float3 line_rgb = gp.color.rgb * (0.45f + 0.55f * shade);
    const float3 rgb =
        (line_rgb * alpha + float3(0.22f, 0.23f, 0.26f) * ga * (1.0f - alpha)) /
        a;
    return float4(rgb, a);
}
