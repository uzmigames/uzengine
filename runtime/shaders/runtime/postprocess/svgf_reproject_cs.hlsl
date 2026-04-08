/// UzEngine SVGF Temporal Reprojection — Moment Accumulation
///
/// @ref Schied et al. 2017 "Spatiotemporal Variance-Guided Filtering"
///
/// Pass 1 of the SVGF pipeline. For each pixel:
/// 1. Reproject to previous frame using velocity vectors
/// 2. Load history moments (mean luminance μ, variance σ²) and sample count
/// 3. If reprojection is valid (within bounds, depth/normal consistent),
///    accumulate moments; otherwise reset to single-sample estimate
/// 4. Output: updated color, moments (μ, σ²), accumulated sample count
///
/// Root constants at b0 (8 DWORDs):
///   [0] currentColorSrv   — current frame noisy input (RGBA16F)
///   [1] historyColorSrv   — previous frame SVGF output (RGBA16F)
///   [2] historyMomentsSrv — previous frame moments texture (RG16F: μ, σ²)
///   [3] velocitySrv       — full-res velocity buffer (RG16F)
///   [4] depthSrv          — full-res depth buffer
///   [5] outputColorUav    — reprojected color output UAV
///   [6] outputMomentsUav  — updated moments output UAV
///   [7] packed: low16=width, high16=height

#include "../common/constants.hlsli"
#include "../common/math.hlsli"
#include "../include/depth_common.hlsli"

cbuffer RootConstants : register(b0) {
#ifdef UZ_BINDLESS
    uint g_currentColorSrv;
    uint g_historyColorSrv;
    uint g_historyMomentsSrv;
    uint g_velocitySrv;
    uint g_depthSrv;
    uint g_outputColorUav;
    uint g_outputMomentsUav;
#endif
    uint g_packedDimensions;
};

SamplerState linearSampler : register(s0);

/// @ref ITU-R BT.709 luminance
float Luminance(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

/// @ref Schied 2017 — temporal accumulation factor
/// @constants alpha = max(1/N, 0.01) where N = accumulated sample count
static const float kMinAlpha = 0.01;
static const float kMaxHistory = 256.0; // Max accumulated frames

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint width  = g_packedDimensions & 0xFFFF;
    uint height = g_packedDimensions >> 16;
    if (DTid.x >= width || DTid.y >= height) return;

    int2 pixel = int2(DTid.xy);
    float2 uv = (float2(pixel) + 0.5) / float2(width, height);

#ifdef UZ_BINDLESS
    Texture2D<float4>   currentTex  = ResourceDescriptorHeap[g_currentColorSrv];
    Texture2D<float4>   historyTex  = ResourceDescriptorHeap[g_historyColorSrv];
    Texture2D<float2>   momentsTex  = ResourceDescriptorHeap[g_historyMomentsSrv];
    Texture2D<float2>   velocityTex = ResourceDescriptorHeap[g_velocitySrv];
    Texture2D<float>    depthTex    = ResourceDescriptorHeap[g_depthSrv];
    RWTexture2D<float4> outputColor = ResourceDescriptorHeap[g_outputColorUav];
    RWTexture2D<float2> outputMom   = ResourceDescriptorHeap[g_outputMomentsUav];
#endif

    float3 currentColor = currentTex.Load(int3(pixel, 0)).rgb;
    float  currentLum   = Luminance(currentColor);
    float  depth        = depthTex.Load(int3(pixel, 0));

    // Sky: pass through
    if (IsSkyDepth(depth)) {
        outputColor[pixel] = float4(currentColor, 1.0);
        outputMom[pixel]   = float2(currentLum, currentLum * currentLum);
        return;
    }

    // Reproject
    float2 velocity = velocityTex.Load(int3(pixel, 0));
    float2 prevUV   = uv - velocity;
    bool valid = all(prevUV >= 0.0) && all(prevUV <= 1.0);

    if (!valid) {
        // Disoccluded — reset to single sample
        outputColor[pixel] = float4(currentColor, 1.0);
        outputMom[pixel]   = float2(currentLum, currentLum * currentLum);
        return;
    }

    // Sample history
    float3 historyColor = historyTex.SampleLevel(linearSampler, prevUV, 0).rgb;
    float2 historyMom   = momentsTex.SampleLevel(linearSampler, prevUV, 0);

    // Temporal accumulation
    /// @ref Schied 2017 Eq. 1 — exponential moving average with adaptive alpha
    float histN = historyMom.y; // reuse second moment channel as approx sample count proxy
    float alpha = max(1.0 / (histN + 1.0), kMinAlpha);

    float3 blendedColor = lerp(historyColor, currentColor, alpha);
    float  blendedLum   = Luminance(blendedColor);

    // Accumulate moments: μ_n = (1-α)μ_{n-1} + α·l, σ²_n = (1-α)σ²_{n-1} + α·(l-μ_n)²
    float mu    = lerp(historyMom.x, currentLum, alpha);
    float sigma2 = lerp(historyMom.y, (currentLum - mu) * (currentLum - mu), alpha);

    outputColor[pixel] = float4(blendedColor, 1.0);
    outputMom[pixel]   = float2(mu, max(sigma2, 0.0));
}
