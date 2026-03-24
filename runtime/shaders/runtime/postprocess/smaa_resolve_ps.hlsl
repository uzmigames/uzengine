/// SMAA Pass 3 — Neighborhood Blending pixel shader.
///
/// Reads the original LDR scene color and the blend weight texture (from Pass 2),
/// then blends neighboring pixels according to the computed weights to produce
/// the final anti-aliased output.
///
/// @ref Jimenez 2012 "SMAA: Enhanced Subpixel Morphological Antialiasing"
///   — Section 3.3 Neighborhood Blending
/// @constants SMAA_THRESHOLD=0.1 (SMAA.hlsl:314), SMAA_MAX_SEARCH_STEPS=16 (SMAA.hlsl:316),
///   SMAA_MAX_SEARCH_STEPS_DIAG=8 (SMAA.hlsl:317), SMAA_CORNER_ROUNDING=25 (SMAA.hlsl:318)
/// @deviations NONE — uses upstream SMAA.hlsl unmodified via #include.
///   SMAA VS offsets computed in PS (see smaa_edge_ps.hlsl for rationale).

// ─────────────────────────────────────────────────────────────────────────────
// SMAA Configuration
// ─────────────────────────────────────────────────────────────────────────────

cbuffer SMAAConstants : register(b2) {
    float4 smaaRTMetrics; // (1/width, 1/height, width, height)
};

#define SMAA_RT_METRICS smaaRTMetrics

#define SMAA_CUSTOM_SL 1
#define SMAA_PRESET_HIGH 1

// ── Porting Macros ──────────────────────────────────────────────────────────

/// s2 = bilinear clamp (post-process static sampler in UzEngine root signature).
/// Used for both "point" and "linear" SMAA sampling. At exact texel centers
/// (where SMAA always samples for "point" reads), bilinear = point, so this
/// is correct. All 4 static samplers: s0=aniso wrap, s1=shadow comparison,
/// s2=bilinear clamp, s3=bilinear wrap. s2 is the only suitable one.
SamplerState smaaLinearSampler : register(s2);

#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(smaaLinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(smaaLinearSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(smaaLinearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.Sample(smaaLinearSampler, coord)
#define SMAASamplePoint(tex, coord) tex.SampleLevel(smaaLinearSampler, coord, 0)
#define SMAASampleOffset(tex, coord, offset) tex.Sample(smaaLinearSampler, coord, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#define SMAAGather(tex, coord) tex.Gather(smaaLinearSampler, coord, 0)

#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1

#include "../../../thirdparty/smaa/SMAA.hlsl"

// ─────────────────────────────────────────────────────────────────────────────
// Input / Output
// ─────────────────────────────────────────────────────────────────────────────

/// LDR scene color (same input as Pass 1, after tonemap).
Texture2D colorTex : register(t0);

/// Blend weight texture from Pass 2 (RGBA8).
Texture2D blendTex : register(t1);

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader
// ─────────────────────────────────────────────────────────────────────────────

float4 main(FSInput input) : SV_Target {
    float2 texcoord = input.texcoord;

    // Compute neighborhood blending VS offset in the PS.
    // @ref Jimenez 2012 SMAA.hlsl:673-676 — SMAANeighborhoodBlendingVS
    float4 offset;
    SMAANeighborhoodBlendingVS(texcoord, offset);

    return SMAANeighborhoodBlendingPS(texcoord, offset, colorTex, blendTex);
}
