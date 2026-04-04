/// UzEngine SSGI Temporal Accumulation — Variance-Guided Clipping
///
/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering" (SVGF)
/// @ref Salvi 2016 "An Excursion in Temporal Supersampling"
///
/// Reprojects previous frame's accumulated result using velocity/motion vectors,
/// clips the history color to the current frame's local color neighborhood
/// (μ ± γ·σ variance-guided clipping), and blends with exponential averaging.
///
/// Root constants at b0 (8 DWORDs):
///   [0] currentSrvIndex     — half-res denoised indirect diffuse SRV
///   [1] currentAOSrvIndex   — half-res denoised AO SRV
///   [2] historySrvIndex     — half-res history indirect diffuse SRV (previous frame)
///   [3] historyAOSrvIndex   — half-res history AO SRV (previous frame)
///   [4] velocitySrvIndex    — full-res velocity buffer SRV (RG16F)
///   [5] outputUavIndex      — half-res output indirect diffuse UAV (new history)
///   [6] aoOutputUavIndex    — half-res output AO UAV (new history)
///   [7] packedDimensions    — low16 = width, high16 = height

#include "../common/constants.hlsli"
#include "../common/math.hlsli"

cbuffer RootConstants : register(b0) {
#ifdef UZ_BINDLESS
    uint g_currentSrvIndex;
    uint g_currentAOSrvIndex;
    uint g_historySrvIndex;
    uint g_historyAOSrvIndex;
    uint g_velocitySrvIndex;
    uint g_outputUavIndex;
    uint g_aoOutputUavIndex;
#endif
    uint g_packedDimensions;  // low16 = width, high16 = height
    uint g_temporalAlphaBits; // CVar r.SSGI.TemporalAlpha as float bits (default 0.05)
    uint g_varianceClipGammaBits; // CVar r.SSGI.VarianceClipGamma as float bits (default 1.0)
};

SamplerState linearSampler : register(s0);

/// @ref Salvi 2016 — temporal blend factor (CVar r.SSGI.TemporalAlpha)
#define kTemporalAlpha asfloat(g_temporalAlphaBits)

/// @ref Schied 2017 — variance clip gamma (CVar r.SSGI.VarianceClipGamma)
#define kVarianceClipGamma asfloat(g_varianceClipGammaBits)

/// @ref ITU-R BT.709 luminance weights
float Luminance(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

/// @ref Schied 2017 Sec. 3.2 — variance-guided color clipping
/// Clips a color to the bounding box defined by μ ± γ·σ.
/// Superior to AABB min/max clamping — prevents ghosting while
/// preserving temporal stability.
float3 VarianceClip(float3 color, float3 mu, float3 sigma) {
    float3 boxMin = mu - kVarianceClipGamma * sigma;
    float3 boxMax = mu + kVarianceClipGamma * sigma;
    return clamp(color, boxMin, boxMax);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint width  = g_packedDimensions & 0xFFFF;
    uint height = g_packedDimensions >> 16;
    if (DTid.x >= width || DTid.y >= height) return;

    int2 pixel = int2(DTid.xy);
    float2 uv = (float2(pixel) + 0.5) / float2(width, height);

#ifdef UZ_BINDLESS
    Texture2D<float4>   currentTex  = ResourceDescriptorHeap[g_currentSrvIndex];
    Texture2D<float>    currentAO   = ResourceDescriptorHeap[g_currentAOSrvIndex];
    Texture2D<float4>   historyTex  = ResourceDescriptorHeap[g_historySrvIndex];
    Texture2D<float>    historyAO   = ResourceDescriptorHeap[g_historyAOSrvIndex];
    Texture2D<float4>   velocityTex = ResourceDescriptorHeap[g_velocitySrvIndex];
    RWTexture2D<float4> outputTex   = ResourceDescriptorHeap[g_outputUavIndex];
    RWTexture2D<float>  aoOutputTex = ResourceDescriptorHeap[g_aoOutputUavIndex];
#endif

    // Current frame values
    float3 currentColor = currentTex.Load(int3(pixel, 0)).rgb;
    float  currentAOVal = currentAO.Load(int3(pixel, 0));

    // Read velocity at full resolution (pixel * 2 maps half→full)
    int2 fullPx = pixel * 2;
    float2 velocity = velocityTex.Load(int3(fullPx, 0)).rg;

    // Reproject to previous frame UV
    float2 prevUV = uv - velocity;

    // Check if reprojection is within bounds
    bool validReproject = all(prevUV >= 0.0) && all(prevUV <= 1.0);

    if (!validReproject) {
        // No valid history — use current frame directly
        outputTex[pixel]   = float4(currentColor, 1.0);
        aoOutputTex[pixel] = currentAOVal;
        return;
    }

    // Sample history with bilinear filtering
    float3 historyColor = historyTex.SampleLevel(linearSampler, prevUV, 0).rgb;
    float  historyAOVal = historyAO.SampleLevel(linearSampler, prevUV, 0);

    // ── Compute local color statistics (3x3 neighborhood) ─────────────────
    /// @ref Schied 2017 Sec. 3.2 — compute μ and σ for variance-guided clipping
    float3 m1 = float3(0, 0, 0); // first moment
    float3 m2 = float3(0, 0, 0); // second moment
    float  aoM1 = 0.0;
    float  aoM2 = 0.0;

    for (int ky = -1; ky <= 1; ++ky) {
        for (int kx = -1; kx <= 1; ++kx) {
            int2 np = clamp(pixel + int2(kx, ky), int2(0, 0),
                            int2(width - 1, height - 1));

            float3 c = currentTex.Load(int3(np, 0)).rgb;
            float  a = currentAO.Load(int3(np, 0));

            m1 += c;
            m2 += c * c;
            aoM1 += a;
            aoM2 += a * a;
        }
    }

    m1 /= 9.0;
    m2 /= 9.0;
    aoM1 /= 9.0;
    aoM2 /= 9.0;

    float3 sigma = sqrt(max(m2 - m1 * m1, 0.0));
    float  aoSigma = sqrt(max(aoM2 - aoM1 * aoM1, 0.0));

    // ── Variance-guided clipping ──────────────────────────────────────────
    /// @ref Schied 2017 — clip history to μ ± γ·σ (replaces AABB min/max clamp)
    float3 clippedHistory = VarianceClip(historyColor, m1, sigma);
    float  clippedAOHist  = clamp(historyAOVal,
                                   aoM1 - kVarianceClipGamma * aoSigma,
                                   aoM1 + kVarianceClipGamma * aoSigma);

    // ── Temporal blend ────────────────────────────────────────────────────
    // Increase alpha (more current frame) when history was significantly clipped
    float clipDist = length(historyColor - clippedHistory);
    float adaptiveAlpha = lerp(kTemporalAlpha, 1.0, saturate(clipDist * 5.0));

    float3 blended   = lerp(clippedHistory, currentColor, adaptiveAlpha);
    float  blendedAO = lerp(clippedAOHist, currentAOVal, adaptiveAlpha);

    outputTex[pixel]   = float4(blended, 1.0);
    aoOutputTex[pixel] = blendedAO;
}
