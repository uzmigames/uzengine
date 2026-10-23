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
    float  localShadowNormalOffset;            // r.Shadow.Local.NormalOffset (fraction of the distance)
    uint   softPCSS;                           // r.Shadows.SoftPCSS: 1 = PCSS variable penumbra on the CSM
    float  pcssMinPenumbraTexels;              // r.Shadow.PCSSMinPenumbra (atlas texels)
    float  pcssMaxPenumbraTexels;              // r.Shadow.PCSSMaxPenumbra (atlas texels)
    float4 cascadePcssDepthToTexelsPacked[2];  // 8 floats: penumbra texels per unit depth gap (shadow_pcss.h)
    uint   localSoftPCSS;                      // r.Shadows.SoftPCSS.Local
    float  localPcssSourceRadius;              // emitter radius in cm
    float2 _shadowPad1;                        // pad to 768
    // 16 entries x 6 float4: [0..3] viewProj rows, [4] (atlasOffsetX, atlasOffsetY, atlasScale,
    // bias cm), [5] (light position xyz, radius). Tiles store distance / radius.
    float4 localShadowRaw[96];                 // 1536B
};

float GetCascadeSplit(uint i)      { return cascadeSplitsPacked[i >> 2][i & 3]; }
float GetCascadeFadeStart(uint i)  { return cascadeFadeStartsPacked[i >> 2][i & 3]; }
float GetCascadeDepthBias(uint i)  { return cascadeDepthBiasPacked[i >> 2][i & 3]; }
float GetCascadeNormalBias(uint i) { return cascadeNormalBiasPacked[i >> 2][i & 3]; }
float GetCascadePcssDepthToTexels(uint i) { return cascadePcssDepthToTexelsPacked[i >> 2][i & 3]; }

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
    /// @ref Holbert 2011 "Saying Goodbye to Shadow Acne"; Castano 2013 "Shadow Mapping Summary"
    /// @constants Normal offset uses sin(theta) in [0,1]; slope offset uses tan(theta), capped at 2.
    /// 1e-6 protects the grazing-angle division; saturate also rejects cosine roundoff above 1.
    /// The previous tan(acos(NdotL)) capped at 10 pushed the offset up to 10x the normal bias at
    /// grazing angles, detaching contact shadows.
    float cosine = saturate(NdotL);
    float normalScale = sqrt(max(0.0, 1.0 - cosine * cosine));
    float slopeScale = min(2.0, normalScale / max(cosine, 1e-6));
    float3 biasedPos = worldPos + N * normalBias * normalScale;

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

    /// @ref Pettineo 2013 "A Sampling of Shadow Techniques" — bias normalized to cascade depth range.
    /// @constants Zero is the lower bound; preserve the producer's world-scaled cascade bias.
    /// The old 0.0005 floor over-biased near cascades and peeled shadows off the ornaments.
    float depthBias = max(GetCascadeDepthBias(cascadeIdx), 0.0);
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

    // Split-boundary blend: when the pixel is near the far edge of `sel`, lerp
    // the shadow FACTOR (not UV) toward `sel+1` to hide the resolution seam.
    // Only blends if the next cascade actually covers the pixel too.
    // Uses the producer's per-cascade fade start (ShadowDataParams::cascadeFadeStarts,
    // the un-extended far split) instead of a fixed 60% of the split distance, so
    // this matches shadow_pcss.hlsli and the CPU's own cascade layout.
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

// ─── Soft shadows: PCSS stage 1 (r.Shadows.SoftPCSS) ─────────────────────────────
//
// The functions above are the r.Shadows.SoftPCSS 0 path and stay untouched; the ones below are
// their variable-penumbra counterparts, selected by the deferred lighting pass per frame.
//
// Structure note: the cascade walk below tests coverage with a projection-only helper and samples
// the atlas once, outside the unrolled walk. The first form of this path sampled inside the walk
// (nine inlined copies of the blocker search); with DXC 1.9.2602 on an RTX 5080 (2026-10-20) that
// compiled to a shader where the first copy returned 0 for every receiver without a blocker,
// while the copy outside the loop (the cascade blend) returned the right value — even a constant
// `return 1.0` placed before the atlas reads rendered dark. Keep texture reads out of the break
// loop.

/// Spatially stable rotation of the Poisson disk: interleaved gradient noise of the pixel position
/// alone, no frame term, so a static view gets the same kernel every frame (no flicker) and a wide
/// penumbra dithers per pixel instead of showing the 16 taps as banding.
/// @ref Jimenez 2014 "Next Generation Post Processing in Call of Duty: Advanced Warfare" — IGN
/// @constants the three IGN coefficients from the talk
float ShadowKernelRotationNoise(float2 pixelPos) {
    return frac(52.9829189 * frac(0.06711056 * pixelPos.x + 0.00583715 * pixelPos.y));
}

