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

/// Sample a single CSM cascade.
///
/// Returns the shadow factor in [0, 1] when the pixel falls inside the
/// cascade's frustum, or the sentinel `-1.0` when the pixel is OUT OF
/// the cascade's coverage (Z outside [0,1] or UV outside [0,1]). The
/// caller MUST check for `< 0` before using the result — returning
/// `1.0` on miss causes "sun leaks through walls" when the camera-
/// aligned cascade frustum rotates away from indoor geometry (Sponza
/// bug, 2026-04-10).
///
/// @ref shadow_fallback_walk.md — why the sentinel matters
float SampleShadowCascade(float3 worldPos, float3 N, float NdotL, uint cascadeIdx,
                          Texture2D<float> shadowAtlas, SamplerComparisonState smplr) {
    float normalBias = GetCascadeNormalBias(cascadeIdx);
    float slopeScale = clamp(tan(acos(max(NdotL, 0.001))), 0.0, 10.0);
    float3 biasedPos = worldPos + N * normalBias * slopeScale;

    float4 lsp = mul(float4(biasedPos, 1.0), cascadeViewProj[cascadeIdx]);
    float3 proj = lsp.xyz / lsp.w;
    // OUT OF CASCADE Z RANGE — caller must fall back to next cascade.
    if (proj.z < 0.0 || proj.z > 1.0) return -1.0;

    float2 shadowUV = proj.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;

    float2 tileSize;
    float2 tileOffset = GetCascadeTileOffset(cascadeIdx, atlasColumns, tileSize);
    float2 atlasUV = tileOffset + shadowUV * tileSize;
    // OUT OF CASCADE XY RANGE — caller must fall back to next cascade.
    if (any(shadowUV < 0.0) || any(shadowUV > 1.0)) return -1.0;

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

/// Full CSM evaluation with cascade-walk fallback + distance fade.
///
/// @param N      World-space surface normal — used for normal-offset bias so
///               walls/ceilings receive bias along their true normal direction.
///               Hardcoding N=(0,0,1) caused the "darkening from far away" artifact:
///               far cascades have larger texels → larger bias magnitude → wrong
///               direction pushed vertical surfaces into self-shadow.
/// @param NdotL  Real cos(angle) between surface normal and light direction,
///               used for slope-scaled depth bias.
///
/// Walks cascades outward from the depth-selected start until one covers the
/// pixel (sentinel >= 0), blends at split boundaries, and fades at max distance.
/// @ref UE5 ShadowProjectionCommon.ush — cascade fallback walk.
float CascadeShadow(float3 worldPos, float viewDepth, float3 N, float NdotL) {
    if (cascadeCount == 0) return 1.0;

#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    // Pick the smallest (tightest) cascade whose split distance contains
    // this pixel. This is the preferred cascade at our depth.
    uint startCascade = cascadeCount - 1;
    [unroll] for (uint c = 0; c < kMaxCascades; ++c) {
        if (c >= cascadeCount) break;
        if (viewDepth < GetCascadeSplit(c)) { startCascade = c; break; }
    }

    // Walk outward from `startCascade` until a cascade covers this pixel.
    // A miss returns sentinel `< 0`. The first hit becomes `shadow` and
    // `sel` records which cascade produced it (needed for the split-
    // boundary blend below).
    float shadow = -1.0;
    uint  sel    = startCascade;
    [unroll] for (uint step = 0; step < kMaxCascades; ++step) {
        uint idx = startCascade + step;
        if (idx >= cascadeCount) break;
        float s = SampleShadowCascade(worldPos, N, NdotL, idx, shadowAtlas, shadowSampler);
        if (s >= 0.0) { shadow = s; sel = idx; break; }
    }

    // If EVERY cascade missed the pixel, it is outside the entire CSM
    // coverage. Fall back to fully lit — distance-field shadows (DFS)
    // or other long-range techniques would ideally cover this case.
    if (shadow < 0.0) {
        shadow = 1.0;
    }

    // Split-boundary blend: when the pixel is near the far edge of
    // `sel`, lerp toward `sel+1` to hide the seam. Only blends if the
    // next cascade actually covers the pixel too.
    if (sel < cascadeCount - 1) {
        float splitDist = GetCascadeSplit(sel);
        float fadeStart = GetCascadeFadeStart(sel);
        float blendRange = splitDist - fadeStart;
        if (blendRange > 0.0 && viewDepth > fadeStart) {
            float bf = saturate((viewDepth - fadeStart) / blendRange);
            float next = SampleShadowCascade(worldPos, N, NdotL, sel + 1, shadowAtlas, shadowSampler);
            if (next >= 0.0) {
                shadow = lerp(shadow, next, bf);
            }
        }
    }

    // Global distance fade: pixels beyond `shadowFadeStart` progressively
    // ignore their shadow sample (fade to fully lit at `shadowFadeEnd`).
    // This is a separate mechanism from the cascade-walk miss above and
    // must stay enabled — it prevents visible shadow popping at the
    // outermost cascade boundary.
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
