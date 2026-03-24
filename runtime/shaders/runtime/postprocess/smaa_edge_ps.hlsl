/// SMAA Pass 1 — Luma Edge Detection pixel shader.
///
/// Detects edges in the LDR tonemapped image using luma differences.
/// Outputs a 2-channel (RG) edge texture where R = horizontal edge, G = vertical edge.
/// The edge texture is used by Pass 2 (blend weight calculation).
///
/// @ref Jimenez 2012 "SMAA: Enhanced Subpixel Morphological Antialiasing"
///   — Section 3.1 Edge Detection (luma-based)
/// @constants SMAA_THRESHOLD=0.1 (SMAA.hlsl:314), SMAA_MAX_SEARCH_STEPS=16 (SMAA.hlsl:316),
///   SMAA_MAX_SEARCH_STEPS_DIAG=8 (SMAA.hlsl:317), SMAA_CORNER_ROUNDING=25 (SMAA.hlsl:318)
/// @deviations NONE — uses upstream SMAA.hlsl unmodified via #include.
///   SMAA VS offsets are computed in the PS instead of the VS (negligible cost
///   since we use a shared fullscreen vertex shader for all post-process passes).

// ─────────────────────────────────────────────────────────────────────────────
// SMAA Configuration — must be defined BEFORE including SMAA.hlsl
// ─────────────────────────────────────────────────────────────────────────────

/// Render target metrics: (1/width, 1/height, width, height).
/// Passed via root constant buffer.
cbuffer SMAAConstants : register(b2) {
    float4 smaaRTMetrics; // (1/width, 1/height, width, height)
};

#define SMAA_RT_METRICS smaaRTMetrics

/// Use SMAA_CUSTOM_SL to define our own porting macros.
/// This avoids SMAA.hlsl declaring its own sampler state objects (which would
/// conflict with our root-signature-bound samplers).
#define SMAA_CUSTOM_SL 1
#define SMAA_PRESET_HIGH 1

// ── Porting Macros ──────────────────────────────────────────────────────────
// These must match the macro signatures expected by SMAA.hlsl.
// We bind our own samplers at s0 (point) and s1 (linear).

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

// Only include pixel shader functions (we use our own fullscreen VS).
#define SMAA_INCLUDE_VS 1
#define SMAA_INCLUDE_PS 1

#include "../../../thirdparty/smaa/SMAA.hlsl"

// ─────────────────────────────────────────────────────────────────────────────
// Input / Output
// ─────────────────────────────────────────────────────────────────────────────

/// LDR scene color (after tonemap, before sRGB — non-sRGB texture).
Texture2D colorTex : register(t0);

/// Fullscreen vertex shader output: clip-space position + screen UV.
struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader
// ─────────────────────────────────────────────────────────────────────────────

/// Computes luma-based edge detection.
///
/// The SMAA VS offsets (neighbor texcoord offsets) are computed here in the PS
/// instead of the VS. This is functionally identical — the offsets are simple
/// `mad(SMAA_RT_METRICS.xyxy, const, texcoord.xyxy)` operations that cost
/// negligible ALU per pixel.
float2 main(FSInput input) : SV_Target {
    float2 texcoord = input.texcoord;

    // Compute edge detection vertex shader offsets in the pixel shader.
    // @ref Jimenez 2012 SMAA.hlsl:645-650 — SMAAEdgeDetectionVS
    float4 offset[3];
    SMAAEdgeDetectionVS(texcoord, offset);

    return SMAALumaEdgeDetectionPS(texcoord, offset, colorTex);
}
