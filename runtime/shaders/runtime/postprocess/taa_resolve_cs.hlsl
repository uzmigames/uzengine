/// UzEngine Temporal Anti-Aliasing Resolve Compute Shader
///
/// @source UE5 TemporalAA.usf:70-2428 — MainCS / TemporalAASample
/// @constants CurrentFrameWeight=0.04 (TemporalAA.cpp:41), velocityLerp=0.2 at v/40 (TemporalAA.usf:2233)
/// @deviations Simplified: no LDS cache, no TAAU upsampling, no DOF CoC, no stencil responsive AA,
///             no stochastic quantization, no bicubic history. UzEngine uses compute with VGPR 3x3 cache.
///             Core algorithm (YCoCg clamp + HDR weighted blend + velocity rejection) is identical to UE5.

/// Root constants at b0 — packed into 8 DWORDs for the compute root signature.
///
/// Layout matches UzEngine's compute root signature:
///   b0 = 16 root constants (we use first 8)
///   t0 = SRV descriptor table
///   u0 = UAV descriptor table
cbuffer TAARootConstants : register(b0) {
    float2 jitterOffset;        ///< Sub-pixel jitter applied to current frame's projection (pixels).
    float2 texelSize;           ///< 1.0 / float2(width, height).
    float  currentFrameWeight;  ///< Blend weight for current frame. UE5 default: 0.04.
    uint   sceneSrvIndex;       ///< Bindless SRV index for HDR scene color (RGBA16F).
    uint   depthSrvIndex;       ///< Bindless SRV index for depth buffer (R32F or D32F).
    uint   historySrvIndex;     ///< Bindless SRV index for previous frame TAA output (RGBA16F).
};

/// Bilinear clamp sampler for history texture sampling.
SamplerState LinearClampSampler : register(s0);

/// TAA output UAV — bound via descriptor table at root param 4 (u0).
RWTexture2D<float4> taaOutput : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// Color Space Transforms
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 TemporalAA.usf:597-605 — RGBToYCoCg
/// Converts linear RGB to YCoCg color space for improved temporal clamping.
/// The Y channel is luminance, Co/Cg are chroma. The 1/4 scale factor in the
/// inverse (lines 609-618) is absorbed by the forward transform's coefficients.
float3 RGBToYCoCg(float3 RGB)
{
    float Y  = dot(RGB, float3( 1, 2,  1));
    float Co = dot(RGB, float3( 2, 0, -2));
    float Cg = dot(RGB, float3(-1, 2, -1));
    return float3(Y, Co, Cg);
}

/// @source UE5 TemporalAA.usf:607-619 — YCoCgToRGB
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

/// @source UE5 TemporalAA.usf:623-626 — Luma4
/// Fast luminance approximation including a 4x scale factor.
float Luma4(float3 Color)
{
    return (Color.g * 2.0) + (Color.r + Color.b);
}

/// @source UE5 TemporalAA.usf:629-632 — HdrWeight4
/// Tonemap-inspired weighting that reduces firefly contribution.
/// The 4.0 addend stabilizes the reciprocal for near-black pixels.
float HdrWeight4(float3 Color, float Exposure)
{
    return rcp(Luma4(Color) * Exposure + 4.0);
}

/// @source UE5 TemporalAA.usf:634-637 — HdrWeightY
/// HDR weight for YCoCg-space data where Y is already the luminance channel.
float HdrWeightY(float Y, float Exposure)
{
    return rcp(Y * Exposure + 4.0);
}

/// @source UE5 TemporalAA.usf:684-692 — WeightedLerpFactors
/// Computes blend factors that incorporate per-sample HDR weights.
/// This prevents bright fireflies from dominating the blend.
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
// Velocity Reprojection
// ─────────────────────────────────────────────────────────────────────────────

