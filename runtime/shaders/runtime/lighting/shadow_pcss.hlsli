/// UzEngine PCSS + ESM + IGN Shadow Extensions
///
/// @ref Fernando 2005 "Percentage-Closer Soft Shadows" — PCSS
/// @ref Annen et al. 2008 "Exponential Shadow Maps" — ESM
/// @ref Jimenez et al. 2016 "Practical Real-Time Strategies for Accurate Indirect Occlusion" — IGN
///
/// Standalone shadow quality extensions. Include AFTER shadow.hlsli.
/// These functions do NOT modify the existing SampleShadowCascade or CascadeShadow.
/// Usage: call SampleShadowCascadePCSS() instead of SampleShadowCascade() when
/// filterMethod > 0 in ShadowDataCB.

#ifndef UZ_SHADOW_PCSS_HLSLI
#define UZ_SHADOW_PCSS_HLSLI

#include "shadow.hlsli"

// ─── Interleaved Gradient Noise ──────────────────────────────────────────

/// @ref Jimenez et al. 2016 — Interleaved Gradient Noise
/// @constants Coefficients from the original paper
float InterleavedGradientNoise_Shadow(float2 pixelPos, uint frameIndex) {
    pixelPos += float(frameIndex) * float2(5.588238, 5.588238);
    return frac(52.9829189 * frac(0.06711056 * pixelPos.x + 0.00583715 * pixelPos.y));
}

/// Rotates a 2D sample by an angle (radians).
float2 RotatePoissonSample(float2 s, float angle) {
    float sn = sin(angle);
    float cs = cos(angle);
    return float2(s.x * cs - s.y * sn, s.x * sn + s.y * cs);
}

// ─── PCSS Blocker Search ─────────────────────────────────────────────────

/// @ref Fernando 2005 "Percentage-Closer Soft Shadows" — blocker search
/// @return x = average blocker depth, y = blocker count (0 = no blockers = fully lit)
float2 BlockerSearchPCSS(float2 atlasUV, float receiverDepth, float searchRadius,
                          Texture2D<float> atlas, float atlasSize,
                          float2 atlasMin, float2 atlasMax, float rotAngle) {
    float blockerSum   = 0.0;
    float blockerCount = 0.0;

    [unroll] for (uint i = 0; i < 16; ++i) {
        float2 offset = RotatePoissonSample(kPoissonDisk[i], rotAngle) * searchRadius;
        float2 sUV = clamp(atlasUV + offset, atlasMin, atlasMax);
        float sampledDepth = atlas.Load(int3(int2(sUV * atlasSize), 0));

        if (sampledDepth < receiverDepth) {
            blockerSum += sampledDepth;
            blockerCount += 1.0;
        }
    }

    return float2(blockerCount > 0.0 ? blockerSum / blockerCount : 0.0, blockerCount);
}

/// @ref Fernando 2005 — penumbra estimation from blocker depth
float EstimatePenumbraPCSS(float receiverDepth, float avgBlockerDepth, float lightSize) {
    float penumbra = lightSize * (receiverDepth - avgBlockerDepth) / max(avgBlockerDepth, 1e-6);
    float pcssMin = (shadowPCSSMinPenumbra > 0.01) ? shadowPCSSMinPenumbra : 0.5;
    float pcssMax = (shadowPCSSMaxPenumbra > 0.01) ? shadowPCSSMaxPenumbra : 16.0;
    return clamp(penumbra, pcssMin, pcssMax);
}

// ─── Exponential Shadow Map (ESM) ────────────────────────────────────────

/// @ref Annen et al. 2008 "Exponential Shadow Maps"
/// @constants c = 80.0 (exponential warp constant)
float SampleShadowESM(float2 atlasUV, float receiverDepth,
                       Texture2D<float> atlas, float atlasSize,
                       float2 atlasMin, float2 atlasMax) {
    float2 sUV = clamp(atlasUV, atlasMin, atlasMax);
    float occluderDepth = atlas.Load(int3(int2(sUV * atlasSize), 0));
    float esmC = (shadowESMExponent > 1.0) ? shadowESMExponent : 80.0;
    return saturate(exp(-esmC * (receiverDepth - occluderDepth)));
}

// ─── Enhanced Shadow Cascade Sampling ────────────────────────────────────

