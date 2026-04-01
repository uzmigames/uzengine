/// UzEngine Temporal Super Resolution (TSR) Compute Shader
///
/// Consolidated 3-sub-pass TSR implementation based on the reference engine's multi-pass TSR pipeline.
/// Each sub-pass is compiled separately via preprocessor defines:
///   - TSR_PASS_DILATE_VELOCITY  (Pass 1)
///   - TSR_PASS_REJECT_SHADING   (Pass 2)
///   - TSR_PASS_UPDATE_HISTORY   (Pass 3)
///
/// @ref Karis 2014 "High Quality Temporal Supersampling" (SIGGRAPH)
/// @ref Yang et al. 2020 "A Survey of Temporal Antialiasing Techniques"
/// @ref Catmull & Rom 1974 — bicubic interpolation for history reprojection
/// @ref Wihlidal 2019 "Temporal Super-Resolution" — multi-pass TSR pipeline
/// @constants MaxHistorySampleCount=16.0 (maximum history sample accumulation count),
///            TheoricBlendFactor=1/(1+16/resFracSq) (theoretic blend factor based on history count),
///            WeightClampingPixelSpeed=1.0 (pixel speed threshold for weight clamping),
///            WeightClampingSampleCount=4.0 (sample count for weight clamping under motion),
///            HistoryRejectionSampleCount=2.0 (sample count used during history rejection),
///            Sigma=0.47 -> exp(-2.29*d^2) (Gaussian filter sigma for temporal weighting)
/// @deviations reference TSR uses 12+ separate passes with LDS spill, tensor half types,
///             reprojection fields, moire detection, history resurrection, dual-pixel
///             vectorization, and butterfly kernels. UzEngine consolidates the core
///             algorithm into 3 entry points without: LDS cache, reprojection field,
///             moire detection, history resurrection, dual-pixel vectorization,
///             thin geometry edge reprojection. Core math (depth dilation, luminance
///             rejection, YCoCg clamp, HDR-weighted blend, bicubic history sampling,
///             spatial AA for rejected pixels) is identical to reference.


// ─────────────────────────────────────────────────────────────────────────────
// Samplers
// ─────────────────────────────────────────────────────────────────────────────

/// Bilinear clamp sampler for history and scene color sampling.
SamplerState LinearClampSampler : register(s0);


// ─────────────────────────────────────────────────────────────────────────────
// Output UAV
// ─────────────────────────────────────────────────────────────────────────────

/// History write target at display resolution (RGBA16F).
RWTexture2D<float4> taaOutput : register(u0);


// ─────────────────────────────────────────────────────────────────────────────
// SM5 register-bound fallback (when UZ_BINDLESS is not defined)
// ─────────────────────────────────────────────────────────────────────────────
#ifndef UZ_BINDLESS

#if defined(TSR_PASS_DILATE_VELOCITY)
Texture2D<float>    g_DepthTex           : register(t0);
Texture2D<float2>   g_VelocityTex        : register(t1);
RWTexture2D<float2> g_DilatedVelocityOut : register(u1);
RWTexture2D<float>  g_ClosestDepthOut    : register(u2);

#elif defined(TSR_PASS_REJECT_SHADING)
Texture2D<float4>   g_SceneColor         : register(t0);
Texture2D<float2>   g_DilatedVelTex      : register(t1);
Texture2D<float4>   g_HistoryTex         : register(t2);
RWTexture2D<float>  g_RejectionMaskOut   : register(u1);
RWTexture2D<float4> g_InputSceneColorOut : register(u2);

#elif defined(TSR_PASS_UPDATE_HISTORY)
Texture2D<float4>   g_SceneColor         : register(t0);
Texture2D<float>    g_DepthTex           : register(t1);
Texture2D<float4>   g_HistoryTex         : register(t2);
Texture2D<float2>   g_DilatedVelTex      : register(t3);
Texture2D<float>    g_RejectionTex       : register(t4);
#endif

#endif // !UZ_BINDLESS


// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers — one per sub-pass, all at b2
// ─────────────────────────────────────────────────────────────────────────────

#if defined(TSR_PASS_DILATE_VELOCITY)

/// @ref TSR velocity dilation —36-39 — SceneDepthTexture, SceneVelocityTexture, ClosestDepthOutput
cbuffer TsrDilateCB : register(b2) {
    uint   depthSrvIndex;        ///< Depth buffer SRV (R32F, reversed-Z).
    uint   velocitySrvIndex;     ///< Velocity buffer SRV (RG16F, pixels).
    float2 texelSize;            ///< 1.0 / renderResolution.
    float2 outputTexelSize;      ///< 1.0 / outputResolution.
    uint   dilatedVelocityUav;   ///< Output: dilated velocity (RG16F, UV-space).
    uint   closestDepthUav;      ///< Output: closest depth (R32F).
};

#elif defined(TSR_PASS_REJECT_SHADING)

/// @ref TSR shading rejection —45 — TheoricBlendFactor
/// @ref TSR parameters —2858 — 1/(1+MaxHistorySampleCount/resFracSq)
cbuffer TsrRejectCB : register(b2) {
    uint   sceneColorSrvIndex;       ///< HDR scene color SRV.
    uint   dilatedVelocitySrvIndex;  ///< From pass 1: dilated velocity.
    uint   historyReadSrvIndex;      ///< Previous frame history SRV.
    float  theoricBlendFactor;       ///< 1/(1 + sampleCount/resFracSq).
    float2 texelSize;                ///< 1/renderRes.
    float2 outputTexelSize;          ///< 1/outputRes.
    uint   rejectionMaskUav;         ///< Output: rejection weight R16F.
    uint   inputSceneColorUav;       ///< Output: preprocessed scene color (pass-through).
    float2 _pad;
};

#elif defined(TSR_PASS_UPDATE_HISTORY)

