/// Editor overlay rounded-rect pixel shader (TextRenderer::AddRoundedRect).
///
/// Evaluates the exact signed distance to a rounded box at the fragment's
/// box-local position and composes, back to front:
///   1. a drop shadow — the same box offset by `shadowOffset`, blurred by a
///      Gaussian of standard deviation `shadowBlur`, clipped to the outside
///      of the box as CSS clips `box-shadow` to the border box;
///   2. the fill — coverage of the box with a one-pixel anti-aliasing band;
///   3. the border — a band of width `borderThickness` centred on the box
///      edge (AddRoundedRect pads the quad by half the thickness for it).
/// The result is straight alpha for the SrcAlpha / InvSrcAlpha pipeline.
///
/// The helpers are defined here rather than in a shared .hlsli: the shader
/// source is embedded (uzembed) and compiled from memory, where the default
/// include handler cannot resolve sibling headers.
///
/// @ref Quilez 2015 "2D distance functions" (iquilezles.org) — sdRoundedBox
/// @ref Wallace 2015 "Fast Rounded Rectangle Shadows" — a Gaussian-blurred
///      box edge is an erf of the distance
/// @ref Abramowitz & Stegun 1964 "Handbook of Mathematical Functions" eq.
///      7.1.27 — erf(x) ≈ 1 − 1/(1 + a1 x + a2 x² + a3 x³ + a4 x⁴)⁴,
///      |error| ≤ 5e-4
/// @ref W3C 2023 "CSS Backgrounds and Borders Module Level 3" §7.1 — outer
///      box-shadow is clipped inside the border box
/// @constants a1..a4 = 0.278393, 0.230389, 0.000972, 0.078108 (A&S 7.1.27);
///            1/√2 = 0.70710678 maps a Gaussian σ to the erf argument;
///            kMinAa = 1e-4 guards the derivative band against zero.

struct PSInput {
    float4 position     : SV_Position;
    float2 localPos     : TEXCOORD0;
    nointerpolation float2 halfExtent   : TEXCOORD1;
    nointerpolation float2 shadowOffset : TEXCOORD2;
    nointerpolation float3 params       : TEXCOORD3;   ///< x corner radius, y border thickness, z shadow blur.
    nointerpolation float4 fillColor    : COLOR0;
    nointerpolation float4 borderColor  : COLOR1;
    nointerpolation float4 shadowColor  : COLOR2;
};

static const float kMinAa = 1e-4;

/// Signed distance from `p` to a box of half-size `b` with corner radius `r`
/// (negative inside). The radius is clamped to the box so a pill shape stays
/// exact. @ref Quilez 2015
float SdRoundedBox(float2 p, float2 b, float r) {
    r = clamp(r, 0.0, min(b.x, b.y));
    const float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

/// Fill coverage: 1 inside, 0 outside, a one-pixel band on the edge.
float AaFillFromSdf(float d, float aa) {
    return 1.0 - smoothstep(-0.5 * aa, 0.5 * aa, d);
}

/// Coverage of a band of width `thickness` centred on the edge (|d| ≤ t/2).
float AaBorderFromSdf(float d, float thickness, float aa) {
    if (thickness <= 0.0) return 0.0;
    return 1.0 - smoothstep(-0.5 * aa, 0.5 * aa, abs(d) - 0.5 * thickness);
}

/// erf approximation, @ref Abramowitz & Stegun 1964 eq. 7.1.27.
float ErfApprox(float x) {
    const float s = sign(x);
    const float a = abs(x);
    float t = 1.0 + (0.278393 + (0.230389 + (0.000972 + 0.078108 * a) * a) * a) * a;
    t *= t;
    return s - s / (t * t);
}

/// Shadow coverage of a box edge blurred by a Gaussian of std-dev `sigma`:
/// 0.5 · erfc(d / (σ√2)) — 1 deep inside, 0.5 on the edge, → 0 outside.
/// A zero blur degenerates to the hard box. @ref Wallace 2015
float ShadowFalloff(float d, float sigma, float aa) {
    if (sigma <= 0.0) return AaFillFromSdf(d, aa);
    return 0.5 - 0.5 * ErfApprox(d * (0.70710678 / sigma));
}

/// Porter-Duff "over" of straight-alpha `src` onto premultiplied `dst`.
float4 OverPremultiplied(float4 src, float4 dst) {
    return float4(src.rgb * src.a + dst.rgb * (1.0 - src.a),
                  src.a + dst.a * (1.0 - src.a));
}

float4 main(PSInput input) : SV_Target {
    const float cornerRadius    = input.params.x;
    const float borderThickness = input.params.y;
    const float shadowBlur      = input.params.z;

    const float d  = SdRoundedBox(input.localPos, input.halfExtent, cornerRadius);
    const float aa = max(fwidth(d), kMinAa);
    const float boxCoverage = AaFillFromSdf(d, aa);

    float4 result = float4(0.0, 0.0, 0.0, 0.0);   // premultiplied

    if (input.shadowColor.a > 0.0 && (shadowBlur > 0.0 || any(input.shadowOffset != 0.0))) {
        const float ds = SdRoundedBox(input.localPos - input.shadowOffset,
                                      input.halfExtent, cornerRadius);
        const float shadow = ShadowFalloff(ds, shadowBlur, aa) * (1.0 - boxCoverage);
        result = OverPremultiplied(float4(input.shadowColor.rgb, input.shadowColor.a * shadow), result);
    }

    result = OverPremultiplied(float4(input.fillColor.rgb, input.fillColor.a * boxCoverage), result);

    const float border = AaBorderFromSdf(d, borderThickness, aa);
    if (border > 0.0) {
        result = OverPremultiplied(float4(input.borderColor.rgb, input.borderColor.a * border), result);
    }

    // Back to straight alpha for the SrcAlpha / InvSrcAlpha blend state.
    return result.a > kMinAa ? float4(result.rgb / result.a, result.a) : float4(0.0, 0.0, 0.0, 0.0);
}
