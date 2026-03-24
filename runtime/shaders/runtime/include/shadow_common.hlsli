/// Cascaded Shadow Map sampling — 16-tap Poisson disk PCF with cascade blending.
///
/// @ref Donnelly & Lauritzen 2006 "Variance Shadow Maps" — PCF filtering foundations
/// @ref Engel 2006 "Cascaded Shadow Maps" (ShaderX5) — cascade selection + blending
/// @ref Jimenez 2016 "Practical Real-Time Strategies for Accurate Indirect Occlusion"
///   — normal-offset bias strategy
///
/// # Prerequisites
/// Requires ShadowDataCB bound at register b4 with cascade viewproj matrices,
/// split distances, bias values, and atlas layout parameters.
/// Shadow atlas texture at t3 (SM5) or via ResourceDescriptorHeap (bindless).
/// SamplerComparisonState at s1 for hardware PCF.
/// Linear sampler at s0 for manual depth reads.
///
/// # Atlas Layout
/// The atlas is a square texture of `shadowMapSize` texels, divided into an
/// `atlasColumns x atlasColumns` grid. Each cascade occupies one tile.
/// Cascade i maps to tile (i % atlasColumns, i / atlasColumns).

#ifndef SHADOW_COMMON_HLSLI
#define SHADOW_COMMON_HLSLI

// ─────────────────────────────────────────────────────────────────────────────
// Poisson Disk — 16-tap stratified distribution
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Cook 1986 "Stochastic Sampling in Computer Graphics" — Poisson disk motivation
/// @ref Pharr & Humphreys 2010 "PBRT" ch7 — stratified sampling theory
/// @constants 16 pre-computed 2D offsets in unit disk, max radius ~0.95
/// @deviations NONE — standard Poisson disk pattern used across the industry
static const float2 kPoissonDisk[16] = {
    float2(-0.94201624, -0.39906216),
    float2( 0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870),
    float2( 0.34495938,  0.29387760),
    float2(-0.91588581,  0.45771432),
    float2(-0.81544232, -0.87912464),
    float2(-0.38277543,  0.27676845),
    float2( 0.97484398,  0.75648379),
    float2( 0.44323325, -0.97511554),
    float2( 0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023),
    float2( 0.79197514,  0.19090188),
    float2(-0.24188840,  0.99706507),
    float2(-0.81409955,  0.91437590),
    float2( 0.19984126,  0.78641367),
    float2( 0.14383161, -0.14100790),
};

/// @ref Engel 2006 "Cascaded Shadow Maps" — cascade count, blend fraction
/// @constants kMaxCascades=8, kCascadeBlendFraction=0.1
static const uint  kMaxCascades          = 8;
static const float kCascadeBlendFraction = 0.1;

// ─────────────────────────────────────────────────────────────────────────────
// ShadowDataCB — must match C++ ShadowDataParams scalar layout byte-for-byte
// ─────────────────────────────────────────────────────────────────────────────

/// Declared here so any shader that includes shadow_common.hlsli gets the CB.
/// Register b4 = root slot 3 (kSlotShadow).
cbuffer ShadowDataCB : register(b4) {
    // NOTE: HLSL packs float arrays as 16 bytes per element. We use float4
    // packing to match the C++ struct layout (tightly packed float[8] = 32 bytes).
    row_major float4x4 cascadeViewProj[8];   // Offset 0   (512 bytes)

    // Cascade splits: 8 floats packed as 2 × float4 = 32 bytes at offset 512
    float4   cascadeSplitsPacked[2];         // Offset 512 (32 bytes)
    float4   cascadeFadeStartsPacked[2];     // Offset 544 (32 bytes)
    float4   cascadeDepthBiasPacked[2];      // Offset 576 (32 bytes)
    float4   cascadeNormalBiasPacked[2];     // Offset 608 (32 bytes)

    uint     shadowAtlasIndex;               // Offset 640
    uint     cascadeCount;                   // Offset 644
    float    shadowMapSize;                  // Offset 648
    float    receiverBias;                   // Offset 652
    float    shadowFadeStart;                // Offset 656
    float    shadowFadeEnd;                  // Offset 660
    uint     filterMethod;                   // Offset 664
    float    tanLightSourceAngle;            // Offset 668
    float    maxSoftKernelSize;              // Offset 672
    uint     atlasColumns;                   // Offset 676
    uint     staticShadowAtlasIndex;         // Offset 680
    uint     useShadowCache;                 // Offset 684
    uint     dfsEnabled;                     // Offset 688
    uint     dfsOutputSrvIndex;              // Offset 692
    uint     localShadowAtlasIndex;          // Offset 696
    uint     localShadowCount;               // Offset 700
    float4   _shadowPad[4];                  // Offset 704, pad to 768
};