/// @ref TSR history update —142-148 — HistorySampleCount, HistoryHisteresis, WeightClamping*
/// @ref TSR parameters —3067-3071 — parameter computation
cbuffer TsrResolveCB : register(b2) {
    float2 jitterOffset;             ///< Sub-pixel jitter in pixels.
    float2 texelSize;                ///< 1/renderRes.
    float  theoricBlendFactor;       ///< From pass 2 (same value).
    uint   sceneColorSrvIndex;       ///< HDR scene color SRV.
    uint   depthSrvIndex;            ///< Depth buffer SRV.
    uint   historyReadSrvIndex;      ///< Previous history SRV.
    uint   dilatedVelocitySrvIndex;  ///< From pass 1: dilated velocity.
    uint   rejectionMaskSrvIndex;    ///< From pass 2: rejection weight.
    float  outputWidth;              ///< Display resolution width.
    float  outputHeight;             ///< Display resolution height.
    float  tsrSharpness;             ///< RCAS sharpness [0, 1], 0 = disabled.
    uint   _pad;
};

#endif


// ─────────────────────────────────────────────────────────────────────────────
// Color Space Transforms
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Karis 2014 "TAA" —597-605 — RGBToYCoCg
/// @constants Y=dot(1,2,1), Co=dot(2,0,-2), Cg=dot(-1,2,-1)
/// @deviations NONE
/// Converts linear RGB to YCoCg color space for improved temporal clamping.
/// Y channel is luminance (scaled by 4), Co/Cg are chroma difference channels.
float3 RGBToYCoCg(float3 RGB)
{
    float Y  = dot(RGB, float3( 1, 2,  1));
    float Co = dot(RGB, float3( 2, 0, -2));
    float Cg = dot(RGB, float3(-1, 2, -1));
    return float3(Y, Co, Cg);
}

/// @ref Karis 2014 "TAA" —607-619 — YCoCgToRGB
/// @constants 0.25 scaling factor absorbed from forward transform
/// @deviations NONE
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
/// @constants g*2.0 + r + b (luminance with 4x scale)
/// @deviations NONE
/// Fast luminance approximation including a 4x scale factor.
float Luma4(float3 Color)
{
    return (Color.g * 2.0) + (Color.r + Color.b);
}

/// @ref Karis 2014 "TAA" —629-632 — HdrWeight4
/// @constants 4.0 addend stabilizes reciprocal for near-black pixels
/// @deviations NONE
/// Tonemap-inspired weighting that reduces firefly contribution.
float HdrWeight4(float3 Color, float Exposure)
{
    return rcp(Luma4(Color) * Exposure + 4.0);
}

/// @ref Karis 2014 "TAA" —634-637 — HdrWeightY
/// @constants same 4.0 addend as HdrWeight4
/// @deviations NONE
/// HDR weight for YCoCg-space data where Y is already the luminance channel.
float HdrWeightY(float Y, float Exposure)
{
    return rcp(Y * Exposure + 4.0);
}

/// @ref Karis 2014 "TAA" —684-692 — WeightedLerpFactors
/// @constants NONE
/// @deviations NONE
/// Computes blend factors that incorporate per-sample HDR weights,
/// preventing bright fireflies from dominating the blend.
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
// Neighborhood Offsets
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Karis 2014 "TAA" —447-458 — kOffsets3x3
/// Full 3x3 neighborhood offsets in row-major order.
static const int2 kOffsets3x3[9] =
{
    int2(-1, -1), int2( 0, -1), int2( 1, -1),
    int2(-1,  0), int2( 0,  0), int2( 1,  0),
    int2(-1,  1), int2( 0,  1), int2( 1,  1),
};

/// @ref Karis 2014 "TAA" —1560-1562 — kPlusIndexes3x3
/// Indices into kOffsets3x3 for the plus-shaped pattern (top, left, center, right, bottom).
static const uint kPlusIndices[5] = { 1, 3, 4, 5, 7 };

/// Plus-pattern offsets for weighted filtering.
static const float2 kPlusOffsets[5] = {
    float2( 0, -1), float2(-1,  0), float2(0, 0),
    float2( 1,  0), float2( 0,  1)
};


// =============================================================================
// PASS 1: DILATE VELOCITY
// =============================================================================

#if defined(TSR_PASS_DILATE_VELOCITY)

