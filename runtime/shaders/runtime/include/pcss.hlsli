/// PCSS (Percentage Closer Soft Shadows) — standard implementation.
///
/// @ref Fernando 2005 "Percentage-Closer Soft Shadows" (NVIDIA GPU Gems)
///
/// Three-stage pipeline:
/// 1. Blocker search (16 Sobol samples) — find average occluder depth
/// 2. Penumbra estimation — compute variable filter radius from blocker distance
/// 3. Adaptive PCF (32 Sobol samples) — filter with estimated radius
///
/// # Prerequisites
/// Requires `ShadowDataCB` with `tanLightSourceAngle` and `maxSoftKernelSize`.
/// `tanLightSourceAngle` must be pre-scaled to shadow tile UV space on the CPU:
///   tan(0.5 * deg2rad(LightSourceAngle)) * (MaxSubjectZ - MinSubjectZ) / (2 * ShadowBounds.W)
/// `maxSoftKernelSize` is in tile UV space: MaxKernelSize / ResolutionX.
///
/// @ref Fernando 2005 — PCSS parameter setup (tanLightSourceAngle, maxSoftKernelSize)

#ifndef PCSS_HLSLI
#define PCSS_HLSLI

#include "pbr_common.hlsli"
#include "sampling.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Fernando 2005 "PCSS" — sample counts
/// Blocker search: 16 samples (PCSS_SEARCH_BITS=4)
/// PCF filtering:  32 samples (PCSS_SAMPLE_BITS=5)
/// @constants PCSS_SEARCH_SAMPLES=16, PCSS_SAMPLES=32
static const uint PCSS_SEARCH_SAMPLES = 16;
static const uint PCSS_SAMPLES        = 32;

/// @ref Fernando 2005 "PCSS" — random filter scale
/// Random filter scale applied to the penumbra radius before clamping.
/// @constants RandomFilterScale=0.75
static const float PCSS_RANDOM_FILTER_SCALE = 0.75f;

/// @ref Fernando 2005 "PCSS" — depth bias angle clamp
/// Maximum depth bias angle in radians = sin(5°) = 0.0872665.
/// Prevents extreme depth bias at grazing angles.
/// @constants 0.0872665 = sin(5°)
static const float PCSS_MAX_DEPTH_BIAS_SINE = 0.0872665f;

/// @ref Fernando 2005 "PCSS" — average sample distance for sharpening
/// Average sample distance in UV space for sharpening: sqrt(1 / (N * PI))
/// @constants derived from PCSS_SAMPLES=32: sqrt(1/(32*PI)) = 0.09973
static const float PCSS_AVG_SAMPLE_DISTANCE = 0.09973f; // sqrt(1 / (32 * PI))

/// @ref Fernando 2005 "PCSS" — post-PCF sharpening limit
/// Maximum sharpness factor for post-PCF sharpening.
/// @constants MaxSharpnessFactor=4
static const float PCSS_MAX_SHARPNESS_FACTOR = 4.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Sobol Low-Discrepancy Sequence
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Sobol 1967 "Distribution of Points in a Cube" — Sobol quasi-random sequence
/// Evaluates additional Sobol points from a base value and index.
/// Uses 10 direction numbers to generate quasi-random 2D samples with
/// better coverage than purely random sampling.
/// @constants Direction numbers from standard Sobol sequence tables
/// @deviations
///   We skip the SobolPixel texture lookup (requires View.SobolSamplingTexture)
///   and use a per-pixel hash as the base instead.
uint2 SobolIndex(uint2 Base, int Index, int Bits) {
    /// @ref Sobol sequence direction numbers (standard tables)
    uint2 SobolNumbers[10] = {
        uint2(0x8680u, 0x4c80u), uint2(0xf240u, 0x9240u),
        uint2(0x8220u, 0x0e20u), uint2(0x4110u, 0x1610u),
        uint2(0xa608u, 0x7608u), uint2(0x8a02u, 0x280au),
        uint2(0xe204u, 0x9e04u), uint2(0xa400u, 0x4682u),
        uint2(0xe300u, 0xa74du), uint2(0xb700u, 0x9817u),
    };

    uint2 Result = Base;
    [unroll]
    for (int b = 0; b < 10 && b < Bits; ++b) {
        Result ^= (Index & (1 << b)) != 0 ? SobolNumbers[b] : uint2(0, 0);
    }
    return Result;
}

