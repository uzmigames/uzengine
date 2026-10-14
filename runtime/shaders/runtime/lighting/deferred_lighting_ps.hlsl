/// UzEngine deferred lighting — simplified Falcor-style pipeline.
/// GBuffer read → directional NdotL → point lights → IBL → HDR output.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4"
/// @ref Lagarde 2014 "Moving Frostbite to PBR"

#include "../common/constants.hlsli"
#include "../common/encoding.hlsli"
#include "../common/region_dump.hlsli"
#include "../material/brdf.hlsli"
#include "../material/diffuse.hlsli"
#include "light_types.hlsli"
#include "ibl.hlsli"
#include "../gi/rc_probe_guard.hlsli"

/// Z-up world direction → Y-up DX cubemap convention.
/// Must match skybox_cubemap_ps.hlsl and sky_cubemap_cs.hlsl.
float3 WorldToCubemap(float3 dir) { return float3(dir.x, dir.z, dir.y); }

/// Octahedral direction encode — must mirror `OctEncodeDir` in
/// `ddgi_probe_update_cs.hlsl` so the deferred sampler reads the texel that
/// the writer produced for the corresponding direction.
/// @ref Cigolle 2014 "A Survey of Efficient Representations for Independent
///                    Unit Vectors"
float2 DDGIOctEncode(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = n.xy;
    if (n.z < 0.0) {
        e = (1.0 - abs(e.yx)) * float2(e.x >= 0.0 ? 1.0 : -1.0,
                                       e.y >= 0.0 ? 1.0 : -1.0);
    }
    return e * 0.5 + 0.5;
}
// NOTE: shadow.hlsli and contact_shadows.hlsli are included AFTER PerFrameCB
// because they reference CVar fields (shadowPCFPenumbra, etc.) from the CB.

// ─── Root constants at b0 ──────────────────────────────────────────────────

#ifdef UZ_BINDLESS
struct GBufferIndices {
    uint albedoIdx;
    uint normalIdx;
    uint depthIdx;
    uint positionIdx;
    uint emissiveIdx;
    uint envMisSrvIdx;  ///< Temporally-accumulated env MIS specular (kSentinelSrvIndex = disabled).
    uint iblPrefilteredSrvIdx; ///< IBL prefiltered specular cubemap (via root constants, bypasses CB ring).
    uint iblIrradianceSrvIdx;  ///< IBL irradiance cubemap (via root constants, bypasses CB ring).
    uint lightingDebugMode;    ///< CVar r.Lighting.DebugMode (0 = off). A root constant: PerFrameCB has no free bytes.
};
ConstantBuffer<GBufferIndices> gBufIndices : register(b0);
#else
Texture2D<float4> gbufferAlbedo    : register(t0);
Texture2D<float4> gbufferNormal    : register(t1);
Texture2D<float>  gbufferDepth     : register(t2);
// CSM atlas read by shadow.hlsli's SM5 path. Bound at t3 by LightingPass (SetFragmentTexture(3))
// and by ShadowDataParams metadata (kShadowAtlasSlot = 3).
Texture2D<float>  shadowAtlasTexture : register(t3);
Texture2D<float4> gbufferPositionWS : register(t4);
Texture2D<float>  gtaoTextureT9    : register(t9);
Texture2D<float4> gbufferEmissive  : register(t10);
Texture2D<float4> ssrTextureT11    : register(t11);
Texture2D<float4> ssgiTextureT12   : register(t12);
#endif

// ─── PerFrameCB at b1 (768 bytes) ─────────────────────────────────────────

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;    // c0-c3   (0-63)
    float4 cameraPos;               // c4      (64-79)
    float4 timeParams;              // c5      (80-95)
    float4 lightDir;                // c6      (96-111)
    float4 lightColor;              // c7      (112-127)
    float4 ambientColor;            // c8      (128-143)
    row_major float4x4 invViewProj; // c9-c12  (144-207)
    float  nearPlane;               // c13.x   (208)
    float  farPlane;                // c13.y   (212)
    uint   irradianceSrvIndex;      // c13.z   (216)
    uint   prefilteredSrvIndex;     // c13.w   (220)
    uint   brdfLutSrvIndex;         // c14.x   (224)
    uint   prefilteredMipLevels;    // c14.y   (228)
    float  skyLightIntensity;       // c14.z   (232)
    float  skyLightColorR;          // c14.w   (236)
    float  skyLightColorG;          // c15.x   (240)
    float  skyLightColorB;          // c15.y   (244)
    float  skyLightMinAmbient;      // c15.z   (248)
    float  exposure;                // c15.w   (252)
    float4 skyIrradianceSH[7];      // c16-c22 (256-367)
    uint   shadowDebugMode;         // c23.x   (368)
    // ── Aerial Perspective parameters ──────────────────────────────────
    uint   aerialPerspectiveEnabled;// c23.y   (372) 1 = apply AP in lighting
    uint   _transmittanceLutSrv;    // c23.z   (376)
    float  _apBottomRadius;         // c23.w   (380)
    float  _apTopRadius;            //         (384)
    float  _apRayBetaX;             //         (388)
    float  _apRayBetaY;             //         (392)
    float  _apRayBetaZ;             //         (396)
    float  _apRayScaleH;            //         (400)
    float  _apMieBeta;              //         (404)
    float  _apMieScaleH;            //         (408)
    float  _apMieAniso;             //         (412)
    float  _apSunI;                 //         (416)
    float  _apRayScale;             //         (420)
    float  _apMieScale;             //         (424)
    // ── Tone curve + post-FX fields (not used by lighting) ────────────
    float  _toneCurveSlope;         //         (428)
    float  _toneCurveToe;           //         (432)
    float  _toneCurveShoulder;      //         (436)
    float  _toneCurveBlackClip;     //         (440)
    float  _toneCurveWhiteClip;     //         (444)
    uint   _gtaoSrv;                //         (448)
    uint   _brdfDebugMode;          //         (452)
    float  _heightFogContribution;  //         (456)
    float  _filmGrain;              //         (460)
    float  _vignette;               //         (464)
    float  _chromaAb;               //         (468)
    float  _contactShadowLen;       //         (472)
    uint   ssrSrvIndex;             //         (476)
    float  _mipBias;                //         (480)
    uint   ssgiSrvIndex;            //         (484)
    // ── Intermediate fields (488-707) — named for AP volume access ──
    uint   _ssgiAO;                 //         (488) ssgiAOSrvIndex
    uint   _ssgiEn;                 //         (492) ssgiEnabled
    uint   _rcEnabled;              //         (496) radianceCacheEnabled
    uint   _rcProbesSrv;            //         (500) rcProbeSHBufferSrv
    uint   _rcProbesPerAxis;        //         (504)
    float  _rcExtent;               //         (508)
    float  _rcOriginX;              //         (512)
    float  _rcOriginY;              //         (516)
    float  _rcOriginZ;              //         (520)
    float  _rcGiIntensity;          //         (524)
    uint   apVolumeSrvIndex;        //         (528) AP 3D volume (32x32x16 RGBA16F)
    uint   _reflProbeCount;         //         (532)
    uint   _reflProbeSrv;           //         (536)
    float  _colorSat;               //         (540)
    float4 _skip4[10];              //         (544-703) 10 × 16 = 160 bytes
    float  _skipA;                  //         (704)
    // ── Shader CVar parameters (Phase 1) — offset 708 ──
    float  shadowPCFPenumbra;       //         (708)
    float  shadowPCSSMinPenumbra;   //         (712)
    float  shadowPCSSMaxPenumbra;   //         (716)
    float  shadowESMExponent;       //         (720)
    uint   shadowContactSteps;      //         (724)
    float  shadowContactThickness;  //         (728)
    float  bloomKnee;               //         (732)
    float  exposureKeyValue;        //         (736)
    float  exposureSpeedUp;         //         (740)
    float  exposureSpeedDown;       //         (744)
    float  exposureLowPercent;      //         (748)
    float  exposureHighPercent;     //         (752)
    float  materialDielectricF0;    //         (756)
    float  materialMinGGXAlpha;     //         (760)
    uint   brixGIDiffuseSrv;        //         (764) BrixelizerGI diffuse SRV (specular = diffuse+1)
    // ── Light Probe Volume (offset 768) ──────────────────────────────
    uint   _lpvEnabled;             //         (768)
    uint   _lpvProbeBufferSrv;      //         (772)
    uint   _lpvDimX;                //         (776)
    uint   _lpvDimY;                //         (780)
    uint   _lpvDimZ;                //         (784)
    float  _lpvOriginX;             //         (788)
    float  _lpvOriginY;             //         (792)
    float  _lpvOriginZ;             //         (796)
    float  _lpvInvSizeX;            //         (800)
    float  _lpvInvSizeY;            //         (804)
    float  _lpvInvSizeZ;            //         (808)
    float  _lpvIntensity;           //         (812)
    // ── DDGI atlases (offset 816) ────────────────────────────────────
    uint   _ddgiIrradSrv;           //         (816)
    uint   _ddgiDepthSrv;           //         (820)
    uint   _ddgiProbesPerAxis;      //         (824)
    float  _ddgiProbeSpacing;       //         (828)
    float  _ddgiOriginX;            //         (832)
    float  _ddgiOriginY;            //         (836)
    float  _ddgiOriginZ;            //         (840)
    float  _ddgiIntensity;          //         (844)
    // ── Indirect AO ──────────────────────────────────────────────────
    float  _indirectAOExponent;     //         (848) pow(ao, N) for indirect light
    // ── SDF gather / reflection bindings (mirror per_frame_params.h) ──
    uint   _sdfReflectionsEnabled;      //         (852)
    uint   _sdfReflectionSkySrv;        //         (856)
    uint   _sdfReflectionSamples;       //         (860)
    float4 _sdfReflectionClipmaps[4];   //         (864) center.xyz + extent
    uint4  _sdfReflectionClipmapSrv;    //         (928) expanded field SRVs
    float  _sdfReflectionTraceDistance; //         (944)
    float  _sdfReflectionVoxelSize;     //         (948)
    uint   _sdfReflectionTraceSteps;    //         (952)
    float  _sdfReflectionIntensity;     //         (956)
    uint   _sdfDiffuseSamples;          //         (960)
    uint   _sdfReflectionPlainMean;     //         (964) 1 = plain mean in GatherSdfReflections (r.GI.Reflections.HdrWeight=0)
    uint2  _padSdfRaw;                  //         (968)
    uint4  _sdfRawClipmapSrv;           //         (976) raw (unexpanded) field SRVs
    uint   _debugRegionUav;             //         (992) region dump UAV (0xffffffff = off), see common/region_dump.hlsli
    uint   _debugRegionSrv;             //         (996) rect table SRV (StructuredBuffer<uint4>, 2 per rect)
    uint   _debugRegionCount;           //        (1000) rect count (0 = off)
    uint   _debugRegionStamp;           //        (1004) frame stamp the GPU writes into the footer
    uint   _debugRegionFields;          //        (1008) field-group bitmask
    float  _materialRoughnessOverride;  //        (1012) CVar r.Material.RoughnessOverride (-1 = off)
    float  _materialMetallicOverride;   //        (1016) CVar r.Material.MetallicOverride (-1 = off)
    uint   _componentReplaySrv;         //        (1020) CVar r.GI.ComponentReplayFile (0xffffffff = off)
};

// LightingCB at b2 via light_types.hlsli (already declared above)

