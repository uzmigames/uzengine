/// Per-Pixel Motion Blur — Compute Shader
///
/// Implements per-pixel directional motion blur using the existing velocity
/// buffer (RG16F, pixel-space). Follows industry-standard's MotionBlurApply.usf gather
/// path: for each pixel, samples scene color along the velocity vector in
/// both directions (forward and backward), weights by depth comparison
/// to prevent background bleeding through foreground, and blends with the
/// center pixel based on coverage.
///
/// # Algorithm (simplified from reference MotionBlurApply.usf MainCS):
/// 1. Read per-pixel velocity from velocity buffer.
/// 2. Skip blur if velocity magnitude < CONFIG_MINIMAL_PIXEL_VELOCITY (0.5 px).
/// 3. Clamp velocity to maxBlurPixels to prevent excessive blur.
/// 4. Scale velocity by motionBlurAmount * 0.5 (bidirectional).
/// 5. Compute sample count proportional to velocity length, clamped to max.
/// 6. For each sample pair (forward + backward along velocity):
///    a. Jitter offset with InterleavedGradientNoise.
///    b. Sample scene color at offset UV.
///    c. Sample depth at offset for depth-aware weighting.
///    d. Compute weight using the reference engine's depth comparison (SOFT_Z_EXTENT).
/// 7. Normalize accumulated color, blend with center based on coverage.
///
/// # Resources
/// - Root constants at b0 (8 DWORDs): SRV/UAV indices + blur parameters.
/// - s0: Linear clamp sampler for bilinear scene color sampling.
///
/// @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —269-714 — MainCS (gather path)
/// @ref Motion blur common —34 — CONFIG_MINIMAL_PIXEL_VELOCITY = 0.5
/// @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —54 — SOFT_Z_EXTENT = 1
/// @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —341 — SampleCountFactor = 4
/// @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —345 — RecommendedSampleCount = clamp(SampleCountFactor * ceil(MaxPixelLength / SampleCountFactor), SampleCountFactor, MaxSampleCount)
/// @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —373 — bSkip = MaxPixelLength < CONFIG_MINIMAL_PIXEL_VELOCITY
/// @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —438-450 — Fast path: bidirectional sampling along SearchVector
/// @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —453 — MotionBlurColor = ColorAccum * InvSampleCount
/// @ref Motion blur defaults —598-600 — MotionBlurAmount=0.5, MotionBlurMax=5.0, MotionBlurTargetFPS=30
/// @ref Motion blur parameters —613 — VelocityScale = MotionBlurTimeScale * MotionBlurAmount * 0.5
/// @ref Motion blur time scale —5428-5431 — MotionBlurTimeScale = (1/TargetFPS) / DeltaRealTime
/// @ref Jimenez 2016 "Interleaved Gradient Noise" —11-18 — InterleavedGradientNoise
/// @constants CONFIG_MINIMAL_PIXEL_VELOCITY=0.5 (MotionBlurCommon.ush:34),
///            SOFT_Z_EXTENT=1 (MotionBlurApply.usf:54),
///            SampleCountFactor=4 (MotionBlurApply.usf:341),
///            IGN magic=(0.06711056, 0.00583715, 52.9829189) (RandomInterleavedGradientNoise.ush:16)
/// @deviations Uses simplified single-direction gather (no tile classification, no scatter path,
///             no half-res gather, no post-motion-blur translucency). UzEngine velocity buffer
///             stores pixel-space velocity directly (reference uses encoded polar velocity via VelocityFlatten).
///             Depth comparison uses linear depth ratio instead of the reference engine's DeviceZ-based encoding.

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Motion blur common —34 — CONFIG_MINIMAL_PIXEL_VELOCITY
/// Pixels with velocity below this threshold are not blurred. Saves cost on
/// static regions and prevents unnecessary sampling noise.
static const float CONFIG_MINIMAL_PIXEL_VELOCITY = 0.5;

/// @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —54 — SOFT_Z_EXTENT
/// Depth comparison softness: controls how aggressively depth separation
/// affects sample weighting. Value of 1 means depth differences of ~1 unit
/// (in normalized depth) produce full attenuation.
/// From the McGuire paper: "We use SOFT Z EXTENT = 1mm to 10cm for our results"
static const float SOFT_Z_EXTENT = 1.0;

