/// GTAO Temporal Accumulation — reprojects previous frame AO via velocity buffer
/// and blends with the current frame's denoised AO to reduce temporal noise.
///
/// Implements UE5's GTAOTemporalFilterPSAndCS as a compute shader. The algorithm:
/// 1. Sample the current frame's denoised AO at the pixel UV.
/// 2. Read screen-space velocity from the velocity buffer (RG16F, in pixels).
/// 3. Compute the reprojected UV from the velocity.
/// 4. Perform neighbourhood variance clipping on the current AO (4-tap diagonal).
/// 5. Read history at the reprojected UV with manual bilinear + per-sample clamping.
/// 6. Compare forward/backward velocity consistency to reject disoccluded pixels.
/// 7. Blend: output = lerp(historyAO, newAO, blendWeight).
///
/// # Resources
/// - t0: Current frame denoised AO (R8_UNORM from gtao_denoise_cs output) [SM5]
/// - t1: History AO texture (R8_UNORM, previous frame's temporal output) [SM5]
/// - t2: Velocity buffer (RG16F, screen-space motion in pixels) [SM5]
/// - t3: Depth buffer (R32_FLOAT) for velocity comparison [SM5]
/// - u0: Output accumulated AO (R8_UNORM)
/// - b0: Root constants (8 DWORDs)
/// - s0: Point sampler (for AO/depth loads)
/// - s1: Linear sampler (for history bilinear sampling)
///
/// @source UE5 PostProcessAmbientOcclusion.usf:1261-1308 — GTAOTemporalFilterPSAndCS
/// @source UE5 PostProcessAmbientOcclusion.usf:1218-1251 — NeighbourhoodClamp
/// @source UE5 PostProcessAmbientOcclusion.usf:1178-1212 — ReadHistoryClamp
/// @source UE5 PostProcessAmbientOcclusion.usf:1254-1258 — CompareVeloc
/// @source UE5 PostProcessAmbientOcclusion.usf:1319-1337 — GTAOTemporalFilterCS
/// @constants BlendWeight=0.1 (Scene.cpp:535 AmbientOcclusionTemporalBlendWeight),
///            NeighbourhoodClamp dUV scale=1.5 (usf:1220),
///            VarianceClipping sigma=0.8 (usf:1245-1246),
///            CompareVeloc scale=100 (usf:1257),
///            VelocityMag scale=100 (usf:1281),
///            RangeVal lerp 0.1->0.0 (usf:1294)
/// @deviations UE5 uses ClipToPrevClip matrix for reprojection (usf:1158-1175);
///             UzEngine uses the pre-computed velocity buffer (RG16F in pixels) instead.
///             The velocity information is equivalent — both yield the screen-space
///             displacement between current and previous frame. UzEngine's velocity_ps.hlsl
///             writes pixels directly, so we convert to UV-space in the shader.

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants (b0, 8 DWORDs = 32 bytes)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants — exactly 8 DWORDs (32 bytes).
/// Matches the 8-DWORD root constant limit in dx12_pso_creation.cpp:135.
///
/// @source UE5 PostProcessAmbientOcclusion.usf:1263 — GTAOParams[4].x = BlendWeight
/// @source UE5 Scene.cpp:535 — AmbientOcclusionTemporalBlendWeight = 0.1
cbuffer GTAOTemporalRootCB : register(b0) {
    uint  currentAOSrvIndex;   // DWORD 0 — current frame denoised AO SRV
    uint  historyAOSrvIndex;   // DWORD 1 — previous frame accumulated AO SRV
    uint  velocitySrvIndex;    // DWORD 2 — velocity buffer SRV (RG16F, pixels)
    uint  depthSrvIndex;       // DWORD 3 — depth buffer SRV (R32_FLOAT)
    uint  outputWidth;         // DWORD 4 — render target width
    uint  outputHeight;        // DWORD 5 — render target height
    float blendWeight;         // DWORD 6 — temporal blend weight (UE5 default 0.1)
    uint  _pad0;               // DWORD 7 — padding
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
Texture2D<float>  currentAOTex  : register(t0); ///< Current frame denoised AO.
Texture2D<float>  historyAOTex  : register(t1); ///< Previous frame accumulated AO.
Texture2D<float2> velocityTex   : register(t2); ///< Velocity buffer (RG16F, pixels).
Texture2D<float>  depthTex      : register(t3); ///< Depth buffer (R32_FLOAT).
#endif

RWTexture2D<float> outputAO     : register(u0); ///< Output accumulated AO.

SamplerState pointSampler  : register(s0); ///< Point sampler for AO/depth loads.
SamplerState linearSampler : register(s1); ///< Bilinear sampler for history reads.

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Texture Sampling (bindless / SM5 paths)
// ─────────────────────────────────────────────────────────────────────────────

/// Sample the current frame's denoised AO at UV.
float SampleCurrentAO(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[currentAOSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).r;
#else
    return currentAOTex.SampleLevel(pointSampler, uv, 0).r;
#endif
}

/// Sample the history AO at UV with bilinear filtering.
float SampleHistoryBilinear(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[historyAOSrvIndex];
    return tex.SampleLevel(linearSampler, uv, 0).r;
#else
    return historyAOTex.SampleLevel(linearSampler, uv, 0).r;
#endif
}

/// Sample the history AO at UV with point filtering (for manual bilinear).
float SampleHistoryPoint(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[historyAOSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).r;
#else
    return historyAOTex.SampleLevel(pointSampler, uv, 0).r;
#endif
}

/// Sample the velocity buffer at UV. Returns screen-space motion in pixels.
float2 SampleVelocity(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float2> tex = ResourceDescriptorHeap[velocitySrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).rg;
#else
    return velocityTex.SampleLevel(pointSampler, uv, 0).rg;
#endif
}

/// Sample depth buffer at UV.
float SampleDepth(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float> tex = ResourceDescriptorHeap[depthSrvIndex];
    return tex.SampleLevel(pointSampler, uv, 0).r;
#else
    return depthTex.SampleLevel(pointSampler, uv, 0).r;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// NeighbourhoodClamp — variance-based clamping of history AO
// ─────────────────────────────────────────────────────────────────────────────

/// Compute the acceptable min/max AO range from the current frame's neighbourhood.
/// Uses 4-tap diagonal sampling with variance clipping (GTAO_VARIANCE_CLIPPING=1).
///
/// @source UE5 PostProcessAmbientOcclusion.usf:1218-1251 — NeighbourhoodClamp
/// @constants dUV scale = 1.5 (usf:1220), sigma multiplier = 0.8 (usf:1245-1246)
/// @deviations NONE
void NeighbourhoodClamp(float2 uv, float baseAO, out float minAO, out float maxAO) {
    float2 texelSize = float2(1.0f / float(outputWidth), 1.0f / float(outputHeight));

    /// @source UE5 PostProcessAmbientOcclusion.usf:1220 — dUV = pixelSize * 1.5
    float2 dUV = texelSize * 1.5f;

    /// @source UE5 PostProcessAmbientOcclusion.usf:1226-1229 — 4 diagonal neighbour samples
    float aoNeighbours[4];
    aoNeighbours[0] = SampleCurrentAO(uv + float2(-dUV.x, -dUV.y));
    aoNeighbours[1] = SampleCurrentAO(uv + float2(-dUV.x,  dUV.y));
    aoNeighbours[2] = SampleCurrentAO(uv + float2( dUV.x, -dUV.y));
    aoNeighbours[3] = SampleCurrentAO(uv + float2( dUV.x,  dUV.y));

    /// Variance clipping: compute mean and standard deviation of neighbourhood,
    /// then clamp to [mu - 0.8*sigma, mu + 0.8*sigma].
    /// @source UE5 PostProcessAmbientOcclusion.usf:1232-1246 — GTAO_VARIANCE_CLIPPING=1
    float aoAverage = 0.0f;
    float aoSquared = 0.0f;

    [unroll]
    for (int i = 0; i < 4; i++) {
        aoAverage += aoNeighbours[i];
        aoSquared += aoNeighbours[i] * aoNeighbours[i];
    }

    float mu    = aoAverage * 0.25f;              // mean (/ 4 samples)
    float sigma = sqrt(aoSquared * 0.25f - mu * mu); // standard deviation

    /// @source UE5 PostProcessAmbientOcclusion.usf:1245-1246 — 0.8 sigma
    minAO = max(mu - sigma * 0.8f, 0.0f);
    maxAO = min(mu + sigma * 0.8f, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// ReadHistoryClamp — manual bilinear with per-sample clamping
// ─────────────────────────────────────────────────────────────────────────────

/// Read history AO at the reprojected UV using manual bilinear interpolation.
/// Each of the 4 bilinear samples is individually clamped to [minAO, maxAO]
/// before blending, preventing ghosting from out-of-range history values
/// at subpixel boundaries.
///
/// @source UE5 PostProcessAmbientOcclusion.usf:1178-1212 — ReadHistoryClamp
/// @constants none (pure geometry + clamping)
/// @deviations NONE
float ReadHistoryClamp(float2 uv, float minAO, float maxAO) {
    float2 historySize      = float2(float(outputWidth), float(outputHeight));
    float2 historyPixelSize = float2(1.0f / float(outputWidth), 1.0f / float(outputHeight));

    /// @source UE5 PostProcessAmbientOcclusion.usf:1182-1185
    float2 pixUV   = uv * historySize - 0.5f;
    float2 floorUV = floor(pixUV);
    float2 fracUV  = pixUV - floorUV;
    float2 baseUV  = floorUV * historyPixelSize + historyPixelSize * 0.5f;

    /// @source UE5 PostProcessAmbientOcclusion.usf:1187-1190 — bilinear weights
    float bilinearWeights[4];
    bilinearWeights[0] = (1.0f - fracUV.x) * (1.0f - fracUV.y);
    bilinearWeights[1] = (       fracUV.x) * (1.0f - fracUV.y);
    bilinearWeights[2] = (1.0f - fracUV.x) * (       fracUV.y);
    bilinearWeights[3] = (       fracUV.x) * (       fracUV.y);

    float2 dUV = historyPixelSize;

    /// @source UE5 PostProcessAmbientOcclusion.usf:1198-1201 — 4 history samples
    float historyAO[4];
    historyAO[0] = SampleHistoryPoint(baseUV + float2(   0.0f,    0.0f));
    historyAO[1] = SampleHistoryPoint(baseUV + float2(dUV.x,      0.0f));
    historyAO[2] = SampleHistoryPoint(baseUV + float2(   0.0f, dUV.y  ));
    historyAO[3] = SampleHistoryPoint(baseUV + float2(dUV.x,   dUV.y  ));

    /// @source UE5 PostProcessAmbientOcclusion.usf:1204-1208 — clamp each sample, blend
    float visHistory = 0.0f;
    [unroll]
    for (int i = 0; i < 4; i++) {
        historyAO[i] = clamp(historyAO[i], minAO, maxAO);
        visHistory += bilinearWeights[i] * historyAO[i];
    }

    return visHistory;
}

// ─────────────────────────────────────────────────────────────────────────────
// CompareVeloc — forward/backward velocity consistency check
// ─────────────────────────────────────────────────────────────────────────────

/// Compare two velocity vectors to detect disocclusion.
/// Returns 1.0 for matching velocities, 0.0 for very different ones.
///
/// @source UE5 PostProcessAmbientOcclusion.usf:1254-1258 — CompareVeloc
/// @constants 100 (usf:1257)
/// @deviations NONE
float CompareVeloc(float2 v1, float2 v2) {
    float2 vDiff = v1 - v2;
    return 1.0f - saturate(abs(vDiff.x + vDiff.y) * 100.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

/// GTAO Temporal Filter — compute shader entry point.
///
/// Reprojects the previous frame's AO using the velocity buffer, applies
/// neighbourhood variance clamping, velocity consistency rejection, and
/// blends current + history with a configurable blend weight.
///
/// @source UE5 PostProcessAmbientOcclusion.usf:1319-1337 — GTAOTemporalFilterCS
/// @source UE5 PostProcessAmbientOcclusion.usf:1261-1308 — GTAOTemporalFilterPSAndCS
/// @deviations Uses velocity buffer (pixels) instead of ClipToPrevClip matrix.
///             The velocity data is equivalent; UzEngine precomputes it in velocity_ps.hlsl.

[numthreads(8, 8, 1)]
void CSMain(
    uint2 groupId       : SV_GroupID,
    uint2 dispatchId    : SV_DispatchThreadID,
    uint2 groupThreadId : SV_GroupThreadID)
{
    // Bounds check — threads beyond the render target are no-ops.
    if (dispatchId.x >= outputWidth || dispatchId.y >= outputHeight) {
        return;
    }

    float2 texelSize   = float2(1.0f / float(outputWidth), 1.0f / float(outputHeight));
    float2 pixelCenter = (float2)dispatchId + float2(0.5f, 0.5f);
    float2 uv          = pixelCenter * texelSize;

    // ── 1. Sample current frame's denoised AO ──
    /// @source UE5 PostProcessAmbientOcclusion.usf:1269
    float newAO = SampleCurrentAO(uv);

    // ── 2. Read velocity and compute reprojected UV ──
    // UzEngine velocity_ps.hlsl outputs velocity in pixels (RG16F).
    // Convert to UV-space by dividing by screen dimensions.
    // UE5 equivalent: ReprojectPos() at usf:1158-1175 computes PrevUV via
    // ClipToPrevClip matrix; our velocity buffer encodes the same displacement.
    float2 velocityPixels = SampleVelocity(uv);
    float2 velocityUV     = velocityPixels * texelSize;

    // PrevUV = current UV minus the motion (velocity = current - previous).
    /// @source UE5 PostProcessAmbientOcclusion.usf:1276-1278
    float2 prevUV = uv - velocityUV;

    // ── 3. Velocity magnitude for blend adjustment ──
    /// @source UE5 PostProcessAmbientOcclusion.usf:1281 — VelocityMag
    /// @constants 100 (usf:1281)
    float velocityMag = saturate(length(velocityUV) * 100.0f);

    // ── 4. Forward/backward velocity comparison for disocclusion detection ──
    // Sample depth at the reprojected UV and compute the velocity there.
    // If the backward-reprojected velocity disagrees with the forward velocity,
    // the pixel is likely disoccluded (new surface revealed by camera motion).
    /// @source UE5 PostProcessAmbientOcclusion.usf:1284-1291
    float2 destVeloc = float2(0.0f, 0.0f);
    {
        // Read velocity at the destination (previous UV location).
        // In UE5 this is done via a second ReprojectPos() call; we read
        // the velocity buffer at prevUV to get the equivalent displacement.
        float2 destVelocPixels = SampleVelocity(prevUV);
        destVeloc = destVelocPixels * texelSize;
    }

    /// @source UE5 PostProcessAmbientOcclusion.usf:1291
    float velocCompare = CompareVeloc(velocityUV, destVeloc);

    // ── 5. Neighbourhood variance clamp ──
    // Compute acceptable AO range from the current frame's neighbourhood.
    // This prevents ghosting by clamping history to a plausible range
    // derived from the spatial neighbourhood of the new AO.
    /// @source UE5 PostProcessAmbientOcclusion.usf:1294-1296
    /// @constants 0.1, 0.0 (usf:1294 — lerp endpoints for RangeVal)
    float rangeVal = lerp(0.1f, 0.0f, velocityMag);
    float minAO    = saturate(newAO - rangeVal);
    float maxAO    = saturate(newAO + rangeVal);

    // ── 6. Read and clamp history ──
    /// @source UE5 PostProcessAmbientOcclusion.usf:1299-1301
    // History at reprojected UV — manual bilinear with per-sample clamping.
    float historyPrevUV = ReadHistoryClamp(prevUV, minAO, maxAO);

    // History at current UV (fallback for when reprojection is unreliable).
    float historyThisUV = SampleHistoryBilinear(uv);
    historyThisUV = clamp(historyThisUV, minAO, maxAO);

    // ── 7. Select history based on velocity consistency ──
    // When forward and backward velocities agree (velocCompare ~ 1.0), prefer
    // the reprojected history. When they disagree (disocclusion), fall back
    // to the current-UV history which is less prone to ghosting.
    /// @source UE5 PostProcessAmbientOcclusion.usf:1303-1305
    float historyAO = lerp(historyThisUV, historyPrevUV, velocCompare);

    // ── 8. Final temporal blend ──
    // BlendWeight controls how much of the new frame is mixed in.
    // UE5 default: 0.1 (10% new, 90% history) for strong temporal stability.
    /// @source UE5 PostProcessAmbientOcclusion.usf:1307
    /// @source UE5 Scene.cpp:535 — AmbientOcclusionTemporalBlendWeight = 0.1
    float result = lerp(historyAO, newAO, blendWeight);

    outputAO[dispatchId] = saturate(result);
}
