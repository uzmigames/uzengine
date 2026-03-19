/// SSGI Temporal Accumulation Compute Shader
///
/// Accumulates the current frame's spatially-denoised SSGI result with the
/// previous frame's history using motion-vector reprojection and neighborhood
/// clamping (variance clipping) for ghost-free temporal stability.
///
/// The temporal filter provides the majority of noise reduction in the SSGI
/// pipeline — spatial denoising removes high-frequency noise, temporal
/// accumulation builds up a clean signal over multiple frames.
///
/// # Algorithm
/// 1. Load current denoised color + AO
/// 2. Load velocity (motion vector) from GBuffer
/// 3. Reproject: historyUV = currentUV - velocity
/// 4. Sample history at reprojected UV (bilinear)
/// 5. Neighborhood clamp: compute min/max color AABB from 3x3 neighborhood
/// 6. Clamp history to AABB (variance clipping to reject ghosting)
/// 7. Blend: result = lerp(clampedHistory, current, alpha)
///    - alpha = 0.05 for stable pixels (strong temporal accumulation)
///    - alpha = 1.0 for disoccluded pixels (out of bounds or large velocity)
///
/// # Resources
/// - t0: Current denoised SSGI color (RGBA16F, half-res)
/// - t1: Current denoised AO (R16F, half-res)
/// - t2: History SSGI color (RGBA16F, half-res)
/// - t3: History AO (R16F, half-res)
/// - t4: Velocity buffer (RG16F, full-res GBuffer)
/// - u0: Output accumulated color (RGBA16F, half-res) — also the new history
/// - u1: Output accumulated AO (R16F, half-res) — also the new history
/// - b0: Root constants (8 DWORDs)
/// - b1: PerFrameCB
///
/// @source UE5 ScreenSpaceDenoise.cpp:2967 — bUseTemporalAccumulation
/// @source UE5 SSDTemporalAccumulation.ush:1-400 — temporal reprojection + clamp
/// @constants BlendAlpha=0.05 (SSDTemporalAccumulation.ush:312 — HistoryWeight ~0.95)
///            NeighborhoodClamp 3x3 AABB (SSDTemporalAccumulation.ush:250-280)
///            DisocclusionAlpha=1.0 (SSDTemporalAccumulation.ush:320)
/// @deviations Simplified from UE5's full SSD temporal which tracks per-pixel
///   sample count, uses signal-specific history rejection, and supports
///   frequency-domain separation. We use a standard neighborhood-clamp TAA-style
///   temporal filter which is the core of UE5's approach.

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Temporal blend factor for stable (non-disoccluded) pixels.
/// Lower = more history reuse = smoother but more ghosting risk.
/// @source UE5 SSDTemporalAccumulation.ush:312 — ~0.05 effective new-frame weight
#define TEMPORAL_BLEND_ALPHA 0.05f

/// Temporal blend factor for disoccluded pixels (no valid history).
/// @source UE5 SSDTemporalAccumulation.ush:320
#define DISOCCLUSION_ALPHA 1.0f

/// Velocity magnitude threshold to consider a pixel "fast moving."
/// Fast pixels use higher alpha (more current frame, less history).
/// In UV space (0-1 range), so 0.01 = 1% of screen per frame.
#define VELOCITY_THRESHOLD 0.01f

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes — DX12 root signature limit)
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 ScreenSpaceDenoise.cpp:2967 — temporal accumulation params
cbuffer TemporalAccumRootCB : register(b0) {
    uint  currentSrvIndex;     // DWORD 0 — current denoised SSGI color SRV
    uint  currentAOSrvIndex;   // DWORD 1 — current denoised AO SRV
    uint  historySrvIndex;     // DWORD 2 — previous frame history color SRV
    uint  historyAOSrvIndex;   // DWORD 3 — previous frame history AO SRV
    uint  velocitySrvIndex;    // DWORD 4 — velocity buffer SRV
    uint  outputUavIndex;      // DWORD 5 — accumulated color UAV (bindless heap index)
    uint  aoOutputUavIndex;    // DWORD 6 — accumulated AO UAV (bindless heap index)
    uint  packedDimensions;    // DWORD 7 — low 16 = width, high 16 = height
};

// Unpack dimensions from packed DWORD
static const uint texWidth  = packedDimensions & 0xFFFF;
static const uint texHeight = packedDimensions >> 16;

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame CB (b1)
// ─────────────────────────────────────────────────────────────────────────────

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
    float    nearPlane;
    float    farPlane;
    float    _pad_pf[14];
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
// SM6.6: textures accessed via ResourceDescriptorHeap[]
#else
Texture2D<float4> currentColorTex : register(t0);
Texture2D<float>  currentAOTex    : register(t1);
Texture2D<float4> historyColorTex : register(t2);
Texture2D<float>  historyAOTex    : register(t3);
Texture2D<float2> velocityTex     : register(t4);
#endif

#ifndef UZ_BINDLESS
RWTexture2D<float4> accumOutput   : register(u0);
RWTexture2D<float>  accumAOOutput : register(u1);
#endif

SamplerState pointSampler  : register(s0);
SamplerState linearSampler : register(s1);

// ─────────────────────────────────────────────────────────────────────────────
// Texture Sampling Helpers
// ─────────────────────────────────────────────────────────────────────────────

float4 SampleCurrentColor(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[currentSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0);
#else
    return currentColorTex.SampleLevel(pointSampler, uv, 0);
#endif
}

float SampleCurrentAO(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[currentAOSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).r;
#else
    return currentAOTex.SampleLevel(pointSampler, uv, 0).r;
#endif
}