// Shadow includes AFTER PerFrameCB — they reference CVar fields from the CB.
#include "shadow.hlsli"
#include "contact_shadows.hlsli"

SamplerState      linearSampler : register(s0);
SamplerState      clampSampler  : register(s2);

#ifdef UZ_BINDLESS
#define UZ_SDF_REFLECTION_SAMPLER clampSampler
#include "../gi/sdf_diffuse_gather.hlsli"  // gather + Karis-weighted SDF reflections (sdf_reflections.hlsli)
#endif

// ─── Input ─────────────────────────────────────────────────────────────────

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─── Debug MRT Output ──────────────────────────────────────────────────────
// When the lighting pipeline uses 4 render targets, SV_Target1-3 provide
// split lighting layers for pass-dump / debug visualization:
//   RT0 = final composited HDR (Lo + ambient + emissive)
//   RT1 = direct lighting only (Lo = sun * shadow)
//   RT2 = indirect lighting only (ambient = SH/GI/IBL)
//   RT3 = shadow mask (R=shadow factor, G=AO, B=specOcclusion)
struct LightingOutput {
    float4 hdr      : SV_Target0;
    float4 direct   : SV_Target1;
    float4 indirect : SV_Target2;
    float4 debug    : SV_Target3;
};

// ─── Entry Point ───────────────────────────────────────────────────────────

/// Region dump (diagnostics): writes this pixel's lighting records into every rectangle that
/// contains it, plus the rect's lighting frame stamp when the pixel owns the rect origin.
/// `dist < 0` marks "no geometry this frame" (sky), so a stale record from a frame whose
/// jittered raster did cover the pixel is never read as if it were current.
/// Coordinates are RENDER-resolution pixels; the stamp carries the render size so the CPU can
/// check that the lighting and TAA domains coincide (they differ when ScreenPercentage != 100).
void WriteRegionDumpLighting(uint2 rp, float3 reflection, float roughness, float3 N, float dist,
                             float3 directLight, float shadowVisibility,
                             float3 diffuseIndirect, float ambientOcclusion,
                             float3 specularIndirect, float metallicValue,
                             float3 emissiveAndMis, uint pathMask,
                             float3 ssgiConsumed, uint ssgiMode,
                             float4 rcSupport, float4 ddgiSupport) {
#ifdef UZ_BINDLESS
    if (_debugRegionUav == 0xffffffffu || _debugRegionCount == 0u
        || (_debugRegionFields & UZ_REGION_FIELD_LIGHTING) == 0u) return;
    StructuredBuffer<uint4>    rects = ResourceDescriptorHeap[_debugRegionSrv];
    RWStructuredBuffer<float4> dump  = ResourceDescriptorHeap[_debugRegionUav];
    Texture2D<float> regionDepthTex = ResourceDescriptorHeap[gBufIndices.depthIdx];
    uint renderW, renderH;
    regionDepthTex.GetDimensions(renderW, renderH);
    for (uint r = 0u; r < _debugRegionCount; ++r) {
        const uint4 a = rects[r * 2u];
        const uint4 b = rects[r * 2u + 1u];
        if (!UzRegionContains(a, rp)) continue;
        const uint rec = UzRegionPixelBase(a, b, rp) + UzRegionLightingOffset(_debugRegionFields);
        dump[rec + 0] = float4(reflection, dist < 0.0 ? -1.0 : roughness);
        dump[rec + 1] = float4(N, dist);
        if ((_debugRegionFields & UZ_REGION_FIELD_LIGHTING_EXTRA) != 0u) {
            const uint lrec = UzRegionPixelBase(a, b, rp) + UzRegionLightingExtraOffset(_debugRegionFields);
            dump[lrec + 0] = float4(directLight, shadowVisibility);
            dump[lrec + 1] = float4(diffuseIndirect, ambientOcclusion);
            dump[lrec + 2] = float4(specularIndirect, metallicValue);
            dump[lrec + 3] = float4(emissiveAndMis, asfloat(pathMask));
            dump[lrec + 4] = float4(ssgiConsumed, asfloat(ssgiMode));
            // +5..+10 belong to the forward translucent pass, which runs after this one. Clearing them
            // here every frame means a pixel the translucent pass does not cover reads "not drawn"
            // (opacity -1, zero layers) instead of whatever it held last frame.
            dump[lrec + 5] = float4(0.0, 0.0, 0.0, -1.0);
            dump[lrec + 6] = float4(0.0, 0.0, 0.0, 0.0);
            dump[lrec + 7] = float4(0.0, 0.0, 0.0, 0.0);
            dump[lrec + 8] = float4(0.0, 0.0, 0.0, 0.0);
            dump[lrec + 9] = float4(0.0, 0.0, 0.0, 0.0);
            dump[lrec + 10] = float4(0.0, 0.0, regionDepthTex.Load(int3(rp, 0)).x, 0.0);
            // +11..+12: support of the probe queries (see region_dump.hlsli).
            dump[lrec + 11] = rcSupport;
            dump[lrec + 12] = ddgiSupport;
        }
        if (rp.x == a.x && rp.y == a.y) {
            dump[UzRegionFooterBase(a, b) + 0] =
                float4(asfloat(_debugRegionStamp), float(renderW), float(renderH), 1.0);
        }
    }
#endif
}

// ── Local light shadow tiles (spot lights and point-light cube faces) ────────
// Entry layout in localShadowRaw[]: 6 float4 per entry — [0..3] viewProj rows, [4] (atlasOffsetX,
// atlasOffsetY, atlasScale, bias cm), [5] (light position xyz, radius). The tile stores
// distance / radius (shadow_depth_linear_ps.hlsl), so the comparison is linear: the receiver is lit
// when its own distance, minus the bias, is closer than the stored occluder (LESS comparison
// sampler, atlas cleared to 1.0). Receivers are pushed along the geometric normal by
// r.Shadow.Local.NormalOffset of their distance before the test (against acne on grazing surfaces).
float SampleLocalShadow(uint entry, float3 worldPos, float3 geometricNormal) {
#ifdef UZ_BINDLESS
    if (localShadowAtlasIndex == kSentinelSrvIndex) return 1.0;
    Texture2D<float> localAtlas = ResourceDescriptorHeap[localShadowAtlasIndex];
    const uint base = entry * 6u;
    const float4x4 localVP = float4x4(localShadowRaw[base + 0], localShadowRaw[base + 1],
                                      localShadowRaw[base + 2], localShadowRaw[base + 3]);
    const float4 atlasParams = localShadowRaw[base + 4];
    const float4 lightParams = localShadowRaw[base + 5];
    const float  dist = length(lightParams.xyz - worldPos);
    const float3 receiver = worldPos + geometricNormal * (dist * localShadowNormalOffset);
    const float4 lsPos = mul(float4(receiver, 1.0), localVP);
    if (lsPos.w <= 0.0) return 1.0;
    const float2 ndc = lsPos.xy / lsPos.w;
    if (any(abs(ndc) > 1.0)) return 1.0;
    const float2 uv = ndc * float2(0.5, -0.5) + 0.5;           // same y flip as the CSM (shadow.hlsli)
    const float2 atlasUV = uv * atlasParams.z + atlasParams.xy;
    const float  radius = max(lightParams.w, 1e-3);
    const float  ref = (length(lightParams.xyz - receiver) - atlasParams.w) / radius;
    return localAtlas.SampleCmpLevelZero(shadowSampler, atlasUV, saturate(ref));
#else
    return 1.0;
#endif
}

// ─── Local lights (block B.7) ─────────────────────────────────────────────
//
// The point and spot loop bodies of the deferred pass, as functions shared by the per-pixel loop and
// the clustered list, with the per-light parameters the light components always carried but the
// shader ignored: `falloffExponent` (custom falloff when the inverse-square flag is clear),
// `diffuseScale` / `specularScale`, and the point light's `sourceRadius` (Karis 2013 sphere light:
// the representative point on the sphere for the specular direction and the roughness widening with
// its energy normalisation). With the defaults (inverse square, scales 1, radius 0) the code paths
// are the previous ones.
// @ref Karis 2013 "Real Shading in Unreal Engine 4" — sphere lights, energy normalisation
// @ref UE5 DeferredLightingCommon.ush — RadialAttenuation (custom falloff exponent)

float LocalLightAttenuation(float distSq, float radius, float falloffExponent, uint flags)
{
    float invRadSq = 1.0 / (radius * radius);
    float f = distSq * invRadSq;
    if ((flags & kLightFlag_InverseSquared) != 0u) {
        float s = saturate(1.0 - f * f);
        return (s * s) / max(distSq, 0.01);
    }
    // UE's custom falloff: no 1/d^2, the colour is a brightness.
    return pow(saturate(1.0 - f), max(falloffExponent, 1e-3));
}

float3 ShadePointLight(PointLightData pl, float3 worldPos, float3 N, float3 geometricNormal, float3 V, float NoV,
                       float alpha, float roughness, float3 F0, float3 F90, float3 diffColor)
{
    float3 toLight = pl.position - worldPos;
    float  distSq  = dot(toLight, toLight);
    if (distSq > pl.radius * pl.radius) return float3(0.0, 0.0, 0.0);

    float3 Lp   = normalize(toLight);
    float  NoLp = max(dot(N, Lp), 0.0);
    if (NoLp <= 0.0) return float3(0.0, 0.0, 0.0);

    float atten = LocalLightAttenuation(distSq, pl.radius, pl.falloffExponent, pl.flags);
    // Shadowed point lights own six consecutive tiles (+X, -X, +Y, -Y, +Z, -Z, the order
    // frame_constants.cpp allocates them); the face is the major axis of light -> pixel.
    if (pl.shadowIndex != 0xFFFFFFFFu && pl.shadowIndex + 5u < localShadowCount) {
        const float3 d = -toLight;
        const float3 a = abs(d);
        const uint face = (a.x >= a.y && a.x >= a.z) ? (d.x > 0.0 ? 0u : 1u)
                        : (a.y >= a.z)               ? (d.y > 0.0 ? 2u : 3u)
                                                     : (d.z > 0.0 ? 4u : 5u);
        atten *= SampleLocalShadow(pl.shadowIndex + face, worldPos, geometricNormal);
    }

    float3 Hp   = normalize(V + Lp);
    float  NoHp = saturate(dot(N, Hp));
    float  VoHp = saturate(dot(V, Hp));

    // Sphere light (sourceRadius > 0): the specular direction is the point of the sphere closest to
    // the reflection ray, the lobe is widened by the sphere's angle and normalised.
    float  specAlpha  = alpha;
    float  specEnergy = 1.0;
    float  NoHs = NoHp;
    float  VoHs = VoHp;
    if (pl.sourceRadius > 0.0) {
        const float3 r = reflect(-V, N);
        const float3 centerToRay = dot(toLight, r) * r - toLight;
        const float3 closest = toLight + centerToRay * saturate(pl.sourceRadius / max(length(centerToRay), 1e-4));
        const float3 Ls = normalize(closest);
        const float3 Hs = normalize(V + Ls);
        NoHs = saturate(dot(N, Hs));
        VoHs = saturate(dot(V, Hs));
        const float alphaPrime = saturate(alpha + pl.sourceRadius / (2.0 * sqrt(distSq)));
        specEnergy = (alpha * alpha) / max(alphaPrime * alphaPrime, 1e-8);
        specAlpha  = alphaPrime;
    }

    float  Dp   = D_GGX(specAlpha, NoHs);
    float  Visp = V_SmithGGXCorrelated(specAlpha, NoV, NoLp);
    float3 Fp   = F_Schlick(F0, F90, VoHs);
    float3 specp = Dp * Visp * Fp * (specEnergy * pl.specularScale);
    float3 diffp = EvalFrostbiteDiffuse(diffColor, roughness, NoV, NoLp, VoHp) * pl.diffuseScale;

    return (diffp + specp) * pl.color * NoLp * atten;
}