/// Penumbra radius in atlas texels: the receiver/average-blocker depth gap times the cascade's
/// constant (tan(half source angle) * r.Shadows.SoftPCSS.LightSize * ortho depth range / texel
/// size, uploaded by SetupShadowFrame). Mirrors PcssPenumbraTexels in shadow_pcss.h, which derives
/// it from Fernando's similar triangles with the sun at infinity.
/// @ref Fernando 2005 "Percentage-Closer Soft Shadows"
float PcssPenumbraTexels(float receiverDepth, float avgBlockerDepth, float depthToTexels) {
    return max(receiverDepth - avgBlockerDepth, 0.0) * depthToTexels;
}

/// Kernel radius clamp to [r.Shadow.PCSSMinPenumbra, r.Shadow.PCSSMaxPenumbra]; a max below the
/// min collapses onto the min (PcssClampRadiusTexels in shadow_pcss.h).
float PcssClampRadiusTexels(float radiusTexels) {
    return clamp(radiusTexels, pcssMinPenumbraTexels, max(pcssMaxPenumbraTexels, pcssMinPenumbraTexels));
}

float2 RotateShadowTap(float2 tap, float2 rot) {
    return float2(tap.x * rot.x - tap.y * rot.y, tap.x * rot.y + tap.y * rot.x);
}

/// A receiver projected into one cascade's tile: everything SampleShadowCascade computes before
/// its taps (normal and slope offsets, tile UV, biased comparison depth) plus whether the cascade
/// covers the point. Pure math — the soft cascade walk uses it to pick a cascade without touching
/// the atlas, so the search and filter below run once, for the selected cascade only.
struct CascadeReceiver {
    float2 atlasUV;     ///< Receiver position in atlas UV.
    float2 tileOffset;  ///< Cascade tile origin in atlas UV.
    float2 tileSize;    ///< Cascade tile extent in atlas UV.
    float  cmpDepth;    ///< Biased comparison depth (SampleShadowCascade's cmpDepth).
    float2 planeGrad;   ///< Receiver-plane depth change per atlas-UV offset (receiver-plane bias).
    bool   covered;     ///< Inside the cascade's depth range and tile (else the -1 sentinel case).
};

/// Receiver slope (depth change per lateral unit, both in world units) beyond which the
/// receiver-plane bias stops growing: tan(83 deg). Past that the receiver is edge-on to the light
/// and N.L is a few percent, so a larger bias would only detach it from its own contact shadow.
/// @constants 8.0 = tan(83 deg)
static const float kPcssMaxReceiverSlope = 8.0;

CascadeReceiver ProjectCascadeReceiver(float3 worldPos, float3 N, float NdotL, uint cascadeIdx) {
    CascadeReceiver r;
    float normalBias = GetCascadeNormalBias(cascadeIdx);
    float cosine = saturate(NdotL);
    float normalScale = sqrt(max(0.0, 1.0 - cosine * cosine));
    float slopeScale = min(2.0, normalScale / max(cosine, 1e-6));
    float3 biasedPos = worldPos + N * normalBias * normalScale;

    float4 lsp = mul(float4(biasedPos, 1.0), cascadeViewProj[cascadeIdx]);
    float3 proj = lsp.xyz / lsp.w;

    float2 shadowUV = proj.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;
    r.tileOffset = GetCascadeTileOffset(cascadeIdx, atlasColumns, r.tileSize);
    r.atlasUV    = r.tileOffset + shadowUV * r.tileSize;

    float depthBias = max(GetCascadeDepthBias(cascadeIdx), 0.0);
    r.cmpDepth = proj.z - depthBias - depthBias * slopeScale;
    r.covered  = !(proj.z < 0.0 || proj.z > 1.0 || any(shadowUV < 0.0) || any(shadowUV > 1.0));

    // Receiver-plane depth bias: the depth gradient of the receiver plane across the shadow map, so
    // every kernel tap compares against the plane's own depth at the tap instead of the centre's.
    // Without it a wide kernel on a receiver seen at a grazing angle reads texels up-slope that are
    // nearer the light than the biased centre and self-shadows (the fixed 2-texel PCF never needed
    // it). The projection is orthographic, so the columns of the view-projection are the NDC
    // gradients of world position; on the plane n.delta = 0, which gives dz/du and dz/dv as the
    // triple products below. Slope-clamped, then expressed per atlas-UV offset (with the V flip).
    /// @ref Isidoro 2006 "Shadow Mapping: GPU-based Tips and Techniques" — receiver plane depth bias
    float4x4 M = cascadeViewProj[cascadeIdx];
    float3 cx = float3(M._11, M._21, M._31);
    float3 cy = float3(M._12, M._22, M._32);
    float3 cz = float3(M._13, M._23, M._33);
    float3 tu = cross(cy, N);   // world direction of a pure +u step along the plane (up to scale)
    float3 tv = cross(cx, N);   // world direction of a pure +v step along the plane
    float du = dot(tu, cx);
    float dv = dot(tv, cy);
    float dzdu = (abs(du) > 1e-8) ? dot(tu, cz) / du : 0.0;
    float dzdv = (abs(dv) > 1e-8) ? dot(tv, cz) / dv : 0.0;
    float gradMax = kPcssMaxReceiverSlope * length(cz) / max(length(cx), 1e-8);
    dzdu = clamp(dzdu, -gradMax, gradMax);
    dzdv = clamp(dzdv, -gradMax, gradMax);
    r.planeGrad = float2(dzdu * 2.0 / r.tileSize.x, -dzdv * 2.0 / r.tileSize.y);
    return r;
}

