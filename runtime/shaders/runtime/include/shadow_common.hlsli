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

/// Computes the atlas UV offset for a given cascade index.
///
/// The atlas is laid out as a dynamic NxM grid. The number of columns is
/// passed from the CPU via `atlasColumns` in ShadowDataCB. Each cell
/// occupies `(1/cols, 1/rows)` of the atlas in UV space.
///
/// # Parameters
/// - idx: cascade index (0 to cascadeCount-1)
/// - cols: number of columns in the atlas grid (from ShadowDataCB::atlasColumns)
/// - totalCascades: total cascade count (from ShadowDataCB::cascadeCount)
///
/// # Returns
/// UV offset to the top-left corner of the cascade tile.
float2 GetCascadeOffset(uint idx, uint cols, uint totalCascades) {
    uint rows = (totalCascades + cols - 1) / cols;
    float colSize = 1.0f / float(cols);
    float rowSize = 1.0f / float(rows);
    return float2(float(idx % cols) * colSize, float(idx / cols) * rowSize);
}

/// Computes the UV-space size of a single cascade tile in the atlas.
///
/// # Parameters
/// - cols: number of columns in the atlas grid
/// - totalCascades: total cascade count
///
/// # Returns
/// UV-space width and height of one cascade tile.
float2 GetCascadeTileSize(uint cols, uint totalCascades) {
    uint rows = (totalCascades + cols - 1) / cols;
    return float2(1.0f / float(cols), 1.0f / float(rows));
}

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
///
/// Depth space: shadow maps use standard-Z (0=near, 1=far, cleared to 1.0).
/// The sceneDepth parameter is NOT the main-view reversed-Z depth — it is the world
/// position reprojected into light-space via cascadeViewProj (standard-Z ortho).
/// Both reference (sceneDepth) and stored (shadow texel) are in the same standard-Z space.
/// Comparison: reference < stored → 1.0 (lit), reference >= stored → 0.0 (shadowed).
float SampleShadowPCF5x5(float2 atlasUV, float sceneDepth, float texelSize, float2 cascadeOffset, float2 tileSize) {
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    float halfTexel = texelSize * 0.5f;
    float2 atlasMin = cascadeOffset + halfTexel;
    float2 atlasMax = cascadeOffset + tileSize - halfTexel;

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
                           float2 cascadeOffset, float2 screenPos, float2 tileSize) {
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    float halfTexel = texelSize * 0.5f;
    float2 atlasMin = cascadeOffset + halfTexel;
    float2 atlasMax = cascadeOffset + tileSize - halfTexel;

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
                            float2 cascadeOffset, float2 screenPos, float2 tileSize) {
    if (filterMethod == 2) {
        return SampleShadowPCSS(atlasUV, sceneDepth, texelSize, cascadeOffset, screenPos, tileSize);
    }
    if (filterMethod == 1) {
        return SampleShadowPoisson(atlasUV, sceneDepth, texelSize, cascadeOffset, screenPos, tileSize);
    }
    return SampleShadowPCF5x5(atlasUV, sceneDepth, texelSize, cascadeOffset, tileSize);
}

// ─────────────────────────────────────────────────────────────────────────────
// Composited Shadow Sampling — static cache + dynamic atlas
// ─────────────────────────────────────────────────────────────────────────────

/// Samples a shadow atlas at the given UV with the selected filter method.
///
/// This variant takes an explicit atlas SRV index instead of using the global
/// `shadowAtlasIndex`. Used by the compositing path to sample both the dynamic
/// and static cached shadow atlases independently.
///
/// # Parameters
/// - atlasSrvIndex: SRV descriptor heap index for the shadow atlas to sample.
/// - atlasUV: UV coordinates within the atlas tile.
/// - sceneDepth: Light-space depth of the surface (standard-Z, biased).
/// - texelSize: Size of one texel in atlas UV space.
/// - cascadeOffset: UV offset to the cascade tile origin.
/// - screenPos: Screen-space pixel position (for Poisson rotation).
/// - tileSize: UV-space size of one cascade tile.
float SampleShadowFilteredFromAtlas(uint atlasSrvIndex, float2 atlasUV, float sceneDepth,
                                     float texelSize, float2 cascadeOffset, float2 screenPos,
                                     float2 tileSize) {
    // Reuse the existing filter functions which read from `shadowAtlasIndex`.
    // Since we need to sample a specific atlas, we use the bindless path directly.
#ifdef UZ_BINDLESS
    Texture2D<float> atlas = ResourceDescriptorHeap[atlasSrvIndex];

    float halfTexel = texelSize * 0.5f;
    float2 atlasMin = cascadeOffset + halfTexel;
    float2 atlasMax = cascadeOffset + tileSize - halfTexel;

    // Simple 5x5 PCF for the static cache — matches the default filter.
    // Using only PCF for the static atlas keeps the compositing path simple and fast.
    // The dynamic atlas uses the full filter selection (PCF/Poisson/PCSS).
    float shadow = 0.0f;
    [unroll]
    for (int y = -2; y <= 2; y++) {
        [unroll]
        for (int x = -2; x <= 2; x++) {
            float2 offset = float2((float)x, (float)y) * texelSize;
            float2 sampleUV = clamp(atlasUV + offset, atlasMin, atlasMax);
            shadow += atlas.SampleCmpLevelZero(shadowSampler, sampleUV, sceneDepth);
        }
    }
    return shadow / 25.0f;
#else
    // SM5 path: static cache sampling not supported — fall back to 1.0 (fully lit).
    // SM5 does not support dynamic descriptor indexing needed for the static atlas.
    return 1.0f;
#endif
}