float3 ShadeSpotLight(SpotLightData sl, float3 worldPos, float3 N, float3 geometricNormal, float3 V, float NoV,
                      float alpha, float roughness, float3 F0, float3 F90, float3 diffColor)
{
    float3 toLight = sl.position - worldPos;
    float  distSq  = dot(toLight, toLight);
    if (distSq > sl.radius * sl.radius) return float3(0.0, 0.0, 0.0);

    float3 Ls   = normalize(toLight);
    float  NoLs = max(dot(N, Ls), 0.0);
    if (NoLs <= 0.0) return float3(0.0, 0.0, 0.0);

    float distAtten = LocalLightAttenuation(distSq, sl.radius, sl.falloffExponent, sl.flags);

    float cosA = dot(Ls, -sl.direction);
    float cone = saturate((cosA - sl.cosOuterAngle) * sl.invCosConeDifference);
    float atten = distAtten * cone * cone;
    if (atten <= 0.0) return float3(0.0, 0.0, 0.0);

    // ── Spot shadow map sampling ─────────────────────────────────
    // Linear-distance tile, sampled by SampleLocalShadow above (block A.1).
    float spotShadow = (sl.shadowIndex != 0xFFFFFFFFu && sl.shadowIndex < localShadowCount)
                     ? SampleLocalShadow(sl.shadowIndex, worldPos, geometricNormal) : 1.0;

    float3 Hs   = normalize(V + Ls);
    float  NoHs = saturate(dot(N, Hs));
    float  VoHs = saturate(dot(V, Hs));

    float  Ds   = D_GGX(alpha, NoHs);
    float  Viss = V_SmithGGXCorrelated(alpha, NoV, NoLs);
    float3 Fs   = F_Schlick(F0, F90, VoHs);
    float3 specs = Ds * Viss * Fs * sl.specularScale;
    float3 diffs = EvalFrostbiteDiffuse(diffColor, roughness, NoV, NoLs, VoHs) * sl.diffuseScale;

    return (diffs + specs) * sl.color * NoLs * atten * spotShadow;
}