/// @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —341 — SampleCountFactor
/// Sample count is quantized to multiples of this factor. Each step in the
/// inner loop processes SampleCountFactor samples (2 UV pairs x 2 directions).
static const uint SAMPLE_COUNT_FACTOR = 4;

/// Maximum sample count when motionBlurQuality is not further constraining.
/// passes MaxSampleCount as a shader parameter; here we use a reasonable
/// upper bound matching the reference engine's "High" quality setting.
/// @ref Motion blur parameters —467 — MaxSampleCount parameter
static const uint MAX_SAMPLE_COUNT = 16;

// ─────────────────────────────────────────────────────────────────────────────
// Parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — 8 DWORDs matching the compute root signature limit.
cbuffer MotionBlurParams : register(b0) {
    uint   sceneSrvIndex;        ///< DWORD 0: HDR scene color SRV.
    uint   velocitySrvIndex;     ///< DWORD 1: velocity buffer SRV (RG16F, pixel-space).
    uint   depthSrvIndex;        ///< DWORD 2: depth buffer SRV (R32_FLOAT, reversed-Z).
    uint   outputDimsPacked;     ///< DWORD 3: (width & 0xFFFF) | (height << 16). Packed to free a DWORD.
    /// @ref Motion blur parameters —613 — VelocityScale = MotionBlurTimeScale * MotionBlurAmount * 0.5
    float  motionBlurScale;      ///< DWORD 4: combined velocity scale (time * amount * 0.5).
    float  maxBlurPixels;        ///< DWORD 5: max blur radius in pixels.
    uint   outputUavIndex;       ///< DWORD 6: output UAV index.
    /// @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —269-285 — neighbor max tile texture for per-tile early-out
    uint   neighborMaxSrvIndex;  ///< DWORD 7: neighbor max tile SRV (RG16F, 1/16 resolution). 0xFFFFFFFF = disabled.
};