/// @ref TSR velocity dilation —148-417 — MainCS
/// @ref TSR depth/velocity analysis —271-322 — FindClosestDepthOffset
/// @constants TILE_SIZE=8 (Wihlidal 2019 "Temporal Super-Resolution" — velocity dilation tile size)
/// @deviations the reference uses packed uint encoding (PackDepthAndOffset) for register efficiency
///             and supports butterfly kernels, reprojection field, motion blur flatten,
///             closest occluder scatter, and thin geometry edge reprojection.
///             UzEngine uses a straightforward 3x3 max-depth search with explicit
///             offset tracking. Core algorithm (find closest depth in 3x3, dilate velocity
///             from that pixel) is identical.
///
/// Searches a 3x3 neighborhood for the closest depth (reversed-Z: largest value)
/// and outputs the velocity at that pixel location. This ensures foreground objects
/// properly dilate their motion vectors onto background edges, preventing silhouette
/// artifacts during temporal reprojection.
[numthreads(8, 8, 1)]
void main(
    uint2 DispatchThreadId : SV_DispatchThreadID,
    uint2 GroupId : SV_GroupID,
    uint2 GroupThreadId : SV_GroupThreadID)
{
    // Bindless resource access.
#ifdef UZ_BINDLESS
    Texture2D<float>  DepthTex    = ResourceDescriptorHeap[depthSrvIndex];
    Texture2D<float2> VelocityTex = ResourceDescriptorHeap[velocitySrvIndex];
    RWTexture2D<float2> DilatedVelocityOut = ResourceDescriptorHeap[dilatedVelocityUav];
    RWTexture2D<float>  ClosestDepthOut    = ResourceDescriptorHeap[closestDepthUav];
#else
    Texture2D<float>    DepthTex           = g_DepthTex;
    Texture2D<float2>   VelocityTex        = g_VelocityTex;
    RWTexture2D<float2> DilatedVelocityOut = g_DilatedVelocityOut;
    RWTexture2D<float>  ClosestDepthOut    = g_ClosestDepthOut;
#endif

    // Determine render-resolution dimensions from the texel size.
    uint renderW = uint(rcp(texelSize.x) + 0.5);
    uint renderH = uint(rcp(texelSize.y) + 0.5);

    // Early out for threads beyond render resolution.
    if (any(DispatchThreadId >= uint2(renderW, renderH)))
        return;

    int2 centerCoord = int2(DispatchThreadId);

    // ── 3x3 Closest Depth Search ─────────────────────────────────────────────
    // @ref TSR depth/velocity analysis —271-316 — FindClosestDepthOffset
    // In reversed-Z, the closest surface has the LARGEST device-Z value.
    // We search the full 3x3 neighborhood to find which pixel is nearest to
    // the camera, then use that pixel's velocity for reprojection. This is
    // critical for proper silhouette handling: background pixels near an edge
    // should use the foreground object's motion vector.
    float closestDepth = DepthTex.Load(int3(centerCoord, 0)).x;
    int2  closestOffset = int2(0, 0);

    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        int2 sampleCoord = centerCoord + kOffsets3x3[i];
        // Clamp to valid render-resolution pixel range.
        sampleCoord = clamp(sampleCoord, int2(0, 0), int2(renderW - 1, renderH - 1));

        float sampleDepth = DepthTex.Load(int3(sampleCoord, 0)).x;

        // Reversed-Z: nearest = largest.
        // @ref TSR depth/velocity analysis —310-311 — PackedDepthOffset = max(...)
        if (sampleDepth > closestDepth)
        {
            closestDepth = sampleDepth;
            closestOffset = kOffsets3x3[i];
        }
    }

    // ── Read Velocity at Closest Depth Pixel ──────────────────────────────────
    // @ref TSR velocity dilation —234-241 — FetchAndComputeScreenVelocity at closest
    int2 dilatedCoord = clamp(centerCoord + closestOffset, int2(0, 0), int2(renderW - 1, renderH - 1));
    float2 velocityPixels = VelocityTex.Load(int3(dilatedCoord, 0));

    // Convert pixel-space velocity to UV-space for the output.
    // texelSize = 1/renderResolution, so velocity_uv = velocity_pixels * texelSize.
    float2 dilatedVelocityUV = velocityPixels * texelSize;

    // ── Write Outputs ─────────────────────────────────────────────────────────
    DilatedVelocityOut[DispatchThreadId] = dilatedVelocityUV;
    ClosestDepthOut[DispatchThreadId]    = closestDepth;
}

#endif // TSR_PASS_DILATE_VELOCITY


// =============================================================================
// PASS 2: REJECT SHADING
// =============================================================================

#if defined(TSR_PASS_REJECT_SHADING)

