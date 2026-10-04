/// UzEngine Temporal Anti-Aliasing Resolve Compute Shader
///
/// @ref Karis 2014 "High Quality Temporal Supersampling" — core TAA algorithm
/// @ref Salvi 2016 "An Excursion in Temporal Supersampling" — variance clipping
/// @ref Catmull & Rom 1974 — bicubic history reprojection (5-tap plus pattern)
/// @constants CurrentFrameWeight=0.04, velocityLerp=0.2 at v/40, varianceClipGamma=1.25
///
/// Algorithm:
/// 1. Sample current frame + 3x3 neighborhood in YCoCg space.
/// 2. Plus-filtered center with Gaussian*HDR weighting (sigma=0.47).
/// 3. Closest-depth dilation (cross pattern) for silhouette-correct velocity.
/// 4. Velocity buffer reprojection (full camera+object motion).
/// 5. 5-tap bicubic Catmull-Rom history sampling.
/// 6. Variance clip (Salvi 2016) intersected with AABB.
/// 7. HDR-weighted blend with velocity-modulated blend factor.

#include "../common/region_dump.hlsli"

/// TAA constant buffer at b2 — uploaded via CBRingAllocator.
cbuffer TaaCB : register(b2) {
    float2 jitterOffset;        ///< Sub-pixel jitter (pixels).
    float2 texelSize;           ///< 1.0 / float2(width, height).
    float  currentFrameWeight;  ///< Blend weight for current frame (0.04).
    uint   sceneSrvIndex;       ///< Bindless SRV: HDR scene color (RGBA16F).
    uint   depthSrvIndex;       ///< Bindless SRV: depth buffer (R32F).
    uint   historySrvIndex;     ///< Bindless SRV: previous TAA output (RGBA16F).
    uint   velocitySrvIndex;    ///< Bindless SRV: velocity buffer (RG16F, pixels).
    float  outputWidth;         ///< Output width in pixels.
    float  outputHeight;        ///< Output height in pixels.
    float  varianceClipGamma;   ///< CVar r.TAA.VarianceClipGamma (default 1.25).
    float  velocityThreshold;   ///< CVar r.TAA.VelocityThreshold (default 40.0, pixels).
    float  antiFlickerMax;      ///< CVar r.TAA.AntiFlickerMax (default 0.3).
    float  spatialWeightMul;    ///< CVar r.TAA.SpatialWeightMultiplier (default 2.29).
    float  exposureScale;       ///< CVar r.TAA.ExposureScale (default 1.0).
    uint   debugRegionUav;      ///< Region dump UAV (0xffffffff = off), see common/region_dump.hlsli.
    uint   debugRegionSrv;      ///< Rect table SRV (StructuredBuffer<uint4>, 2 elements per rect).
    uint   debugRegionCount;    ///< Rect count (0 = off).
    uint   debugRegionStamp;    ///< Frame stamp the GPU writes into the rect footer.
    uint   debugRegionFields;   ///< Field-group bitmask (UZ_REGION_FIELD_*).
    uint   clampBoxSrvMin;      ///< r.TAA.ClampFreezeAfterFrame storage (0xffffffff = unavailable).
    uint   clampBoxSrvMax;
    uint   clampBoxUavMin;
    uint   clampBoxUavMax;
    uint   clampFreezeActive;   ///< 1 = read the stored clamp box, 0 = compute it and store it.
    uint   clampFreezeEnabled;  ///< 0 = diagnostic off; do not touch the storage at all.
    float  hdrWeightExponent;   ///< CVar r.TAA.ReconstructionHdrWeightExponent (1 = production).
    uint   reconstructionMode;  ///< CVar r.TAA.ReconstructionMode (0 = production).
    uint   clampMode;           ///< CVar r.TAA.ClampMode (0 = production).
    uint   blendMode;           ///< CVar r.TAA.BlendMode (0 = production).
    uint   _padModes;
};

SamplerState LinearClampSampler : register(s0);

RWTexture2D<float4> taaOutput : register(u0);

