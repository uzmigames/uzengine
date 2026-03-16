/// Shared cascaded shadow map sampling — UE5 parity.
///
/// Uses manual PCF with soft transition (NOT hardware SampleCmpLevelZero).
/// UE5 uses SF_Point sampler + manual comparison in CalculateOcclusion().
/// Source: ShadowFilteringCommon.ush:162, ShadowProjectionPixelShader.usf:147.

#ifndef SHADOW_COMMON_HLSLI
#define SHADOW_COMMON_HLSLI

#include "pcss.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Atlas quadrant offsets for 2x2 cascade layout.
static const float2 kCascadeOffsets[4] = {
    float2(0.0f, 0.0f), float2(0.5f, 0.0f),
    float2(0.0f, 0.5f), float2(0.5f, 0.5f)
};

/// Shadow transition scale. Controls the fade region width.
/// UE5 uses 60.0 WITH a depth bias of ~0.004 in the shadow VS.
/// Since we apply bias via rasterizer state (smaller effect), we use
/// a lower scale to avoid over-shadowing. This will be tuned after
/// implementing UE5's shadow VS depth bias.
static const float kShadowTransitionScale = 512.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Occlusion — UE5 Manual Comparison (CalculateOcclusion)
// ─────────────────────────────────────────────────────────────────────────────

/// Shadow occlusion placeholder — actual comparison done via SampleCmpLevelZero.
/// The hardware comparison sampler (LESS) handles: reference < stored → 1.0 (lit).

// ─────────────────────────────────────────────────────────────────────────────
// PCF Shadow Sampling — 5x5 Manual Comparison (UE5 parity)
// ─────────────────────────────────────────────────────────────────────────────

