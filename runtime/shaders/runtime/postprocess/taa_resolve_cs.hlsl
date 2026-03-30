/// UzEngine Temporal Anti-Aliasing Resolve Compute Shader
///
/// @ref Karis 2014 "High Quality Temporal Supersampling" (SIGGRAPH)
/// @ref Catmull & Rom 1974 — bicubic history reprojection (5-tap plus pattern)
/// @ref Yang et al. 2020 "A Survey of Temporal Antialiasing Techniques"
/// @constants CurrentFrameWeight=0.04 (blend weight for current frame, standard TAA default), velocityLerp=0.2 at v/40 (velocity-based blend lerp factor)
/// @deviations Simplified: no LDS cache, no TAAU upsampling, no DOF CoC, no stencil responsive AA,
///             no stochastic quantization. UzEngine uses compute with VGPR 3x3 cache.
///             Core algorithm (YCoCg clamp + bicubic history + HDR weighted blend + velocity rejection)
///             is identical to reference.

/// TAA constant buffer at b2 — uploaded via CBRingAllocator (kSlotPerObject).
///
/// Contains all TAA parameters including bindless SRV indices and texture dimensions.
/// Moved from root constants to CBV to accommodate velocitySrvIndex beyond the
/// 8-DWORD root constant limit.
cbuffer TaaCB : register(b2) {
    float2 jitterOffset;        ///< Sub-pixel jitter applied to current frame's projection (pixels).
    float2 texelSize;           ///< 1.0 / float2(width, height).
    float  currentFrameWeight;  ///< Blend weight for current frame. Default: 0.04.
    uint   sceneSrvIndex;       ///< Bindless SRV index for HDR scene color (RGBA16F).
    uint   depthSrvIndex;       ///< Bindless SRV index for depth buffer (R32F or D32F).
    uint   historySrvIndex;     ///< Bindless SRV index for previous frame TAA output (RGBA16F).
    uint   velocitySrvIndex;    ///< Bindless SRV index for velocity buffer (RG16F, pixels).
    float  outputWidth;         ///< Render target width in pixels (for bicubic history sampling).
    float  outputHeight;        ///< Render target height in pixels (for bicubic history sampling).
    uint   _pad0;               ///< Padding to 16-byte alignment.
};

/// Bilinear clamp sampler for history texture sampling.
SamplerState LinearClampSampler : register(s0);

/// TAA output UAV — bound via descriptor table at root param 4 (u0).
RWTexture2D<float4> taaOutput : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// SM5 register-bound fallback (when UZ_BINDLESS is not defined)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef UZ_BINDLESS
Texture2D<float4> g_SceneColor  : register(t0);
Texture2D<float>  g_DepthTex    : register(t1);
Texture2D<float4> g_HistoryTex  : register(t2);
Texture2D<float2> g_VelocityTex : register(t3);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Color Space Transforms
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Karis 2014 "TAA" —597-605 — RGBToYCoCg
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

/// @ref Karis 2014 "TAA" —607-619 — YCoCgToRGB
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

/// @ref Karis 2014 "TAA" —623-626 — Luma4
/// Fast luminance approximation including a 4x scale factor.
float Luma4(float3 Color)
{
    return (Color.g * 2.0) + (Color.r + Color.b);
}

/// @ref Karis 2014 "TAA" —629-632 — HdrWeight4
/// Tonemap-inspired weighting that reduces firefly contribution.
/// The 4.0 addend stabilizes the reciprocal for near-black pixels.
float HdrWeight4(float3 Color, float Exposure)
{
    return rcp(Luma4(Color) * Exposure + 4.0);
}

/// @ref Karis 2014 "TAA" —634-637 — HdrWeightY
/// HDR weight for YCoCg-space data where Y is already the luminance channel.
float HdrWeightY(float Y, float Exposure)
{
    return rcp(Y * Exposure + 4.0);
}

/// @ref Karis 2014 "TAA" —684-692 — WeightedLerpFactors
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