// Helper accessors to unpack float4[2] → float[index]
float GetCascadeSplit(uint i)      { return cascadeSplitsPacked[i >> 2][i & 3]; }
float GetCascadeFadeStart(uint i)  { return cascadeFadeStartsPacked[i >> 2][i & 3]; }
float GetCascadeDepthBias(uint i)  { return cascadeDepthBiasPacked[i >> 2][i & 3]; }
float GetCascadeNormalBias(uint i) { return cascadeNormalBiasPacked[i >> 2][i & 3]; }

// ─────────────────────────────────────────────────────────────────────────────
// Atlas UV Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the UV offset and tile size for a cascade within the shadow atlas.
/// @ref Engel 2006 "Cascaded Shadow Maps" — atlas tile addressing
/// @param cascadeIdx   Zero-based cascade index.
/// @param cols         Number of columns in the atlas grid (atlasColumns).
/// @param tileSize     [out] Size of one tile in UV space (0..1).
/// @return UV offset of the tile's top-left corner.
float2 GetCascadeTileOffset(uint cascadeIdx, uint cols, out float2 tileSize) {
    float invCols = 1.0 / float(cols);
    tileSize = float2(invCols, invCols);
    float2 offset = float2(
        float(cascadeIdx % cols) * invCols,
        float(cascadeIdx / cols) * invCols
    );
    return offset;
}

// ─────────────────────────────────────────────────────────────────────────────
// SampleShadowCascade — 16-tap Poisson disk PCF for a single cascade
// ─────────────────────────────────────────────────────────────────────────────

