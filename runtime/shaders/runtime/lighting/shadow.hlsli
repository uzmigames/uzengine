/// UzEngine CSM shadow sampling — 16-tap Poisson PCF + cascade blending.
/// @ref Engel 2006 "Cascaded Shadow Maps" (ShaderX5)
/// @ref Donnelly & Lauritzen 2006 "Variance Shadow Maps" — PCF
/// @ref Jimenez 2016 — normal-offset bias

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

// ─── Atlas tile helpers ───────────────────────────────────────────────────

float2 GetCascadeTileOffset(uint idx, uint cols, out float2 tileSize) {
    float invCols = 1.0 / float(cols);
    tileSize = float2(invCols, invCols);
    return float2(float(idx % cols) * invCols, float(idx / cols) * invCols);
}

// ─── Single cascade PCF ──────────────────────────────────────────────────

float SampleShadowCascade(float3 worldPos, float3 N, float NdotL, uint cascadeIdx,
                          Texture2D<float> shadowAtlas, SamplerComparisonState smplr) {
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
    // CVar r.Shadow.PCFPenumbraRadius (default 2.0) via PerFrameCB.
    // Safety: if CB value is zero/invalid (padding mismatch), use default 2.0.
    float penumbra = (shadowPCFPenumbra > 0.01) ? shadowPCFPenumbra : 2.0;

    float halfTexel = texelSize * 0.5;
    float2 atlasMin = tileOffset + halfTexel;
    float2 atlasMax = tileOffset + tileSize - halfTexel;

    float shadow = 0.0;
    [unroll] for (uint i = 0; i < 16; ++i) {
        float2 offset = kPoissonDisk[i] * penumbra * texelSize;
        float2 sUV = clamp(atlasUV + offset, atlasMin, atlasMax);
        shadow += shadowAtlas.SampleCmpLevelZero(smplr, sUV, cmpDepth);
    }
    return shadow / 16.0;
}

// ─── Full cascade shadow with blending + distance fade ────────────────────

/// Sample cascaded shadow map with normal-offset bias.
/// @param N      World-space surface normal — used for normal-offset bias so
///               walls/ceilings receive bias along their true normal direction
///               instead of world-up. Hardcoding N=(0,0,1) was the cause of the
///               "darkening from far away, clean circle near camera" artifact:
///               far cascades have larger texels → larger bias magnitude → the
///               wrong bias direction pushed vertical surfaces into self-shadow.
/// @param NdotL  Real cos(angle) between surface normal and light direction,
///               used for slope-scaled depth bias. Caller already fast-paths
///               back-facing surfaces (NdotL <= 0) before invoking this.
float CascadeShadow(float3 worldPos, float viewDepth, float3 N, float NdotL) {
    if (cascadeCount == 0) return 1.0;

#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    uint sel = cascadeCount - 1;
    [unroll] for (uint c = 0; c < kMaxCascades; ++c) {
        if (c >= cascadeCount) break;
        if (viewDepth < GetCascadeSplit(c)) { sel = c; break; }
    }

    float shadow = SampleShadowCascade(worldPos, N, NdotL, sel, shadowAtlas, shadowSampler);

    if (sel < cascadeCount - 1) {
        float splitDist = GetCascadeSplit(sel);
        float fadeStart = GetCascadeFadeStart(sel);
        float blendRange = splitDist - fadeStart;
        if (blendRange > 0.0 && viewDepth > fadeStart) {
            float bf = saturate((viewDepth - fadeStart) / blendRange);
            float next = SampleShadowCascade(worldPos, N, NdotL, sel + 1, shadowAtlas, shadowSampler);
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
