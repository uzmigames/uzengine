/// PCSS (Percentage Closer Soft Shadows) — UE5 parity implementation.
///
/// Three-stage pipeline:
/// 1. Blocker search (16 Sobol samples) — find average occluder depth
/// 2. Penumbra estimation — compute variable filter radius from blocker distance
/// 3. Adaptive PCF (32 Sobol samples) — filter with estimated radius
///
/// Source: UE5 ShadowPercentageCloserFiltering.ush
///
/// # Prerequisites
/// Requires `ShadowDataCB` with `tanLightSourceAngle` and `maxSoftKernelSize`.
/// Requires shadow atlas texture accessible via bindless or register.

#ifndef PCSS_HLSLI
#define PCSS_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Sobol Low-Discrepancy Sequence (2D)
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a 2D Sobol point from a sample index and scramble seed.
/// Based on UE5's SobolIndex function — bitwise construction of quasi-random
/// points with better coverage than purely random sampling.
float2 SobolSample2D(uint sampleIndex, uint scramble) {
    // Direction numbers for Sobol in 2D
    uint v0 = scramble;
    uint v1 = scramble;

    // Dimension 0
    uint bits0 = sampleIndex;
    uint result0 = 0;
    uint dir0 = 0x80000000u;
    while (bits0 != 0) {
        if ((bits0 & 1u) != 0) result0 ^= dir0;
        dir0 >>= 1;
        bits0 >>= 1;
    }
    result0 ^= v0;

    // Dimension 1 — use Van der Corput for second dimension
    uint bits1 = sampleIndex;
    uint result1 = 0;
    uint dir1 = 0x80000000u;
    // Use different scramble pattern for dimension independence
    bits1 = (bits1 << 16u) | (bits1 >> 16u);
    bits1 = ((bits1 & 0x55555555u) << 1u) | ((bits1 & 0xAAAAAAAAu) >> 1u);
    bits1 = ((bits1 & 0x33333333u) << 2u) | ((bits1 & 0xCCCCCCCCu) >> 2u);
    bits1 = ((bits1 & 0x0F0F0F0Fu) << 4u) | ((bits1 & 0xF0F0F0F0u) >> 4u);
    bits1 = ((bits1 & 0x00FF00FFu) << 8u) | ((bits1 & 0xFF00FF00u) >> 8u);
    result1 = bits1 ^ v1;

    return float2(
        float(result0) * (1.0f / 4294967296.0f),
        float(result1) * (1.0f / 4294967296.0f)
    );
}