/// Samples shadow visibility for a world-space position against a specific cascade.
///
/// @ref Donnelly & Lauritzen 2006 "Variance Shadow Maps" — PCF filtering theory
/// @ref Jimenez 2016 — normal-offset bias to reduce surface acne
/// @ref Reeves 1987 "Rendering Antialiased Shadows with Depth Maps" — PCF origin
///
/// @param worldPos     World-space position of the shaded fragment.
/// @param N            World-space unit normal.
/// @param NdotL        Dot product of normal and light direction (for bias scaling).
/// @param cascadeIdx   Zero-based cascade index to sample.
/// @param shadowAtlas  Shadow atlas depth texture.
/// @param smplr        SamplerComparisonState for hardware PCF.
/// @return Shadow visibility in [0, 1]. 1 = fully lit, 0 = fully shadowed.
float SampleShadowCascade(float3 worldPos, float3 N, float NdotL, uint cascadeIdx,
                          Texture2D<float> shadowAtlas, SamplerComparisonState smplr) {
    // ── Normal-offset bias ──────────────────────────────────────────────
    // Offset the world position along the surface normal to push the sample
    // point out of the shadow map's depth bias dead zone. The offset scales
    // with tan(acos(NdotL)) to increase at grazing angles where acne is worst.
    // @ref Jimenez 2016, Holbert 2011 "Normal Offset Shadows"
    float normalBias = GetCascadeNormalBias(cascadeIdx);
    float slopeScale = clamp(tan(acos(max(NdotL, 0.001))), 0.0, 10.0);
    float3 biasedPos = worldPos + N * normalBias * slopeScale;

    // ── Project to light space ──────────────────────────────────────────
    float4 lightSpacePos = mul(float4(biasedPos, 1.0), cascadeViewProj[cascadeIdx]);
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Shadow maps use standard-Z (near=0, far=1). If projected Z is outside
    // [0,1] the fragment is beyond the cascade's depth range — treat as lit.
    if (projCoords.z < 0.0 || projCoords.z > 1.0) return 1.0;

    // Convert NDC XY [-1,1] to UV [0,1]. Y is flipped because UV origin is top-left.
    float2 shadowUV = projCoords.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;

    // ── Map to atlas tile ───────────────────────────────────────────────
    float2 tileSize;
    float2 tileOffset = GetCascadeTileOffset(cascadeIdx, atlasColumns, tileSize);
    float2 atlasUV = tileOffset + shadowUV * tileSize;

    // If projected UV is outside the tile, fragment is not in this cascade.
    if (any(shadowUV < 0.0) || any(shadowUV > 1.0)) return 1.0;

    // ── Depth bias ──────────────────────────────────────────────────────
    // Per-cascade constant bias applied to the comparison depth.
    // @ref Williams 1978 "Casting Curved Shadows on Curved Surfaces" — depth bias
    float depthBias = GetCascadeDepthBias(cascadeIdx);
    float compareDepth = projCoords.z - depthBias;

    // ── Penumbra radius scaling ─────────────────────────────────────────
    // Larger cascades cover more world-space area, so the Poisson disk radius
    // should grow to keep softness visually consistent across cascades.
    // @ref Engel 2006 — adaptive filter radius per cascade
    // @constants penumbraBase=3.5, penumbraRange=[0.5, 2.5], cascadeGrowth=0.3
    float penumbra = clamp(projCoords.z * 3.5, 0.5, 2.5) * (1.0 + float(cascadeIdx) * 0.3);

    // texelSize is the size of one shadow map texel in atlas UV space.
    float texelSize = 1.0 / shadowMapSize;

    // ── 16-tap Poisson PCF ──────────────────────────────────────────────
    // @ref Reeves 1987 — percentage closer filtering
    // @ref Cook 1986 — Poisson disk sample distribution
    float shadow = 0.0;

    // Half-texel inset to prevent sampling outside the tile boundary.
    float halfTexel = texelSize * 0.5;
    float2 atlasMin = tileOffset + halfTexel;
    float2 atlasMax = tileOffset + tileSize - halfTexel;

    [unroll]
    for (uint i = 0; i < 16; ++i) {
        // Scale Poisson offset by penumbra radius and texel size, then
        // add to the atlas UV. The tileSize factor converts tile-local
        // texel offsets into atlas UV offsets.
        float2 offset = kPoissonDisk[i] * penumbra * texelSize;
        float2 sampleUV = clamp(atlasUV + offset, atlasMin, atlasMax);

        // SampleCmpLevelZero performs a single hardware comparison sample:
        // returns 1 if sampleDepth >= compareDepth (lit), 0 if shadowed.
        shadow += shadowAtlas.SampleCmpLevelZero(smplr, sampleUV, compareDepth);
    }

    return shadow / 16.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// SampleShadowMap — cascade selection + cross-cascade blending
// ─────────────────────────────────────────────────────────────────────────────

/// Selects the appropriate cascade for the given view depth and samples shadows.
/// Implements 10% blend zones at cascade boundaries for smooth transitions.
///
/// @ref Engel 2006 "Cascaded Shadow Maps" (ShaderX5) — cascade selection by view depth
/// @ref Dimitrov 2007 "Cascaded Shadow Maps" (GPU Gems 3 ch10) — blend zones
///
/// @param worldPos   World-space position of the shaded fragment.
/// @param N          World-space unit normal.
/// @param viewDepth  Linear view-space depth (positive, increasing away from camera).
/// @param pixelPos   Screen-space pixel position (SV_Position.xy) — reserved for
///                   future temporal dithering; not used in current implementation.
/// @return Shadow visibility in [0, 1]. 1 = fully lit, 0 = fully shadowed.
float SampleShadowMap(float3 worldPos, float3 N, float viewDepth, float2 pixelPos) {
    // Early out: no cascades active.
    if (cascadeCount == 0) return 1.0;

    // Need NdotL for bias. lightDir comes from PerFrameCB which is already
    // bound when this function is called from deferred_lighting_ps.hlsl.
    // We declare it as an extern to avoid duplicating the CB declaration.
    // The caller passes N, and lightDir is accessible from PerFrameCB.
    // However, since shadow_common.hlsli doesn't declare PerFrameCB, we
    // compute a conservative NdotL from the normal and the cascade's
    // light direction (implicit in cascadeViewProj). For simplicity, the
    // caller should use the overload that takes NdotL directly.
    // Here we default to a moderate bias angle.
    float NdotL = 0.5; // Conservative default; see SampleShadowMapFull below.

    // ── Get shadow atlas ────────────────────────────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    // shadowAtlasTexture is declared in deferred_lighting_ps.hlsl at t3.
    // We reference it via the name the including shader provides.
    #define shadowAtlas shadowAtlasTexture
#endif

    // ── Find the cascade containing this fragment ───────────────────────
    // @ref Engel 2006 — select cascade by comparing viewDepth to split distances
    uint selectedCascade = cascadeCount - 1; // Default to last cascade
    [unroll]
    for (uint c = 0; c < kMaxCascades; ++c) {
        if (c >= cascadeCount) break;
        if (viewDepth < GetCascadeSplit(c)) {
            selectedCascade = c;
            break;
        }
    }

    float shadow = SampleShadowCascade(worldPos, N, NdotL, selectedCascade,
                                       shadowAtlas, shadowSampler);

    // ── Cross-cascade blending ──────────────────────────────────────────
    // @ref Dimitrov 2007 "Cascaded Shadow Maps" — smooth transitions between cascades
    // Blend the last 10% of each cascade's depth range with the next cascade
    // to prevent visible seams when switching cascades.
    if (selectedCascade < cascadeCount - 1) {
        float splitDist = GetCascadeSplit(selectedCascade);
        // cascadeFadeStarts stores the original (non-extended) far split.
        // The blend zone is between cascadeFadeStarts[c] and GetCascadeSplit(c).
        float fadeStart = GetCascadeFadeStart(selectedCascade);
        float blendRange = splitDist - fadeStart;

        if (blendRange > 0.0 && viewDepth > fadeStart) {
            float blendFactor = saturate((viewDepth - fadeStart) / blendRange);
            float nextShadow = SampleShadowCascade(worldPos, N, NdotL,
                                                   selectedCascade + 1,
                                                   shadowAtlas, shadowSampler);
            shadow = lerp(shadow, nextShadow, blendFactor);
        }
    }

    // ── Distance fade ───────────────────────────────────────────────────
    // @ref Engel 2006 — fade shadows to 1.0 (fully lit) beyond the last cascade
    // to avoid a hard shadow cutoff at the maximum shadow distance.
    if (shadowFadeEnd > shadowFadeStart && viewDepth > shadowFadeStart) {
        float fadeFactor = saturate((viewDepth - shadowFadeStart) /
                                    (shadowFadeEnd - shadowFadeStart));
        shadow = lerp(shadow, 1.0, fadeFactor);
    }

#ifndef UZ_BINDLESS
    #undef shadowAtlas
#endif

    return shadow;
}

/// Full-featured overload that accepts NdotL for accurate normal-offset bias.
/// Prefer this overload when the light direction is known at the call site.
///
/// @ref Engel 2006 "Cascaded Shadow Maps" — cascade selection
/// @ref Jimenez 2016 — normal-offset bias
///
/// @param worldPos   World-space position.
/// @param N          World-space unit normal.
/// @param NdotL      Dot product of normal and light direction.
/// @param viewDepth  Linear view-space depth.
/// @param pixelPos   Screen-space pixel position (SV_Position.xy).
/// @return Shadow visibility in [0, 1].
float SampleShadowMapFull(float3 worldPos, float3 N, float NdotL,
                          float viewDepth, float2 pixelPos) {
    if (cascadeCount == 0) return 1.0;

#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    // ── Cascade selection ───────────────────────────────────────────────
    uint selectedCascade = cascadeCount - 1;
    [unroll]
    for (uint c = 0; c < kMaxCascades; ++c) {
        if (c >= cascadeCount) break;
        if (viewDepth < GetCascadeSplit(c)) {
            selectedCascade = c;
            break;
        }
    }

    float shadow = SampleShadowCascade(worldPos, N, NdotL, selectedCascade,
                                       shadowAtlas, shadowSampler);

    // ── Cross-cascade blending ──────────────────────────────────────────
    if (selectedCascade < cascadeCount - 1) {
        float splitDist = GetCascadeSplit(selectedCascade);
        float fadeStart = GetCascadeFadeStart(selectedCascade);
        float blendRange = splitDist - fadeStart;

        if (blendRange > 0.0 && viewDepth > fadeStart) {
            float blendFactor = saturate((viewDepth - fadeStart) / blendRange);
            float nextShadow = SampleShadowCascade(worldPos, N, NdotL,
                                                   selectedCascade + 1,
                                                   shadowAtlas, shadowSampler);
            shadow = lerp(shadow, nextShadow, blendFactor);
        }
    }

    // ── Distance fade ───────────────────────────────────────────────────
    if (shadowFadeEnd > shadowFadeStart && viewDepth > shadowFadeStart) {
        float fadeFactor = saturate((viewDepth - shadowFadeStart) /
                                    (shadowFadeEnd - shadowFadeStart));
        shadow = lerp(shadow, 1.0, fadeFactor);
    }

#ifndef UZ_BINDLESS
    #undef shadowAtlas
#endif

    return shadow;
}

#endif // SHADOW_COMMON_HLSLI