/// Linear clamp sampler for bilinear scene color and depth sampling.
SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// SM5 Fallback Resources (file-scope bindings)
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
Texture2D<float4>   sceneColor  : register(t0);
Texture2D<float2>   velocityTex : register(t1);
Texture2D<float>    depthTex    : register(t2);
RWTexture2D<float4> outputTex   : register(u0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Interleaved Gradient Noise
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Jimenez 2016 "Interleaved Gradient Noise" —11-18 — InterleavedGradientNoise
/// @constants magic = (0.06711056, 0.00583715, 52.9829189) (RandomInterleavedGradientNoise.ush:16)
///            frameJitter = FrameId * float2(47, 17) * 0.695 (RandomInterleavedGradientNoise.ush:14)
/// @deviations NONE
///
/// Temporal dithering noise (Jimenez 2014). Returns [0,1].
float InterleavedGradientNoise(float2 pixelPos, float frameId)
{
    pixelPos += frameId * (float2(47.0, 17.0) * 0.695);
    const float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(pixelPos, magic.xy)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Depth Linearization
// ─────────────────────────────────────────────────────────────────────────────

/// Converts reversed-Z hardware depth [0,1] to linear [0,1] depth for
/// depth comparison. In reversed-Z, near=1.0 and far=0.0.
/// We use the raw depth directly for comparison since the reference engine's motion blur
/// also operates on device-Z for its depth comparisons
/// (MotionBlurVelocityFlatten.ush:144 — ConvertFromDeviceZ).
///
/// For our simplified depth-aware weighting, we just use raw depth directly.
/// Reversed-Z preserves relative ordering which is all we need.

// ─────────────────────────────────────────────────────────────────────────────
// Depth-Aware Sample Weighting
// ─────────────────────────────────────────────────────────────────────────────

/// @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —229-249 — ComputeCenterOrSampleWeight
/// @constants DepthScale = SOFT_Z_EXTENT = 1 (MotionBlurApply.usf:54)
/// @deviations Simplified: uses single weight combining center vs sample depth
///             priority, without separate convolution weight and hole-fill weight.
///
/// Computes a blend weight that gives priority to foreground (closer) objects.
/// When the sample is behind the center pixel, its contribution is reduced.
/// When the sample is in front of the center pixel, it can contribute more
/// (representing foreground object blur extending over background).
///
/// @param centerDepth   Raw depth of the center pixel (reversed-Z: near=1, far=0).
/// @param sampleDepth   Raw depth of the sample pixel.
/// @return              Weight in [0, 1].
float ComputeDepthWeight(float centerDepth, float sampleDepth)
{
    // In reversed-Z: larger depth = closer to camera.
    // depthDiff > 0 means sample is further away (behind center) — reduce weight.
    // depthDiff < 0 means sample is closer (in front of center) — allow contribution.
    //
    // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —235 — CenterWeight = saturate(0.5 + DepthScale * (SampleDepth - CenterDepth))
    // In reference engines, depth is converted from device Z(DeviceZ) which is linear [0,1] where 0=near, 1=far.
    // In reversed-Z: near=1, far=0, so our comparison is flipped.
    // We use: weight = saturate(0.5 + SOFT_Z_EXTENT * (centerDepth - sampleDepth))
    // This gives weight ~1.0 when sample is at same depth or behind,
    // and weight ~0.0 when sample is significantly in front.
    //
    // But for motion blur, we actually want foreground objects to blur over background,
    // so we use the combined center-or-sample approach from reference:
    // The returned weight represents how much this sample should contribute,
    // combining both directions (foreground-over-background and vice versa).

    // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —235-238
    float centerWeight = saturate(0.5 + SOFT_Z_EXTENT * (sampleDepth - centerDepth));
    float sampleWeight = saturate(0.5 - SOFT_Z_EXTENT * (sampleDepth - centerDepth));

    // For the simplified single-direction gather, we use the maximum of both
    // to allow contribution from both foreground-over-background and
    // background-under-foreground scenarios.
    return max(centerWeight, sampleWeight);
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —269 — MainCS
/// @deviations Single-direction gather only (no tile classification, no scatter).
///             See file header for full deviation list.
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    // ── Unpack dimensions ────────────────────────────────────────────────
    const uint outputWidth  = outputDimsPacked & 0xFFFFu;
    const uint outputHeight = outputDimsPacked >> 16u;

    // ── Bounds check ─────────────────────────────────────────────────────
    if (dtid.x >= outputWidth || dtid.y >= outputHeight)
        return;

    // ── Neighbor max tile early-out ──────────────────────────────────────
    // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —269-285 — skip tiles with zero motion
    if (neighborMaxSrvIndex != 0xFFFFFFFFu) {
#ifdef UZ_BINDLESS
        Texture2D<float2> neighborMaxTex = ResourceDescriptorHeap[neighborMaxSrvIndex];
        uint2 tileCoord = dtid.xy / 16u;
        float2 tileMaxVel = neighborMaxTex.Load(int3(tileCoord, 0));
        float tileMaxSpeed = length(tileMaxVel) * motionBlurScale;
        // @ref Motion blur common —34 — CONFIG_MINIMAL_PIXEL_VELOCITY = 0.5
        if (tileMaxSpeed < 0.5f) {
            RWTexture2D<float4> outputTex = ResourceDescriptorHeap[outputUavIndex];
            Texture2D<float4> sceneColorEarly = ResourceDescriptorHeap[sceneSrvIndex];
            float2 earlyUV = (float2(dtid.xy) + 0.5f) / float2(outputWidth, outputHeight);
            outputTex[dtid.xy] = sceneColorEarly.SampleLevel(linearSampler, earlyUV, 0);
            return;
        }
#endif
    }

    // ── Resource access (bindless via descriptor heap) ──────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4>   sceneColor  = ResourceDescriptorHeap[sceneSrvIndex];
    Texture2D<float2>   velocityTex = ResourceDescriptorHeap[velocitySrvIndex];
    Texture2D<float>    depthTex    = ResourceDescriptorHeap[depthSrvIndex];
    RWTexture2D<float4> outputTex   = ResourceDescriptorHeap[outputUavIndex];
#endif

    const float2 outputSize = float2(outputWidth, outputHeight);
    const float2 invOutputSize = 1.0 / outputSize;
    const int2 pixelCoord = int2(dtid.xy);

    // Center pixel UV (pixel center at +0.5).
    // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —296 — ColorUV = (float2(iColorPixelPos) + 0.5) * Color_ExtentInverse
    float2 centerUV = (float2(pixelCoord) + 0.5) * invOutputSize;

    // ── Read center pixel data ───────────────────────────────────────────
    float4 centerColor = sceneColor.SampleLevel(linearSampler, centerUV, 0);
    float2 velocity    = velocityTex.Load(int3(pixelCoord, 0));
    float  centerDepth = depthTex.Load(int3(pixelCoord, 0));

    // ── Velocity scaling ─────────────────────────────────────────────────
    // @ref Motion blur parameters —613
    //   VelocityScale = MotionBlurTimeScale * MotionBlurAmount * 0.5
    // @ref Motion blur time scale —5428-5431
    //   MotionBlurTimeScale = (1.0 / MotionBlurTargetFPS) / DeltaRealTime
    // The 0.5 factor accounts for blur samples going both directions.
    // motionBlurScale is pre-computed on CPU as:
    //   (1.0 / targetFPS) / deltaTime * motionBlurAmount * 0.5
    // This normalizes blur length to the target frame rate, so blur appears
    // consistent regardless of actual framerate.
    float2 scaledVelocity = velocity * motionBlurScale;

    // Compute velocity magnitude in pixels after scaling.
    float velocityLength = length(scaledVelocity);

    // ── Early out: skip blur for near-static pixels ──────────────────────
    // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —373 — bSkip = MaxPixelLength < CONFIG_MINIMAL_PIXEL_VELOCITY
    if (velocityLength < CONFIG_MINIMAL_PIXEL_VELOCITY)
    {
        outputTex[pixelCoord] = centerColor;
        return;
    }

    // ── Clamp velocity to maximum blur radius ────────────────────────────
    // @ref Motion blur velocity flatten —80 — VelocityPolar.x = min(VelocityPolar.x, VelocityMax)
    // @ref Motion blur parameters —621 — VelocityMax = SceneViewportSizeX * 0.5 * UVVelocityMax
    if (velocityLength > maxBlurPixels)
    {
        scaledVelocity = scaledVelocity * (maxBlurPixels / velocityLength);
        velocityLength = maxBlurPixels;
    }

    // ── Compute sample count ─────────────────────────────────────────────
    // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —345
    //   RecommendedSampleCount = clamp(SampleCountFactor * ceil(MaxPixelLength / SampleCountFactor),
    //                                  SampleCountFactor, MaxSampleCount)
    // Sample count scales with velocity length: more samples for longer blur.
    uint sampleCount = clamp(
        SAMPLE_COUNT_FACTOR * (uint)ceil(velocityLength / (float)SAMPLE_COUNT_FACTOR),
        SAMPLE_COUNT_FACTOR,
        MAX_SAMPLE_COUNT
    );
    float invSampleCount = rcp((float)sampleCount);

    // ── Search vector in UV space ────────────────────────────────────────
    // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —405 — SearchVector0 = Max0VelocityPixels * Color_ExtentInverse.xy
    float2 searchVector = scaledVelocity * invOutputSize;

    // ── Per-pixel jitter ─────────────────────────────────────────────────
    // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —301 — Random = InterleavedGradientNoise(iColorPixelPos, 0)
    float random = InterleavedGradientNoise(float2(pixelCoord), 0);

    // ── Gather samples along velocity vector ─────────────────────────────
    // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —432-451 — Fast path gather loop
    //
    // For each step, we take two samples: one in the forward direction and
    // one in the backward direction along the velocity vector. This is the
    // bidirectional sampling from the reference engine's fast path.
    //
    // The offset fraction determines how far along the velocity vector each
    // sample is placed, normalized so the full range covers [-1, 1] of the
    // search vector.

    float4 colorAccum = 0;
    float  weightAccum = 0;

    [loop]
    for (uint i = 0; i < sampleCount; i += SAMPLE_COUNT_FACTOR)
    {
        // Process 2 pairs of samples per iteration (matching the reference engine's j=0..1 inner loop).
        // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —436 — UNROLL_N(2) for (uint j = 0; j < 2; j++)
        [unroll]
        for (uint j = 0; j < 2; j++)
        {
            // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —438
            //   OffsetLength = float(i / 2 + j) + (0.5 + float2(Random - 0.5, 0.5 - Random))
            float2 offsetLength = float(i / 2 + j) + (0.5 + float2(random - 0.5, 0.5 - random));

            // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —439
            //   OffsetFraction = OffsetLength * (2.0 * InvSampleCount)
            float2 offsetFraction = offsetLength * (2.0 * invSampleCount);

            // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —442-443
            //   SampleUV[0] = ColorUV + OffsetFraction.x * SearchVector0
            //   SampleUV[1] = ColorUV - OffsetFraction.y * SearchVector0
            float2 sampleUV0 = centerUV + offsetFraction.x * searchVector;
            float2 sampleUV1 = centerUV - offsetFraction.y * searchVector;

            // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —445-446 — clamp to viewport bounds
            sampleUV0 = clamp(sampleUV0, invOutputSize * 0.5, 1.0 - invOutputSize * 0.5);
            sampleUV1 = clamp(sampleUV1, invOutputSize * 0.5, 1.0 - invOutputSize * 0.5);

            // Sample scene color (bilinear).
            float4 sampleColor0 = sceneColor.SampleLevel(linearSampler, sampleUV0, 0);
            float4 sampleColor1 = sceneColor.SampleLevel(linearSampler, sampleUV1, 0);

            // Sample depth for depth-aware weighting.
            float sampleDepth0 = depthTex.SampleLevel(linearSampler, sampleUV0, 0);
            float sampleDepth1 = depthTex.SampleLevel(linearSampler, sampleUV1, 0);

            // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —573-576 — depth-aware weighting
            float weight0 = ComputeDepthWeight(centerDepth, sampleDepth0);
            float weight1 = ComputeDepthWeight(centerDepth, sampleDepth1);

            colorAccum += weight0 * sampleColor0 + weight1 * sampleColor1;
            weightAccum += weight0 + weight1;
        }
    }

    // ── Normalize and blend with center ──────────────────────────────────
    // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —453 — MotionBlurColor = ColorAccum * InvSampleCount
    // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —1038 — OutputColor = CenterColor * FullResBlend + MotionBlurColor
    //
    // In the reference engine's fast path (MotionBlurApply.usf:454), FullResBlend = 0.0 when
    // blur is fully active. In the depth-aware path (line 702-706),
    // FullResBlend = 1.0 - DirectionalColorWeight, allowing the center pixel
    // to show through where blur coverage is insufficient.
    //
    // We compute coverage from the accumulated weight vs expected weight.
    // If all samples contributed fully, weightAccum = sampleCount.
    // Coverage < 1.0 means some samples were depth-rejected, so we blend
    // the center pixel back in proportionally.

    float4 blurredColor;
    if (weightAccum > 0.0)
    {
        // Normalize accumulated color by actual weight (not sample count).
        // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —160-163 — NormalizeAccumulator
        //   return -min(-Accumulator * rcp(AccumulatorWeight), 0.0)
        // This is equivalent to max(Accumulator / AccumulatorWeight, 0.0) for positive values.
        blurredColor = max(colorAccum * rcp(weightAccum), 0.0);

        // Coverage = how much of the blur actually contributed.
        // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —702-706 — FullResBlend = 1.0 - DirectionalColorWeight
        // DirectionalColorWeight is normalized weight / StepPerDirectionCount.
        float coverage = saturate(weightAccum / (float)sampleCount);
        float fullResBlend = 1.0 - coverage;

        // @ref McGuire et al. 2012 "A Reconstruction Filter for Plausible Motion Blur" —1038 — OutputColor = CenterColor * FullResBlend + MotionBlurColor
        blurredColor = centerColor * fullResBlend + blurredColor * coverage;
    }
    else
    {
        // No valid samples — passthrough center color.
        blurredColor = centerColor;
    }

    // Preserve alpha channel (scene alpha should not be affected by blur).
    blurredColor.a = centerColor.a;

    outputTex[pixelCoord] = blurredColor;
}