/// @ref Karis 2014 "TAA" —2087-2121 — Camera motion via ClipToPrevClip + velocity buffer
/// @constants NONE (no numeric constants in this function)
/// @deviations the reference uses ClipToPrevClip for camera-only motion and overrides with velocity
///             buffer for dynamic objects. UzEngine velocity buffer already encodes full
///             per-pixel motion (camera + object) via depth reprojection in velocity_ps.hlsl,
///             so we read it directly without a separate camera-only path.
///
/// Reads the velocity buffer to obtain per-pixel screen-space motion in UV space.
///
/// The velocity buffer (velocity_ps.hlsl) outputs motion vectors in pixel-space as
/// `currScreen - prevScreen`. To reproject history, TAA needs to go FROM current TO
/// previous, so the history UV = currentUV - velocity * texelSize.
///
/// # Arguments
/// * `pixelCoord` — Integer pixel coordinate of the sample (after closest-depth dilation).
/// * `uv` — UV coordinate of the sample (may include velocityOffset from depth dilation).
///
/// # Returns
/// UV-space motion vector: positive = current pixel moved rightward/downward from previous frame.
float2 ComputeMotionVector(int2 pixelCoord, float2 uv)
{
    // Read velocity buffer at the dilated pixel coordinate.
    // Velocity buffer is RG16F: velocity in pixels (currScreen - prevScreen).
#ifdef UZ_BINDLESS
    Texture2D<float2> VelocityTex = ResourceDescriptorHeap[velocitySrvIndex];
#else
    Texture2D<float2> VelocityTex = g_VelocityTex;
#endif
    float2 velocityPixels = VelocityTex.Load(int3(pixelCoord, 0));

    // Convert pixel-space velocity to UV-space by dividing by screen dimensions.
    // texelSize = 1.0 / float2(width, height), so multiply is equivalent to divide.
    float2 motionUV = velocityPixels * texelSize;

    return motionUV;
}

// ─────────────────────────────────────────────────────────────────────────────
// 3x3 Neighborhood Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Karis 2014 "TAA" —447-458 — kOffsets3x3
static const int2 kOffsets3x3[9] =
{
    int2(-1, -1), int2( 0, -1), int2( 1, -1),
    int2(-1,  0), int2( 0,  0), int2( 1,  0),
    int2(-1,  1), int2( 0,  1), int2( 1,  1),
};

// ─────────────────────────────────────────────────────────────────────────────
// TAA Resolve Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Karis 2014 "TAA" —2327-2407 — MainCS
/// @ref Karis 2014 "TAA" —1967-2321 — TemporalAASample
///
/// UzEngine TAA resolve compute shader. Implements the core TAA algorithm:
///
/// 1. Sample current frame HDR scene color at the jittered pixel position.
/// 2. Gather 3x3 neighborhood in YCoCg space for color bounding box.
/// 3. Find closest depth in a cross pattern for velocity dilation.
/// 4. Compute reprojected UV from velocity buffer (full camera+object motion).
/// 5. Sample history buffer at reprojected UV (5-tap bicubic Catmull-Rom).
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
    // Get output dimensions from TaaCB (avoids rcp precision issues).
    uint outW = uint(outputWidth  + 0.5);
    uint outH = uint(outputHeight + 0.5);

    // Early out for threads outside the render target.
    if (any(DispatchThreadId >= uint2(outW, outH)))
        return;

    // ── UV Computation ──────────────────────────────────────────────────────
    // Center of the output pixel in UV space (display resolution).
    // texelSize = 1/renderResolution (scene color input texel size).
    // For UV computation in output space, use 1/outputResolution.
    float2 outputTexelSize = float2(1.0 / outputWidth, 1.0 / outputHeight);
    float2 uv = (float2(DispatchThreadId) + 0.5) * outputTexelSize;

    // ── Bindless Texture Access ─────────────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4> SceneColor = ResourceDescriptorHeap[sceneSrvIndex];
    Texture2D<float>  DepthTex   = ResourceDescriptorHeap[depthSrvIndex];
    Texture2D<float4> HistoryTex = ResourceDescriptorHeap[historySrvIndex];
#else
    Texture2D<float4> SceneColor = g_SceneColor;
    Texture2D<float>  DepthTex   = g_DepthTex;
    Texture2D<float4> HistoryTex = g_HistoryTex;