// SM5 fallback
#ifndef UZ_BINDLESS
Texture2D<float4> g_SceneColor  : register(t0);
Texture2D<float>  g_DepthTex    : register(t1);
Texture2D<float4> g_HistoryTex  : register(t2);
Texture2D<float2> g_VelocityTex : register(t3);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Color Space Transforms
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Karis 2014 "High Quality Temporal Supersampling" — RGBToYCoCg
/// 4x scaled YCoCg (factor cancels in inverse).
float3 RGBToYCoCg(float3 RGB)
{
    float Y  = dot(RGB, float3( 1, 2,  1));
    float Co = dot(RGB, float3( 2, 0, -2));
    float Cg = dot(RGB, float3(-1, 2, -1));
    return float3(Y, Co, Cg);
}

/// @ref Karis 2014 "High Quality Temporal Supersampling" — YCoCgToRGB
float3 YCoCgToRGB(float3 YCoCg)
{
    float Y  = YCoCg.x * 0.25;
    float Co = YCoCg.y * 0.25;
    float Cg = YCoCg.z * 0.25;
    float R = Y + Co - Cg;
    float G = Y + Cg;
    float B = Y - Co - Cg;
    return float3(R, G, B);
}

// ─────────────────────────────────────────────────────────────────────────────
// HDR Weighting
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Karis 2014 "High Quality Temporal Supersampling" — Luma4
float Luma4(float3 Color)
{
    return (Color.g * 2.0) + (Color.r + Color.b);
}

/// @ref Karis 2014 "High Quality Temporal Supersampling" — HdrWeight4
/// Tonemap-inspired weighting that reduces firefly contribution.
float HdrWeight4(float3 Color, float Exposure)
{
    return rcp(Luma4(Color) * Exposure + 4.0);
}

/// @ref Karis 2014 "High Quality Temporal Supersampling" — HdrWeightY
float HdrWeightY(float Y, float Exposure)
{
    return rcp(Y * Exposure + 4.0);
}