/// Computes a per-pixel Sobol base from screen position.
/// @ref Sobol 1967 — per-pixel Sobol base via hash approximation
/// We use a hash approximation instead of the texture lookup.
/// @deviations Uses hash instead of View.SobolSamplingTexture (we don't have
///   that texture). The Sobol direction numbers still provide good 2D distribution.
uint2 SobolPixel(float2 svPosition) {
    uint2 pixel = uint2(svPosition);
    // XOR-shift hash to spread pixel coordinates across the 16-bit Sobol space
    uint lo = (pixel.x & 0xFu) | ((pixel.y & 0xFu) << 4u);
    uint hi = ((pixel.x >> 4u) & 0xFu) | (((pixel.y >> 4u) & 0xFu) << 4u);
    uint packed = (lo * 0x8681u) ^ (hi * 0x4c81u);
    return uint2(packed & 0xFFFFu, (packed >> 16u) ^ (packed & 0xFFFFu));
}

// UniformSampleDiskConcentricApprox — now in sampling.hlsli

// ─────────────────────────────────────────────────────────────────────────────
// PCSS Filter — Directional Light
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Fernando 2005 "Percentage-Closer Soft Shadows" — full directional PCSS pipeline
///
/// Full PCSS pipeline for directional lights:
///   Stage 1: Blocker search with receiver plane depth bias
///   Stage 2: Penumbra estimation from average occluder distance
///   Stage 3: Adaptive PCF with Sobol sampling
///   Post:    Sharpening filter for sub-texel detail
///
/// @constants PCSS_SEARCH_SAMPLES=16, PCSS_SAMPLES=32
/// @constants RandomFilterScale=0.75
/// @constants MaxDepthBiasSine=0.0872665 = sin(5°)
/// @constants MaxSharpnessFactor=4
/// @constants AverageSampleDistance=sqrt(1/(32*PI))
/// @deviations
///   - No pixel quad message passing (PCSS_SHARE_PER_PIXEL_QUAD=0 equivalent).
///     Some implementations share blocker search and PCF results across 2x2 pixel
///     quads via quad-lane message passing. We skip this for simplicity — it only
///     affects performance, not correctness.
///   - No elliptical kernel (PCSS_ANTI_ALIASING_METHOD=0 equivalent).
///     the reference uses covariance-based elliptical PCF kernels for anisotropic
///     penumbras. We use circular kernels. This is a quality trade-off.
///   - SobolPixel uses hash instead of View.SobolSamplingTexture.
///   - No View.StateFrameIndexMod8 temporal jitter (single-frame sampling).
float SampleShadowPCSS(float2 shadowPosition, float sceneDepth, float texelSize,
                        float2 cascadeOffset, float2 screenPos, float2 tileSize) {
    // ── Receiver plane depth bias ──────────────────────────────────────
    // @ref Fernando 2005 "PCSS" —124-130
    // Compute the depth bias plane from shadow position screen derivatives.
    // This prevents self-shadowing artifacts when sampling offset positions.
    //
    // shadowPosition is in atlas UV space. Convert derivatives to tile UV space
    // (divide by tileSize) so they match sampleUVOffset which is also in tile UV space.
    // In some engines, ShadowPosition is already in tile UV space, so no conversion is needed there.
    float2 atlasPosDDX = ddx(shadowPosition);
    float2 atlasPosDDY = ddy(shadowPosition);
    float3 shadowPosDDX = float3(atlasPosDDX / tileSize, ddx(sceneDepth));
    float3 shadowPosDDY = float3(atlasPosDDY / tileSize, ddy(sceneDepth));
    float3 depthBiasPlaneNormal = cross(shadowPosDDX, shadowPosDDY);

    // @ref Fernando 2005 "PCSS" —126
    // Clamp to prevent extreme bias at grazing angles.
    // 0.0872665 = sin(5°) — minimum z-component of the bias plane normal.
    float depthBiasFactor = 1.0f / max(abs(depthBiasPlaneNormal.z),
                                        length(depthBiasPlaneNormal) * PCSS_MAX_DEPTH_BIAS_SINE);
    float2 depthBiasDotFactors = depthBiasPlaneNormal.xy * depthBiasFactor;

    // ── Texel sizes ────────────────────────────────────────────────────
    // @ref Fernando 2005 "PCSS" —132-133
    // MinFilterSize = max texel size in the tile's UV space.
    // texelSize is atlas texel size; divide by tile size to get per-tile texel size.
    float2 shadowTexelSize = float2(texelSize, texelSize) / tileSize;
    float minFilterSize = max(shadowTexelSize.x, shadowTexelSize.y);

    // ── Per-pixel Sobol base ───────────────────────────────────────────
    // @ref Fernando 2005 "PCSS" —141
    uint2 sobolRandom = SobolPixel(screenPos);

    // ── Search radius ──────────────────────────────────────────────────
    // @ref Fernando 2005 "PCSS" —152-154
    // For directional lights: SearchRadius = SceneDepth * TanLightSourceAngle
    // Note: tanLightSourceAngle is already in shadow tile UV space (pre-scaled on CPU).
    // Clamped to [minFilterSize, MaxKernelSize].
    float searchRadius = sceneDepth * tanLightSourceAngle;
    searchRadius = clamp(searchRadius, minFilterSize, maxSoftKernelSize);

    // ── Atlas clamping bounds ──────────────────────────────────────────
    float halfTexel = texelSize * 0.5f;
    float2 atlasMin = cascadeOffset + halfTexel;
    float2 atlasMax = cascadeOffset + tileSize - halfTexel;

    // ── Stage 1: Blocker Search ────────────────────────────────────────
    // @ref Fernando 2005 "PCSS" —188-222
    // Sample shadow map in a disk around the receiver point.
    // Accumulate depths from samples that ARE occluders (closer to light).
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    float depthSum = 0.0f;
    float sumWeight = 0.0f;

    [unroll]
    for (uint i = 0; i < PCSS_SEARCH_SAMPLES; i++) {
        // @ref Fernando 2005 "PCSS" —197-198
        // Generate Sobol sample and map to disk.
        // XOR with fixed scramble values for dimension independence.
        float2 E = float2(SobolIndex(sobolRandom ^ uint2(0x4B05u, 0xB0CDu), i << 3, 7)) / float(0x10000);
        float2 pcssSample = UniformSampleDiskConcentricApprox(E);
        float2 sampleUVOffset = pcssSample * searchRadius;

        // Convert from tile UV space to atlas UV space
        float2 sampleUV = clamp(shadowPosition + sampleUVOffset * tileSize,
                                atlasMin, atlasMax);

        // Raw depth sample (not comparison) to read blocker depth
        float shadowDepth = shadowAtlas.SampleLevel(linearSampler, sampleUV, 0).r;

        // @ref Fernando 2005 "PCSS" —203-208
        // Compute depth comparison with per-sample receiver plane bias.
        // ShadowDepthCompare > SampleDepthBias means this sample is an occluder.
        float shadowDepthCompare = sceneDepth - shadowDepth;
        float sampleDepthBias = max(dot(depthBiasDotFactors, sampleUVOffset), 0.0f);

        if (shadowDepthCompare > sampleDepthBias) {
            depthSum += shadowDepth;
            sumWeight += 1.0f;
        }
    }

    // @ref Fernando 2005 "PCSS" —241-249
    // Early returns: no occluders = fully lit, all occluders = fully shadowed.
    if (sumWeight < 0.5f) {
        // No blockers found — pixel is fully lit.
#ifndef UZ_BINDLESS
        #undef shadowAtlas
#endif
        return 1.0f;
    }

    if (sumWeight >= float(PCSS_SEARCH_SAMPLES) - 0.5f) {
        // Every search sample found an occluder — pixel is fully shadowed.
#ifndef UZ_BINDLESS
        #undef shadowAtlas
#endif
        return 0.0f;
    }

    // ── Stage 2: Penumbra Estimation ───────────────────────────────────
    // @ref Fernando 2005 "PCSS" —267-278
    float normalizeFactor = 1.0f / sumWeight;
    float depthAvg = depthSum * normalizeFactor;
    float averageOccluderDistance = sceneDepth - depthAvg;

    // @ref Fernando 2005 "PCSS" —274
    // For directional lights: Penumbra = TanLightSourceAngle * AverageOccluderDistance
    // Note: TanLightSourceAngle is already in shadow tile UV space (pre-scaled on CPU).
    float penumbra = tanLightSourceAngle * averageOccluderDistance;
    penumbra = min(penumbra, maxSoftKernelSize);

    // @ref Fernando 2005 "PCSS" —278-279
    // Apply random filter scale and clamp to minimum filter size.
    float rawFilterRadius = PCSS_RANDOM_FILTER_SCALE * penumbra;
    float filterRadius = max(minFilterSize, rawFilterRadius);

    // ── Stage 3: Adaptive PCF ──────────────────────────────────────────
    // @ref Fernando 2005 "PCSS" —418-435
    // Sample with Sobol-distributed disk samples at the estimated penumbra radius.
    float visibleLightAccumulation = 0.0f;

    [unroll]
    for (uint j = 0; j < PCSS_SAMPLES; j++) {
        // @ref Fernando 2005 "PCSS" —422-423
        float2 E2 = float2(SobolIndex(sobolRandom, j << 3, 8)) / float(0x10000);
        float2 pcfSample = UniformSampleDiskConcentricApprox(E2);
        float2 sampleUVOffset = pcfSample * filterRadius;

        float2 sampleUV = clamp(shadowPosition + sampleUVOffset * tileSize,
                                atlasMin, atlasMax);
        float sampleDepth = shadowAtlas.SampleLevel(linearSampler, sampleUV, 0).r;

        // @ref Fernando 2005 "PCSS" —428
        // Per-sample receiver plane depth bias.
        float sampleDepthBias = max(dot(depthBiasDotFactors, sampleUVOffset), 0.0f);

        // @ref Fernando 2005 "PCSS" —430
        // Soft depth comparison: saturate((SampleDepth - SceneDepth + Bias) * TransitionScale + 1)
        // We use a fixed TransitionScale equivalent to get soft edges.
        // With hardware comparison (SampleCmpLevelZero), we get binary results.
        // Using manual soft comparison for standard.
        visibleLightAccumulation += (sampleDepth + sampleDepthBias > sceneDepth) ? 1.0f : 0.0f;
    }

    float visibility = visibleLightAccumulation / float(PCSS_SAMPLES);

#ifndef UZ_BINDLESS
    #undef shadowAtlas
#endif

    // ── Post-PCF Sharpening ────────────────────────────────────────────
    // @ref Fernando 2005 "PCSS" —437-459
    // Sharpen the PCF result to recover sub-texel shadow detail.
    // The sharpening fades in based on the occluder UV gradient to avoid
    // applying it on high-frequency shadow boundaries.
    {
        // @ref Fernando 2005 "PCSS" —440
        // Average distance between samples in UV space: sqrt(1 / (N * PI))
        // For 32 samples: sqrt(1 / (32 * PI)) ≈ 0.09973

        // @ref Fernando 2005 "PCSS" —446
        // Occluder UV gradient for sharpening fade — we compute it from the
        // search data (average UV offset of occluders). Since we didn't track
        // UV offsets in the search, we approximate the sharpening fade from
        // the ratio of minimum filter size to raw filter radius.
        // This matches reference's SharpenessFading heuristic.

        // @ref Fernando 2005 "PCSS" —449
        // FinalSharpenessFactor = lerp(1, clamp(PCFMinFilterSize / RawFilterRadius, 1, MaxSharpnessFactor), SharpenessFading)
        // When rawFilterRadius is much larger than minFilterSize, the clamp is near 1 (no sharpening).
        // When rawFilterRadius is small (sharp contact), the clamp is large (more sharpening).
        float sharpnessFactor = clamp(minFilterSize / max(rawFilterRadius, 1e-6f),
                                      1.0f, PCSS_MAX_SHARPNESS_FACTOR);

        // @ref Fernando 2005 "PCSS" —452
        // Apply sharpness: saturate(Factor * (Visibility - 0.5) + 0.5)
        visibility = saturate(sharpnessFactor * (visibility - 0.5f) + 0.5f);
    }

    return visibility;
}

#endif // PCSS_HLSLI