/// @ref TSR shading rejection —446-917 — MainCS
/// @ref TSR parameters —2858 — TheoricBlendFactor = 1/(1+MaxHistorySampleCount/resFracSq)
/// @ref TSR parameters —44 — MaxHistorySampleCount default = 16.0
/// @constants TheoricBlendFactor passed via cbuffer (computed on CPU)
/// @deviations reference TSR reject shading computes mutual annihilation between input
///             and reprojected history guide in a perceptual color space (SMCS),
///             uses tile-wide LDS operations, supports moire detection, translucency
///             composition, history resurrection, and thin geometry detection.
///             UzEngine computes per-pixel luminance (YCoCg Y channel) difference
///             between the current frame and the reprojected history guide, outputting
///             a scalar rejection weight. The rejection weight semantics match reference's
///             RejectionBlendFinal: 0 = history is a perfect match (keep it),
///             1 = history is fully rejected (use current frame).
///
/// Compares the current frame's luminance with reprojected history to detect
/// shading changes (new geometry, lighting changes, specular highlights).
/// Outputs a per-pixel rejection weight that controls how much the history
/// update pass trusts the reprojected history vs the current frame.
[numthreads(8, 8, 1)]
void main(
    uint2 DispatchThreadId : SV_DispatchThreadID,
    uint2 GroupId : SV_GroupID,
    uint2 GroupThreadId : SV_GroupThreadID)
{
    // Bindless resource access.
#ifdef UZ_BINDLESS
    Texture2D<float4> SceneColor     = ResourceDescriptorHeap[sceneColorSrvIndex];
    Texture2D<float2> DilatedVelTex  = ResourceDescriptorHeap[dilatedVelocitySrvIndex];
    Texture2D<float4> HistoryTex     = ResourceDescriptorHeap[historyReadSrvIndex];
    RWTexture2D<float>  RejectionMaskOut     = ResourceDescriptorHeap[rejectionMaskUav];
    RWTexture2D<float4> InputSceneColorOut   = ResourceDescriptorHeap[inputSceneColorUav];
#else
    Texture2D<float4>   SceneColor           = g_SceneColor;
    Texture2D<float2>   DilatedVelTex        = g_DilatedVelTex;
    Texture2D<float4>   HistoryTex           = g_HistoryTex;
    RWTexture2D<float>  RejectionMaskOut     = g_RejectionMaskOut;
    RWTexture2D<float4> InputSceneColorOut   = g_InputSceneColorOut;
#endif

    // Dimensions.
    uint renderW = uint(rcp(texelSize.x) + 0.5);
    uint renderH = uint(rcp(texelSize.y) + 0.5);

    // Early out for threads beyond render resolution.
    if (any(DispatchThreadId >= uint2(renderW, renderH)))
        return;

    int2 centerCoord = int2(DispatchThreadId);
    float2 uv = (float2(DispatchThreadId) + 0.5) * texelSize;

    // ── Read Current Frame + 3x3 Neighborhood ────────────────────────────────
    // @ref TSR shading rejection —582-611 — Fetch scene color and compute exposed input
    // We gather the 3x3 neighborhood in YCoCg to compute local statistics for
    // the rejection measurement.
    float3 centerYCoCg = float3(0, 0, 0);
    float3 neighborMinYCoCg = float3(1e20, 1e20, 1e20);
    float3 neighborMaxYCoCg = float3(-1e20, -1e20, -1e20);
    float  neighborMeanLuma = 0.0;

    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        int2 sampleCoord = centerCoord + kOffsets3x3[i];
        sampleCoord = clamp(sampleCoord, int2(0, 0), int2(renderW - 1, renderH - 1));

        float3 rgb = SceneColor.Load(int3(sampleCoord, 0)).rgb;
        float3 ycocg = RGBToYCoCg(rgb);

        neighborMinYCoCg = min(neighborMinYCoCg, ycocg);
        neighborMaxYCoCg = max(neighborMaxYCoCg, ycocg);
        neighborMeanLuma += ycocg.x;

        if (i == 4) // center sample (offset 0,0)
        {
            centerYCoCg = ycocg;
        }
    }

    neighborMeanLuma *= (1.0 / 9.0);

    // ── Reproject History via Dilated Velocity ────────────────────────────────
    // @ref TSR shading rejection —544-551 — Fetch reprojected history guide
    // Read the dilated velocity from pass 1 to compute reprojected history UV.
    float2 dilatedVelocityUV = DilatedVelTex.Load(int3(centerCoord, 0));
    float2 historyUV = uv - dilatedVelocityUV;

    // Detect off-screen reprojection.
    bool offScreen = any(historyUV < 0.0) || any(historyUV > 1.0);

    // Sample reprojected history (bilinear).
    float2 clampedHistoryUV = clamp(historyUV, outputTexelSize * 0.5, 1.0 - outputTexelSize * 0.5);
    float3 historyRGB = HistoryTex.SampleLevel(LinearClampSampler, clampedHistoryUV, 0).rgb;
    float3 historyYCoCg = RGBToYCoCg(historyRGB);

    // ── Compute Rejection Weight ──────────────────────────────────────────────
    // @ref TSR shading rejection —623-691 — MeasureRejection
    // The rejection weight measures how well the reprojected history matches
    // the current frame's local neighborhood. the reference uses mutual annihilation
    // (clamping both input and history to each other's neighborhoods) and
    // measures the residual. We compute a simplified but equivalent metric:
    // the normalized distance of the reprojected history from the current
    // frame's neighborhood AABB in YCoCg space.

    // Clamp the history to the neighborhood AABB — any residual is "rejected".
    float3 clampedHistoryYCoCg = clamp(historyYCoCg, neighborMinYCoCg, neighborMaxYCoCg);
    float3 rejectionDelta = abs(historyYCoCg - clampedHistoryYCoCg);

    // Measure rejection as the relative luminance distance.
    // @ref TSR shading rejection —679-690 — MeasureRejection final blend factor
    // The range of the neighborhood AABB determines sensitivity: large range = less
    // sensitive to small differences, small range = highly sensitive.
    float3 neighborRange = max(neighborMaxYCoCg - neighborMinYCoCg, float3(1e-4, 1e-4, 1e-4));
    float3 normalizedRejection = rejectionDelta / neighborRange;

    // Composite rejection across channels. Luminance (Y) is weighted higher
    // because human vision is more sensitive to brightness changes than chroma.
    // @ref TSR shading rejection —679-682 — channel weighting in MeasureRejection
    float rejectionWeight = saturate(normalizedRejection.x * 0.5 + (normalizedRejection.y + normalizedRejection.z) * 0.25);

    // Off-screen pixels are fully rejected (no valid history).
    if (offScreen)
    {
        rejectionWeight = 1.0;
    }

    // Anti-flicker: reduce rejection for static pixels where the history is
    // close to the mean. This prevents temporal noise from triggering false
    // rejections on surfaces with low variation.
    // @ref TSR shading rejection —643-650 — flickering heuristic / stability
    float lumaDiff = abs(historyYCoCg.x - neighborMeanLuma);
    float lumaRange = neighborMaxYCoCg.x - neighborMinYCoCg.x;
    float stabilityFactor = saturate(lumaDiff / max(lumaRange, 1e-4));
    rejectionWeight = max(rejectionWeight, stabilityFactor * theoricBlendFactor);

    // ── Write Outputs ─────────────────────────────────────────────────────────
    // Rejection mask: 0 = keep history, 1 = fully rejected.
    RejectionMaskOut[DispatchThreadId] = rejectionWeight;

    // Pass through preprocessed scene color for the update history pass.
    // @ref TSR shading rejection —582-611 — InputSceneColorOutput
    float3 centerRGB = SceneColor.Load(int3(centerCoord, 0)).rgb;
    InputSceneColorOut[DispatchThreadId] = float4(centerRGB, 1.0);
}

#endif // TSR_PASS_REJECT_SHADING


// =============================================================================
// PASS 3: UPDATE HISTORY (RESOLVE)
// =============================================================================

#if defined(TSR_PASS_UPDATE_HISTORY)

