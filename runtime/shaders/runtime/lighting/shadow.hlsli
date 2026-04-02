/// UzEngine CSM shadow sampling — 16-tap Poisson PCF + cascade blending.
/// @ref Engel 2006 "Cascaded Shadow Maps" (ShaderX5)
/// @ref Donnelly & Lauritzen 2006 "Variance Shadow Maps" — PCF
/// @ref Jimenez 2016 — normal-offset bias
/// @ref Fernando 2005 "Percentage-Closer Soft Shadows" — PCSS
/// @ref Annen et al. 2008 "Exponential Shadow Maps" — ESM
/// @ref Jimenez et al. 2016 "Practical Real-Time Strategies for Accurate Indirect Occlusion" — IGN

#ifndef UZ_SHADOW_HLSLI
#define UZ_SHADOW_HLSLI

static const float2 kPoissonDisk[16] = {
    float2(-0.94201624, -0.39906216), float2( 0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870), float2( 0.34495938,  0.29387760),
    float2(-0.91588581,  0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543,  0.27676845), float2( 0.97484398,  0.75648379),
    float2( 0.44323325, -0.97511554), float2( 0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2( 0.79197514,  0.19090188),
    float2(-0.24188840,  0.99706507), float2(-0.81409955,  0.91437590),
    float2( 0.19984126,  0.78641367), float2( 0.14383161, -0.14100790),
};

static const uint kMaxCascades = 8;

// ─── ShadowDataCB at b4 — packed float4 arrays to match C++ layout ────────

cbuffer ShadowDataCB : register(b4) {
    row_major float4x4 cascadeViewProj[8];    // 512B
    float4 cascadeSplitsPacked[2];             // 32B (8 floats as 2×float4)
    float4 cascadeFadeStartsPacked[2];         // 32B
    float4 cascadeDepthBiasPacked[2];          // 32B
    float4 cascadeNormalBiasPacked[2];         // 32B
    uint   shadowAtlasIndex;
    uint   cascadeCount;
    float  shadowMapSize;
    float  receiverBias;
    float  shadowFadeStart;
    float  shadowFadeEnd;
    uint   filterMethod;
    float  tanLightSourceAngle;
    float  maxSoftKernelSize;
    uint   atlasColumns;
    uint   staticShadowAtlasIndex;
    uint   useShadowCache;
    uint   dfsEnabled;
    uint   dfsOutputSrvIndex;
    uint   localShadowAtlasIndex;
    uint   localShadowCount;
    float4 _shadowPad[4];                      // pad to 768
    float4 localShadowRaw[80];                 // 1280B (16 entries × 5 float4s)
    float4 _shadowPad3[16];                    // pad to 2304
};

float GetCascadeSplit(uint i)      { return cascadeSplitsPacked[i >> 2][i & 3]; }
float GetCascadeFadeStart(uint i)  { return cascadeFadeStartsPacked[i >> 2][i & 3]; }
float GetCascadeDepthBias(uint i)  { return cascadeDepthBiasPacked[i >> 2][i & 3]; }
float GetCascadeNormalBias(uint i) { return cascadeNormalBiasPacked[i >> 2][i & 3]; }

SamplerComparisonState shadowSampler : register(s1);

// ─── Per-pixel Poisson disk rotation ─────────────────────────────────────

/// @ref Jimenez et al. 2016 — Interleaved Gradient Noise
/// Returns a pseudo-random value in [0, 1) unique per pixel + frame.
#ifndef UZ_COMMON_MATH_HLSLI
float InterleavedGradientNoise_Shadow(float2 pixelPos, uint frameIndex) {
    pixelPos += float(frameIndex) * float2(5.588238, 5.588238);
    return frac(52.9829189 * frac(0.06711056 * pixelPos.x + 0.00583715 * pixelPos.y));
}
#define IGN_SHADOW(p, f) InterleavedGradientNoise_Shadow(p, f)
#else
#define IGN_SHADOW(p, f) InterleavedGradientNoise(p, f)
#endif

/// @ref Jimenez et al. 2016 — Interleaved Gradient Noise for per-pixel rotation.
/// Rotates Poisson disk samples by a unique angle per pixel to eliminate
/// visible banding on shadow edges.
float2 RotatePoissonSample(float2 sample, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return float2(sample.x * c - sample.y * s,
                  sample.x * s + sample.y * c);
}

// ─── PCSS Blocker Search ─────────────────────────────────────────────────

/// @ref Fernando 2005 "Percentage-Closer Soft Shadows" — blocker search
/// Searches a region for occluding texels and returns their average depth.
/// The search radius is derived from the light source angular size.
///
/// @param atlasUV      Shadow atlas UV for the receiver.
/// @param receiverDepth Receiver's projected depth.
/// @param searchRadius  Search radius in atlas UV space.
/// @param shadowAtlas   Shadow depth texture.
/// @param atlasMin/Max  Tile bounds to clamp UV.
/// @param rotAngle      Per-pixel rotation angle for noise.
/// @return x = average blocker depth, y = blocker count (0 = no blockers = fully lit)
float2 BlockerSearchPCSS(float2 atlasUV, float receiverDepth, float searchRadius,
                          Texture2D<float> shadowAtlas, float atlasSize,
                          float2 atlasMin, float2 atlasMax, float rotAngle) {
    float blockerSum  = 0.0;
    float blockerCount = 0.0;

    [unroll] for (uint i = 0; i < 16; ++i) {
        float2 offset = RotatePoissonSample(kPoissonDisk[i], rotAngle) * searchRadius;
        float2 sUV = clamp(atlasUV + offset, atlasMin, atlasMax);
        int2 loadPx = int2(sUV * atlasSize);
        float sampledDepth = shadowAtlas.Load(int3(loadPx, 0));

        // Blocker = texel closer to light than receiver
        if (sampledDepth < receiverDepth) {
            blockerSum += sampledDepth;
            blockerCount += 1.0;
        }
    }

    return float2(blockerCount > 0.0 ? blockerSum / blockerCount : 0.0,
                  blockerCount);
}

/// @ref Fernando 2005 — PCSS penumbra estimation from blocker depth
/// @param receiverDepth Receiver's projected depth.
/// @param avgBlockerDepth Average depth of found blockers.
/// @param lightSize Light source size (in shadow map UV units).
/// @return Penumbra radius (in texels) for PCF filter.
float EstimatePenumbraPCSS(float receiverDepth, float avgBlockerDepth, float lightSize) {
    float penumbra = lightSize * (receiverDepth - avgBlockerDepth) / max(avgBlockerDepth, 1e-6);
    return clamp(penumbra, 0.5, 16.0); // clamp to reasonable range
}

// ─── Exponential Shadow Map (ESM) ────────────────────────────────────────

/// @ref Annen et al. 2008 "Exponential Shadow Maps"
/// @constants c = 80.0 (exponential warp constant, higher = sharper but more artifacts)
///
/// ESM provides soft shadow edges without multiple depth comparisons.
/// Uses a single texture lookup + exponential depth difference.
/// The shadow map must be pre-blurred for soft penumbra.
float SampleShadowESM(float2 atlasUV, float receiverDepth,
                       Texture2D<float> shadowAtlas, float atlasSize,
                       float2 atlasMin, float2 atlasMax) {
    float2 sUV = clamp(atlasUV, atlasMin, atlasMax);
    int2 loadPx = int2(sUV * atlasSize);
    float occluderDepth = shadowAtlas.Load(int3(loadPx, 0));

    const float c = 80.0;
    return saturate(exp(-c * (receiverDepth - occluderDepth)));
}

// ─── Atlas tile helpers ───────────────────────────────────────────────────

float2 GetCascadeTileOffset(uint idx, uint cols, out float2 tileSize) {
    float invCols = 1.0 / float(cols);
    tileSize = float2(invCols, invCols);
    return float2(float(idx % cols) * invCols, float(idx / cols) * invCols);
}

// ─── Single cascade PCF ──────────────────────────────────────────────────

/// @param pixelPos Screen-space pixel position (for per-pixel noise).
/// @param frameIdx Current frame index (for temporal noise variation).
float SampleShadowCascade(float3 worldPos, float3 N, float NdotL, uint cascadeIdx,
                          Texture2D<float> shadowAtlas, SamplerComparisonState smplr,
                          float2 pixelPos, uint frameIdx) {
    float normalBias = GetCascadeNormalBias(cascadeIdx);
    float slopeScale = clamp(tan(acos(max(NdotL, 0.001))), 0.0, 10.0);
    float3 biasedPos = worldPos + N * normalBias * slopeScale;

    float4 lsp = mul(float4(biasedPos, 1.0), cascadeViewProj[cascadeIdx]);
    float3 proj = lsp.xyz / lsp.w;
    if (proj.z < 0.0 || proj.z > 1.0) return 1.0;

    float2 shadowUV = proj.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;

    float2 tileSize;
    float2 tileOffset = GetCascadeTileOffset(cascadeIdx, atlasColumns, tileSize);
    float2 atlasUV = tileOffset + shadowUV * tileSize;
    if (any(shadowUV < 0.0) || any(shadowUV > 1.0)) return 1.0;

    // Depth bias: use CB value with a minimum floor to prevent shadow acne.
    // @ref Holbert 2011 "Normal Offset Shadows" — constant + slope-scaled bias
    float depthBias = max(GetCascadeDepthBias(cascadeIdx), 0.0005);
    // Slope-scaled depth bias: surfaces at grazing angles need more bias
    float slopeBias = depthBias * slopeScale;
    float cmpDepth = proj.z - depthBias - slopeBias;
    float texelSize = 1.0 / shadowMapSize;
    // Consistent penumbra across all cascades — constant texel radius.
    // Each cascade covers different world area but the PCF radius in texels
    // stays the same, producing uniform softness regardless of distance.
    float penumbra = 2.0;

    float halfTexel = texelSize * 0.5;
    float2 atlasMin = tileOffset + halfTexel;
    float2 atlasMax = tileOffset + tileSize - halfTexel;

    // Per-pixel Poisson disk rotation via IGN to eliminate shadow edge banding
    /// @ref Jimenez et al. 2016 — Interleaved Gradient Noise rotation
    float rotAngle = IGN_SHADOW(pixelPos, frameIdx) * 6.28318530718;

    // PCSS: variable penumbra based on blocker depth (when filterMethod == 1)
    float filterPenumbra = penumbra;
    if (filterMethod == 1 && tanLightSourceAngle > 0.0) {
        float searchRadius = maxSoftKernelSize * texelSize;
        float2 blockerResult = BlockerSearchPCSS(atlasUV, cmpDepth, searchRadius,
                                                  shadowAtlas, shadowMapSize,
                                                  atlasMin, atlasMax, rotAngle);
        if (blockerResult.y > 0.0) {
            filterPenumbra = EstimatePenumbraPCSS(cmpDepth, blockerResult.x, tanLightSourceAngle);
            filterPenumbra = clamp(filterPenumbra, 0.5, maxSoftKernelSize);
        } else {
            return 1.0; // No blockers found — fully lit
        }
    }

    // ESM mode (filterMethod == 2)
    if (filterMethod == 2) {
        return SampleShadowESM(atlasUV, cmpDepth, shadowAtlas, shadowMapSize, atlasMin, atlasMax);
    }

    // Standard PCF with rotated Poisson disk
    float shadow = 0.0;
    [unroll] for (uint i = 0; i < 16; ++i) {
        float2 rotated = RotatePoissonSample(kPoissonDisk[i], rotAngle);
        float2 offset = rotated * filterPenumbra * texelSize;
        float2 sUV = clamp(atlasUV + offset, atlasMin, atlasMax);
        shadow += shadowAtlas.SampleCmpLevelZero(smplr, sUV, cmpDepth);
    }
    return shadow / 16.0;
}

// ─── Full cascade shadow with blending + distance fade ────────────────────

float CascadeShadow(float3 worldPos, float viewDepth) {
    if (cascadeCount == 0) return 1.0;

    float NdotL = 0.5; // conservative default

#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    float3 N = float3(0, 0, 1); // placeholder — caller should use SampleShadowMapFull for accurate bias

    uint sel = cascadeCount - 1;
    [unroll] for (uint c = 0; c < kMaxCascades; ++c) {
        if (c >= cascadeCount) break;
        if (viewDepth < GetCascadeSplit(c)) { sel = c; break; }
    }

    float2 defaultPixelPos = float2(0, 0);
    uint   defaultFrameIdx = 0;
    float shadow = SampleShadowCascade(worldPos, N, NdotL, sel, shadowAtlas, shadowSampler, defaultPixelPos, defaultFrameIdx);

    if (sel < cascadeCount - 1) {
        float splitDist = GetCascadeSplit(sel);
        float fadeStart = GetCascadeFadeStart(sel);
        float blendRange = splitDist - fadeStart;
        if (blendRange > 0.0 && viewDepth > fadeStart) {
            float bf = saturate((viewDepth - fadeStart) / blendRange);
            float next = SampleShadowCascade(worldPos, N, NdotL, sel + 1, shadowAtlas, shadowSampler, defaultPixelPos, defaultFrameIdx);
            shadow = lerp(shadow, next, bf);
        }
    }

    if (shadowFadeEnd > shadowFadeStart && viewDepth > shadowFadeStart) {
        float ff = saturate((viewDepth - shadowFadeStart) / (shadowFadeEnd - shadowFadeStart));
        shadow = lerp(shadow, 1.0, ff);
    }

#ifndef UZ_BINDLESS
    #undef shadowAtlas
#endif
    return shadow;
}

#endif