LightingOutput main(FSInput input) {
    int2 px = int2(input.positionCS.xy);

    // ── GBuffer Load (pixel-exact, no filtering) ──────────────────────────
#ifdef UZ_BINDLESS
    Texture2D<float4> gbAlbedo = ResourceDescriptorHeap[gBufIndices.albedoIdx];
    Texture2D<float4> gbNormal = ResourceDescriptorHeap[gBufIndices.normalIdx];
    Texture2D<float>  gbDepth  = ResourceDescriptorHeap[gBufIndices.depthIdx];
    Texture2D<float4> gbPos    = ResourceDescriptorHeap[gBufIndices.positionIdx];
    Texture2D<float4> gbEmis   = ResourceDescriptorHeap[gBufIndices.emissiveIdx];

    float4 rt0 = gbAlbedo.Load(int3(px, 0));
    float4 rt1 = gbNormal.Load(int3(px, 0));
    float  z   = gbDepth.Load(int3(px, 0)).r;
    float3 worldPos = gbPos.Load(int3(px, 0)).xyz;
    float3 emissive = gbEmis.Load(int3(px, 0)).rgb;
#else
    float4 rt0 = gbufferAlbedo.Load(int3(px, 0));
    float4 rt1 = gbufferNormal.Load(int3(px, 0));
    float  z   = gbufferDepth.Load(int3(px, 0)).r;
    float3 worldPos = gbufferPositionWS.Load(int3(px, 0)).xyz;
    float3 emissive = gbufferEmissive.Load(int3(px, 0)).rgb;
#endif

    // Raster surface derivatives are independent of interpolated/normal-map shading normals.
    // The SDF trace needs the real facet orientation: starting a ray from a
    // normal-mapped direction can push the origin behind the surface it sits on.
    // @ref Pharr, Jakob and Humphreys 2023 "Physically Based Rendering", surface normal orientation.
    // @constants Squared area <=1e-12 is degenerate; unknown geometry produces zero transport.
    float3 geometricNormal = cross(ddx_fine(worldPos), ddy_fine(worldPos));
    float geometricArea = dot(geometricNormal, geometricNormal);
    geometricNormal = geometricArea > 1e-12 ? geometricNormal * rsqrt(geometricArea) : 0.0;
    if (dot(geometricNormal, cameraPos.xyz - worldPos) < 0.0) geometricNormal = -geometricNormal;

    // Sky pixels: reversed-Z far = 0
    if (z <= 0.0001) {
#ifdef UZ_BINDLESS
        // Region dump (diagnostics): mark "no geometry this frame" (distance -1) so stale
        // records from a frame where the jittered raster covered this pixel are not read.
        // The footer stamp is written here too: the rect origin may be a sky pixel, and a
        // missing stamp invalidates the whole file on the CPU side.
        WriteRegionDumpLighting(uint2(px), float3(0.0, 0.0, 0.0), -1.0, float3(0.0, 0.0, 0.0), -1.0,
                                float3(0.0, 0.0, 0.0), 0.0, float3(0.0, 0.0, 0.0), 0.0,
                                float3(0.0, 0.0, 0.0), 0.0, float3(0.0, 0.0, 0.0), 0u,
                                float3(0.0, 0.0, 0.0), 0u,
                                float4(0.0, 0.0, 0.0, 0.0), float4(0.0, 0.0, 0.0, 0.0));
#endif
        discard;
    }

    // ── Decode GBuffer ────────────────────────────────────────────────────
    float3 baseColor = rt0.rgb;
    float  ao        = rt0.a;  // mesh AO from GBuffer

    // GTAO: combine with mesh AO when available.
    // SM5 path: fixed texture register t9.
    // Bindless path: SRV index from PerFrameCB._gtaoSrv.
    //
    // The CPU gates `_gtaoSrv` to UINT32_MAX when `r.GTAO=0`, and this
    // branch guards against stale/zero SRV slots. Never sample GTAO
    // unconditionally — see frame_constants.cpp:555 and gtao_pass.h
    // IsEnabledCVar() for the CPU-side gating contract.
    if (_gtaoSrv != 0xFFFFFFFFu) {
#ifdef UZ_BINDLESS
        Texture2D<float> gtaoTex = ResourceDescriptorHeap[_gtaoSrv];
        float gtao = gtaoTex.Load(int3(px, 0));
#else
        float gtao = gtaoTextureT9.Load(int3(px, 0)).r;
#endif
        if (gtao > 0.01) ao = min(ao, gtao);
    }

    // minOcclusion: clamp AO so fully occluded areas still receive configurable
    // minimum ambient. Affects both diffuse (ao * diffuseAmbient) and specular
    // occlusion paths. @ref SkyLightComponent.minOcclusion
    ao = max(ao, skyLightMinAmbient);

    // Diagnostic substitution (r.GI.ComponentReplayFile): replace AO and the specular indirect with
    // the values a reference run recorded at THIS frame and THIS pixel. Per frame and per pixel on
    // purpose — holding one frozen frame would add a second temporal change and confound the
    // separation this exists to make. Header in element 0: x, y, w, h | firstFrame, frameCount.
    float3 replaySpecular = float3(0, 0, 0);
    bool   replayActive = false;
#ifdef UZ_BINDLESS
    if (_componentReplaySrv != 0xffffffffu) {
        StructuredBuffer<float4> replay = ResourceDescriptorHeap[_componentReplaySrv];
        const float4 h0 = replay[0];
        const float4 h1 = replay[1];
        const int rx = int(h0.x), ry = int(h0.y), rw = int(h0.z), rh = int(h0.w);
        const int f0 = int(h1.x), fn = int(h1.y);
        const int fi = int(timeParams.z) - f0;
        const int lx = int(px.x) - rx, ly = int(px.y) - ry;
        if (fi >= 0 && fi < fn && lx >= 0 && lx < rw && ly >= 0 && ly < rh) {
            const float4 v = replay[2 + (fi * rh + ly) * rw + lx];
            replaySpecular = v.rgb;
            ao = v.a;
            replayActive = true;
        }
    }
#endif

    float3 N         = OctDecode(rt1.rg);
    float  metallic  = rt1.b;
    // Roughness floor = sqrt(r.Material.MinGGXAlpha) (0.04 until 2026-10-05, when it was a literal).
    float  roughness = max(rt1.a, sqrt(materialMinGGXAlpha));
    float  alpha     = roughness * roughness;
    float3 dbgReflection = 0.0;   // SDF reflection term before the SSR blend (region dump record 0)

    // ── Decode shading model + specular from emissive.w ──────────────────
    // @ref gbuffer_ps.hlsl — packedAlpha = specular + smID * 16.0
#ifdef UZ_BINDLESS
    float  packedAlpha = gbEmis.Load(int3(px, 0)).a;
#else
    float  packedAlpha = gbufferEmissive.Load(int3(px, 0)).a;
#endif
    uint   smID = uint(packedAlpha / 16.0);
    float  transmission = (smID == 9) ? float((uint(packedAlpha) >> 0) & 0xFu) / 15.0 : 0.0;
    // For ThinTranslucent (smID==9), bits[28:31] of materialFlags encode transmission
    // which is packed in the emissive alpha alongside the smID.
    // Rebuild from the GBuffer: customW in RT2.w holds the 4-bit value.
    float  customW = 0.0;
#ifdef UZ_BINDLESS
    customW = gbPos.Load(int3(px, 0)).w;
#else
    customW = gbufferPositionWS.Load(int3(px, 0)).w;
#endif
    if (smID == 9) {
        transmission = customW; // RT2.w holds the packed value (0-1)
    }

    // Dielectric reflectance from the authored Specular parameter instead of a
    // fixed 4%. The GBuffer packs it next to the shading-model id, so it comes
    // back by subtracting the id: a material authored with Specular=0 (the dirt
    // decal) now has no grazing reflection at all, which a constant F0 gave it.
    /// @ref Karis 2013 "Real Shading in Unreal Engine 4"
    /// @ref Guy and Agopian, "Physically Based Rendering in Filament" — specular micro-occlusion
    /// @constants 16 separates shading IDs; 2 * r.Material.DielectricF0 maps Specular=0.5 to F0 (0.04 by default);
    /// 50 extinguishes grazing reflectance for authored F0 below 2%.
    float  materialSpecular = saturate(packedAlpha - float(smID) * 16.0);
    float3 dielectricF0  = (2.0 * materialDielectricF0) * materialSpecular;
    float  dielectricF90 = saturate(50.0 * dielectricF0.g);
    float3 F0         = lerp(dielectricF0, baseColor, metallic);
    float3 F90        = lerp(dielectricF90, 1.0, metallic);
    float3 diffColor  = baseColor * (1.0 - metallic);

    float3 V   = normalize(cameraPos.xyz - worldPos);
    float  NoV = max(dot(N, V), 1e-4);

    // ── Directional light + CSM shadows ─────────────────────────────────
    float  viewDepth = length(cameraPos.xyz - worldPos);
    float3 sunL = normalize(lightDir.xyz);
    float  sunNoL = max(dot(N, sunL), 0.0);
    // Interior fast path: surfaces facing away from the sun (sunNoL == 0) are
    // self-shadowed regardless of CSM result, so skip the expensive PCF/PCSS
    // filtering entirely. Saves 16-tap PCF work on every back-facing fragment
    // in enclosed interiors. @ref UE5 ShadowProjectionPixelShader — N·L cull.
    float  shadow = (sunNoL > 0.0) ? CascadeShadow(worldPos, viewDepth, N, sunNoL) : 0.0;

    // Screen-space contact shadows: fine detail shadows from small features
    // (column edges, window frames, floor contact). Gated by CVar r.ContactShadows.
    if (sunNoL > 0.0 && _contactShadowLen > 0.0) {
        uint csW, csH;
#ifdef UZ_BINDLESS
        Texture2D<float> csDpth = ResourceDescriptorHeap[gBufIndices.depthIdx];
        csDpth.GetDimensions(csW, csH);
        float cs = ComputeContactShadow(worldPos, sunL, csDpth,
                                         viewProj, invViewProj,
                                         csW, csH, nearPlane, farPlane,
                                         farPlane * _contactShadowLen);
#else
        gbufferDepth.GetDimensions(csW, csH);
        float cs = ComputeContactShadow(worldPos, sunL,
                                         viewProj, invViewProj,
                                         csW, csH, nearPlane, farPlane,
                                         farPlane * _contactShadowLen);
#endif
        shadow = min(shadow, cs);
    }

    float3 Lo = float3(0, 0, 0);
    {
        float3 L   = sunL;
        float  NoL = sunNoL;

        if (NoL > 0.0) {
            float3 H   = normalize(V + L);
            float  NoH = saturate(dot(N, H));
            float  VoH = saturate(dot(V, H));

            // Specular: D * V * F where V = G / (4*NdotV*NdotL)
            // V_SmithGGXCorrelated is numerically stable (no division by NdotV)
            // @ref Heitz 2014 + Hammon 2017 — optimized correlated Smith visibility
            float  D = D_GGX(alpha, NoH);
            float  Vis = V_SmithGGXCorrelated(alpha, NoV, NoL);
            float3 F = F_Schlick(F0, F90, VoH);
            float3 spec = D * Vis * F;

            // Diffuse: Frostbite energy-conserving (already divided by PI)
            float3 diff = EvalFrostbiteDiffuse(diffColor, roughness, NoV, NoL, VoH);

            Lo = (diff + spec) * lightColor.xyz * NoL * shadow;
        }
    }

    // ── Local lights: point and spot (block B.7) ──────────────────────────
    // The lights of the pixel's cluster (cluster_assign_cs.hlsl), in the order the per-pixel loop
    // used (points in index order, then spots), so the sums are identical; a cluster that overflowed
    // its slots, or the per-pixel loop (r.Lights.Clustered 0), evaluates every light of the frame.
    uint dbgClusterCount = 0u;
    bool dbgClusterOverflow = false;
    // r.Lights.ClusterDebug: (slice, count + 1000 x overflow, first slot, flat tile) in the region
    // dump's rc_support record (the cache query is not what is being inspected then).
    float4 dbgClusterInfo = float4(0.0, 0.0, 0.0, 0.0);
    {
        bool useList = false;
        uint clusterBase = 0u;
        uint clusterCount = 0u;
#ifdef UZ_BINDLESS
        if ((clusterFlags & kClusterFlag_Enabled) != 0u && clusterListSrv != kSentinelSrvIndex) {
            const float pixelViewDepth = dot(worldPos - cameraPos.xyz, clusterForward);
            StructuredBuffer<uint> clusterList = ResourceDescriptorHeap[clusterListSrv];
            const uint clusterIdx = ClusterIndexForPixel(uint2(px), pixelViewDepth);
            clusterBase = clusterIdx * kClusterRecordSize;
            const uint packed = clusterList[clusterBase];
            clusterCount = packed & 0x7FFFFFFFu;
            dbgClusterCount = clusterCount;
            dbgClusterOverflow = (packed & kClusterOverflow) != 0u;
            useList = !dbgClusterOverflow;
            const uint dbgMode = (clusterFlags >> 2) & 3u;
            if (dbgMode == 2u) {
                dbgClusterInfo = float4(asfloat(clusterList[clusterBase + kClusterSlotsMax - 5u]), asfloat(clusterList[clusterBase + kClusterSlotsMax - 4u]),
                                        asfloat(clusterList[clusterBase + kClusterSlotsMax - 2u]), asfloat(clusterList[clusterBase + kClusterSlotsMax - 1u]));
            } else if (dbgMode == 3u) {
                dbgClusterInfo = float4(asfloat(clusterList[clusterBase + kClusterSlotsMax - 3u]), asfloat(clusterList[clusterBase + kClusterSlotsMax]),
                                        float(clusterCount), clusterCount > 0u ? float(clusterList[clusterBase + 1u]) : -1.0);
            } else {
                dbgClusterInfo = float4(float(clusterIdx / (clusterTilesX * clusterTilesY)),
                                        float(clusterCount) + (dbgClusterOverflow ? 1000.0 : 0.0),
                                        clusterCount > 0u ? float(clusterList[clusterBase + 1u]) : -1.0,
                                        float(clusterIdx % (clusterTilesX * clusterTilesY)));
            }
        }
        if (useList) {
            StructuredBuffer<uint> clusterList = ResourceDescriptorHeap[clusterListSrv];
            [loop]
            for (uint ci = 0u; ci < clusterCount; ++ci) {
                const uint slot = clusterList[clusterBase + 1u + ci];
                if ((slot & kClusterSpotBit) == 0u) {
                    Lo += ShadePointLight(pointLights[slot], worldPos, N, geometricNormal, V, NoV, alpha, roughness, F0, F90, diffColor);
                } else {
                    Lo += ShadeSpotLight(spotLights[slot & 0x7FFFu], worldPos, N, geometricNormal, V, NoV, alpha, roughness, F0, F90, diffColor);
                }
            }
        } else
#endif
        {
            for (uint pi = 0; pi < pointLightCount; ++pi) {
                Lo += ShadePointLight(pointLights[pi], worldPos, N, geometricNormal, V, NoV, alpha, roughness, F0, F90, diffColor);
            }
            for (uint si = 0; si < spotLightCount; ++si) {
                Lo += ShadeSpotLight(spotLights[si], worldPos, N, geometricNormal, V, NoV, alpha, roughness, F0, F90, diffColor);
            }
        }
    }

    // ── IBL ambient (Falcor split-sum pattern) ──────────────────────────
    // @ref Karis 2013 "Real Shading in Unreal Engine 4" — split-sum IBL
    // @ref Falcor StandardBSDF — F_SchlickRoughness for env Fresnel
    //
    // IBL (indirect lighting) is NOT attenuated by directional shadow.
    // @ref UE5 DeferredLightingCommon.ush — IndirectLighting is independent of shadow.
    // Only AO affects indirect; shadows only affect direct lights.
    // Split tint vs intensity: SH diffuse is already pre-scaled by
    // indirectLightingIntensity on the CPU (frame_constants.cpp:269), so diffuse
    // only gets the color tint. Specular IBL reads the prefiltered cubemap which
    // is NOT pre-scaled, so it needs the full intensity multiplier.
    float3 skyColor = float3(skyLightColorR, skyLightColorG, skyLightColorB);
    float3 skyTintDiffuse  = skyColor;
    float3 skyTintSpecular = skyColor * skyLightIntensity;
    float3 ambient = float3(0, 0, 0);

    // Lighting decomposition debug captures (r.Lighting.DebugMode) — set in
    // the branches below, consumed by the final debug override. Source ids:
    // 0=sky-SH fallback, 1=DDGI, 2=RC, 3=LPV, 4=SDF world gather.
    float3 dbgIrradiance    = float3(0, 0, 0);
    uint   dbgGiSource      = 0u;
    // Region dump (diagnostics, UZ_REGION_FIELD_LIGHTING_EXTRA): the lighting components as they
    // are composited, so the analysis can check that they add up to the scene colour instead of
    // assuming a breakdown.
    float3 dbgDiffuseIndirect = float3(0, 0, 0);
    // What the lighting pass reads from the SSGI/gather texture, and which branch read it.
    float3 dbgSsgiConsumed = float3(0, 0, 0);
    uint   dbgSsgiMode = 0u;   // 0 = never read, 1 = SDF world gather, 2 = legacy SSGI

    float3 dbgSpecularIndirect = float3(0, 0, 0);
    float4 ssrHit = float4(0, 0, 0, 0);          // block B.5: screen-space reflection sample (rgb radiance, a confidence)
    float3 ssrSingleScatter = float3(1, 1, 1);  // block B.5: the single-scatter Fresnel term of the pixel
    float  specOcclusionOut = 1.0;              // block B.5b: the specular occlusion the split-sum term received
    float3 dbgEmissiveAndMis = float3(0, 0, 0);
    uint   dbgPath = 0u;
    // Support of the two probe queries at this pixel (2026-09-18, followup-01 review item 2): is a
    // zero from the cache path an unsupported query, and which rule emptied the support? Written
    // to LIGHTING_EXTRA +11/+12; zero when the query did not run. Nothing reads them.
    //   rc:   x surviving weight sum, y probes facing (weight > 0), z probes valid, w surviving both
    //   ddgi: x visibility-weighted weight sum, y visibility > 1e-3, z visibility <= 1e-3, w trilinear > 0
    float4 dbgRcSupport = float4(0, 0, 0, 0);
    float4 dbgDdgiSupport = float4(0, 0, 0, 0);
    {
        // ── Indirect diffuse: pick ONE GI source by priority ─────────────
        //
        // Indirect diffuse irradiance has exactly one source per pixel. We
        // pick by priority — the highest-quality available technique fully
        // replaces the others. NEVER lerp/sum multiple GI sources; that
        // double-counts light. Falcor follows the same rule — its path
        // tracer writes its own output and does not blend with rasterized
        // sky SH or rasterized irradiance volumes.
        // @ref Falcor MinimalPathTracer.rt.slang — outColor = direct + indirect
        // @ref UE5 DeferredLightingCommon.ush — IndirectLighting is a single source
        //
        // Priority order (highest first):
        //   1. DDGI atlases (DXR ray-traced, Chebyshev visibility)
        //   2. Radiance cache (SDF sphere-traced, spatially-varying SH)
        //   3. Light Probe Volume (author-placed irradiance grid)
        //   4. Global sky SH (precomputed cubemap projection — fallback)

        float3 irradiance = float3(0, 0, 0);
        bool   irradianceFromGI = false;

        // ── 1. DDGI (highest priority) ───────────────────────────────────
#ifdef UZ_BINDLESS
        if (!irradianceFromGI
            && _ddgiIrradSrv != 0xFFFFFFFFu && _ddgiDepthSrv != 0xFFFFFFFFu
            && _ddgiProbesPerAxis > 0 && _ddgiProbeSpacing > 0.0
            && _ddgiIntensity > 0.0) {
            Texture2D<float4> ddgiIrradTex = ResourceDescriptorHeap[_ddgiIrradSrv];
            Texture2D<float2> ddgiDepthTex = ResourceDescriptorHeap[_ddgiDepthSrv];

            // Map world position to grid coordinates [0, dim-1].
            float3 ddgiOrigin = float3(_ddgiOriginX, _ddgiOriginY, _ddgiOriginZ);
            float3 ddgiCell   = (worldPos - ddgiOrigin) / _ddgiProbeSpacing;
            int3   ddgi0      = clamp(int3(floor(ddgiCell)),
                                      int3(0,0,0),
                                      int3(int(_ddgiProbesPerAxis)-1,
                                           int(_ddgiProbesPerAxis)-1,
                                           int(_ddgiProbesPerAxis)-1));
            int3   ddgi1      = min(ddgi0 + 1,
                                    int3(int(_ddgiProbesPerAxis)-1,
                                         int(_ddgiProbesPerAxis)-1,
                                         int(_ddgiProbesPerAxis)-1));
            float3 ddgiFrac   = saturate(ddgiCell - float3(ddgi0));

            // Atlas tile constants — must match DDGISystem in C++.
            const uint kIrradTile = 6u;
            const uint kDepthTile = 16u;
            const uint kIrradStride = kIrradTile + 2u;
            const uint kDepthStride = kDepthTile + 2u;
            const uint atlasIrradW = _ddgiProbesPerAxis * _ddgiProbesPerAxis * kIrradStride;
            const uint atlasIrradH = _ddgiProbesPerAxis * kIrradStride;
            const uint atlasDepthW = _ddgiProbesPerAxis * _ddgiProbesPerAxis * kDepthStride;
            const uint atlasDepthH = _ddgiProbesPerAxis * kDepthStride;

            float2 octIrrad = DDGIOctEncode(N);

            float3 ddgiIrrad = float3(0, 0, 0);
            float  ddgiTotalW = 0.0;
            float  ddgiCntTri = 0.0, ddgiCntVis = 0.0, ddgiCntRej = 0.0;   // region dump only
            [unroll]
            for (uint corner = 0; corner < 8; ++corner) {
                int3 idx3 = int3(
                    (corner & 1) ? ddgi1.x : ddgi0.x,
                    (corner & 2) ? ddgi1.y : ddgi0.y,
                    (corner & 4) ? ddgi1.z : ddgi0.z);
                float wTri = ((corner & 1) ? ddgiFrac.x : (1.0 - ddgiFrac.x))
                           * ((corner & 2) ? ddgiFrac.y : (1.0 - ddgiFrac.y))
                           * ((corner & 4) ? ddgiFrac.z : (1.0 - ddgiFrac.z));
                if (wTri > 0.0) ddgiCntTri += 1.0;

                // Linear probe index using the same packing as the writer
                // (`tileX = idx3.y * dim + idx3.x` then stacked along Y by z).
                uint probeLinear = uint(idx3.x)
                                 + uint(idx3.y) * _ddgiProbesPerAxis
                                 + uint(idx3.z) * _ddgiProbesPerAxis * _ddgiProbesPerAxis;
                uint tileX = probeLinear % (_ddgiProbesPerAxis * _ddgiProbesPerAxis);
                uint tileY = probeLinear / (_ddgiProbesPerAxis * _ddgiProbesPerAxis);

                // Probe world position for the visibility test.
                float3 probePos = ddgiOrigin
                                + float3(float(idx3.x), float(idx3.y), float(idx3.z))
                                  * _ddgiProbeSpacing;
                float3 toProbe   = probePos - worldPos;
                float  distToProbe = length(toProbe);

                // ── Chebyshev visibility (Majercik 2019 §3.4) ────────
                {
                    float2 dirOct = DDGIOctEncode(normalize(-toProbe));
                    float2 depthOrigin = float2(
                        float(tileX * kDepthStride + 1u),
                        float(tileY * kDepthStride + 1u));
                    float2 depthUV = (depthOrigin + dirOct * float(kDepthTile))
                                   / float2(float(atlasDepthW),
                                            float(atlasDepthH));
                    // The writer stores both moments normalised by
                    // maxTraceDistance so the squared term fits FP16 (see
                    // ddgi_probe_update_cs.hlsl). Rescale with the same
                    // definition the writer uses:
                    //   maxTraceDistance = probeSpacing * probesPerAxis * 2
                    const float ddgiMaxTrace = _ddgiProbeSpacing
                                             * float(_ddgiProbesPerAxis) * 2.0;
                    float2 momentT = ddgiDepthTex.SampleLevel(linearSampler, depthUV, 0);
                    float meanD  = momentT.x * ddgiMaxTrace;
                    float meanD2 = momentT.y * (ddgiMaxTrace * ddgiMaxTrace);
                    float variance = max(meanD2 - meanD * meanD, 1e-4);
                    // VSM-style Chebyshev — fully visible if probe is closer
                    // than the surface, otherwise upper-bound the visibility
                    // by p_max = variance / (variance + diff²).
                    float visibility = 1.0;
                    if (distToProbe > meanD) {
                        float diff = distToProbe - meanD;
                        visibility = variance / (variance + diff * diff);
                        // Sharpen — Majercik recommends pow(p_max, 8) to
                        // remove residual leakage.
                        visibility = visibility * visibility * visibility;
                    }
                    if (visibility > 1e-3) ddgiCntVis += 1.0; else ddgiCntRej += 1.0;
                    wTri *= visibility;
                }

                if (wTri <= 0.0) continue;

                // Sample the irradiance atlas with the surface normal.
                float2 irradOrigin = float2(
                    float(tileX * kIrradStride + 1u),
                    float(tileY * kIrradStride + 1u));
                float2 irradUV = (irradOrigin + octIrrad * float(kIrradTile))
                               / float2(float(atlasIrradW),
                                        float(atlasIrradH));
                float3 probeIrrad = ddgiIrradTex.SampleLevel(linearSampler, irradUV, 0).rgb;

                ddgiIrrad += wTri * probeIrrad;
                ddgiTotalW += wTri;
            }
            dbgDdgiSupport = float4(ddgiTotalW, ddgiCntVis, ddgiCntRej, ddgiCntTri);

            if (ddgiTotalW > 1e-6) {
                ddgiIrrad /= ddgiTotalW;
                irradiance = ddgiIrrad * _ddgiIntensity;
                irradianceFromGI = true;
                dbgGiSource = 1u;
            }
        }
#endif

        // ── 2. Radiance cache (SDF sphere-traced spatially-varying SH) ───
#ifdef UZ_BINDLESS
        if (!irradianceFromGI && _rcEnabled != 0 && _rcProbesSrv != 0xFFFFFFFFu) {
            StructuredBuffer<float4> probeSH = ResourceDescriptorHeap[_rcProbesSrv];

            float3 gridOrigin = float3(_rcOriginX, _rcOriginY, _rcOriginZ);
            float  gridSize   = _rcExtent * 2.0;
            float3 uvw = saturate((worldPos - gridOrigin) / gridSize);

            float  ppa  = float(_rcProbesPerAxis);
            float3 cell = uvw * (ppa - 1.0);
            int3   c0   = int3(floor(cell));
            float3 frac3 = cell - float3(c0);
            int3   c1   = min(c0 + 1, int3(int(_rcProbesPerAxis) - 1,
                                            int(_rcProbesPerAxis) - 1,
                                            int(_rcProbesPerAxis) - 1));

            float3 rcIrrad = float3(0,0,0);
            float rcTotalWeight = 0.0;
            float rcCntFacing = 0.0, rcCntValid = 0.0, rcCntSurv = 0.0;   // region dump only
            [unroll]
            for (uint corner = 0; corner < 8; ++corner) {
                int3 idx3 = int3(
                    (corner & 1) ? c1.x : c0.x,
                    (corner & 2) ? c1.y : c0.y,
                    (corner & 4) ? c1.z : c0.z);
                float w = ((corner & 1) ? frac3.x : (1.0 - frac3.x))
                        * ((corner & 2) ? frac3.y : (1.0 - frac3.y))
                        * ((corner & 4) ? frac3.z : (1.0 - frac3.z));

                // Match the surfel gather: probes behind the tangent plane must not darken or light this side.
                //
                // Which probes lie in front of the surface is a question about the
                // surface, so weight with the GEOMETRIC normal, not the normal-mapped
                // shading normal. With N here, neighbouring texels of a bumpy wall
                // admitted different probe sets: a texel whose bump tilted toward a
                // probe out in the sun-lit atrium picked it up (and the renormalisation
                // below scaled it up further) while its neighbours rejected it — the
                // isolated white specks on Sponza's shadowed walls, which survived with
                // the sun off, reflections exhausted, gather off and GTAO off. The SDF
                // paths already weight with the field normal (`SamplePreviousDiffuse`,
                // `SdfReflectionIrradiance`); the shading normal still drives EvalSH_L2.
                float3 probePosition = gridOrigin + float3(idx3) * (gridSize / (ppa - 1.0));
                uint probeLinear = uint(idx3.x)
                                 + uint(idx3.y) * _rcProbesPerAxis
                                 + uint(idx3.z) * _rcProbesPerAxis * _rcProbesPerAxis;
                // A relocated probe (rc_probe_guard.hlsli) is weighted where it was traced.
                const float4 probeGuard = probeSH[ProbeGuardIndex(_rcProbesPerAxis, probeLinear)];
                float3 toProbe = ProbeEffectivePosition(probeGuard, probePosition) - worldPos;
                float probeDistance = length(toProbe);
                float3 probeFacing = dot(geometricNormal, geometricNormal) > 0.5 ? geometricNormal : N;
                float normalWeight = probeDistance > 0.0 ? saturate(dot(probeFacing, toProbe / probeDistance)) : 1.0;
                w *= normalWeight * normalWeight;
                if (normalWeight > 0.0) rcCntFacing += 1.0;

                uint base = probeLinear * 7u;
                // Record 6.w carries validity: a probe buried inside geometry
                // holds meaningless irradiance and must not be blended in.
                const bool rcValid = probeSH[base + 6].w > 0.0;
                // A probe kept near a surface by the candidate classification does not reach behind
                // that surface (rc_probe_guard.hlsli; no guard under the current rule).
                if (rcValid && ProbeGuardRejects(probeGuard, probePosition, worldPos)) w = 0.0;
                if (rcValid) rcCntValid += 1.0;
                if (rcValid && w > 0.0) rcCntSurv += 1.0;
                if (!rcValid) continue;

                float4 sh0 = probeSH[base + 0];
                float4 sh1 = probeSH[base + 1];
                float4 sh2 = probeSH[base + 2];
                float4 sh3 = probeSH[base + 3];
                float4 sh4 = probeSH[base + 4];
                float4 sh5 = probeSH[base + 5];
                float4 sh6 = probeSH[base + 6];
                float4 shArr[7] = { sh0, sh1, sh2, sh3, sh4, sh5, sh6 };

                float3 probeIrrad = EvalSH_L2(N, shArr);
                rcIrrad += w * max(probeIrrad, 0.0);
                rcTotalWeight += w;
            }
            dbgRcSupport = float4(rcTotalWeight, rcCntFacing, rcCntValid, rcCntSurv);

            // Zero is a valid answer: an unlit enclosure really does receive no
            // bounce light. Treating it as "not converged yet" and falling back
            // to sky light was what made interiors glow. Validity is decided on
            // the CPU (HasProducedResult) and per probe above; here we only
            // renormalise by the weights that survived rejection.
            irradiance = rcTotalWeight > 0.0 ? rcIrrad * (_rcGiIntensity / rcTotalWeight) : 0.0;
            irradianceFromGI = true;
            dbgGiSource = 2u;
        }
#endif

        // ── 3. Light Probe Volume (author-placed irradiance grid) ────────
#ifdef UZ_BINDLESS
        if (!irradianceFromGI
            && _lpvEnabled != 0 && _lpvProbeBufferSrv != 0xFFFFFFFFu
            && _lpvDimX > 0 && _lpvDimY > 0 && _lpvDimZ > 0
            && _lpvIntensity > 0.0) {
            StructuredBuffer<float4> lpvSH = ResourceDescriptorHeap[_lpvProbeBufferSrv];

            float3 lpvOrigin = float3(_lpvOriginX, _lpvOriginY, _lpvOriginZ);
            float3 lpvInvSize = float3(_lpvInvSizeX, _lpvInvSizeY, _lpvInvSizeZ);
            float3 lpvUVW = saturate((worldPos - lpvOrigin) * lpvInvSize);

            float3 lpvDims = float3(float(_lpvDimX), float(_lpvDimY), float(_lpvDimZ));
            float3 lpvCell = lpvUVW * max(lpvDims - 1.0, 0.0);
            int3   lpv0 = int3(floor(lpvCell));
            float3 lpvFrac = lpvCell - float3(lpv0);
            int3   lpv1 = min(lpv0 + 1,
                              int3(int(_lpvDimX) - 1,
                                   int(_lpvDimY) - 1,
                                   int(_lpvDimZ) - 1));

            float3 lpvIrrad = float3(0, 0, 0);
            [unroll]
            for (uint corner = 0; corner < 8; ++corner) {
                int3 idx3 = int3(
                    (corner & 1) ? lpv1.x : lpv0.x,
                    (corner & 2) ? lpv1.y : lpv0.y,
                    (corner & 4) ? lpv1.z : lpv0.z);
                float w = ((corner & 1) ? lpvFrac.x : (1.0 - lpvFrac.x))
                        * ((corner & 2) ? lpvFrac.y : (1.0 - lpvFrac.y))
                        * ((corner & 4) ? lpvFrac.z : (1.0 - lpvFrac.z));

                uint probeLinear = uint(idx3.x)
                                 + uint(idx3.y) * _lpvDimX
                                 + uint(idx3.z) * _lpvDimX * _lpvDimY;
                uint base = probeLinear * 7u;

                float4 sh0 = lpvSH[base + 0];
                float4 sh1 = lpvSH[base + 1];
                float4 sh2 = lpvSH[base + 2];
                float4 sh3 = lpvSH[base + 3];
                float4 sh4 = lpvSH[base + 4];
                float4 sh5 = lpvSH[base + 5];
                float4 sh6 = lpvSH[base + 6];
                float4 lpvShArr[7] = { sh0, sh1, sh2, sh3, sh4, sh5, sh6 };

                float3 probeIrrad = EvalSH_L2(N, lpvShArr);
                lpvIrrad += w * max(probeIrrad, 0.0);
            }

            if (dot(lpvIrrad, float3(1,1,1)) > 1e-5) {
                irradiance = lpvIrrad * _lpvIntensity;
                irradianceFromGI = true;
                dbgGiSource = 3u;
            }
        }
#endif

        // ── 4. Sky SH (fallback when no GI source provides data) ─────────
        // @ref Ramamoorthi & Hanrahan 2001 — L2 SH for diffuse irradiance
        if (!irradianceFromGI) {
            // The sky SH is projected in the cubemap's frame (+Y up; sh_projection.cpp walks the DX face
            // directions), so the normal is converted like every other cubemap read (split-sum item,
            // 2026-10-08: with the world normal every surface read the irradiance of the direction with y and
            // z swapped — an upward normal got the horizon's grey instead of the sky's blue).
            irradiance = EvalSH_L2(WorldToCubemap(N), skyIrradianceSH);

#ifdef UZ_BINDLESS
            if (IsSHZero(skyIrradianceSH)
                && gBufIndices.iblIrradianceSrvIdx != kSentinelSrvIndex) {
                TextureCube<float4> irrTex = ResourceDescriptorHeap[gBufIndices.iblIrradianceSrvIdx];
                irradiance = irrTex.SampleLevel(linearSampler, WorldToCubemap(N), 0).rgb;
            }
#endif

            // No macro-occlusion hack here any more. This used to multiply the
            // sky irradiance by the sun's shadow factor (10% floor) to fake the
            // visibility that a sky SH lookup does not have. It was tuned while
            // Quat::FromEuler put Sponza's sun at 9.1 degrees of elevation; with
            // the composition order fixed the sun is at 56 and most of the
            // interior falls outside direct light, so the hack drove whole walls
            // to 10% sky and auto-exposure then crushed them to black.
            //
            // Real visibility now comes from the SDF gather / radiance cache
            // (r.GI.FinalGather, r.RadianceCache), which the block was always
            // documented to defer to. Scenes with no GI solver get an unoccluded
            // sky hemisphere — brighter and flatter, but not wrong in a way that
            // a shadow factor can fix.
        }

        // skyLightMinAmbient provides a HARD floor on irradiance so the
        // shadow-modulated sky above cannot go below a minimum. Keep this
        // value low (0.005–0.02 in linear RGB) — high values defeat the
        // shadow modulation by lifting every pixel back up to full ambient.
        if (!irradianceFromGI) {
            irradiance = max(irradiance, skyLightMinAmbient);
        }
        // Irradiance from a GI source (DDGI, radiance cache, LPV) already carries the sky light colour and
        // intensity: the probe writers sample the sky cubemap times skyLightColor * intensity. Only the
        // sky SH fallback is untinted, so the diffuse tint applies to that path alone (block A.3).
        if (irradianceFromGI) skyTintDiffuse = 1.0;

        // Our SH coefficients are pre-convolved with cosine lobe (UE5 pattern),
        // so EvalSH_L2 returns irradiance E directly. No /PI needed — our
        // Frostbite diffuse BRDF already accounts for this.
        // (Three.js multiplies by PI then divides by PI — net identity)
        float3 cosineIrradiance = irradiance;
        dbgIrradiance    = irradiance;

        // ── Specular: prefiltered env + BRDF LUT (DFG) ──────────────────
        // @ref Three.js — reflection vector blended with normal for rough surfaces
        float3 R = reflect(-V, N);
        float r4 = roughness * roughness * roughness * roughness;
        R = normalize(lerp(R, N, r4));

        // The mip the bake wrote for this roughness. The engine's own bake (IBLBaker::BakeEnvironment)
        // prefilters mip m at roughness m / (mips - 1), so the read is linear in roughness. A PMREM
        // environment (bit 8 of prefilteredMipLevels, set by SetPMREMEnvironment) keeps the curve
        // written for Three.js CubeUV atlases, whose LODs follow non-linear roughness breakpoints
        // (r=0→LOD0, r=0.21→LOD4, r=0.305→LOD5, r=0.4→LOD6, r=0.8→LOD7, r=1.0→LOD8; the quadratic
        // r*(2-r), @ref Filament perceptualRoughnessToLod, scaled by 1.2 to push r=0.4 to mip 5.4).
        // Until the split-sum item (2026-10-08) the curve was applied to the engine's bake too, so
        // roughness 0.5 read a mip baked for 0.9 and the rough metals of the reflection reference
        // came out 3-5 x the integral of the BRDF over the environment.
        float maxMip = float((prefilteredMipLevels & 0xFFu) - 1u);
        const bool prefilteredIsPmrem = (prefilteredMipLevels & 0x100u) != 0u;
        float mipLevel = prefilteredIsPmrem
            ? clamp(roughness * (2.0 - roughness) * maxMip * 1.2, 0.0, maxMip)
            : clamp(roughness * maxMip, 0.0, maxMip);
        float3 radiance = float3(0, 0, 0);
        float2 envBRDF = float2(0, 0);
#ifdef UZ_BINDLESS
        if (gBufIndices.iblPrefilteredSrvIdx != kSentinelSrvIndex) {
            TextureCube<float4> prefTex = ResourceDescriptorHeap[gBufIndices.iblPrefilteredSrvIdx];
            radiance = prefTex.SampleLevel(linearSampler, WorldToCubemap(R), mipLevel).rgb;
        }
#endif
        // The specular counterpart of the diffuse hack was removed with it: the
        // prefiltered cubemap used to be multiplied by the sun's shadow factor
        // to suppress "hot spot" reflections inside enclosed rooms. Real
        // reflection visibility now comes from the SDF trace (r.GI.Reflections)
        // with SSR blended in below, both of which know what the surface can
        // actually see.
        // Analytical DFG approximation — replaces GPU-baked BRDF LUT with the
        // closed-form fit from Karis 2014. No texture lookup needed. This is the
        // same approach used by Three.js, Filament, and Frostbite.
        // Shared helper in ibl.hlsli so the specular and diffuse (NoV=1) call
        // sites cannot drift apart. The previous inline form added r.z where the
        // published fit adds r.y and omitted the (-1.04, 1.04) scale, inflating A.
        // No clamping: A and B leave [0,1] only marginally at NoV→0
        // (A ≈ -0.044, B ≈ 1.044) and their sum stays ≈ 1, which is what the
        // multi-scatter terms below rely on.
        // @ref Karis 2014 "Physically Based Shading on Mobile" — EnvBRDFApprox
#ifdef UZ_BINDLESS
        // World-space SDF gather. Diffuse replaces the probe-interpolated
        // irradiance (never adds to it), and reflections replace the prefiltered
        // cubemap lookup, with SSR blended in *before* the BRDF rather than
        // summed after TAA. Both neutralise their sky tint because the traced
        // radiance already carries the sky it hit.
        if (_sdfReflectionsEnabled != 0 || _sdfDiffuseSamples != 0) {
            SdfReflectionScene scene;
            [unroll] for (uint cascade = 0; cascade < 4; ++cascade)
                scene.clipmaps[cascade] = _sdfReflectionClipmaps[cascade];
            scene.clipmapSrv = _sdfReflectionClipmapSrv;
            scene.rawClipmapSrv = _sdfRawClipmapSrv;
            scene.skySrv = _sdfReflectionSkySrv;
            scene.probeSrv = _rcProbesSrv;
            scene.probesPerAxis = _rcProbesPerAxis;
            scene.probeOrigin = float3(_rcOriginX, _rcOriginY, _rcOriginZ);
            scene.probeExtent = _rcExtent;
            scene.voxelSize = _sdfReflectionVoxelSize;
            scene.traceDistance = _sdfReflectionTraceDistance;
            scene.traceSteps = _sdfReflectionTraceSteps;
            scene.sunDirection = sunL;
            scene.sunColor = lightColor.xyz;
            scene.skyMultiplier = skyTintSpecular;
            scene.plainMean = _sdfReflectionPlainMean;
            if (_sdfDiffuseSamples != 0) {
                Texture2D<float4> gatheredDiffuse = ResourceDescriptorHeap[ssgiSrvIndex];
                cosineIrradiance = gatheredDiffuse.Load(int3(px, 0)).rgb;
                dbgSsgiConsumed = cosineIrradiance;   // recorded before any threshold
                dbgSsgiMode = 1u;                     // 1 = SDF world gather
                dbgIrradiance = cosineIrradiance;     // r.Lighting.DebugMode 3: the irradiance that is lit
                dbgGiSource = 4u;
                skyTintDiffuse = 1.0;
                dbgPath |= 2u;
            }
            if (_sdfReflectionsEnabled != 0) {
                // timeParams.w < 0 is production (seed = frame count); a non-negative value is the
                // diagnostic seed freeze of `r.GI.Reflections.FreezeSeed`.
                const uint reflectionSeed = timeParams.w >= 0.0 ? uint(timeParams.w) : uint(timeParams.z);
                radiance = GatherSdfReflections(scene, worldPos, N, geometricNormal, reflect(-V, N), roughness,
                    uint2(px), reflectionSeed, _sdfReflectionSamples) * _sdfReflectionIntensity;
                dbgReflection = radiance;
                dbgPath |= 1u;
                skyTintSpecular = 1.0;
            }
        }
#endif
#ifdef UZ_BINDLESS
        // Block B.5: the screen-space reflection is blended before the BRDF over whichever layer is
        // active, the SDF gather above or the prefiltered cubemap, so the DFG / Fresnel / multi-scatter
        // terms below weight every layer alike (SSR -> SDF -> IBL by confidence). The additive post-TAA
        // composite that used to carry the IBL case is off by default (r.SSR.Composite).
        if (ssrSrvIndex != kSentinelSrvIndex) {
            Texture2D<float4> screenReflections = ResourceDescriptorHeap[ssrSrvIndex];
            ssrHit = screenReflections.SampleLevel(clampSampler, input.texcoord, 0);
            ssrHit.a = saturate(ssrHit.a);
            radiance = lerp(radiance, ssrHit.rgb, ssrHit.a);
            if (ssrHit.a > 0.0) dbgPath |= 128u;
        }
#endif
        envBRDF = EnvironmentBRDFApprox(roughness, NoV);

        // ── Split dielectric/metallic multi-scattering (Three.js pattern) ──
        // @ref Fdez-Aguera 2019 "A Multiple-Scattering Microfacet Model"
        // @ref Three.js computeMultiscattering — separate F0 per material type
        // dielectricF0/F90 come from the material's authored Specular (decoded
        // near the top of main); the ambient path must use the same reflectance
        // as the analytic lights above, not a hardcoded 4%.
        float3 metallicF0   = baseColor;

        // Dielectric path — authored F0, with the micro-occlusion F90.
        float3 FssEss_d = dielectricF0 * envBRDF.x + dielectricF90 * envBRDF.y;
        float  Ess_d    = envBRDF.x + envBRDF.y;
        float  Ems_d    = 1.0 - Ess_d;
        float3 Favg_d   = dielectricF0 + (dielectricF90 - dielectricF0) * 0.047619;
        float3 Fms_d    = FssEss_d * Favg_d / max(1.0 - Ems_d * Favg_d, 0.001);

        // Metallic path (F0 = baseColor)
        float3 FssEss_m = metallicF0 * envBRDF.x + envBRDF.y;
        float  Ess_m    = Ess_d; // same DFG integral
        float  Ems_m    = Ems_d;
        float3 Favg_m   = metallicF0 + (1.0 - metallicF0) * 0.047619;
        float3 Fms_m    = FssEss_m * Favg_m / max(1.0 - Ems_m * Favg_m, 0.001);

        // Blend by metallic factor
        float3 singleScatter = lerp(FssEss_d, FssEss_m, metallic);
        ssrSingleScatter = singleScatter;
        // Clamp multi-scatter to non-negative — analytical DFG can produce Ems < 0
        // at very low roughness, causing negative multi-scatter that cancels specular.
        float3 multiScatter  = max(float3(0,0,0), lerp(Fms_d * Ems_d, Fms_m * Ems_m, metallic));

        // Specular ambient: radiance × single-scatter + irradiance × multi-scatter.
        // @ref Fdez-Aguera 2019 "A Multiple-Scattering Microfacet Model"
        // @ref Three.js RE_IndirectSpecular — applies multi-scatter unconditionally
        // @ref Filament surface_light_indirect.fs — applies to all materials
        // Multi-scatter compensates energy lost by single-scatter GGX for ALL
        // material types including metals. No suppression needed.
        float3 specularAmbient = radiance * singleScatter
                               + cosineIrradiance * multiScatter;

        // Diffuse: energy-conserving using DIELECTRIC scattering only.
        //
        // @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR" §4.9.2 p.32
        //      — the diffuse energy compensation term represents the average
        //      energy NOT captured by the specular lobe over the whole
        //      hemisphere, which is a VIEW-INDEPENDENT quantity. It must be
        //      evaluated with NoV=1 (or, equivalently, from a 1D LUT indexed
        //      by roughness alone). Reusing the view-dependent `envBRDF`
        //      (a function of NoV) for this term causes the ambient diffuse
        //      to shift visibly when rotating the camera — the exact Sponza
        //      bug we hit after disabling Brixelizer GI.
        //
        // @ref Three.js RE_IndirectSpecular uses the NoV-dependent version
        //      (known discrepancy with Frostbite/Filament — prioritises ALU
        //      count over correctness).
        // Same published fit evaluated at NoV = 1 (view-independent split-sum
        // energy for the diffuse compensation term).
        float2 envBRDF_diff = EnvironmentBRDFApprox(roughness, 1.0);
        float3 FssEss_d_diff = dielectricF0 * envBRDF_diff.x + dielectricF90 * envBRDF_diff.y;
        float  Ess_d_diff    = envBRDF_diff.x + envBRDF_diff.y;
        float  Ems_d_diff    = 1.0 - Ess_d_diff;
        float3 Fms_d_diff    = FssEss_d_diff * Favg_d
                             / max(1.0 - Ems_d_diff * Favg_d, 0.001);
        float3 totalScatterDielectric = FssEss_d_diff + Fms_d_diff * Ems_d_diff;

        // Unified indirect diffuse irradiance (lumen-lite 4.1-4.4): one quantity,
        // E/pi in scene-radiance units (albedo + Lambert 1/pi excluded). Sky colour
        // reaches only the sky-SH fallback - skyTintDiffuse is 1.0 for every GI source
        // and for the world gather - so it is applied here, before the screen blend,
        // and screen-traced radiance is never tinted.
        float3 diffuseIrradiance = cosineIrradiance * skyTintDiffuse;

        // Screen-validity blend (4.1). The legacy screen trace writes the cosine-sampled
        // mean of hit radiance (misses = 0 => E_screen/pi) and, in its AO target, the
        // cosine-weighted fraction of rays with no on-screen hit. The screen term owns the
        // hemisphere it covered; the probe/sky cascade fills only the uncovered fraction:
        //     E/pi = ssgi + miss * fallback   (miss 0 = screen only, 1 = cascade only).
        // Summing the two unscaled double-counted the probe bounce (+1.4 dB vs the path
        // tracer on Sponza pose A). Skipped while the SDF world gather is on (it replaced
        // cosineIrradiance above and writes a constant 1.0 to the AO target).
#ifdef UZ_BINDLESS
        if (ssgiSrvIndex != kSentinelSrvIndex && _ssgiAO != kSentinelSrvIndex
            && _sdfDiffuseSamples == 0) {
            Texture2D<float4> ssgiTex   = ResourceDescriptorHeap[ssgiSrvIndex];
            Texture2D<float>  ssgiAoTex = ResourceDescriptorHeap[_ssgiAO];
            float3 ssgiColor = ssgiTex.Load(int3(px, 0)).rgb;
            float  ssgiMiss  = saturate(ssgiAoTex.Load(int3(px, 0)));
            dbgSsgiConsumed  = ssgiColor;
            dbgSsgiMode      = 2u;                  // 2 = legacy screen-space SSGI
            diffuseIrradiance = ssgiColor + ssgiMiss * diffuseIrradiance;
            dbgPath |= 4u;
        }
#endif

        float3 diffuseAmbient = (1.0 - metallic) * diffuseIrradiance * baseColor
                              * (1.0 - totalScatterDielectric);

        // SSR is applied POST-TAA to prevent TAA neighborhood contamination.
        // See ssr_composite_cs.hlsl — composites SSR onto TAA-resolved image.
        // The deferred lighting pass outputs IBL-only specular; SSR is added later.
        {
            // SSR composite moved to post-TAA pass. This block intentionally empty.
            // The singleScatter and multiScatter terms remain in specularAmbient (IBL only).
        }

        // Horizon occlusion — prevent reflections below surface.
        // @ref Lagarde 2012 "Horizon Occlusion for Normal Mapped Reflections"
        // Skip for metals — Three.js MeshStandardMaterial does not apply horizon
        // or specular occlusion to metallic surfaces.
        float horizon = saturate(1.0 + dot(R, N));
        float horizonFade = saturate(roughness * 4.0) * (1.0 - metallic);
        specularAmbient *= lerp(1.0, horizon * horizon, horizonFade);

        // Specular occlusion — rough dielectrics in occluded areas.
        // @ref Lagarde 2014 "Moving Frostbite to PBR" — specular occlusion
        // Metals get full specular (no occlusion) — matches Three.js behavior.
        float specOcclusion = (metallic > 0.5)
            ? 1.0
            : saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
        specOcclusionOut = specOcclusion;

        // SSGI is folded into the unified indirect diffuse above by the screen-validity
        // blend (lumen-lite 4.1); the old additive SSGI branch has been removed.

        // ── Brixelizer GI: add sparse SDF voxel indirect lighting ──────
        // @ref AMD 2024 "FidelityFX Brixelizer GI" — screen-space probe GI
        //
        // Brixelizer writes per-pixel diffuse irradiance to its own atlas.
        // We REPLACE the SH-only diffuseAmbient with the GI value when it
        // contains real data — Brixelizer is spatially-varying and already
        // accounts for the sky contribution, so adding it on top of the
        // sky SH would double-count light.
#ifdef UZ_BINDLESS
        if (brixGIDiffuseSrv != kSentinelSrvIndex) {
            Texture2D<float4> brixDiffTex = ResourceDescriptorHeap[brixGIDiffuseSrv];
            float3 brixDiffuse = brixDiffTex.Load(int3(px, 0)).rgb;
            // Only adopt the GI value when it actually contains data.
            // First few frames the atlas is still warming up — fall back
            // to the sky SH path until we get real spatially-varying
            // bounce light.
            float brixLuma = dot(brixDiffuse, float3(0.299, 0.587, 0.114));
            if (brixLuma > 0.001) {
                dbgPath |= 8u;
                // diffuseAmbient was previously set from sky SH +
                // multi-scattered Fms_d. Replace with the Brixelizer value
                // tinted by base color (modulated by 1-metallic so metals
                // still get their pure F0 specular path).
                diffuseAmbient = (1.0 - metallic) * brixDiffuse * baseColor;
            }
        }
#endif

        // Single combine for every material. The old chrome special case
        // (`metallic > 0.95 && roughness < 0.1` bypassing DFG, multi-scatter and
        // specular occlusion) existed to hide a warm ring artifact that came from
        // the incorrect envBRDF fit; with the published fit and the authored
        // F0/F90 above there is nothing left to hide, and the bypass was itself
        // an energy error on mirrors.
        //
        // `r.IndirectAO.Exponent` stays available for scenes that want extra
        // crevice darkening (the pre-port Sponza calibration used 3.0), but its
        // default is now 1.0, i.e. plain `ao` — an artistic exponent must be
        // opted into, not baked into every scene's indirect light.
        float aoForDiffuse = pow(saturate(ao), _indirectAOExponent);
        dbgDiffuseIndirect = diffuseAmbient * aoForDiffuse;  // AO once; sky tint was applied to the fallback before the blend
        dbgSpecularIndirect = specularAmbient * specOcclusion * skyTintSpecular;
        // Substituted before it contributes, so the render and the recording agree.
        if (replayActive) dbgSpecularIndirect = replaySpecular;
        ambient = dbgDiffuseIndirect + dbgSpecularIndirect;
    }

    // ── Environment MIS (temporally accumulated specular) ───────────────
#ifdef UZ_BINDLESS
    if (gBufIndices.envMisSrvIdx != kSentinelSrvIndex) {
        Texture2D<float4> envMisTex = ResourceDescriptorHeap[gBufIndices.envMisSrvIdx];
        float4 envMIS = envMisTex.Load(int3(px, 0));
        // The MIS estimate is the specular environment term for the pixels it covers; its alpha is the
        // coverage (1 on metals below the roughness threshold, fading to 0 across the thresholds, 0
        // elsewhere). It REPLACES the split-sum specular by that weight: adding it on top counted the
        // environment specular twice on metals (block A.3), and replacing wherever alpha was non-zero
        // took the split-sum term from every lit pixel while the pass wrote alpha 1 everywhere (block
        // B.5b). The pass applies the sky light tint itself.
        if (envMIS.a > 0.0 && !replayActive) {
            // Block B.5: the screen-space reflection is the first layer here too. The MIS term already
            // carries the BRDF weight, so the raw hit radiance takes the single-scatter Fresnel term before
            // the confidence blend (the same weight the split-sum path applies to its blended radiance).
            float3 misSpecular = lerp(envMIS.rgb, ssrHit.rgb * ssrSingleScatter, ssrHit.a) * specOcclusionOut;
            float3 specular = lerp(dbgSpecularIndirect, misSpecular, envMIS.a);
            ambient = dbgDiffuseIndirect + specular;
            dbgEmissiveAndMis += misSpecular * envMIS.a;
            dbgSpecularIndirect = specular;
            dbgPath |= 16u;
        }
    }
#endif

    // ── Transmission / Glass (smID == 9: ThinTranslucent) ──────────────
    // @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-Based Rendering"
    // @ref Walter 2007 "Microfacet Models for Refraction" — Snell's law refraction
    // @ref Three.js MeshPhysicalMaterial.transmission — KHR_materials_transmission
    //
    // Deferred approximation: Fresnel-weighted blend of reflected and refracted
    // environment cubemap samples. No true screen-space refraction (would need
    // forward pass with scene color target).
    //
    // At normal incidence (center): ~96% transmission, ~4% reflection (F0=0.04 for glass)
    // At grazing angles (rim): ~100% reflection (Fresnel → 1.0)
#ifdef UZ_BINDLESS
    const bool glassEnvAvailable = gBufIndices.iblPrefilteredSrvIdx != kSentinelSrvIndex;
#else
    // The SM5 path declares no prefiltered cubemap register (the split-sum IBL above is bindless-only
    // too), so the environment this approximation samples is absent: same as the bindless sentinel.
    const bool glassEnvAvailable = false;
#endif
    if (transmission > 0.0 && glassEnvAvailable) {
        // @ref Three.js MeshPhysicalMaterial — specular=0.5 → F0=0.04 → IOR=1.5
        // @constants F0_dielectric = specular * 0.08, IOR = (1+√F0)/(1-√F0)
        float specularFromAlpha = frac(packedAlpha / 16.0) * 16.0;
        float f0_scalar = max(specularFromAlpha * 0.08, 0.001);
        float sqrtF0 = sqrt(f0_scalar);
        float ior = (1.0 + sqrtF0) / max(1.0 - sqrtF0, 0.001);
        ior = clamp(ior, 1.0, 2.5);

        // @ref Schlick 1994 — Fresnel approximation for dielectrics
        // @constants F0_glass = ((ior-1)/(ior+1))^2 = 0.04 for IOR=1.5
        float F0_glass = pow((ior - 1.0) / (ior + 1.0), 2.0);
        float fresnel = F0_glass + (1.0 - F0_glass) * pow(1.0 - NoV, 5.0);

        // Reflection: sample prefiltered cubemap at reflection direction
        float3 R_glass = reflect(-V, N);
        float3 reflectedColor = float3(0, 0, 0);
#ifdef UZ_BINDLESS
        TextureCube<float4> prefTexT = ResourceDescriptorHeap[gBufIndices.iblPrefilteredSrvIdx];
        reflectedColor = prefTexT.SampleLevel(linearSampler, WorldToCubemap(R_glass), roughness * float((prefilteredMipLevels & 0xFFu) - 1u)).rgb;
#endif

        // @ref Walter 2007 — Snell's law refraction direction via HLSL refract()
        // @constants eta = 1/IOR (air→glass), e.g. eta=0.667 for IOR=1.5
        //
        // Deferred glass approximation: without screen-space refraction, we sample
        // the environment cubemap in the VIEW-THROUGH direction (-V) to simulate
        // seeing "behind" the glass. This makes glass visually distinct from chrome
        // (which reflects in the mirror direction). The refracted cubemap at IOR=1.5
        // looks nearly identical to reflection for a sphere, so using -V is more
        // perceptually correct for a deferred pipeline.
        float3 refractedColor = float3(0, 0, 0);
#ifdef UZ_BINDLESS
        // Sample environment in the view-through direction (what's behind the glass).
        // Use a slight roughness mip to soften the transmitted image (frosted glass effect
        // that visually separates it from the sharp background).
        float transmitMip = max(roughness, 0.15) * float((prefilteredMipLevels & 0xFFu) - 1u);
        refractedColor = prefTexT.SampleLevel(linearSampler, WorldToCubemap(-V), transmitMip).rgb;
#endif

        // Blend: Fresnel determines reflection vs transmission ratio.
        // At center (NoV≈1): fresnel≈0.04 → 96% view-through (glass is nearly transparent)
        // At rim (NoV≈0): fresnel≈1.0 → bright mirror reflection (Fresnel rim)
        // @ref Schlick 1994, Three.js MeshPhysicalMaterial.transmission
        float3 glassColor = lerp(refractedColor, reflectedColor, fresnel);

        Lo = float3(0, 0, 0);
        ambient = glassColor * transmission;
        emissive = float3(0, 0, 0);
        dbgDiffuseIndirect = ambient;
        dbgSpecularIndirect = float3(0, 0, 0);
        dbgEmissiveAndMis = float3(0, 0, 0);
        dbgPath |= 64u;
    }

    // ── Aerial Perspective ───────────────────────────────────────────────
    // @ref Hillaire 2020 "A Scalable Sky and Atmosphere Rendering Technique"
    // Sample precomputed 32x32x16 AP volume: RGB = inscattering, A = mean transmittance.
    // Applied after all lighting to attenuate distant objects and add atmospheric haze.
    float3 color = Lo + ambient + emissive;

    // r.Lights.ClusterDebug (block B.7): the pixel's cluster light count. Blue 0, green 1-4,
    // yellow 5-10, red 11+, magenta = the cluster overflowed its slots (every light evaluated).
    if ((clusterFlags & kClusterFlag_Debug) != 0u) {
        float3 heat = float3(0.0, 0.0, 0.5);
        if (dbgClusterOverflow)          heat = float3(1.0, 0.0, 1.0);
        else if (dbgClusterCount >= 11u) heat = float3(1.0, 0.0, 0.0);
        else if (dbgClusterCount >= 5u)  heat = float3(1.0, 1.0, 0.0);
        else if (dbgClusterCount >= 1u)  heat = float3(0.0, 1.0, 0.0);
        color = heat * 0.25 + color * 0.15;
    }

    // ── Lighting decomposition debug (r.Lighting.DebugMode) ─────────────
    // Replaces the composed color with an isolated term so each stage of
    // the indirect cascade can be captured headlessly (--hidden --cvar
    // r.Lighting.DebugMode=N --screenshot). Placed BEFORE aerial
    // perspective so haze does not pollute the term being inspected.
    // Modes: 1 direct (Lo), 2 indirect (ambient), 3 GI irradiance chosen by
    // the cascade (pre-albedo, pre-AO), 4 the SSGI / SDF-gather texture as the
    // pass consumed it, 5 AO, 6 GI source map (green DDGI, blue RC, yellow LPV,
    // magenta SDF world gather, red sky-SH fallback). The mode arrives as a root constant, so the SM5
    // path (no GBufferIndices block) always renders mode 0.
#ifdef UZ_BINDLESS
    const uint lightingDebugMode = gBufIndices.lightingDebugMode;
#else
    const uint lightingDebugMode = 0u;
#endif
    if (lightingDebugMode != 0u) {
        if      (lightingDebugMode == 1u) color = Lo;
        else if (lightingDebugMode == 2u) color = ambient;
        else if (lightingDebugMode == 3u) color = dbgIrradiance;
        else if (lightingDebugMode == 4u) color = dbgSsgiConsumed;
        else if (lightingDebugMode == 5u) color = float3(ao, ao, ao);
        else if (lightingDebugMode == 6u) {
            // GI source map: green=DDGI, blue=RC, yellow=LPV, magenta=SDF gather, red=sky-SH.
            if      (dbgGiSource == 1u) color = float3(0, 1, 0);
            else if (dbgGiSource == 2u) color = float3(0, 0, 1);
            else if (dbgGiSource == 3u) color = float3(1, 1, 0);
            else if (dbgGiSource == 4u) color = float3(1, 0, 1);
            else                        color = float3(1, 0, 0);
        }

        LightingOutput dbgOut;
        dbgOut.hdr      = float4(max(color, 0.0), 1.0);
        dbgOut.direct   = float4(max(Lo, 0.0), 1.0);
        dbgOut.indirect = float4(max(ambient, 0.0), 1.0);
        dbgOut.debug    = float4(shadow, ao, 0.0, 1.0);
        return dbgOut;
    }


#ifdef UZ_BINDLESS
    if (aerialPerspectiveEnabled != 0 && apVolumeSrvIndex != kSentinelSrvIndex) {
        Texture3D<float4> apVolume = ResourceDescriptorHeap[apVolumeSrvIndex];

        // AP volume UVW: XY = screen UV, Z = depth slice (quadratic distribution).
        // @ref Hillaire 2020 — exponential/quadratic depth slicing for near-camera detail.
        float2 screenUV = input.texcoord;
        // totalDepthKm = 100.0, convert viewDepth from cm to km
        float depthKm = viewDepth * kKmPerCm;
        float totalDepthKm = 100.0;
        float depthFraction = saturate(depthKm / totalDepthKm);
        // Inverse quadratic: volume was written with t*t distribution, so sample with sqrt
        float w = sqrt(depthFraction);

        float4 ap = apVolume.SampleLevel(linearSampler, float3(screenUV, w), 0);
        // ap.rgb = inscattering (additive haze), ap.a = mean transmittance
        color = color * ap.a + ap.rgb;
        dbgPath |= 32u;
    }
#endif

#ifdef UZ_BINDLESS
    // Region dump (diagnostics): lighting-side records, see common/region_dump.hlsli.
    WriteRegionDumpLighting(uint2(px), dbgReflection, roughness, N, length(worldPos - cameraPos.xyz),
                            Lo, shadow, dbgDiffuseIndirect, ao, dbgSpecularIndirect, metallic,
                            emissive + dbgEmissiveAndMis, dbgPath,
                            dbgSsgiConsumed, dbgSsgiMode,
                            (clusterFlags & kClusterFlag_Debug) != 0u ? dbgClusterInfo : dbgRcSupport, dbgDdgiSupport);
#endif
    LightingOutput output;
    output.hdr      = float4(max(color, 0.0), 1.0);
    output.direct   = float4(max(Lo, 0.0), 1.0);
    output.indirect = float4(max(ambient, 0.0), 1.0);
    output.debug    = float4(shadow, ao, 0.0, 1.0);
    return output;
}
