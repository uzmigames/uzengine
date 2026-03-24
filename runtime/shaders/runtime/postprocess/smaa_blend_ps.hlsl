/// SMAA Pass 2 — Blending Weight Calculation pixel shader.
///
/// Reads the edge texture (from Pass 1) and the precomputed area/search textures,
/// then calculates per-pixel blending weights for horizontal and vertical patterns.
/// Also handles diagonal edge patterns and corner rounding.
///
/// The output is an RGBA8 blend weight texture consumed by Pass 3 (neighborhood blend).
///
/// @ref Jimenez 2012 "SMAA: Enhanced Subpixel Morphological Antialiasing"
///   — Section 3.2 Pattern Detection and Blending Weight Calculation
/// @constants SMAA_THRESHOLD=0.1 (SMAA.hlsl:314), SMAA_MAX_SEARCH_STEPS=16 (SMAA.hlsl:316),
///   SMAA_MAX_SEARCH_STEPS_DIAG=8 (SMAA.hlsl:317), SMAA_CORNER_ROUNDING=25 (SMAA.hlsl:318)
/// @deviations NONE — uses upstream SMAA.hlsl unmodified via #include.
///   subsampleIndices = float4(0,0,0,0) for SMAA 1x (no temporal supersampling).
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

/// Edge texture from Pass 1 (RG8 — R=horizontal edge, G=vertical edge).
Texture2D edgesTex  : register(t0);

/// Precomputed area texture (160x560, R8G8_UNORM).
/// @ref Jimenez 2012 SMAA — AreaTex.h
Texture2D areaTex   : register(t1);

/// Precomputed search texture (64x16, R8_UNORM).
/// @ref Jimenez 2012 SMAA — SearchTex.h
Texture2D searchTex : register(t2);

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader
// ─────────────────────────────────────────────────────────────────────────────

float4 main(FSInput input) : SV_Target {
    float2 texcoord = input.texcoord;

    // Compute blend weight VS offsets and pixel coordinates in the PS.
    // @ref Jimenez 2012 SMAA.hlsl:655-668 — SMAABlendingWeightCalculationVS
    float2 pixcoord;
    float4 offset[3];
    SMAABlendingWeightCalculationVS(texcoord, pixcoord, offset);

    // SMAA 1x: no temporal supersampling → subsampleIndices = 0.
    // @ref Jimenez 2012 SMAA.hlsl:1151 — "Just pass zero for SMAA 1x"
    return SMAABlendingWeightCalculationPS(texcoord, pixcoord, offset,
                                            edgesTex, areaTex, searchTex,
                                            float4(0.0, 0.0, 0.0, 0.0));
}