/// Percentage-closer soft shadow of one cascade for a covered receiver:
///   1. blocker search — the average depth of the atlas texels closer to the light than the
///      receiver, within the widest penumbra this receiver can have (a blocker at the cascade's
///      near plane);
///   2. penumbra estimate from the receiver/blocker depth gap (shadow_pcss.h);
///   3. 16-tap Poisson PCF with that radius — sharp at contact, wide far from the caster.
/// Kernel offsets are in true atlas texels (the atlas is atlasColumns x rows tiles of
/// shadowMapSize), so the CVar radii mean texels of the map the search reads. No blocker in the
/// search disc means none in the (never larger) filter disc: the receiver is lit.
/// @ref Fernando 2005 "Percentage-Closer Soft Shadows"
float SampleShadowCascadeSoft(CascadeReceiver r, uint cascadeIdx,
                              Texture2D<float> shadowAtlas, SamplerComparisonState smplr,
                              float2 pixelPos) {
    float2 atlasDims;
    shadowAtlas.GetDimensions(atlasDims.x, atlasDims.y);
    float2 texelUV  = 1.0 / atlasDims;
    float2 atlasMin = r.tileOffset + 0.5 * texelUV;
    float2 atlasMax = r.tileOffset + r.tileSize - 0.5 * texelUV;

    float angle = ShadowKernelRotationNoise(pixelPos) * 6.28318530718;
    float2 rot = float2(cos(angle), sin(angle));

    // 1. Blocker search. The depth gap cannot exceed the receiver depth, so the search radius
    //    (clamped like the filter) is never smaller than the filter radius.
    float depthToTexels = GetCascadePcssDepthToTexels(cascadeIdx);
    float searchTexels  = PcssClampRadiusTexels(r.cmpDepth * depthToTexels);
    float blockerSum   = 0.0;
    uint  blockerCount = 0u;
    [unroll] for (uint i = 0; i < 16; ++i) {
        float2 offset = RotateShadowTap(kPoissonDisk[i], rot) * searchTexels * texelUV;
        float2 sUV = clamp(r.atlasUV + offset, atlasMin, atlasMax);
        float stored = shadowAtlas.Load(int3(int2(sUV * atlasDims), 0));
        float cmpTap = r.cmpDepth + dot(r.planeGrad, sUV - r.atlasUV);
        if (stored < cmpTap) {   // closer to the light than the receiver plane there: an occluder
            blockerSum   += stored;
            blockerCount += 1u;
        }
    }

    // 2. Penumbra from the depth gap (the average is over the blockers found; the max keeps the
    //    division finite when there are none, in which case the result below is lit anyway);
    // 3. PCF with that radius.
    float avgBlocker   = blockerSum / float(max(blockerCount, 1u));
    float filterTexels = PcssClampRadiusTexels(PcssPenumbraTexels(r.cmpDepth, avgBlocker, depthToTexels));
    float shadow = 1.0;
    [branch] if (blockerCount != 0u) {
        float lit = 0.0;
        [unroll] for (uint j = 0; j < 16; ++j) {
            float2 offset = RotateShadowTap(kPoissonDisk[j], rot) * filterTexels * texelUV;
            float2 sUV = clamp(r.atlasUV + offset, atlasMin, atlasMax);
            lit += shadowAtlas.SampleCmpLevelZero(smplr, sUV, r.cmpDepth + dot(r.planeGrad, sUV - r.atlasUV));
        }
        shadow = lit / 16.0;
    }
    return shadow;
}