/// @ref Karis 2014 "High Quality Temporal Supersampling" — WeightedLerpFactors
float2 WeightedLerpFactors(float WeightA, float WeightB, float Blend)
{
    float BlendA = (1.0 - Blend) * WeightA;
    float BlendB =        Blend  * WeightB;
    float RcpBlend = rcp(BlendA + BlendB);
    BlendA *= RcpBlend;
    BlendB *= RcpBlend;
    return float2(BlendA, BlendB);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3x3 Neighborhood
// ─────────────────────────────────────────────────────────────────────────────

static const int2 kOffsets3x3[9] =
{
    int2(-1, -1), int2( 0, -1), int2( 1, -1),
    int2(-1,  0), int2( 0,  0), int2( 1,  0),
    int2(-1,  1), int2( 0,  1), int2( 1,  1),
};

// ─────────────────────────────────────────────────────────────────────────────
// TAA Resolve
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Karis 2014 "High Quality Temporal Supersampling" — main TAA resolve
/// @ref Salvi 2016 "An Excursion in Temporal Supersampling" — variance clipping
[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    uint outW = uint(outputWidth  + 0.5);
    uint outH = uint(outputHeight + 0.5);

    if (any(DTid >= uint2(outW, outH)))
        return;

    // ── UV ──────────────────────────────────────────────────────────────────
    float2 outputTexelSize = float2(1.0 / outputWidth, 1.0 / outputHeight);
    float2 uv = (float2(DTid) + 0.5) * outputTexelSize;

    // ── Texture access ──────────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4> SceneColor = ResourceDescriptorHeap[sceneSrvIndex];
    Texture2D<float>  DepthTex   = ResourceDescriptorHeap[depthSrvIndex];
    Texture2D<float4> HistoryTex = ResourceDescriptorHeap[historySrvIndex];
    Texture2D<float2> VelocityTex = ResourceDescriptorHeap[velocitySrvIndex];
#else
    Texture2D<float4> SceneColor = g_SceneColor;
    Texture2D<float>  DepthTex   = g_DepthTex;
    Texture2D<float4> HistoryTex = g_HistoryTex;
    Texture2D<float2> VelocityTex = g_VelocityTex;
#endif

    uint sceneW, sceneH;
    SceneColor.GetDimensions(sceneW, sceneH);

    // ── 3x3 Neighborhood in YCoCg ──────────────────────────────────────────
    /// @ref Karis 2014 "High Quality Temporal Supersampling" — VGPR 3x3 cache
    float3 neighbors[9];
    float neighborWeights[9];
    int2 centerCoord = int2(uv * float2(sceneW, sceneH));
    centerCoord = clamp(centerCoord, int2(0, 0), int2(sceneW - 1, sceneH - 1));

    const float FrameExposureScale = exposureScale;

    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        int2 coord = clamp(centerCoord + kOffsets3x3[i], int2(0, 0), int2(sceneW - 1, sceneH - 1));
        float3 rawColor = SceneColor.Load(int3(coord, 0)).rgb;
        neighbors[i] = RGBToYCoCg(rawColor);
        neighborWeights[i] = HdrWeightY(neighbors[i].x, FrameExposureScale);
    }

    // ── Plus-Filtered Center ────────────────────────────────────────────────
    /// @ref Karis 2014 "High Quality Temporal Supersampling" — AA_FILTERED=1, sigma=0.47
    static const uint kPlusIndices[5] = { 1, 3, 4, 5, 7 };
    static const float2 kPlusOffsets[5] = {
        float2( 0, -1), float2(-1,  0), float2(0, 0),
        float2( 1,  0), float2( 0,  1)
    };

    // `reconstructionMode` is a diagnostic switch (r.TAA.ReconstructionMode, 0 = production).
    // It changes ONE factor at a time so the reconstruction can be separated from the clamp and
    // the blend: 1 drops the HDR weighting, 2 drops the filter entirely, 3 widens it to the full
    // 3x3 with the same sample-position-aware weights.
    float3 filteredColor = float3(0, 0, 0);
    float totalWeight = 0.0;
    // Region dump (diagnostics): the weight applied to each plus tap, so the reconstruction can be
    // recomputed from the records. Stays 0 for the 3x3 mode, which the records cannot describe.
    float dbgPlusWeight[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };

    if (reconstructionMode == 2u) {
        filteredColor = neighbors[4];
        totalWeight = 1.0;
        dbgPlusWeight[2] = 1.0;
    } else if (reconstructionMode == 3u) {
        [unroll]
        for (uint k = 0; k < 9; k++) {
            float2 pixelOffset = float2(kOffsets3x3[k]) - jitterOffset;
            float spatialWeight = exp(-spatialWeightMul * dot(pixelOffset, pixelOffset));
            float sampleWeight = spatialWeight * pow(neighborWeights[k], hdrWeightExponent);
            filteredColor += neighbors[k] * sampleWeight;
            totalWeight   += sampleWeight;
        }
        filteredColor *= rcp(totalWeight);
    } else {
        [unroll]
        for (uint j = 0; j < 5; j++)
        {
            float2 pixelOffset = kPlusOffsets[j] - jitterOffset;
            /// @ref Karis 2014 — exp(-2.29 * d^2), sigma=0.47
            float spatialWeight = exp(-spatialWeightMul * dot(pixelOffset, pixelOffset));
            // `hdrWeightExponent` (1 = production) softens the HDR weighting instead of removing
            // it: pow(w, 0) is the unweighted filter, pow(w, 1) is Karis 2014.
            float hdrWeight = (reconstructionMode == 1u)
                            ? 1.0
                            : pow(neighborWeights[kPlusIndices[j]], hdrWeightExponent);
            float sampleWeight = spatialWeight * hdrWeight;
            dbgPlusWeight[j] = sampleWeight;
            filteredColor += neighbors[kPlusIndices[j]] * sampleWeight;
            totalWeight   += sampleWeight;
        }
        filteredColor *= rcp(totalWeight);
    }

    // ── Neighborhood Min/Max ────────────────────────────────────────────────
    /// @ref Karis 2014 "High Quality Temporal Supersampling" — AABB clamping box
    float3 neighborMin = min(min(neighbors[1], neighbors[3]), neighbors[4]);
    neighborMin = min(min(neighborMin, neighbors[5]), neighbors[7]);
    float3 neighborMax = max(max(neighbors[1], neighbors[3]), neighbors[4]);
    neighborMax = max(max(neighborMax, neighbors[5]), neighbors[7]);

    // Expand to full 3x3
    neighborMin = min(min(neighborMin, neighbors[0]), neighbors[2]);
    neighborMin = min(min(neighborMin, neighbors[6]), neighbors[8]);
    neighborMax = max(max(neighborMax, neighbors[0]), neighbors[2]);
    neighborMax = max(max(neighborMax, neighbors[6]), neighbors[8]);

    // ── Closest Depth Search (Cross Pattern) ────────────────────────────────
    /// @ref Karis 2014 "High Quality Temporal Supersampling" — AA_CROSS=2 depth dilation
    /// Find nearest depth in 5-sample cross for silhouette-correct reprojection.
    float centerDepth = DepthTex.Load(int3(centerCoord, 0)).x;
    float2 velocityOffset = float2(0, 0);

    {
        float4 crossDepths;
        crossDepths.x = DepthTex.Load(int3(clamp(centerCoord + int2(-2, -2), int2(0,0), int2(sceneW-1,sceneH-1)), 0)).x;
        crossDepths.y = DepthTex.Load(int3(clamp(centerCoord + int2( 2, -2), int2(0,0), int2(sceneW-1,sceneH-1)), 0)).x;
        crossDepths.z = DepthTex.Load(int3(clamp(centerCoord + int2(-2,  2), int2(0,0), int2(sceneW-1,sceneH-1)), 0)).x;
        crossDepths.w = DepthTex.Load(int3(clamp(centerCoord + int2( 2,  2), int2(0,0), int2(sceneW-1,sceneH-1)), 0)).x;

        // Reversed-Z: nearest = largest depth value.
        float2 depthOffset = float2(2, 2);
        float depthOffsetXx = 2.0;

        if (crossDepths.x > crossDepths.y) depthOffsetXx = -2.0;
        if (crossDepths.z > crossDepths.w) depthOffset.x = -2.0;

        float depthsXY = max(crossDepths.x, crossDepths.y);
        float depthsZW = max(crossDepths.z, crossDepths.w);
        if (depthsXY > depthsZW)
        {
            depthOffset.y = -2.0;
            depthOffset.x = depthOffsetXx;
        }
        float depthsXYZW = max(depthsXY, depthsZW);
        if (depthsXYZW > centerDepth)
        {
            velocityOffset = depthOffset * texelSize;
            centerDepth = depthsXYZW;
        }
    }

    // ── Reprojection ────────────────────────────────────────────────────────
    /// @ref Karis 2014 "High Quality Temporal Supersampling" — velocity buffer reprojection
    int2 dilatedCoord = centerCoord + int2(velocityOffset * rcp(texelSize) + 0.5);
    dilatedCoord = clamp(dilatedCoord, int2(0, 0), int2(sceneW - 1, sceneH - 1));
    float2 velocityPixels_raw = VelocityTex.Load(int3(dilatedCoord, 0));
    float2 motionVector = velocityPixels_raw * texelSize;
    float2 historyUV = uv - motionVector;

    float2 velocityPixels = motionVector * float2(outputWidth, outputHeight);
    float velocityMag = length(velocityPixels);

    bool offScreen = any(historyUV < 0.0) || any(historyUV > 1.0);

    // ── Bicubic Catmull-Rom History (5-tap) ─────────────────────────────────
    /// @ref Catmull & Rom 1974 — cubic spline interpolation
    /// @ref Karis 2014 "High Quality Temporal Supersampling" — 5-tap optimization
    float2 clampedHistoryUV = clamp(historyUV, outputTexelSize * 0.5, 1.0 - outputTexelSize * 0.5);
    float3 rawHistory;
    // Recorded so an offline replay does not have to reconstruct the sample position from the
    // velocity: a position error and a filtering difference are then separable.
    float2 dbgHistorySamplePos = float2(0, 0);
    float2 dbgHistoryFrac = float2(0, 0);
    {
        float2 historySize    = float2(outputWidth, outputHeight);
        float2 historyInvSize = outputTexelSize;

        float2 samplePos = clampedHistoryUV * historySize;
        float2 tc = floor(samplePos - 0.5) + 0.5;
        float2 f  = samplePos - tc;
        dbgHistorySamplePos = samplePos;   // recorded for the offline replay
        dbgHistoryFrac      = f;
        float2 f2 = f * f;
        float2 f3 = f2 * f;

        // Catmull-Rom basis weights
        float2 w0 = f2 - 0.5 * (f3 + f);
        float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
        float2 w3 = 0.5 * (f3 - f2);
        float2 w2 = 1.0 - w0 - w1 - w3;

        float2 w12 = w1 + w2;

        float2 sample0 = (tc - 1.0) * historyInvSize;
        float2 sample1 = (tc + w2 * rcp(w12)) * historyInvSize;
        float2 sample2 = (tc + 2.0) * historyInvSize;

        // 5-tap plus pattern (omit corner taps)
        float weight0 = w12.x * w0.y;
        float weight1 = w0.x  * w12.y;
        float weight2 = w12.x * w12.y;
        float weight3 = w3.x  * w12.y;
        float weight4 = w12.x * w3.y;

        rawHistory  = HistoryTex.SampleLevel(LinearClampSampler, float2(sample1.x, sample0.y), 0).rgb * weight0;
        rawHistory += HistoryTex.SampleLevel(LinearClampSampler, float2(sample0.x, sample1.y), 0).rgb * weight1;
        rawHistory += HistoryTex.SampleLevel(LinearClampSampler, float2(sample1.x, sample1.y), 0).rgb * weight2;
        rawHistory += HistoryTex.SampleLevel(LinearClampSampler, float2(sample2.x, sample1.y), 0).rgb * weight3;
        rawHistory += HistoryTex.SampleLevel(LinearClampSampler, float2(sample1.x, sample2.y), 0).rgb * weight4;

        float finalMultiplier = rcp(weight0 + weight1 + weight2 + weight3 + weight4);
        rawHistory *= finalMultiplier;
    }

    float3 historyYCoCg = RGBToYCoCg(rawHistory);

    // ── Variance Clip ───────────────────────────────────────────────────────
    /// @ref Salvi 2016 "An Excursion in Temporal Supersampling" — variance clipping
    /// @constants gamma = 1.25 (standard variance clip multiplier)
    float3 neighborMu = float3(0, 0, 0);
    float3 neighborSigma = float3(0, 0, 0);
    [unroll] for (int ni = 0; ni < 9; ++ni) {
        neighborMu    += neighbors[ni];
        neighborSigma += neighbors[ni] * neighbors[ni];
    }
    neighborMu    /= 9.0;
    neighborSigma = sqrt(abs(neighborSigma / 9.0 - neighborMu * neighborMu));

    float kVarianceClipGamma = varianceClipGamma;
    float3 varianceMin = neighborMu - kVarianceClipGamma * neighborSigma;
    float3 varianceMax = neighborMu + kVarianceClipGamma * neighborSigma;

    // Intersect variance box with AABB for robustness.
    // `clampMode` is a diagnostic switch (r.TAA.ClampMode, 0 = production): 1 lets the history
    // through unclamped, 2 keeps only the variance box, 3 only the neighbourhood AABB.
    float3 clipMin = max(neighborMin, varianceMin);
    float3 clipMax = min(neighborMax, varianceMax);
    if (clampMode == 2u) { clipMin = varianceMin; clipMax = varianceMax; }
    else if (clampMode == 3u) { clipMin = neighborMin; clipMax = neighborMax; }
    else if (clampMode == 1u) { clipMin = float3(-1e30, -1e30, -1e30); clipMax = float3(1e30, 1e30, 1e30); }

    // ── Frozen clamp bounds (diagnostic, r.TAA.ClampFreezeAfterFrame — ledger H-25) ────────
    // Until the freeze frame the box is stored; after it the stored box is reused, so the bounds
    // stop moving while the reconstruction, the exposure and the lighting are untouched. Stored in
    // YCoCg, the space the clamp works in, so no conversion is involved either way.
    if (clampFreezeEnabled != 0u) {
        if (clampBoxSrvMin != 0xffffffffu && clampBoxSrvMax != 0xffffffffu) {
            Texture2D<float4> BoxMin = ResourceDescriptorHeap[clampBoxSrvMin];
            Texture2D<float4> BoxMax = ResourceDescriptorHeap[clampBoxSrvMax];
            clipMin = BoxMin.Load(int3(DTid, 0)).rgb;
            clipMax = BoxMax.Load(int3(DTid, 0)).rgb;
        } else if (clampBoxUavMin != 0xffffffffu && clampBoxUavMax != 0xffffffffu) {
            RWTexture2D<float4> BoxMinOut = ResourceDescriptorHeap[clampBoxUavMin];
            RWTexture2D<float4> BoxMaxOut = ResourceDescriptorHeap[clampBoxUavMax];
            BoxMinOut[DTid] = float4(clipMin, 0.0);
            BoxMaxOut[DTid] = float4(clipMax, 0.0);
        }
    }

    float3 clampedHistory = clamp(historyYCoCg, clipMin, clipMax);

    // ── Blend Weight ────────────────────────────────────────────────────────
    /// @ref Karis 2014 "High Quality Temporal Supersampling" — blend amount computation
    float blendFinal = currentFrameWeight;

    /// @ref Karis 2014 — lerp(blend, 0.2, saturate(velocity/40))
    blendFinal = lerp(blendFinal, 0.2, saturate(velocityMag / velocityThreshold));

    /// @ref Karis 2014 — anti-flicker minimum contribution guard
    /// Added epsilon to prevent near-zero denominator in static scenes.
    /// Without epsilon, abs(lumaFiltered - lumaHistory) → 0 when scene is static,
    /// causing blendFinal → 1.0 which reintroduces jitter (current frame dominates).
    float lumaFiltered = filteredColor.x;
    float lumaHistory  = clampedHistory.x;
    float lumaDiff = abs(lumaFiltered - lumaHistory);
    float antiFlicker = saturate(0.01 * lumaHistory / max(lumaDiff, 0.001));
    // `blendMode` is a diagnostic switch (r.TAA.BlendMode, 0 = production): 1 keeps the constant
    // r.TAA.CurrentFrameWeight, dropping both the velocity lerp and the anti-flicker guard.
    if (blendMode == 1u) blendFinal = currentFrameWeight;
    else blendFinal = max(blendFinal, min(antiFlicker, antiFlickerMax));

    // NaN guard
    blendFinal = -min(-blendFinal, 0.0);

    // ── Off-screen reset ────────────────────────────────────────────────────
    if (offScreen)
        clampedHistory = filteredColor;

    // ── HDR-Weighted Blend ──────────────────────────────────────────────────
    /// @ref Karis 2014 "High Quality Temporal Supersampling" — HDR weighted lerp
    float filterWeight  = HdrWeightY(filteredColor.x, FrameExposureScale);
    float historyWeight = HdrWeightY(clampedHistory.x, FrameExposureScale);

    float2 blendWeights = WeightedLerpFactors(historyWeight, filterWeight, blendFinal);
    float3 outputYCoCg = clampedHistory * blendWeights.x + filteredColor * blendWeights.y;

    // ── Convert back to RGB ─────────────────────────────────────────────────
    float3 outputRGB = YCoCgToRGB(outputYCoCg);
    outputRGB = -min(-outputRGB, 0.0); // NaN/negative guard

    taaOutput[DTid] = float4(outputRGB, 1.0);

#ifdef UZ_BINDLESS
    // Region dump (diagnostics): resolve-side records, see common/region_dump.hlsli.
    // Every colour is written as linear HDR RGB (the YCoCg working values are converted back),
    // so the analysis never has to know this shader's internal colour space.
    if (debugRegionUav != 0xffffffffu && debugRegionCount != 0u) {
        StructuredBuffer<uint4>    rects = ResourceDescriptorHeap[debugRegionSrv];
        RWStructuredBuffer<float4> dump  = ResourceDescriptorHeap[debugRegionUav];
        const uint2 rp = DTid.xy;
        for (uint r = 0u; r < debugRegionCount; ++r) {
            const uint4 a = rects[r * 2u];
            const uint4 b = rects[r * 2u + 1u];
            if (!UzRegionContains(a, rp)) continue;
            const uint pixelBase = UzRegionPixelBase(a, b, rp);
            if ((debugRegionFields & UZ_REGION_FIELD_TAA) != 0u) {
                const uint rec = pixelBase + UzRegionTaaOffset(debugRegionFields);
                dump[rec + 0] = float4(YCoCgToRGB(neighbors[4]), blendFinal);
                dump[rec + 1] = float4(YCoCgToRGB(filteredColor), velocityPixels.x);
                dump[rec + 2] = float4(rawHistory, velocityPixels.y);
                dump[rec + 3] = float4(YCoCgToRGB(clampedHistory), offScreen ? 1.0 : 0.0);
                dump[rec + 4] = float4(outputRGB, FrameExposureScale);
            }
            if ((debugRegionFields & UZ_REGION_FIELD_OUTPUT) != 0u) {
                dump[pixelBase + UzRegionOutputOffset(debugRegionFields)] =
                    float4(outputRGB, FrameExposureScale);
            }
            if ((debugRegionFields & UZ_REGION_FIELD_TAA_EXTRA) != 0u) {
                const uint rec = pixelBase + UzRegionTaaExtraOffset(debugRegionFields);
                dump[rec + 0] = float4(YCoCgToRGB(clipMin), centerDepth);
                dump[rec + 1] = float4(YCoCgToRGB(clipMax), length(velocityPixels));
                dump[rec + 2] = float4(neighbors[1].x, neighbors[3].x, neighbors[5].x, neighbors[7].x);
                dump[rec + 3] = float4(dbgHistorySamplePos, dbgHistoryFrac);
                // +4..+8 the five plus taps the reconstruction used (up, left, centre, right, down):
                // colour as linear RGB, and in w the weight ACTUALLY applied (spatial x HDR).
                dump[rec + 4] = float4(YCoCgToRGB(neighbors[1]), dbgPlusWeight[0]);
                dump[rec + 5] = float4(YCoCgToRGB(neighbors[3]), dbgPlusWeight[1]);
                dump[rec + 6] = float4(YCoCgToRGB(neighbors[4]), dbgPlusWeight[2]);
                dump[rec + 7] = float4(YCoCgToRGB(neighbors[5]), dbgPlusWeight[3]);
                dump[rec + 8] = float4(YCoCgToRGB(neighbors[7]), dbgPlusWeight[4]);
                // +9 the jitter the filter consumed, the normalisation and the spatial multiplier;
                // +10 the HDR weighting parameters and the reconstruction mode.
                dump[rec + 9]  = float4(jitterOffset.x, jitterOffset.y, totalWeight, spatialWeightMul);
                dump[rec + 10] = float4(FrameExposureScale, hdrWeightExponent, float(reconstructionMode), 0.0);
            }
            if (rp.x == a.x && rp.y == a.y) {
                dump[UzRegionFooterBase(a, b) + 1] =
                    float4(asfloat(debugRegionStamp), float(outputWidth), float(outputHeight), 1.0);
            }
        }
    }
#endif
}