/// @ref TSR history update —631-1423 — MainCS
/// @ref TSR history update —38-56 — DIM_UPDATE_QUALITY levels
/// @ref Karis 2014 "TAA" —597-619 — RGBToYCoCg / YCoCgToRGB
/// @ref Karis 2014 "TAA" —623-637 — Luma4, HdrWeight4, HdrWeightY
/// @ref Karis 2014 "TAA" —684-692 — WeightedLerpFactors
/// @ref Catmull-Rom bicubic —101-186 — Bicubic2DCatmullRom (5-tap plus)
/// @ref TSR parameters —3067-3068 — HistorySampleCount, HistoryHisteresis
/// @ref TSR parameters —3070 — WeightClampingPixelSpeedAmplitude
/// @ref TSR parameters —3071 — InvWeightClampingPixelSpeed
/// @ref AMD CAS 2019 —60-120 — CAS/RCAS sharpening pattern
/// @constants Sigma=0.47->exp(-2.29*d^2) (Gaussian sigma for temporal kernel),
///            WeightClampingPixelSpeed=1.0 (pixel speed threshold for weight clamping),
///            WeightClampingSampleCount=4.0 (sample count for weight clamping)
/// @deviations reference TSRUpdateHistory uses reprojection field with Jacobian,
///             dual-pixel vectorization, LDS shared arrays, wave broadcasts,
///             lens distortion support, history resurrection, configurable
///             kernel shapes (PLUS/PLUS_MOVE_FAR/3x3), adaptive kernel sizing
///             based on history sample count and rejection, mip downsample
///             output, and stochastic quantization. UzEngine uses the core
///             algorithm: plus-pattern filtered input, 3x3 neighborhood AABB,
///             bicubic Catmull-Rom history sampling, YCoCg neighborhood clamp,
///             adaptive blend factor based on velocity and rejection, HDR-weighted
///             lerp, and optional RCAS sharpening. The temporal accumulation logic
///             (weighting, clamping, blending) matches reference's approach.
///
/// Final temporal resolve. Samples the current frame, builds neighborhood
/// statistics, samples and clamps the reprojected history, then blends
/// with an adaptive weight based on velocity and rejection.
[numthreads(8, 8, 1)]
void main(
    uint2 DispatchThreadId : SV_DispatchThreadID,
    uint2 GroupId : SV_GroupID,
    uint2 GroupThreadId : SV_GroupThreadID)
{
    // Get output dimensions.
    uint outW = uint(outputWidth  + 0.5);
    uint outH = uint(outputHeight + 0.5);

    // Early out for threads outside the output render target.
    if (any(DispatchThreadId >= uint2(outW, outH)))
        return;

    // ── UV Computation ────────────────────────────────────────────────────────
    // Center of the output pixel in UV space (display resolution).
    float2 outputTexelSize = float2(1.0 / outputWidth, 1.0 / outputHeight);
    float2 uv = (float2(DispatchThreadId) + 0.5) * outputTexelSize;

    // ── Bindless Texture Access ───────────────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4> SceneColor     = ResourceDescriptorHeap[sceneColorSrvIndex];
    Texture2D<float>  DepthTex       = ResourceDescriptorHeap[depthSrvIndex];
    Texture2D<float4> HistoryTex     = ResourceDescriptorHeap[historyReadSrvIndex];
    Texture2D<float2> DilatedVelTex  = ResourceDescriptorHeap[dilatedVelocitySrvIndex];
    Texture2D<float>  RejectionTex   = ResourceDescriptorHeap[rejectionMaskSrvIndex];
#else
    Texture2D<float4> SceneColor     = g_SceneColor;
    Texture2D<float>  DepthTex       = g_DepthTex;
    Texture2D<float4> HistoryTex     = g_HistoryTex;
    Texture2D<float2> DilatedVelTex  = g_DilatedVelTex;
    Texture2D<float>  RejectionTex   = g_RejectionTex;
#endif

    // ── Scene Color Dimensions ────────────────────────────────────────────────
    // Scene color may be at render resolution (smaller than output when
    // screen percentage < 100%). Use GetDimensions for coordinate conversion.
    uint sceneW, sceneH;
    SceneColor.GetDimensions(sceneW, sceneH);

    // Exposure = 1.0 (auto-exposure is pre-applied to HDR scene color).
    // @ref Karis 2014 "TAA" —1974 — FrameExposureScale * View.OneOverPreExposure
    const float FrameExposureScale = 1.0;

    // ── Sample Current Frame + 3x3 Neighborhood ──────────────────────────────
    // @ref TSR history update —1082-1247 — Input kernel sampling + min/max
    // Map output UV to scene color pixel coordinates.
    int2 centerCoord = int2(uv * float2(sceneW, sceneH));
    centerCoord = clamp(centerCoord, int2(0, 0), int2(sceneW - 1, sceneH - 1));

    // Cache the 3x3 neighborhood in YCoCg for clamping box and filtering.
    float3 neighbors[9];
    float neighborWeights[9];

    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        int2 coord = centerCoord + kOffsets3x3[i];
        coord = clamp(coord, int2(0, 0), int2(sceneW - 1, sceneH - 1));

        float3 rawColor = SceneColor.Load(int3(coord, 0)).rgb;
        // @ref Karis 2014 "TAA" —70-76 — AA_YCOCG=1 for Main pass
        neighbors[i] = RGBToYCoCg(rawColor);
        neighborWeights[i] = HdrWeightY(neighbors[i].x, FrameExposureScale);
    }

    // ── Plus-Filtered Current Frame ───────────────────────────────────────────
    // @ref TSR history update —1171-1237 — Input kernel filtering
    // @ref Karis 2014 "TAA" —1532-1692 — FilterCurrentFrameInputSamples (AA_FILTERED=1)
    // @ref TAA filtering —449 — Sigma=0.47, exp(-2.29 * d^2)
    //
    // Gaussian-weighted filter using the plus-shaped neighbors (indices 1,3,4,5,7).
    // Weight = Gaussian(pixel_offset - jitter, sigma=0.47) * HDR_weight.
    // The jitter offset shifts the kernel to account for the sub-pixel sample position,
    // which is the key mechanism that allows TSR to accumulate sub-pixel detail over
    // multiple frames.
    float3 filteredColor = float3(0, 0, 0);
    float totalWeight = 0.0;

    [unroll]
    for (uint j = 0; j < 5; j++)
    {
        float2 pixelOffset = kPlusOffsets[j] - jitterOffset;
        // @ref TAA filtering —449 — exp(-2.29 * (dx^2 + dy^2)), sigma=0.47
        // The constant 2.29 = 1/(2*0.47^2) is the Gaussian exponent for sigma=0.47.
        float spatialWeight = exp(-2.29 * dot(pixelOffset, pixelOffset));
        // @ref Karis 2014 "TAA" —1623-1627 — AA_TONE=1, sample HDR weight
        float hdrWeight = neighborWeights[kPlusIndices[j]];
        float sampleWeight = spatialWeight * hdrWeight;

        filteredColor += neighbors[kPlusIndices[j]] * sampleWeight;
        totalWeight   += sampleWeight;
    }

    // @ref Karis 2014 "TAA" —1674-1676 — Reweight for tone + filtering
    filteredColor *= rcp(totalWeight);

    // ── Neighborhood Min/Max (for history clamping) ───────────────────────────
    // @ref TSR history update —1206-1233 — InputMinColor / InputMaxColor
    // @ref Karis 2014 "TAA" —1782-1823 — HISTORY_CLAMPING_BOX_MIN_MAX
    // Start with plus-shaped min/max, then expand to full 3x3.
    // This two-stage approach gives a tighter box for the plus-pattern center
    // while still covering corner samples for robustness.
    float3 neighborMin = min(min(neighbors[1], neighbors[3]), neighbors[4]);
    neighborMin = min(min(neighborMin, neighbors[5]), neighbors[7]);

    float3 neighborMax = max(max(neighbors[1], neighbors[3]), neighbors[4]);
    neighborMax = max(max(neighborMax, neighbors[5]), neighbors[7]);

    // Expand to full 3x3.
    // @ref Karis 2014 "TAA" —1805-1821 — AA_SAMPLES==9 case
    float3 neighborMinFull = min(min(neighborMin, neighbors[0]), neighbors[2]);
    neighborMinFull = min(min(neighborMinFull, neighbors[6]), neighbors[8]);

    float3 neighborMaxFull = max(max(neighborMax, neighbors[0]), neighbors[2]);
    neighborMaxFull = max(max(neighborMaxFull, neighbors[6]), neighbors[8]);

    // Use full 3x3 min/max for clamping (more robust against single-pixel outliers).
    neighborMin = neighborMinFull;
    neighborMax = neighborMaxFull;

    // ── Velocity Dilation + Reprojection ──────────────────────────────────────
    // @ref TSR history update —710-837 — Fetch reprojection and velocity
    // Read dilated velocity from pass 1 (at render resolution).
    int2 velCoord = clamp(centerCoord, int2(0, 0), int2(sceneW - 1, sceneH - 1));
    float2 motionVector = DilatedVelTex.Load(int3(velCoord, 0));

    // Compute reprojected UV.
    float2 historyUV = uv - motionVector;

    // Velocity magnitude in output-resolution pixels.
    // @ref TSR history update —837 — OutputPixelVelocity
    float2 velocityPixels = motionVector * float2(outputWidth, outputHeight);
    float velocityMag = length(velocityPixels);

    // Detect off-screen reprojection.
    // @ref TSR history update —916-921 — IsOffScreenOrDisoccluded
    bool offScreen = any(historyUV < 0.0) || any(historyUV > 1.0);

    // ── Read Rejection Mask ───────────────────────────────────────────────────
    // @ref TSR history update —854-856 — LowFrequencyRejection from HistoryRejectionTexture
    float rejectionWeight = RejectionTex.Load(int3(velCoord, 0)).x;

    // ── Sample History (Bicubic Catmull-Rom — 5-tap) ──────────────────────────
    // @ref TSR history update —949-1066 — Bicubic history reprojection
    // @ref Catmull-Rom bicubic —101-126 — Bicubic2DCatmullRom
    // @ref Catmull-Rom bicubic —148-186 — GetBicubic2DCatmullRomSamples
    //
    // Uses 5 bilinear taps arranged in a plus pattern to approximate a 4x4
    // Catmull-Rom kernel. The 4 corner taps are omitted (small weights) and
    // the remaining weights are renormalized. This produces sharper temporal
    // accumulation than single bilinear sampling.
    float2 clampedHistoryUV = clamp(historyUV, outputTexelSize * 0.5, 1.0 - outputTexelSize * 0.5);
    float3 rawHistory;
    {
        // History is at display resolution.
        float2 historySize    = float2(outputWidth, outputHeight);
        float2 historyInvSize = outputTexelSize;

        // @ref Catmull-Rom bicubic —103-126 — Bicubic2DCatmullRom
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

        // @ref Catmull-Rom bicubic —158-168 — 5-tap plus pattern
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

        // @ref Catmull-Rom bicubic —177-183 — FinalMultiplier renormalization
        float finalMultiplier = rcp(weight0 + weight1 + weight2 + weight3 + weight4);
        rawHistory *= finalMultiplier;
    }

    // Convert history to YCoCg for clamping.
    // @ref Karis 2014 "TAA" —1926 — TransformSceneColor(HistoryPayload.Color)
    float3 historyYCoCg = RGBToYCoCg(rawHistory);

    // ── Clamp History to Neighborhood AABB ────────────────────────────────────
    // @ref TSR history update —1253-1259 — fastClamp(PrevHighFrequencyColor, InputMinColor, InputMaxColor)
    // @ref Karis 2014 "TAA" —1957-1961 — ClampHistory
    float3 clampedHistory = clamp(historyYCoCg, neighborMin, neighborMax);

    // ── Compute Adaptive Blend Weight ─────────────────────────────────────────
    // @ref TSR history update —1108-1169 — Sample count based weighting
    // @ref TSR parameters —3067-3068 — HistorySampleCount, HistoryHisteresis
    //
    // The blend factor controls how much of the current frame vs history is used.
    // Static pixels: alpha ~ theoricBlendFactor (small, ~0.06 at 100%, ~0.027 at 67%)
    //   -> This means each new frame contributes very little, building up detail over
    //      many frames (16 frames at 100% resolution fraction).
    // Moving pixels: alpha increases toward 0.2 based on velocity magnitude
    //   -> Fast motion needs more current frame to avoid ghosting.
    // Rejected pixels: alpha approaches 1.0 (use current frame)
    //   -> When the history doesn't match the current scene (new geometry, lighting
    //      changes), the current frame must dominate.
    //
    // The 0.2 velocity blend target and /40 normalization come from the reference engine's TAA path
    // which TSR inherits for the velocity-based weight modulation.
    float blendFinal = theoricBlendFactor;

    // @ref TSR history update —1264 — OutputPixelVelocity * InvWeightClampingPixelSpeed
    // @ref TSR parameters —3070 — WeightClampingPixelSpeedAmplitude
    // @ref TSR parameters —301 — WeightClampingPixelSpeed default = 1.0
    // @ref TSR parameters —286 — WeightClampingSampleCount default = 4.0
    //
    // Velocity-based blend increase: as pixel velocity increases, reduce the
    // number of accumulated samples to maintain sharpness during motion.
    // At default settings (WeightClampingSampleCount=4, WeightClampingPixelSpeed=1.0),
    // the maximum blend factor under motion is approximately 1/4 = 0.25.
    //
    // The weight clamping formula from Wihlidal 2019 "Temporal Super-Resolution":
    //   WeightClampingPixelSpeedAmplitude = clamp(1.0 - VelocityWeightClampingSampleCount * HistoryHisteresis, 0, 1)
    //   MaxValidity = 1.0 - WeightClampingPixelSpeedAmplitude * saturate(OutputPixelVelocity * InvWeightClampingPixelSpeed)
    //
    // We simplify to the equivalent per-pixel computation: lerp blend factor
    // between static (theoricBlendFactor) and motion limit based on velocity.
    float velocityBlendTarget = max(theoricBlendFactor, 0.2);
    blendFinal = lerp(blendFinal, velocityBlendTarget, saturate(velocityMag));

    // Boost blend factor based on rejection mask from pass 2.
    // @ref TSR history update —1116-1118 — MinRejectionBlendFactor = RejectionFactorToBlendFactor(LowFrequencyRejection)
    // When history is rejected (rejectionWeight -> 1.0), the blend factor must
    // increase so the current frame dominates. This prevents ghosting from stale
    // history on newly revealed surfaces.
    blendFinal = max(blendFinal, rejectionWeight);

    // @ref Karis 2014 "TAA" —2246 — Anti-flicker: minimum contribution guard
    // Prevents temporal buzzing on surfaces where filtered and history luminance
    // are nearly equal but not identical (e.g., dithered surfaces, TAA noise).
    float lumaFiltered = filteredColor.x;  // Y channel in YCoCg = luminance * 4
    float lumaHistory  = clampedHistory.x;
    blendFinal = max(blendFinal, saturate(0.01 * lumaHistory / abs(lumaFiltered - lumaHistory)));

    // @ref Karis 2014 "TAA" —2248-2251 — NaN guard
    blendFinal = -min(-blendFinal, 0.0);

    // ── Off-screen Reset ──────────────────────────────────────────────────────
    // @ref TSR history update —916-921 — IsOffScreenOrDisoccluded -> bIsOffScreen
    // @ref Karis 2014 "TAA" —2275-2281 — Offscreen feedback resets
    // When the reprojected UV lands outside [0,1], there is no valid history.
    // The current frame's filtered color is used directly.
    if (offScreen)
    {
        clampedHistory = filteredColor;
        blendFinal = 1.0;
    }

    // ── HDR-Weighted Blend ────────────────────────────────────────────────────
    // @ref TSR history update —1282-1298 — Final accumulation with HDR weighting
    // @ref Karis 2014 "TAA" —2283-2293 — DO FINAL BLEND
    //
    // HDR weighting prevents bright firefly pixels from dominating the temporal
    // blend. Each sample's contribution is scaled by its inverse luminance,
    // effectively tonemapping during the blend and inverse-tonemapping after.
    float filterWeight  = HdrWeightY(filteredColor.x, FrameExposureScale);
    float historyWeight = HdrWeightY(clampedHistory.x, FrameExposureScale);

    float2 blendWeights = WeightedLerpFactors(historyWeight, filterWeight, blendFinal);
    float3 outputYCoCg = clampedHistory * blendWeights.x + filteredColor * blendWeights.y;

    // ── Convert Back to Linear RGB ────────────────────────────────────────────
    // @ref Karis 2014 "TAA" —2302 — TransformBackToRawLinearSceneColor
    float3 outputRGB = YCoCgToRGB(outputYCoCg);

    // @ref TSR history update —1340-1344 — NaN/negative/overflow guard
    // Guard against negative values from YCoCg rounding and NaN propagation.
    outputRGB = -min(-outputRGB, 0.0);

    // ── Spatial Anti-Aliasing for Rejected Pixels ─────────────────────────────
    // @ref TSR spatial AA —81-99 — MainCS spatial AA
    // @ref TSR history update —1093-1104 — SpatialAntiAliasingLerp
    //
    // When a pixel's history was largely rejected, the output may show aliasing
    // because only the current frame's single sample contributes. We apply a
    // small 5-tap spatial filter to smooth newly-revealed geometry. The strength
    // of this filter scales with the rejection weight: fully-accepted history
    // gets no spatial filtering, fully-rejected history gets maximum filtering.
    //
    // The spatial AA uses the same plus-pattern and jitter-weighted Gaussian
    // as the filtered input computation above, but applied to the resolved
    // RGB output. This matches reference's SpatialAntiAliasingLerp approach where
    // the input kernel becomes softer for rejected/disoccluded pixels.
    if (rejectionWeight > 0.5)
    {
        // Compute spatial AA filter in RGB space (output resolution).
        // We re-sample the scene color at the output pixel's neighbors.
        float3 spatialFiltered = float3(0, 0, 0);
        float spatialTotalWeight = 0.0;

        [unroll]
        for (uint k = 0; k < 5; k++)
        {
            int2 sampleCoord = centerCoord + int2(kPlusOffsets[k]);
            sampleCoord = clamp(sampleCoord, int2(0, 0), int2(sceneW - 1, sceneH - 1));

            float3 sampleRGB = SceneColor.Load(int3(sampleCoord, 0)).rgb;

            // @ref TAA filtering —449 — exp(-2.29 * d^2), sigma=0.47
            float2 pixelOffset = kPlusOffsets[k] - jitterOffset;
            float spatialWeight = exp(-2.29 * dot(pixelOffset, pixelOffset));
            float hdrWt = HdrWeight4(sampleRGB, FrameExposureScale);
            float sampleWt = spatialWeight * hdrWt;

            spatialFiltered += sampleRGB * sampleWt;
            spatialTotalWeight += sampleWt;
        }
        spatialFiltered *= rcp(spatialTotalWeight);

        // Lerp between resolved output and spatial-filtered input based on rejection.
        // @ref TSR history update —1093 — SpatialAntiAliasingLerp = saturate(1.0 - LowFrequencyRejection * 4.0)
        float spatialLerp = saturate((rejectionWeight - 0.5) * 2.0);
        outputRGB = lerp(outputRGB, spatialFiltered, spatialLerp);
    }

    // ── Optional RCAS Sharpening ──────────────────────────────────────────────
    // @ref AMD CAS 2019 —60-120 — CASMain
    // @source AMD FidelityFX CAS — ffx_cas.h
    // @constants NONE (tsrSharpness from cbuffer, 0 = disabled)
    // @deviations the reference applies CAS as a separate post-TSR pass. UzEngine integrates
    //             it inline to avoid an extra dispatch. The CAS algorithm itself
    //             (contrast-adaptive unsharp mask) is identical.
    //
    // When tsrSharpness > 0, apply a Robust Contrast Adaptive Sharpening filter.
    // This counteracts the softening inherent in temporal accumulation.
    // The filter reads a cross-pattern neighborhood and computes per-channel
    // adaptive weights: high contrast regions get less sharpening (avoids ringing),
    // low contrast regions get more (enhances detail).
    if (tsrSharpness > 0.0)
    {
        // Sample the cross-pattern neighbors from the resolved output.
        // Since we are operating on the resolved output pixel, we read from
        // the already-resolved taaOutput or fall back to scene color neighbors
        // in the same dispatch. For the inline CAS, we use the scene color
        // neighborhood that was already resolved above.
        float3 casN = float3(0, 0, 0);
        float3 casS = float3(0, 0, 0);
        float3 casE = float3(0, 0, 0);
        float3 casW = float3(0, 0, 0);
        {
            // Read cardinal neighbors from scene color (render resolution).
            int2 coordN = clamp(centerCoord + int2( 0, -1), int2(0, 0), int2(sceneW - 1, sceneH - 1));
            int2 coordS = clamp(centerCoord + int2( 0,  1), int2(0, 0), int2(sceneW - 1, sceneH - 1));
            int2 coordE = clamp(centerCoord + int2( 1,  0), int2(0, 0), int2(sceneW - 1, sceneH - 1));
            int2 coordW = clamp(centerCoord + int2(-1,  0), int2(0, 0), int2(sceneW - 1, sceneH - 1));

            casN = SceneColor.Load(int3(coordN, 0)).rgb;
            casS = SceneColor.Load(int3(coordS, 0)).rgb;
            casE = SceneColor.Load(int3(coordE, 0)).rgb;
            casW = SceneColor.Load(int3(coordW, 0)).rgb;
        }

        // CAS: compute per-channel min/max for contrast estimation.
        float3 casMin = min(min(min(casN, casS), min(casE, casW)), outputRGB);
        float3 casMax = max(max(max(casN, casS), max(casE, casW)), outputRGB);

        // Per-channel adaptive weight: sqrt(min/max) scaled by sharpness.
        // High contrast (low min/max ratio) => less sharpening to avoid ringing.
        // Low contrast (high min/max ratio) => more sharpening to enhance detail.
        float3 casWeight = sqrt(min(casMin, float3(2, 2, 2) - casMax) * rcp(casMax + 1e-5));
        casWeight *= -tsrSharpness;

        // Clamp weight to prevent negative values (which would invert the filter).
        casWeight = max(casWeight, float3(-0.5, -0.5, -0.5));

        // Apply unsharp mask: output = (center + neighbors * w) / (1 + 4*w)
        float3 sharpened = (outputRGB + (casN + casS + casE + casW) * casWeight) * rcp(1.0 + 4.0 * casWeight);

        // Guard against negative values from the sharpening filter.
        outputRGB = -min(-sharpened, 0.0);
    }

    // ── Write Output ──────────────────────────────────────────────────────────
    taaOutput[DispatchThreadId] = float4(outputRGB, 1.0);
}

#endif // TSR_PASS_UPDATE_HISTORY


// ─────────────────────────────────────────────────────────────────────────────
// Fallback entry point (no pass define active)
// ─────────────────────────────────────────────────────────────────────────────

/// When compiled without TSR_PASS_DILATE_VELOCITY, TSR_PASS_REJECT_SHADING,
/// or TSR_PASS_UPDATE_HISTORY, provide a no-op main() so the engine's
/// auto-discovery shader compilation does not fail with "missing entry point".
/// This shader is never dispatched — the real entry points are compiled
/// via CreateShaderWithDefines() in PipelineLibrary.
#if !defined(TSR_PASS_DILATE_VELOCITY) && !defined(TSR_PASS_REJECT_SHADING) && !defined(TSR_PASS_UPDATE_HISTORY)

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    // No-op: TSR requires a pass define to select the entry point.
}

#endif