/// CascadeShadow with the soft sampler: the same cascade selection, outward walk, split-boundary
/// blend and distance fade. The walk projects only; the selected cascade (and the next one in
/// the blend band) is sampled once.
float CascadeShadowSoft(float3 worldPos, float viewDepth, float3 N, float NdotL, float2 pixelPos) {
    if (cascadeCount == 0) return 1.0;

#ifdef UZ_BINDLESS
    Texture2D<float> shadowAtlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    #define shadowAtlas shadowAtlasTexture
#endif

    uint startCascade = cascadeCount - 1;
    [unroll] for (uint c = 0; c < kMaxCascades; ++c) {
        if (c >= cascadeCount) break;
        if (viewDepth < GetCascadeSplit(c)) { startCascade = c; break; }
    }

    // Walk outward from startCascade until a cascade covers the pixel (projection only).
    uint sel = cascadeCount;   // "no cascade covers the pixel"
    CascadeReceiver rec = (CascadeReceiver)0;
    [unroll] for (uint step = 0; step < kMaxCascades; ++step) {
        uint idx = startCascade + step;
        if (idx >= cascadeCount) break;
        CascadeReceiver r = ProjectCascadeReceiver(worldPos, N, NdotL, idx);
        if (r.covered) { rec = r; sel = idx; break; }
    }

    // Outside the whole CSM coverage: fully lit, as CascadeShadow.
    float shadow = 1.0;
    if (sel < cascadeCount) {
        shadow = SampleShadowCascadeSoft(rec, sel, shadowAtlas, shadowSampler, pixelPos);

        // Split-boundary blend toward sel + 1 when it covers the pixel too.
        if (sel < cascadeCount - 1) {
            float splitDist = GetCascadeSplit(sel);
            float fadeStart = GetCascadeFadeStart(sel);
            float blendRange = splitDist - fadeStart;
            if (blendRange > 0.0 && viewDepth > fadeStart) {
                float bf = saturate((viewDepth - fadeStart) / blendRange);
                CascadeReceiver rn = ProjectCascadeReceiver(worldPos, N, NdotL, sel + 1);
                if (rn.covered) {
                    float next = SampleShadowCascadeSoft(rn, sel + 1, shadowAtlas, shadowSampler, pixelPos);
                    shadow = lerp(shadow, next, bf);
                }
            }
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

// ─── Volume variant (block B.4) ───────────────────────────────────────────

/// One comparison tap of a cascade for volumes (fog froxels): the projection, tile and depth bias
/// of SampleShadowCascade without the normal / slope offsets (a froxel has no normal) and without
/// the Poisson kernel. -1 outside the cascade, as above.
float SampleShadowCascadeTap(float3 worldPos, uint cascadeIdx,
                             Texture2D<float> shadowAtlas, SamplerComparisonState smplr) {
    float4 lsp = mul(float4(worldPos, 1.0), cascadeViewProj[cascadeIdx]);
    float3 proj = lsp.xyz / lsp.w;
    if (proj.z < 0.0 || proj.z > 1.0) return -1.0;
    float2 shadowUV = proj.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;
    if (any(shadowUV < 0.0) || any(shadowUV > 1.0)) return -1.0;
    float2 tileSize;
    float2 tileOffset = GetCascadeTileOffset(cascadeIdx, atlasColumns, tileSize);
    float2 atlasUV = tileOffset + shadowUV * tileSize;
    float cmpDepth = proj.z - max(GetCascadeDepthBias(cascadeIdx), 0.0);
    float halfTexel = 0.5 / shadowMapSize;
    atlasUV = clamp(atlasUV, tileOffset + halfTexel, tileOffset + tileSize - halfTexel);
    return shadowAtlas.SampleCmpLevelZero(smplr, atlasUV, cmpDepth);
}

/// The cascade walk and the distance fade of CascadeShadow with one tap per sample; for volumes,
/// where 16 taps per froxel would not pay for themselves (block B.4).
float CascadeShadowVolume(float3 worldPos, float viewDepth) {
    if (cascadeCount == 0) return 1.0;
#ifdef UZ_BINDLESS
    Texture2D<float> atlas = ResourceDescriptorHeap[shadowAtlasIndex];
#else
    Texture2D<float> atlas = shadowAtlasTexture;
#endif
    uint startCascade = cascadeCount - 1;
    [unroll] for (uint c = 0; c < kMaxCascades; ++c) {
        if (c >= cascadeCount) break;
        if (viewDepth < GetCascadeSplit(c)) { startCascade = c; break; }
    }
    float shadow = -1.0;
    [unroll] for (uint step = 0; step < kMaxCascades; ++step) {
        uint idx = startCascade + step;
        if (idx >= cascadeCount) break;
        float s = SampleShadowCascadeTap(worldPos, idx, atlas, shadowSampler);
        if (s >= 0.0) { shadow = s; break; }
    }
    if (shadow < 0.0) shadow = 1.0;
    if (shadowFadeEnd > shadowFadeStart && viewDepth > shadowFadeStart) {
        float ff = saturate((viewDepth - shadowFadeStart) / (shadowFadeEnd - shadowFadeStart));
        shadow = lerp(shadow, 1.0, ff);
    }
    return shadow;
}

#endif