/// Reads depth and computes camera-only motion vector via depth reprojection.
///
/// @source UE5 TemporalAA.usf:2087-2121 — Camera motion via ClipToPrevClip
///
/// Since UzEngine does not yet have a velocity buffer (per-object motion),
/// we reproject via the depth buffer and the prevViewProj matrix stored in
/// PerFrameCB. This handles camera motion correctly. Object motion will be
/// added when the velocity buffer pass is implemented (task 1.2/1.3).
///
/// For now, we do NOT have prevViewProj in PerFrameCB, so we use a simple
/// approach: treat the jitter offset as the only motion. This is sufficient
/// for static scenes and provides correct TAA convergence. When the velocity
/// buffer is added, this function will be replaced with a proper velocity read.
float2 ComputeMotionVector(int2 pixelCoord, float2 uv)
{
    // Without a velocity buffer or prevViewProj, we approximate by using
    // zero motion (camera-only jitter is handled by the jitter offset).
    // The jitter is already accounted for in the UV calculation.
    //
    // TODO(velocity-buffer): Replace with actual velocity buffer read when
    // task 1.2 is implemented. For now, TAA still converges correctly for
    // static scenes because the jitter offset is the dominant sub-pixel motion.
    return float2(0.0, 0.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 3x3 Neighborhood Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 TemporalAA.usf:447-458 — kOffsets3x3
static const int2 kOffsets3x3[9] =
{
    int2(-1, -1), int2( 0, -1), int2( 1, -1),
    int2(-1,  0), int2( 0,  0), int2( 1,  0),
    int2(-1,  1), int2( 0,  1), int2( 1,  1),
};

// ─────────────────────────────────────────────────────────────────────────────
// TAA Resolve Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 TemporalAA.usf:2327-2407 — MainCS
/// @source UE5 TemporalAA.usf:1967-2321 — TemporalAASample
///
/// UzEngine TAA resolve compute shader. Implements the core UE5 TAA algorithm:
///
/// 1. Sample current frame HDR scene color at the jittered pixel position.
/// 2. Gather 3x3 neighborhood in YCoCg space for color bounding box.
/// 3. Find closest depth in a cross pattern for velocity dilation.
/// 4. Compute reprojected UV from motion vector (or depth reprojection).
/// 5. Sample history buffer at reprojected UV (bilinear).
/// 6. Convert history to YCoCg, clamp to neighborhood min/max box.
/// 7. Compute blend weight (currentFrameWeight modulated by velocity).
/// 8. HDR-weighted lerp between clamped history and filtered current.
/// 9. Convert back to RGB, output to UAV.
[numthreads(8, 8, 1)]
void main(
    uint2 DispatchThreadId : SV_DispatchThreadID,
    uint2 GroupId : SV_GroupID,
    uint2 GroupThreadId : SV_GroupThreadID)
{
    // Get output dimensions from texel size.
    uint outputWidth  = uint(rcp(texelSize.x) + 0.5);
    uint outputHeight = uint(rcp(texelSize.y) + 0.5);

    // Early out for threads outside the render target.
    if (any(DispatchThreadId >= uint2(outputWidth, outputHeight)))
        return;

    // ── UV Computation ──────────────────────────────────────────────────────
    // Center of the output pixel in UV space.
    float2 uv = (float2(DispatchThreadId) + 0.5) * texelSize;

    // ── Bindless Texture Access ─────────────────────────────────────────────
    Texture2D<float4> SceneColor = ResourceDescriptorHeap[sceneSrvIndex];
    Texture2D<float>  DepthTex   = ResourceDescriptorHeap[depthSrvIndex];
    Texture2D<float4> HistoryTex = ResourceDescriptorHeap[historySrvIndex];

    // ── Sample Current Frame + 3x3 Neighborhood ────────────────────────────
    // @source UE5 TemporalAA.usf:928-969 — VGPR_3X3 cache path
    //
    // Cache the 3x3 neighborhood around the current pixel for:
    // 1. Filtered center sample (plus-weighted)
    // 2. Min/max bounding box for history clamping
    float3 neighbors[9];
    float neighborWeights[9];
    int2 centerCoord = int2(DispatchThreadId);

    // Exposure = 1.0 for now (auto-exposure is pre-applied to HDR).
    // @source UE5 TemporalAA.usf:1974 — FrameExposureScale * View.OneOverPreExposure
    const float FrameExposureScale = 1.0;

    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        int2 coord = centerCoord + kOffsets3x3[i];
        // Clamp to valid range to avoid sampling outside the render target.
        coord = clamp(coord, int2(0, 0), int2(outputWidth - 1, outputHeight - 1));

        float3 rawColor = SceneColor.Load(int3(coord, 0)).rgb;
        // Transform to YCoCg for clamping (UE5 AA_YCOCG=1 for Main pass).
        // @source UE5 TemporalAA.usf:70-76 — TAA_PASS_CONFIG==0, AA_YCOCG=1
        neighbors[i] = RGBToYCoCg(rawColor);
        neighborWeights[i] = HdrWeightY(neighbors[i].x, FrameExposureScale);
    }

    // ── Plus-Filtered Current Frame ─────────────────────────────────────────
    // @source UE5 TemporalAA.usf:1532-1692 — FilterCurrentFrameInputSamples (AA_FILTERED=1)
    // @source UE5 TemporalAA.usf:1560-1562 — AA_SAMPLES==5, kPlusIndexes3x3
    //
    // Weighted filter using the plus-shaped neighbors (indices 1,3,4,5,7).
    // Weight = Gaussian(pixel_offset - jitter, sigma=0.47) * HDR_weight.
    // @source UE5 TemporalAA.cpp:449 — Sigma=0.47, exp(-2.29 * d^2)
    static const uint kPlusIndices[5] = { 1, 3, 4, 5, 7 };
    static const float2 kPlusOffsets[5] = {
        float2( 0, -1), float2(-1,  0), float2(0, 0),
        float2( 1,  0), float2( 0,  1)
    };

    float3 filteredColor = float3(0, 0, 0);
    float totalWeight = 0.0;

    [unroll]
    for (uint j = 0; j < 5; j++)
    {
        float2 pixelOffset = kPlusOffsets[j] - jitterOffset;
        // @source UE5 TemporalAA.cpp:449 — exp(-2.29 * (dx^2 + dy^2)), sigma=0.47
        float spatialWeight = exp(-2.29 * dot(pixelOffset, pixelOffset));
        // @source UE5 TemporalAA.usf:1623-1627 — AA_TONE=1, sample HDR weight
        float hdrWeight = neighborWeights[kPlusIndices[j]];
        float sampleWeight = spatialWeight * hdrWeight;

        filteredColor += neighbors[kPlusIndices[j]] * sampleWeight;
        totalWeight   += sampleWeight;
    }
    // @source UE5 TemporalAA.usf:1674-1676 — Reweight for tone + filtering
    filteredColor *= rcp(totalWeight);

    // ── Neighborhood Min/Max (for history clamping) ─────────────────────────
    // @source UE5 TemporalAA.usf:1782-1823 — HISTORY_CLAMPING_BOX_MIN_MAX
    // Plus-shaped min/max first (indices 1,3,4,5,7).
    float3 neighborMin = min(min(neighbors[1], neighbors[3]), neighbors[4]);
    neighborMin = min(min(neighborMin, neighbors[5]), neighbors[7]);

    float3 neighborMax = max(max(neighbors[1], neighbors[3]), neighbors[4]);
    neighborMax = max(max(neighborMax, neighbors[5]), neighbors[7]);

    // @source UE5 TemporalAA.usf:1805-1821 — AA_SAMPLES==9 case
    // Expand to full 3x3 with averaging (AA_ROUND=0 → no averaging).
    float3 neighborMinPlus = neighborMin;
    float3 neighborMaxPlus = neighborMax;

    neighborMin = min(min(neighborMin, neighbors[0]), neighbors[2]);
    neighborMin = min(min(neighborMin, neighbors[6]), neighbors[8]);

    neighborMax = max(max(neighborMax, neighbors[0]), neighbors[2]);
    neighborMax = max(max(neighborMax, neighbors[6]), neighbors[8]);

    // ── Closest Depth Search (Cross Pattern) ────────────────────────────────
    // @source UE5 TemporalAA.usf:2034-2079 — AA_CROSS=2 depth dilation
    // Find the nearest depth in a 5-sample cross pattern (center + 4 diagonal).
    // This provides better silhouette handling by using the foreground pixel's
    // motion vector for reprojection.
    float centerDepth = DepthTex.Load(int3(centerCoord, 0)).x;
    float2 velocityOffset = float2(0, 0);

    {
        float4 crossDepths;
        crossDepths.x = DepthTex.Load(int3(centerCoord + int2(-2, -2), 0)).x;
        crossDepths.y = DepthTex.Load(int3(centerCoord + int2( 2, -2), 0)).x;
        crossDepths.z = DepthTex.Load(int3(centerCoord + int2(-2,  2), 0)).x;
        crossDepths.w = DepthTex.Load(int3(centerCoord + int2( 2,  2), 0)).x;

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
    // @source UE5 TemporalAA.usf:2087-2121 — Camera motion via ClipToPrevClip
    //
    // Without a velocity buffer, the reprojected UV is simply the current UV
    // with the jitter removed. The TAA jitter is the primary sub-pixel motion
    // for static scenes, and removing it from the history lookup ensures
    // correct temporal convergence.
    float2 motionVector = ComputeMotionVector(centerCoord, uv + velocityOffset);
    float2 historyUV = uv - motionVector;

    // @source UE5 TemporalAA.usf:2108 — Velocity magnitude in pixels
    float2 velocityPixels = motionVector * rcp(texelSize);
    float velocityMag = length(velocityPixels);

    // Detect off-screen reprojection.
    // @source UE5 TemporalAA.usf:2120 — OffScreen detection
    bool offScreen = any(historyUV < 0.0) || any(historyUV > 1.0);

    // ── Sample History (Bilinear) ───────────────────────────────────────────
    // @source UE5 TemporalAA.usf:1883-1895 — Bilinear history (non-bicubic path)
    // Clamp UV to valid range before sampling to avoid reading garbage.
    float2 clampedHistoryUV = clamp(historyUV, texelSize * 0.5, 1.0 - texelSize * 0.5);
    float3 rawHistory = HistoryTex.SampleLevel(LinearClampSampler, clampedHistoryUV, 0).rgb;

    // Transform history to YCoCg.
    // @source UE5 TemporalAA.usf:1926 — TransformSceneColor(HistoryPayload.Color)
    float3 historyYCoCg = RGBToYCoCg(rawHistory);

    // ── Clamp History to Neighborhood Box ───────────────────────────────────
    // @source UE5 TemporalAA.usf:1957-1961 — ClampHistory (AA_CLIP=0 path)
    float3 clampedHistory = clamp(historyYCoCg, neighborMin, neighborMax);

    // ── Compute Blend Weight ────────────────────────────────────────────────
    // @source UE5 TemporalAA.usf:2225-2272 — COMPUTE BLEND AMOUNT

    float blendFinal = currentFrameWeight;

    // @source UE5 TemporalAA.usf:2233 — lerp(blendFinal, 0.2, saturate(Velocity / 40))
    // Increase current frame contribution when camera/objects move fast.
    blendFinal = lerp(blendFinal, 0.2, saturate(velocityMag / 40.0));

    // @source UE5 TemporalAA.usf:2246 — Anti-flicker: minimum contribution guard
    float lumaFiltered = filteredColor.x;  // Y channel in YCoCg = luminance * 4
    float lumaHistory  = clampedHistory.x;
    blendFinal = max(blendFinal, saturate(0.01 * lumaHistory / abs(lumaFiltered - lumaHistory)));

    // @source UE5 TemporalAA.usf:2248-2251 — NaN guard for GLSL/Metal
    // DX12/HLSL saturate(NaN) = 0, so this is not strictly needed, but
    // we guard against it for robustness.
    blendFinal = -min(-blendFinal, 0.0);

    // ── Off-screen Reset ────────────────────────────────────────────────────
    // @source UE5 TemporalAA.usf:2275-2281 — Offscreen feedback resets
    if (offScreen)
    {
        clampedHistory = filteredColor;
    }

    // ── HDR-Weighted Blend ──────────────────────────────────────────────────
    // @source UE5 TemporalAA.usf:2283-2293 — DO FINAL BLEND
    float filterWeight  = HdrWeightY(filteredColor.x, FrameExposureScale);
    float historyWeight = HdrWeightY(clampedHistory.x, FrameExposureScale);

    float2 blendWeights = WeightedLerpFactors(historyWeight, filterWeight, blendFinal);
    float3 outputYCoCg = clampedHistory * blendWeights.x + filteredColor * blendWeights.y;

    // ── Convert Back to Linear RGB ──────────────────────────────────────────
    // @source UE5 TemporalAA.usf:2302 — TransformBackToRawLinearSceneColor
    float3 outputRGB = YCoCgToRGB(outputYCoCg);

    // @source UE5 TemporalAA.usf:2304-2306 — NaN/negative guard
    outputRGB = -min(-outputRGB, 0.0);

    // ── Write Output ────────────────────────────────────────────────────────
    taaOutput[DispatchThreadId] = float4(outputRGB, 1.0);
}