float4 SampleHistoryColor(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> tex = ResourceDescriptorHeap[historySrvIndex];
    return tex.SampleLevel(linearSampler, uv, 0);
#else
    return historyColorTex.SampleLevel(linearSampler, uv, 0);
#endif
}

float SampleHistoryAO(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[historyAOSrvIndex];
    return tex.SampleLevel(linearSampler, uv, 0).r;
#else
    return historyAOTex.SampleLevel(linearSampler, uv, 0).r;
#endif
}

float2 SampleVelocity(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float2> tex = ResourceDescriptorHeap[velocitySrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).rg;
#else
    return velocityTex.SampleLevel(pointSampler, uv, 0).rg;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Luminance (for Karis-style weighting in neighborhood clamp)
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 Common.ush:405
float Luminance(float3 color) {
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 SSDTemporalAccumulation.ush:200-380 — temporal accumulation pass
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID) {
    // Bounds check
    if (dispatchThreadId.x >= texWidth || dispatchThreadId.y >= texHeight) {
        return;
    }

    float2 pixelCenter = (float2)dispatchThreadId.xy + 0.5f;
    float2 invSize = 1.0f / float2((float)texWidth, (float)texHeight);
    float2 uv = pixelCenter * invSize;

    // ── Load current frame data ──
    float4 currentColor = SampleCurrentColor(uv);
    float  currentAO    = SampleCurrentAO(uv);

    // ── Load velocity for reprojection ──
    // Velocity buffer is full-res; UV coordinates are the same since we're
    // reading at the same normalized position.
    // @source UE5 SSDTemporalAccumulation.ush:220 — EncodedVelocity
    float2 velocity = SampleVelocity(uv);

    // ── Reproject to history UV ──
    // @source UE5 SSDTemporalAccumulation.ush:225 — HistoryScreenPosition = CurrentUV - Velocity
    float2 historyUV = uv - velocity;

    // ── Check for disocclusion (history UV out of bounds) ──
    bool isDisoccluded = any(historyUV < 0.0f) || any(historyUV > 1.0f);

    // ── Compute 3x3 neighborhood color AABB for variance clipping ──
    // This prevents ghosting by rejecting history samples that are outside
    // the range of plausible values given the current frame's local neighborhood.
    // @source UE5 SSDTemporalAccumulation.ush:250-280 — neighborhood AABB
    float4 neighborMin = float4(1e10f, 1e10f, 1e10f, 1e10f);
    float4 neighborMax = float4(-1e10f, -1e10f, -1e10f, -1e10f);
    float  aoMin = 1e10f;
    float  aoMax = -1e10f;

    [unroll]
    for (int dy = -1; dy <= 1; dy++) {
        [unroll]
        for (int dx = -1; dx <= 1; dx++) {
            float2 neighborUV = uv + float2((float)dx, (float)dy) * invSize;

            // Clamp to screen bounds
            neighborUV = clamp(neighborUV, 0.0f, 1.0f);

            float4 neighborColor = SampleCurrentColor(neighborUV);
            float  neighborAO    = SampleCurrentAO(neighborUV);

            neighborMin = min(neighborMin, neighborColor);
            neighborMax = max(neighborMax, neighborColor);
            aoMin = min(aoMin, neighborAO);
            aoMax = max(aoMax, neighborAO);
        }
    }

    // ── Sample history ──
    float4 historyColor;
    float  historyAO;

    if (isDisoccluded) {
        // No valid history — use current frame directly
        historyColor = currentColor;
        historyAO    = currentAO;
    } else {
        historyColor = SampleHistoryColor(historyUV);
        historyAO    = SampleHistoryAO(historyUV);

        // ── Clamp history to neighborhood AABB (variance clipping) ──
        // @source UE5 SSDTemporalAccumulation.ush:270-278
        historyColor = clamp(historyColor, neighborMin, neighborMax);
        historyAO    = clamp(historyAO, aoMin, aoMax);
    }

    // ── Compute blend alpha ──
    // @source UE5 SSDTemporalAccumulation.ush:312-320
    float alpha = TEMPORAL_BLEND_ALPHA;

    // Increase alpha for disoccluded pixels (use more current frame)
    if (isDisoccluded) {
        alpha = DISOCCLUSION_ALPHA;
    }

    // Increase alpha for fast-moving pixels to reduce ghosting
    // @source UE5 SSDTemporalAccumulation.ush:315 — velocity-based rejection
    float velocityMagnitude = length(velocity);
    if (velocityMagnitude > VELOCITY_THRESHOLD) {
        // Smoothly ramp alpha up as velocity increases
        float velocityFactor = saturate(velocityMagnitude / (VELOCITY_THRESHOLD * 10.0f));
        alpha = lerp(alpha, 1.0f, velocityFactor);
    }

    // ── Temporal blend ──
    // result = lerp(history, current, alpha) = history * (1-alpha) + current * alpha
    // @source UE5 SSDTemporalAccumulation.ush:325
    float4 resultColor = lerp(historyColor, currentColor, alpha);
    float  resultAO    = lerp(historyAO, currentAO, alpha);

    // Write accumulated outputs (these become history for next frame)
#ifdef UZ_BINDLESS
    RWTexture2D<float4> outColor = ResourceDescriptorHeap[outputUavIndex];
    RWTexture2D<float>  outAO   = ResourceDescriptorHeap[aoOutputUavIndex];
    outColor[dispatchThreadId.xy] = resultColor;
    outAO[dispatchThreadId.xy]    = resultAO;
#else
    accumOutput[dispatchThreadId.xy]   = resultColor;
    accumAOOutput[dispatchThreadId.xy] = resultAO;
#endif
}