/// Composites static (cached) and dynamic shadow results.
///
/// When shadow caching is active, the pixel is in shadow if occluded by
/// EITHER static or dynamic geometry. Taking the minimum (most shadowed)
/// of both samples produces the correct composite result.
///
/// Source: UE5 ShadowProjectionPixelShader.usf — static+dynamic compositing.
///
/// # Parameters
/// - dynamicShadow: Shadow value from the dynamic atlas (0=shadowed, 1=lit).
/// - atlasUV: UV coordinates within the cascade tile.
/// - sceneDepth: Light-space depth (standard-Z, biased).
/// - texelSize: Atlas texel size in UV space.
/// - cascadeOffset: UV offset to cascade tile origin.
/// - screenPos: Screen-space position for filter rotation.
/// - tileSize: UV-space size of one cascade tile.
///
/// # Returns
/// Composited shadow factor (0=fully shadowed, 1=fully lit).
float SampleShadowComposited(float dynamicShadow, float2 atlasUV, float sceneDepth,
                              float texelSize, float2 cascadeOffset, float2 screenPos,
                              float2 tileSize) {
    if (useShadowCache == 0) {
        return dynamicShadow;
    }

    float staticShadow = SampleShadowFilteredFromAtlas(
        staticShadowAtlasIndex, atlasUV, sceneDepth,
        texelSize, cascadeOffset, screenPos, tileSize);

    // Min compositing: pixel is shadowed if occluded by either static or dynamic geometry
    return min(staticShadow, dynamicShadow);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cascade Shadow Map Sampling — UE5 Parity
// ─────────────────────────────────────────────────────────────────────────────

float SampleShadowMap(float3 positionWS, float3 normalWS, float viewDepth, float2 screenPos) {
    // Compute atlas texel size using the full atlas width (cols * shadowMapSize)
    uint cols = atlasColumns;
    float texelSize = 1.0f / (shadowMapSize * float(cols));
    float2 tileSize = GetCascadeTileSize(cols, cascadeCount);

    // Select cascade — search through all cascades (main + far, up to 8)
    uint cascadeIdx = cascadeCount - 1;
    for (uint i = 0; i < cascadeCount; i++) {
        if (viewDepth <= cascadeSplits[i]) {
            cascadeIdx = i;
            break;
        }
    }

    float2 cascOff = GetCascadeOffset(cascadeIdx, cols, cascadeCount);

    // Normal bias offset
    float3 biasedPos = positionWS + normalWS * cascadeNormalBias[cascadeIdx];

    // Project into light clip space (standard-Z ortho)
    float4 shadowClip = mul(float4(biasedPos, 1.0f), cascadeViewProj[cascadeIdx]);
    float3 shadowNDC  = shadowClip.xyz / shadowClip.w;

    // NDC to atlas UV — scale by tile size instead of hardcoded 0.5
    float2 shadowUV = float2(shadowNDC.x * 0.5f + 0.5f,
                             -shadowNDC.y * 0.5f + 0.5f);
    float2 atlasUV = shadowUV * tileSize + cascOff;

    // Scene depth with bias subtracted (pushes toward light = more lit = less acne).
    // Clamp to 0.99999 to avoid false-positive shadow from comparing against the
    // cleared depth value (kShadowDepthClear=1.0). Without this clamp, pixels at
    // the exact far plane would compare equal to cleared texels and appear shadowed.
    float sceneDepth = min(shadowNDC.z - cascadeDepthBias[cascadeIdx], 0.99999f);

    // Sample with selected filter method (dynamic atlas)
    float shadow = SampleShadowFiltered(atlasUV, sceneDepth, texelSize,
                                         cascOff, screenPos, tileSize);

    // Composite with static (cached) shadow atlas if caching is active.
    // Both atlases use the same cascade layout, so we reuse the same UV/depth.
    shadow = SampleShadowComposited(shadow, atlasUV, sceneDepth, texelSize,
                                     cascOff, screenPos, tileSize);

    // ── Cascade blending ──
    if (cascadeIdx < cascadeCount - 1) {
        float blendStart = cascadeFadeStarts[cascadeIdx];
        if (viewDepth > blendStart) {
            uint nextIdx = cascadeIdx + 1;
            float2 nextOff = GetCascadeOffset(nextIdx, cols, cascadeCount);

            float3 biasedPosNext = positionWS + normalWS * cascadeNormalBias[nextIdx];
            float4 nextClip = mul(float4(biasedPosNext, 1.0f), cascadeViewProj[nextIdx]);
            float3 nextNDC  = nextClip.xyz / nextClip.w;

            float2 nextUV = float2(nextNDC.x * 0.5f + 0.5f,
                                   -nextNDC.y * 0.5f + 0.5f);
            float2 nextAtlasUV = nextUV * tileSize + nextOff;
            float nextSceneDepth = min(nextNDC.z - cascadeDepthBias[nextIdx], 0.99999f);

            float nextShadow = SampleShadowFiltered(nextAtlasUV, nextSceneDepth, texelSize,
                                                     nextOff, screenPos, tileSize);

            // Composite next cascade with static cache too
            nextShadow = SampleShadowComposited(nextShadow, nextAtlasUV, nextSceneDepth,
                                                 texelSize, nextOff, screenPos, tileSize);

            float blendFactor = saturate((viewDepth - blendStart) /
                                         (cascadeSplits[cascadeIdx] - blendStart));
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

// ─────────────────────────────────────────────────────────────────────────────
// Distance Field Shadow Compositing
// ─────────────────────────────────────────────────────────────────────────────

/// Samples the Distance Field Shadow result texture at the given screen UV.
///
/// The DFS compute shader writes a screen-space R16_FLOAT texture where 0 = fully
/// shadowed and 1 = fully lit. When DFS is disabled or the output texture is not
/// bound (dfsOutputSrvIndex == 0xFFFFFFFF), returns 1.0 (no shadow contribution).
///
/// # Parameters
/// - screenUV: normalized screen-space UV in [0,1] (from SV_Position / screen dimensions).
///
/// # Returns
/// DFS shadow factor in [0,1]: 0 = fully shadowed, 1 = fully lit.
float SampleDfsShadow(float2 screenUV) {
    if (dfsEnabled == 0 || dfsOutputSrvIndex == 0xFFFFFFFF) {
        return 1.0f;
    }

#ifdef UZ_BINDLESS
    Texture2D<float> dfsOutput = ResourceDescriptorHeap[dfsOutputSrvIndex];
    return dfsOutput.SampleLevel(linearSampler, screenUV, 0).r;
#else
    // SM5 path: DFS output not available without bindless descriptor heap.
    // DFS requires SM6.6 bindless for dynamic SRV access.
    return 1.0f;
#endif
}

/// Samples cascaded shadow maps with optional DFS compositing.
///
/// Combines CSM shadow (cascade shadow maps) with DFS (distance field shadows).
/// DFS provides soft long-range shadows beyond CSM range. The final shadow factor
/// is the product: `csmShadow * dfsShadow`.
///
/// # Parameters
/// - positionWS: world-space surface position.
/// - normalWS: world-space surface normal (for normal bias offset).
/// - viewDepth: camera-space distance to the surface.
/// - screenPos: pixel position from SV_Position (xy).
/// - screenDimensions: render target dimensions in pixels (for UV computation).
///
/// # Returns
/// Combined shadow factor in [0,1]: 0 = fully shadowed, 1 = fully lit.
float SampleShadowMapWithDfs(float3 positionWS, float3 normalWS, float viewDepth,
                              float2 screenPos, float2 screenDimensions) {
    float csmShadow = SampleShadowMap(positionWS, normalWS, viewDepth, screenPos);
    float dfsShadow = SampleDfsShadow(screenPos / screenDimensions);
    return csmShadow * dfsShadow;
}

#endif // SHADOW_COMMON_HLSLI