#endif

    // ── Scene Color Dimensions ──────────────────────────────────────────────
    // Scene color and depth may be at render resolution (smaller than output
    // when screen percentage < 100%). Use texture dimensions for coordinate
    // conversion. When render res == display res, these equal outW/outH.
    uint sceneW, sceneH;
    SceneColor.GetDimensions(sceneW, sceneH);

    // ── Sample Current Frame + 3x3 Neighborhood ────────────────────────────
    // @ref Karis 2014 "TAA" —928-969 — VGPR_3X3 cache path
    //
    // Cache the 3x3 neighborhood around the current pixel for:
    // 1. Filtered center sample (plus-weighted)
    // 2. Min/max bounding box for history clamping
    //
    // When screen percentage < 100%, the scene color is at lower resolution.
    // Convert output UV to scene color pixel coordinates for Load().
    // The UV space [0,1] maps identically across both resolutions.
    float3 neighbors[9];
    float neighborWeights[9];
    // Map output pixel to scene color pixel space for neighborhood sampling.
    int2 centerCoord = int2(uv * float2(sceneW, sceneH));
    centerCoord = clamp(centerCoord, int2(0, 0), int2(sceneW - 1, sceneH - 1));

    // Exposure = 1.0 for now (auto-exposure is pre-applied to HDR).
    // @ref Karis 2014 "TAA" —1974 — FrameExposureScale * View.OneOverPreExposure
    const float FrameExposureScale = 1.0;

    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        int2 coord = centerCoord + kOffsets3x3[i];
        // Clamp to valid scene color range.
        coord = clamp(coord, int2(0, 0), int2(sceneW - 1, sceneH - 1));

        float3 rawColor = SceneColor.Load(int3(coord, 0)).rgb;
        // Transform to YCoCg for clamping (reference AA_YCOCG=1 for Main pass).
        // @ref Karis 2014 "TAA" —70-76 — TAA_PASS_CONFIG==0, AA_YCOCG=1
        neighbors[i] = RGBToYCoCg(rawColor);
        neighborWeights[i] = HdrWeightY(neighbors[i].x, FrameExposureScale);
    }

    // ── Plus-Filtered Current Frame ─────────────────────────────────────────
    // @ref Karis 2014 "TAA" —1532-1692 — FilterCurrentFrameInputSamples (AA_FILTERED=1)
    // @ref Karis 2014 "TAA" —1560-1562 — AA_SAMPLES==5, kPlusIndexes3x3
    //
    // Weighted filter using the plus-shaped neighbors (indices 1,3,4,5,7).
    // Weight = Gaussian(pixel_offset - jitter, sigma=0.47) * HDR_weight.
    // @ref TAA filtering —449 — Sigma=0.47, exp(-2.29 * d^2)
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
        // @ref TAA filtering —449 — exp(-2.29 * (dx^2 + dy^2)), sigma=0.47
        float spatialWeight = exp(-2.29 * dot(pixelOffset, pixelOffset));
        // @ref Karis 2014 "TAA" —1623-1627 — AA_TONE=1, sample HDR weight
        float hdrWeight = neighborWeights[kPlusIndices[j]];
        float sampleWeight = spatialWeight * hdrWeight;

        filteredColor += neighbors[kPlusIndices[j]] * sampleWeight;
        totalWeight   += sampleWeight;
    }
    // @ref Karis 2014 "TAA" —1674-1676 — Reweight for tone + filtering
    filteredColor *= rcp(totalWeight);

    // ── Neighborhood Min/Max (for history clamping) ─────────────────────────
    // @ref Karis 2014 "TAA" —1782-1823 — HISTORY_CLAMPING_BOX_MIN_MAX
    // Plus-shaped min/max first (indices 1,3,4,5,7).
    float3 neighborMin = min(min(neighbors[1], neighbors[3]), neighbors[4]);
    neighborMin = min(min(neighborMin, neighbors[5]), neighbors[7]);

    float3 neighborMax = max(max(neighbors[1], neighbors[3]), neighbors[4]);
    neighborMax = max(max(neighborMax, neighbors[5]), neighbors[7]);

    // @ref Karis 2014 "TAA" —1805-1821 — AA_SAMPLES==9 case
    // Expand to full 3x3 with averaging (AA_ROUND=0 → no averaging).
    float3 neighborMinPlus = neighborMin;
    float3 neighborMaxPlus = neighborMax;

    neighborMin = min(min(neighborMin, neighbors[0]), neighbors[2]);
    neighborMin = min(min(neighborMin, neighbors[6]), neighbors[8]);

    neighborMax = max(max(neighborMax, neighbors[0]), neighbors[2]);
    neighborMax = max(max(neighborMax, neighbors[6]), neighbors[8]);

    // ── Closest Depth Search (Cross Pattern) ────────────────────────────────
    // @ref Karis 2014 "TAA" —2034-2079 — AA_CROSS=2 depth dilation
    // Find the nearest depth in a 5-sample cross pattern (center + 4 diagonal).
    // This provides better silhouette handling by using the foreground pixel's
    // motion vector for reprojection.
    // Depth is at render resolution (same as scene color).
    float centerDepth = DepthTex.Load(int3(centerCoord, 0)).x;
    float2 velocityOffset = float2(0, 0);

    {
        // Cross pattern offsets in render-resolution pixels.
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
            // velocityOffset is in UV space (render-resolution texels).
            velocityOffset = depthOffset * texelSize;
            centerDepth = depthsXYZW;
        }
    }

    // ── Reprojection ────────────────────────────────────────────────────────
    // @ref Karis 2014 "TAA" —2087-2121 — Camera motion + velocity buffer
    //
    // Read the velocity buffer at the closest-depth dilated pixel (velocityOffset
    // shifts to the foreground pixel found by the cross-pattern search above).
    // The velocity buffer already encodes full camera + object motion via depth
    // reprojection (velocity_ps.hlsl). ComputeMotionVector returns UV-space delta.
    // Velocity buffer is at render resolution, so dilated coords use scene dims.
    int2 dilatedCoord = centerCoord + int2(velocityOffset * rcp(texelSize) + 0.5);
    dilatedCoord = clamp(dilatedCoord, int2(0, 0), int2(sceneW - 1, sceneH - 1));
    float2 motionVector = ComputeMotionVector(dilatedCoord, uv + velocityOffset);
    float2 historyUV = uv - motionVector;

    // @ref Karis 2014 "TAA" —2108 — Velocity magnitude in pixels
    // Velocity is in UV space; convert to display-resolution pixels for magnitude.
    float2 velocityPixels = motionVector * float2(outputWidth, outputHeight);
    float velocityMag = length(velocityPixels);

    // Detect off-screen reprojection.
    // @ref Karis 2014 "TAA" —2120 — OffScreen detection
    bool offScreen = any(historyUV < 0.0) || any(historyUV > 1.0);

    // ── Sample History (Bicubic Catmull-Rom — 5-tap) ────────────────────────
    // @ref Karis 2014 "TAA" —1840-1880 — AA_BICUBIC path
    // @ref Catmull-Rom bicubic —101-126 — Bicubic2DCatmullRom
    // @ref Catmull-Rom bicubic —148-186 — GetBicubic2DCatmullRomSamples
    //
    // Uses 5 bilinear taps arranged in a plus pattern to approximate a 4x4
    // Catmull-Rom kernel. The corner samples are omitted (their weights are
    // small) and the remaining weights are renormalized via FinalMultiplier.
    // This produces sharper temporal accumulation than single bilinear.
    // History is at display resolution, so use outputTexelSize for clamping.
    float2 clampedHistoryUV = clamp(historyUV, outputTexelSize * 0.5, 1.0 - outputTexelSize * 0.5);
    float3 rawHistory;
    {
        // History texture dimensions for Catmull-Rom weight computation.
        // History is at display resolution (outputWidth x outputHeight).
        float2 historySize    = float2(outputWidth, outputHeight);
        float2 historyInvSize = outputTexelSize;

        // @ref Catmull-Rom bicubic —103-126 — Bicubic2DCatmullRom
        // Compute Catmull-Rom basis weights and bilinear sample positions.
        float2 samplePos = clampedHistoryUV * historySize;
        float2 tc = floor(samplePos - 0.5) + 0.5;
        float2 f  = samplePos - tc;
        float2 f2 = f * f;
        float2 f3 = f2 * f;

        // Catmull-Rom basis weights (Catmull & Rom 1974, standard cubic spline).
        float2 w0 = f2 - 0.5 * (f3 + f);
        float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
        float2 w3 = 0.5 * (f3 - f2);
        float2 w2 = 1.0 - w0 - w1 - w3;

        // Combined weights for bilinear trick.
        // @ref Karis 2014 "High Quality Temporal Supersampling" — 5-tap Catmull-Rom bilinear optimization
        float2 w12 = w1 + w2;

        // Bilinear sample positions in texel space.
        // @ref Karis 2014 "High Quality Temporal Supersampling" — optimized Catmull-Rom sample positions
        float2 sample0 = (tc - 1.0) * historyInvSize;
        float2 sample1 = (tc + w2 * rcp(w12)) * historyInvSize;
        float2 sample2 = (tc + 2.0) * historyInvSize;

        // @ref Catmull-Rom bicubic —158-168 — 5-tap plus pattern UVs and weights.
        // Optimized layout omits the 4 corner taps for 5 taps total.
        float weight0 = w12.x * w0.y;    // top center
        float weight1 = w0.x  * w12.y;   // left center
        float weight2 = w12.x * w12.y;   // center (dominant)
        float weight3 = w3.x  * w12.y;   // right center
        float weight4 = w12.x * w3.y;    // bottom center

        rawHistory  = HistoryTex.SampleLevel(LinearClampSampler, float2(sample1.x, sample0.y), 0).rgb * weight0;
        rawHistory += HistoryTex.SampleLevel(LinearClampSampler, float2(sample0.x, sample1.y), 0).rgb * weight1;
        rawHistory += HistoryTex.SampleLevel(LinearClampSampler, float2(sample1.x, sample1.y), 0).rgb * weight2;
        rawHistory += HistoryTex.SampleLevel(LinearClampSampler, float2(sample2.x, sample1.y), 0).rgb * weight3;
        rawHistory += HistoryTex.SampleLevel(LinearClampSampler, float2(sample1.x, sample2.y), 0).rgb * weight4;

        // @ref Catmull-Rom bicubic —177-183 — FinalMultiplier renormalization.
        // Corner taps were omitted so reweight to preserve energy.
        float finalMultiplier = rcp(weight0 + weight1 + weight2 + weight3 + weight4);
        rawHistory *= finalMultiplier;
    }

    // Transform history to YCoCg.
    // @ref Karis 2014 "TAA" —1926 — TransformSceneColor(HistoryPayload.Color)
    float3 historyYCoCg = RGBToYCoCg(rawHistory);

    // ── Variance Clip History to Neighborhood ──────────────────────────────
    // @ref Salvi 2016 "An Excursion in Temporal Supersampling" — variance clipping
    // Replace AABB min/max clamp with μ ± γ·σ clip. Tighter than min/max AABB
    // at oblique angles, significantly reduces ghosting on disocclusion edges.
    // @constants γ (gamma) = 1.25 — standard variance clip multiplier
    float3 neighborMu = float3(0, 0, 0);
    float3 neighborSigma = float3(0, 0, 0);
    [unroll] for (int ni = 0; ni < 9; ++ni) {
        neighborMu    += neighbors[ni];
        neighborSigma += neighbors[ni] * neighbors[ni];
    }
    neighborMu    /= 9.0;
    neighborSigma = sqrt(abs(neighborSigma / 9.0 - neighborMu * neighborMu));

    static const float kVarianceClipGamma = 1.25;
    float3 varianceMin = neighborMu - kVarianceClipGamma * neighborSigma;
    float3 varianceMax = neighborMu + kVarianceClipGamma * neighborSigma;

    // Intersect variance box with AABB for robustness (prevents extreme outliers)
    float3 clipMin = max(neighborMin, varianceMin);
    float3 clipMax = min(neighborMax, varianceMax);

    float3 clampedHistory = clamp(historyYCoCg, clipMin, clipMax);

    // ── Compute Blend Weight ────────────────────────────────────────────────
    // @ref Karis 2014 "TAA" —2225-2272 — COMPUTE BLEND AMOUNT

    float blendFinal = currentFrameWeight;

    // @ref Karis 2014 "TAA" —2233 — lerp(blendFinal, 0.2, saturate(Velocity / 40))
    // Increase current frame contribution when camera/objects move fast.
    blendFinal = lerp(blendFinal, 0.2, saturate(velocityMag / 40.0));

    // @ref Karis 2014 "TAA" —2246 — Anti-flicker: minimum contribution guard
    float lumaFiltered = filteredColor.x;  // Y channel in YCoCg = luminance * 4
    float lumaHistory  = clampedHistory.x;
    blendFinal = max(blendFinal, saturate(0.01 * lumaHistory / abs(lumaFiltered - lumaHistory)));

    // @ref Karis 2014 "TAA" —2248-2251 — NaN guard for GLSL/Metal
    // DX12/HLSL saturate(NaN) = 0, so this is not strictly needed, but
    // we guard against it for robustness.
    blendFinal = -min(-blendFinal, 0.0);

    // ── Off-screen Reset ────────────────────────────────────────────────────
    // @ref Karis 2014 "TAA" —2275-2281 — Offscreen feedback resets
    if (offScreen)
    {
        clampedHistory = filteredColor;
    }

    // ── HDR-Weighted Blend ──────────────────────────────────────────────────
    // @ref Karis 2014 "TAA" —2283-2293 — DO FINAL BLEND
    float filterWeight  = HdrWeightY(filteredColor.x, FrameExposureScale);
    float historyWeight = HdrWeightY(clampedHistory.x, FrameExposureScale);

    float2 blendWeights = WeightedLerpFactors(historyWeight, filterWeight, blendFinal);
    float3 outputYCoCg = clampedHistory * blendWeights.x + filteredColor * blendWeights.y;

    // ── Convert Back to Linear RGB ──────────────────────────────────────────
    // @ref Karis 2014 "TAA" —2302 — TransformBackToRawLinearSceneColor
    float3 outputRGB = YCoCgToRGB(outputYCoCg);

    // @ref Karis 2014 "TAA" —2304-2306 — NaN/negative guard
    outputRGB = -min(-outputRGB, 0.0);

    // ── Write Output ────────────────────────────────────────────────────────
    taaOutput[DispatchThreadId] = float4(outputRGB, 1.0);
}
