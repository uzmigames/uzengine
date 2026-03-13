/// Shared cascaded shadow map sampling functions.
///
/// Used by both the forward pass (mesh_ps.hlsl) and deferred lighting pass
/// (deferred_lighting_ps.hlsl). Implements cascade selection, 3x3 PCF with
/// atlas boundary clamping, and inter-cascade blending.
///
/// # Prerequisites
/// The including shader must declare:
/// - `ShadowDataCB` cbuffer with: cascadeViewProj[4], cascadeSplits, shadowAtlasIndex,
///   cascadeCount, normalBias, depthBias, shadowMapSize
/// - `SamplerComparisonState shadowSampler : register(s1)`
/// - For UZ_BINDLESS: nothing extra (uses ResourceDescriptorHeap)
/// - For SM5: `Texture2D<float> shadowAtlasTexture : register(t4 or t5)`

#ifndef SHADOW_COMMON_HLSLI
#define SHADOW_COMMON_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Shadow Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Fraction of each cascade's range used for blending with the next cascade.
/// At 10%, a cascade covering [0, 100] units begins blending at 90 units.
static const float CASCADE_BLEND_BAND = 0.1f;

/// Atlas quadrant offsets for 2x2 cascade layout.
/// 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right.
static const float2 kCascadeOffsets[4] = {
    float2(0.0f, 0.0f), float2(0.5f, 0.0f),
    float2(0.0f, 0.5f), float2(0.5f, 0.5f)
};

// ─────────────────────────────────────────────────────────────────────────────
// PCF Shadow Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Samples the shadow atlas with 3x3 PCF at the given atlas UV and depth.
///
/// Performs 9 taps in a 3x3 grid around the sample point, each offset by one
/// texel. The comparison sampler returns 1.0 for lit and 0.0 for shadowed per
/// tap, and the results are averaged for a soft penumbra.
///
/// PCF kernel UVs are clamped to the current cascade's atlas quadrant to
/// prevent sampling from adjacent cascades at quadrant boundaries.
///
/// # Parameters
/// - atlasUV:       UV coordinates within the full shadow atlas, in [0,1].
/// - depth:         light-space depth to compare against, in [0,1].
/// - texelSize:     1.0 / shadow atlas width in texels.
/// - cascadeOffset: atlas quadrant origin for the current cascade (from kCascadeOffsets).
///
/// # Returns
/// Shadow factor in [0,1]: 1.0 = fully lit, 0.0 = fully shadowed.
float SampleShadowPCF(float2 atlasUV, float depth, float texelSize, float2 cascadeOffset) {
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    // SM5 path uses register-bound texture
    #define shadowAtlas shadowAtlasTexture
#endif

    // Cascade quadrant bounds: each quadrant spans [offset, offset + 0.5]
    // Inset by half a texel to avoid bilinear bleed at the boundary
    float halfTexel = texelSize * 0.5f;
    float2 atlasMin = cascadeOffset + halfTexel;
    float2 atlasMax = cascadeOffset + float2(0.5f, 0.5f) - halfTexel;

    float shadow = 0.0f;

    // 3x3 PCF kernel — 9 taps centered on the sample point
    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            float2 offset = float2((float)x, (float)y) * texelSize;
            float2 sampleUV = clamp(atlasUV + offset, atlasMin, atlasMax);
            shadow += shadowAtlas.SampleCmpLevelZero(
                shadowSampler, sampleUV, depth);
        }
    }

#ifndef UZ_BINDLESS
    #undef shadowAtlas
#endif

    return shadow / 9.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cascade Shadow Map Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the shadow factor for a world-space position using cascaded shadow maps.
///
/// Selects the tightest cascade that contains the fragment based on view-space
/// depth, projects into light space, applies normal and depth bias, then samples
/// with 3x3 PCF. In the last 10% of each cascade's range, the result is blended
/// with the next cascade to eliminate visible seams.
///
/// # Parameters
/// - positionWS: fragment world-space position.
/// - normalWS:   fragment world-space surface normal (normalized).
/// - viewDepth:  distance from camera to fragment along the view axis.
///
/// # Returns
/// Shadow factor in [0,1]: 1.0 = fully lit, 0.0 = fully shadowed.
float SampleShadowMap(float3 positionWS, float3 normalWS, float viewDepth) {
    // Per-cascade atlas texel size: atlas is 2x the cascade resolution
    float texelSize = 1.0f / (shadowMapSize * 2.0f);

    // Access cascade split distances as an array
    float splits[4] = { cascadeSplits.x, cascadeSplits.y,
                         cascadeSplits.z, cascadeSplits.w };

    // Select the first cascade whose far split exceeds the fragment depth.
    // Default to the last cascade for fragments beyond all split distances.
    uint cascadeIdx = cascadeCount - 1;
    for (uint i = 0; i < cascadeCount; i++) {
        if (viewDepth <= splits[i]) {
            cascadeIdx = i;
            break;
        }
    }

    // Offset position along surface normal to reduce shadow acne on slopes
    float3 biasedPos = positionWS + normalWS * normalBias;

    // Project into light clip space for the selected cascade
    float4 shadowClip = mul(float4(biasedPos, 1.0f), cascadeViewProj[cascadeIdx]);
    float3 shadowNDC  = shadowClip.xyz / shadowClip.w;

    // Convert from NDC [-1,1] to UV [0,1], flipping Y for texture space
    float2 shadowUV = float2(shadowNDC.x * 0.5f + 0.5f,
                             -shadowNDC.y * 0.5f + 0.5f);

    // Map UV into the cascade's atlas quadrant (each quadrant is 0.5 x 0.5)
    float2 atlasUV = shadowUV * 0.5f + kCascadeOffsets[cascadeIdx];

    // Apply depth bias before comparison
    float shadowDepth = shadowNDC.z - depthBias;

    float shadow = SampleShadowPCF(atlasUV, shadowDepth, texelSize, kCascadeOffsets[cascadeIdx]);

    // ── Cascade blending ──
    // In the last CASCADE_BLEND_BAND fraction of the cascade range, lerp
    // toward the next cascade's shadow to avoid hard seams between cascades.
    if (cascadeIdx < cascadeCount - 1) {
        float cascadeNear = (cascadeIdx == 0) ? 0.0f : splits[cascadeIdx - 1];
        float cascadeFar  = splits[cascadeIdx];
        float cascadeRange = cascadeFar - cascadeNear;
        float blendStart  = cascadeFar - cascadeRange * CASCADE_BLEND_BAND;

        if (viewDepth > blendStart) {
            // Compute next cascade's shadow
            uint nextIdx = cascadeIdx + 1;
            float3 biasedPosNext = positionWS + normalWS * normalBias;
            float4 nextClip = mul(float4(biasedPosNext, 1.0f), cascadeViewProj[nextIdx]);
            float3 nextNDC  = nextClip.xyz / nextClip.w;

            float2 nextUV = float2(nextNDC.x * 0.5f + 0.5f,
                                   -nextNDC.y * 0.5f + 0.5f);
            float2 nextAtlasUV = nextUV * 0.5f + kCascadeOffsets[nextIdx];
            float nextDepth = nextNDC.z - depthBias;

            float nextShadow = SampleShadowPCF(nextAtlasUV, nextDepth, texelSize, kCascadeOffsets[nextIdx]);

            // Blend factor: 0 at blendStart, 1 at cascadeFar
            float blendFactor = saturate((viewDepth - blendStart) /
                                         (cascadeFar - blendStart));
            shadow = lerp(shadow, nextShadow, blendFactor);
        }
    }

    return shadow;
}

#endif // SHADOW_COMMON_HLSLI