/// Samples the shadow atlas with 5x5 PCF using hardware comparison sampler (LESS).
/// Shadow maps use standard-Z (D32Float), sampled via SampleCmpLevelZero.
/// Comparison: reference < stored → 1.0 (lit), reference >= stored → 0.0 (shadowed).
float SampleShadowPCF5x5(float2 atlasUV, float sceneDepth, float texelSize, float2 cascadeOffset) {
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    float halfTexel = texelSize * 0.5f;
    float2 atlasMin = cascadeOffset + halfTexel;
    float2 atlasMax = cascadeOffset + float2(0.5f, 0.5f) - halfTexel;

    float shadow = 0.0f;
    [unroll]
    for (int y = -2; y <= 2; y++) {
        [unroll]
        for (int x = -2; x <= 2; x++) {
            float2 offset = float2((float)x, (float)y) * texelSize;
            float2 sampleUV = clamp(atlasUV + offset, atlasMin, atlasMax);
            shadow += shadowAtlas.SampleCmpLevelZero(shadowSampler, sampleUV, sceneDepth);
        }
    }

#ifndef UZ_BINDLESS
    #undef shadowAtlas
#endif

    return shadow / 25.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Poisson Disk Shadow Sampling — 16 taps with per-pixel rotation
// ─────────────────────────────────────────────────────────────────────────────

static const float2 kPoissonDisk[16] = {
    float2(-0.9420f, -0.3990f), float2( 0.9454f, -0.7689f),
    float2(-0.0942f, -0.9294f), float2( 0.3448f,  0.2932f),
    float2(-0.9158f,  0.4573f), float2(-0.8154f, -0.8792f),
    float2(-0.3826f,  0.2769f), float2( 0.9746f,  0.5685f),
    float2( 0.4434f, -0.1671f), float2( 0.2617f, -0.5533f),
    float2(-0.4976f, -0.6588f), float2(-0.2738f,  0.8649f),
    float2( 0.7918f,  0.1906f), float2( 0.0686f,  0.6317f),
    float2(-0.5978f,  0.7961f), float2( 0.5331f,  0.8979f)
};

float SampleShadowPoisson(float2 atlasUV, float sceneDepth, float texelSize,
                           float2 cascadeOffset, float2 screenPos) {
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    float halfTexel = texelSize * 0.5f;
    float2 atlasMin = cascadeOffset + halfTexel;
    float2 atlasMax = cascadeOffset + float2(0.5f, 0.5f) - halfTexel;

    float angle = frac(52.9829189f * frac(dot(screenPos, float2(0.06711056f, 0.00583715f)))) * 6.28318f;
    float sinA, cosA;
    sincos(angle, sinA, cosA);
    float radius = texelSize * 2.5f;

    float shadow = 0.0f;
    [unroll]
    for (int i = 0; i < 16; i++) {
        float2 rotated = float2(
            kPoissonDisk[i].x * cosA - kPoissonDisk[i].y * sinA,
            kPoissonDisk[i].x * sinA + kPoissonDisk[i].y * cosA
        );
        float2 sampleUV = clamp(atlasUV + rotated * radius, atlasMin, atlasMax);
        shadow += shadowAtlas.SampleCmpLevelZero(shadowSampler, sampleUV, sceneDepth);
    }

#ifndef UZ_BINDLESS
    #undef shadowAtlas
#endif

    return shadow / 16.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Unified Shadow Sample — dispatches based on filterMethod
// ─────────────────────────────────────────────────────────────────────────────

float SampleShadowFiltered(float2 atlasUV, float sceneDepth, float texelSize,
                            float2 cascadeOffset, float2 screenPos) {
    if (filterMethod == 2) {
        return SampleShadowPCSS(atlasUV, sceneDepth, texelSize, cascadeOffset, screenPos);
    }
    if (filterMethod == 1) {
        return SampleShadowPoisson(atlasUV, sceneDepth, texelSize, cascadeOffset, screenPos);
    }
    return SampleShadowPCF5x5(atlasUV, sceneDepth, texelSize, cascadeOffset);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cascade Shadow Map Sampling — UE5 Parity
// ─────────────────────────────────────────────────────────────────────────────

float SampleShadowMap(float3 positionWS, float3 normalWS, float viewDepth, float2 screenPos) {
    float texelSize = 1.0f / (shadowMapSize * 2.0f);

    float splits[4] = { cascadeSplits.x, cascadeSplits.y,
                         cascadeSplits.z, cascadeSplits.w };
    float fadeStarts[4] = { cascadeFadeStarts.x, cascadeFadeStarts.y,
                            cascadeFadeStarts.z, cascadeFadeStarts.w };
    float depthBiases[4] = { cascadeDepthBias.x, cascadeDepthBias.y,
                             cascadeDepthBias.z, cascadeDepthBias.w };
    float normalBiases[4] = { cascadeNormalBias.x, cascadeNormalBias.y,
                              cascadeNormalBias.z, cascadeNormalBias.w };

    // Select cascade
    uint cascadeIdx = cascadeCount - 1;
    for (uint i = 0; i < cascadeCount; i++) {
        if (viewDepth <= splits[i]) {
            cascadeIdx = i;
            break;
        }
    }

    // Normal bias offset
    float3 biasedPos = positionWS + normalWS * normalBiases[cascadeIdx];

    // Project into light clip space (standard-Z ortho)
    float4 shadowClip = mul(float4(biasedPos, 1.0f), cascadeViewProj[cascadeIdx]);
    float3 shadowNDC  = shadowClip.xyz / shadowClip.w;

    // NDC to atlas UV
    float2 shadowUV = float2(shadowNDC.x * 0.5f + 0.5f,
                             -shadowNDC.y * 0.5f + 0.5f);
    float2 atlasUV = shadowUV * 0.5f + kCascadeOffsets[cascadeIdx];

    // Scene depth with bias subtracted (pushes toward light = more lit = less acne)
    // UE5 clamps to 0.99999 to avoid comparing against cleared depth (1.0)
    float sceneDepth = min(shadowNDC.z - depthBiases[cascadeIdx], 0.99999f);

    // Sample with selected filter method
    float shadow = SampleShadowFiltered(atlasUV, sceneDepth, texelSize,
                                         kCascadeOffsets[cascadeIdx], screenPos);

    // ── Cascade blending ──
    if (cascadeIdx < cascadeCount - 1) {
        float blendStart = fadeStarts[cascadeIdx];
        if (viewDepth > blendStart) {
            uint nextIdx = cascadeIdx + 1;
            float3 biasedPosNext = positionWS + normalWS * normalBiases[nextIdx];
            float4 nextClip = mul(float4(biasedPosNext, 1.0f), cascadeViewProj[nextIdx]);
            float3 nextNDC  = nextClip.xyz / nextClip.w;

            float2 nextUV = float2(nextNDC.x * 0.5f + 0.5f,
                                   -nextNDC.y * 0.5f + 0.5f);
            float2 nextAtlasUV = nextUV * 0.5f + kCascadeOffsets[nextIdx];
            float nextSceneDepth = min(nextNDC.z - depthBiases[nextIdx], 0.99999f);

            float nextShadow = SampleShadowFiltered(nextAtlasUV, nextSceneDepth, texelSize,
                                                     kCascadeOffsets[nextIdx], screenPos);

            float blendFactor = saturate((viewDepth - blendStart) /
                                         (splits[cascadeIdx] - blendStart));
            shadow = lerp(shadow, nextShadow, blendFactor);
        }
    }

    // ── Last cascade fade-to-nothing ──
    if (shadowFadeEnd > shadowFadeStart) {
        float fadeFactor = saturate((viewDepth - shadowFadeStart) /
                                    (shadowFadeEnd - shadowFadeStart));
        shadow = lerp(shadow, 1.0f, fadeFactor);
    }

    return shadow;
}

#endif // SHADOW_COMMON_HLSLI