/// Maps a [0,1]² point to a unit disk using concentric mapping (Shirley-Chiu).
float2 MapToUnitDisk(float2 sample) {
    float2 offset = sample * 2.0f - 1.0f;

    if (abs(offset.x) < 1e-6f && abs(offset.y) < 1e-6f)
        return float2(0.0f, 0.0f);

    float r, theta;
    if (abs(offset.x) > abs(offset.y)) {
        r = offset.x;
        theta = (3.14159265f / 4.0f) * (offset.y / offset.x);
    } else {
        r = offset.y;
        theta = (3.14159265f / 2.0f) - (3.14159265f / 4.0f) * (offset.x / offset.y);
    }

    float sinT, cosT;
    sincos(theta, sinT, cosT);
    return float2(r * cosT, r * sinT);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 1: Blocker Search
// ─────────────────────────────────────────────────────────────────────────────

/// Searches for average blocker depth in a disk around the sample point.
///
/// # Returns
/// x = average blocker depth, y = blocker count (0 = fully lit)
float2 FindBlockerDepth(float2 atlasUV, float pixelDepth, float searchRadius,
                         float2 cascadeOffset, float2 screenPos, float2 tileSize) {
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    float halfTexel = searchRadius * 0.1f;
    float2 atlasMin = cascadeOffset + halfTexel;
    float2 atlasMax = cascadeOffset + tileSize - halfTexel;

    // Per-pixel scramble from screen position
    uint scramble = uint(screenPos.x * 7.3f + screenPos.y * 97.1f) * 1664525u + 1013904223u;

    float totalBlockerDepth = 0.0f;
    float blockerCount = 0.0f;

    [unroll]
    for (uint i = 0; i < 16; i++) {
        float2 sobol = SobolSample2D(i, scramble);
        float2 diskPt = MapToUnitDisk(sobol);
        float2 sampleUV = clamp(atlasUV + diskPt * searchRadius, atlasMin, atlasMax);

        // Raw depth sample (not comparison) to read blocker depth
        float blockerDepth = shadowAtlas.SampleLevel(linearSampler, sampleUV, 0).r;

        // Shadow maps use standard-Z: closer objects have SMALLER depth.
        if (blockerDepth < pixelDepth) {
            totalBlockerDepth += blockerDepth;
            blockerCount += 1.0f;
        }
    }

#ifndef UZ_BINDLESS
    #undef shadowAtlas
#endif

    return float2(totalBlockerDepth, blockerCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 2: Penumbra Estimation
// ─────────────────────────────────────────────────────────────────────────────

/// Estimates penumbra size from blocker distance and light source angle.
float EstimatePenumbra(float pixelDepth, float avgBlockerDepth,
                        float tanLightAngle, float maxKernelTexels, float texelSize) {
    float penumbraWorld = tanLightAngle * (pixelDepth - avgBlockerDepth);
    float penumbraTexels = penumbraWorld / texelSize;
    return min(penumbraTexels, maxKernelTexels) * texelSize;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage 3: Adaptive PCF
// ─────────────────────────────────────────────────────────────────────────────

/// Performs PCF filtering with an adaptive radius determined by the penumbra.
float AdaptivePCF(float2 atlasUV, float depth, float filterRadius,
                   float2 cascadeOffset, float2 screenPos, float2 tileSize) {
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    float halfTexel = filterRadius * 0.1f;
    float2 atlasMin = cascadeOffset + halfTexel;
    float2 atlasMax = cascadeOffset + tileSize - halfTexel;

    uint scramble = uint(screenPos.x * 13.7f + screenPos.y * 53.3f) * 1664525u + 1013904223u;

    float shadow = 0.0f;

    [unroll]
    for (uint i = 0; i < 32; i++) {
        float2 sobol = SobolSample2D(i, scramble + 256u);
        float2 diskPt = MapToUnitDisk(sobol);
        float2 sampleUV = clamp(atlasUV + diskPt * filterRadius, atlasMin, atlasMax);
        shadow += shadowAtlas.SampleCmpLevelZero(shadowSampler, sampleUV, depth);
    }

#ifndef UZ_BINDLESS
    #undef shadowAtlas
#endif

    return shadow / 32.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Full PCSS Pipeline
// ─────────────────────────────────────────────────────────────────────────────

/// Full PCSS: blocker search → penumbra estimation → adaptive PCF.
/// Returns shadow factor [0,1]: 1.0 = fully lit, 0.0 = fully shadowed.
float SampleShadowPCSS(float2 atlasUV, float depth, float texelSize,
                        float2 cascadeOffset, float2 screenPos, float2 tileSize) {
    // Search radius = maxSoftKernelSize texels (in atlas UV space)
    float searchRadius = maxSoftKernelSize * texelSize;

    // Stage 1: Find blockers
    float2 blockerResult = FindBlockerDepth(atlasUV, depth, searchRadius,
                                             cascadeOffset, screenPos, tileSize);
    float totalBlockerDepth = blockerResult.x;
    float blockerCount      = blockerResult.y;

    // No blockers found = fully lit
    if (blockerCount < 0.5f) return 1.0f;

    float avgBlockerDepth = totalBlockerDepth / blockerCount;

    // Stage 2: Estimate penumbra
    float filterRadius = EstimatePenumbra(depth, avgBlockerDepth,
                                           tanLightSourceAngle,
                                           maxSoftKernelSize, texelSize);

    // Very small penumbra = hard shadow (skip expensive adaptive PCF)
    if (filterRadius < texelSize * 0.5f) {
#ifdef UZ_BINDLESS
        Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
        #define shadowAtlas shadowAtlasTexture
#endif
        float result = shadowAtlas.SampleCmpLevelZero(shadowSampler, atlasUV, depth);
#ifndef UZ_BINDLESS
        #undef shadowAtlas
#endif
        return result;
    }

    // Stage 3: Adaptive PCF with estimated penumbra radius
    return AdaptivePCF(atlasUV, depth, filterRadius, cascadeOffset, screenPos, tileSize);
}

#endif // PCSS_HLSLI