/// Drop-in replacement for SampleShadowCascade with PCSS/ESM/IGN support.
/// filterMethod: 0 = PCF with IGN rotation, 1 = PCSS, 2 = ESM.
/// Call this instead of SampleShadowCascade when you want enhanced shadows.
///
/// Like the plain PCF variant, this returns the sentinel `-1.0` when
/// the pixel falls outside the cascade's frustum. Callers MUST walk
/// to the next cascade on miss — returning `1.0` causes sun leak
/// (Sponza bug 2026-04-10).
float SampleShadowCascadePCSS(float3 worldPos, float3 N, float NdotL, uint cascadeIdx,
                               Texture2D<float> shadowAtlas, SamplerComparisonState smplr,
                               float2 pixelPos, uint frameIdx) {
    float normalBias = GetCascadeNormalBias(cascadeIdx);
    float slopeScale = clamp(tan(acos(max(NdotL, 0.001))), 0.0, 10.0);
    float3 biasedPos = worldPos + N * normalBias * slopeScale;

    float4 lsp = mul(float4(biasedPos, 1.0), cascadeViewProj[cascadeIdx]);
    float3 proj = lsp.xyz / lsp.w;
    if (proj.z < 0.0 || proj.z > 1.0) return -1.0;

    float2 shadowUV = proj.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;

    float2 tileSize;
    float2 tileOffset = GetCascadeTileOffset(cascadeIdx, atlasColumns, tileSize);
    float2 atlasUV = tileOffset + shadowUV * tileSize;
    if (any(shadowUV < 0.0) || any(shadowUV > 1.0)) return -1.0;

    float depthBias = max(GetCascadeDepthBias(cascadeIdx), 0.0005);
    float slopeBias = depthBias * slopeScale;
    float cmpDepth  = proj.z - depthBias - slopeBias;
    float texelSize = 1.0 / shadowMapSize;
    float penumbra  = 2.0;

    float halfTexel = texelSize * 0.5;
    float2 atlasMin = tileOffset + halfTexel;
    float2 atlasMax = tileOffset + tileSize - halfTexel;

    // Per-pixel rotation angle
    float rotAngle = InterleavedGradientNoise_Shadow(pixelPos, frameIdx) * 6.28318530718;

    // PCSS variable penumbra (filterMethod == 1)
    float filterPenumbra = penumbra;
    if (filterMethod == 1 && tanLightSourceAngle > 0.0) {
        float searchRadius = maxSoftKernelSize * texelSize;
        float2 blocker = BlockerSearchPCSS(atlasUV, cmpDepth, searchRadius,
                                            shadowAtlas, shadowMapSize,
                                            atlasMin, atlasMax, rotAngle);
        if (blocker.y > 0.0) {
            filterPenumbra = EstimatePenumbraPCSS(cmpDepth, blocker.x, tanLightSourceAngle);
            filterPenumbra = clamp(filterPenumbra, 0.5, maxSoftKernelSize);
        } else {
            return 1.0;
        }
    }

    // ESM mode (filterMethod == 2)
    if (filterMethod == 2) {
        return SampleShadowESM(atlasUV, cmpDepth, shadowAtlas, shadowMapSize, atlasMin, atlasMax);
    }

    // PCF with rotated Poisson disk
    float shadow = 0.0;
    [unroll] for (uint i = 0; i < 16; ++i) {
        float2 rotated = RotatePoissonSample(kPoissonDisk[i], rotAngle);
        float2 offset  = rotated * filterPenumbra * texelSize;
        float2 sUV = clamp(atlasUV + offset, atlasMin, atlasMax);
        shadow += shadowAtlas.SampleCmpLevelZero(smplr, sUV, cmpDepth);
    }
    return shadow / 16.0;
}

// ─── Full cascade shadow with PCSS/ESM (mirrors CascadeShadow) ───────────

/// Drop-in replacement for CascadeShadow() with PCSS/ESM/IGN support.
/// Uses filterMethod from ShadowDataCB to select algorithm.
///
/// Implements the same cascade-walk fallback as CascadeShadow() —
/// when the depth-selected cascade misses, we walk outward through
/// larger cascades until one covers the pixel. Critical to prevent
/// the "sun leaks indoors on camera rotation" Sponza bug.
float CascadeShadowPCSS(float3 worldPos, float viewDepth, float3 N, float NdotL,
                         float2 pixelPos, uint frameIdx) {
    if (cascadeCount == 0) return 1.0;

#ifdef UZ_BINDLESS
    Texture2D<float> sa = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define sa shadowAtlasTexture
#endif

    // Depth-based starting cascade (tightest that contains this depth).
    uint startCascade = cascadeCount - 1;
    [unroll] for (uint c = 0; c < kMaxCascades; ++c) {
        if (c >= cascadeCount) break;
        if (viewDepth < GetCascadeSplit(c)) { startCascade = c; break; }
    }

    // Walk outward until a cascade covers the pixel. Sentinel `< 0` = miss.
    float shadow = -1.0;
    uint  sel    = startCascade;
    [unroll] for (uint step = 0; step < kMaxCascades; ++step) {
        uint idx = startCascade + step;
        if (idx >= cascadeCount) break;
        float s = SampleShadowCascadePCSS(worldPos, N, NdotL, idx, sa, shadowSampler,
                                          pixelPos, frameIdx);
        if (s >= 0.0) { shadow = s; sel = idx; break; }
    }

    // If EVERY cascade missed, pixel is outside CSM range — fall back
    // to fully lit (DFS handles very distant shadows if enabled).
    if (shadow < 0.0) {
        shadow = 1.0;
    }

    // Split-boundary blend when near the far edge of the selected cascade.
    if (sel < cascadeCount - 1) {
        float splitDist = GetCascadeSplit(sel);
        float fadeStart = GetCascadeFadeStart(sel);
        float blendRange = splitDist - fadeStart;
        if (blendRange > 0.0 && viewDepth > fadeStart) {
            float bf = saturate((viewDepth - fadeStart) / blendRange);
            float next = SampleShadowCascadePCSS(worldPos, N, NdotL, sel + 1, sa, shadowSampler,
                                                  pixelPos, frameIdx);
            if (next >= 0.0) {
                shadow = lerp(shadow, next, bf);
            }
        }
    }

    if (shadowFadeEnd > shadowFadeStart && viewDepth > shadowFadeStart) {
        float ff = saturate((viewDepth - shadowFadeStart) / (shadowFadeEnd - shadowFadeStart));
        shadow = lerp(shadow, 1.0, ff);
    }

#ifndef UZ_BINDLESS
    #undef sa
#endif
    return shadow;
}

#endif // UZ_SHADOW_PCSS_HLSLI
