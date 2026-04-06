/// UzEngine deferred lighting — simplified Falcor-style pipeline.
/// GBuffer read → directional NdotL → point lights → IBL → HDR output.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4"
/// @ref Lagarde 2014 "Moving Frostbite to PBR"

#include "../common/constants.hlsli"
#include "../common/encoding.hlsli"
#include "../material/brdf.hlsli"
#include "../material/diffuse.hlsli"
#include "light_types.hlsli"
#include "ibl.hlsli"

/// Z-up world direction → Y-up DX cubemap convention.
/// Must match skybox_cubemap_ps.hlsl and sky_cubemap_cs.hlsl.
float3 WorldToCubemap(float3 dir) { return float3(dir.x, dir.z, dir.y); }
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
};
ConstantBuffer<GBufferIndices> gBufIndices : register(b0);
#else
Texture2D<float4> gbufferAlbedo    : register(t0);
Texture2D<float4> gbufferNormal    : register(t1);
Texture2D<float>  gbufferDepth     : register(t2);
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
    float  _pad2;                   //         (764)
};

// LightingCB at b2 via light_types.hlsli (already declared above)

// Shadow includes AFTER PerFrameCB — they reference CVar fields from the CB.
#include "shadow.hlsli"
#include "contact_shadows.hlsli"

SamplerState      linearSampler : register(s0);
SamplerState      clampSampler  : register(s2);

// ─── Input ─────────────────────────────────────────────────────────────────

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─── Entry Point ───────────────────────────────────────────────────────────

float4 main(FSInput input) : SV_Target {
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

    // Sky pixels: reversed-Z far = 0
    if (z <= 0.0001) discard;

    // ── Decode GBuffer ────────────────────────────────────────────────────
    float3 baseColor = rt0.rgb;
    float  ao        = rt0.a;  // mesh AO from GBuffer

    // GTAO: combine with mesh AO when available.
    // SM5 path: fixed texture register t9.
    // Bindless path: SRV index from PerFrameCB._gtaoSrv.
#ifdef UZ_BINDLESS
    if (_gtaoSrv != 0xFFFFFFFFu && _gtaoSrv > 0) {
        Texture2D<float> gtaoTex = ResourceDescriptorHeap[_gtaoSrv];
        float gtao = gtaoTex.Load(int3(px, 0));
        if (gtao > 0.01) ao = min(ao, gtao);
    }
#else
    {
        float gtao = gtaoTextureT9.Load(int3(px, 0)).r;
        if (gtao > 0.01) ao = min(ao, gtao);
    }
#endif

    // minOcclusion: clamp AO so fully occluded areas still receive configurable
    // minimum ambient. Affects both diffuse (ao * diffuseAmbient) and specular
    // occlusion paths. @ref SkyLightComponent.minOcclusion
    ao = max(ao, skyLightMinAmbient);

    float3 N         = OctDecode(rt1.rg);
    float  metallic  = rt1.b;
    float  roughness = max(rt1.a, 0.04);
    float  alpha     = roughness * roughness;

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

    float3 F0         = ComputeF0(baseColor, metallic);
    float3 diffColor  = baseColor * (1.0 - metallic);

    float3 V   = normalize(cameraPos.xyz - worldPos);
    float  NoV = max(dot(N, V), 1e-4);

    // ── Directional light + CSM shadows ─────────────────────────────────
    float  viewDepth = length(cameraPos.xyz - worldPos);
    float3 sunL = normalize(lightDir.xyz);
    float  sunNoL = max(dot(N, sunL), 0.0);
    float  shadow = CascadeShadow(worldPos, viewDepth);

    // Screen-space contact shadows: fine detail shadows from small features
    // (column edges, window frames, floor contact). Gated by CVar r.ContactShadows.
    if (_contactShadowLen > 0.0) {
        uint csW, csH;
#ifdef UZ_BINDLESS
        Texture2D<float> csDpth = ResourceDescriptorHeap[gBufIndices.depthIdx];
        csDpth.GetDimensions(csW, csH);
        float cs = ComputeContactShadow(worldPos, sunL, csDpth,
                                         viewProj, invViewProj,
                                         csW, csH, nearPlane, farPlane);
#else
        gbufferDepth.GetDimensions(csW, csH);
        float cs = ComputeContactShadow(worldPos, sunL,
                                         viewProj, invViewProj,
                                         csW, csH, nearPlane, farPlane);
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
            float3 F = F_Schlick(F0, float3(1, 1, 1), VoH);
            float3 spec = D * Vis * F;

            // Diffuse: Frostbite energy-conserving (already divided by PI)
            float3 diff = EvalFrostbiteDiffuse(diffColor, roughness, NoV, NoL, VoH);

            Lo = (diff + spec) * lightColor.xyz * NoL * shadow;
        }
    }

    // ── Point lights ──────────────────────────────────────────────────────
    for (uint pi = 0; pi < pointLightCount; ++pi) {
        PointLightData pl = pointLights[pi];
        float3 toLight = pl.position - worldPos;
        float  distSq  = dot(toLight, toLight);
        if (distSq > pl.radius * pl.radius) continue;

        float3 Lp   = normalize(toLight);
        float  NoLp = max(dot(N, Lp), 0.0);
        if (NoLp <= 0.0) continue;

        float invRadSq = 1.0 / (pl.radius * pl.radius);
        float f = distSq * invRadSq;
        float s = saturate(1.0 - f * f);
        float atten = (s * s) / max(distSq, 0.01);

        float3 Hp   = normalize(V + Lp);
        float  NoHp = saturate(dot(N, Hp));
        float  VoHp = saturate(dot(V, Hp));

        float  Dp   = D_GGX(alpha, NoHp);
        float  Visp = V_SmithGGXCorrelated(alpha, NoV, NoLp);
        float3 Fp   = F_Schlick(F0, float3(1,1,1), VoHp);
        float3 specp = Dp * Visp * Fp;
        float3 diffp = EvalFrostbiteDiffuse(diffColor, roughness, NoV, NoLp, VoHp);

        Lo += (diffp + specp) * pl.color * NoLp * atten;
    }

    // ── Spot lights ───────────────────────────────────────────────────────
    for (uint si = 0; si < spotLightCount; ++si) {
        SpotLightData sl = spotLights[si];
        float3 toLight = sl.position - worldPos;
        float  distSq  = dot(toLight, toLight);
        if (distSq > sl.radius * sl.radius) continue;

        float3 Ls   = normalize(toLight);
        float  NoLs = max(dot(N, Ls), 0.0);
        if (NoLs <= 0.0) continue;

        float invRadSq = 1.0 / (sl.radius * sl.radius);
        float f = distSq * invRadSq;
        float s = saturate(1.0 - f * f);
        float distAtten = (s * s) / max(distSq, 0.01);

        float cosA = dot(Ls, -sl.direction);
        float cone = saturate((cosA - sl.cosOuterAngle) * sl.invCosConeDifference);
        float atten = distAtten * cone * cone;
        if (atten <= 0.0) continue;

        float3 Hs   = normalize(V + Ls);
        float  NoHs = saturate(dot(N, Hs));
        float  VoHs = saturate(dot(V, Hs));

        float  Ds   = D_GGX(alpha, NoHs);
        float  Viss = V_SmithGGXCorrelated(alpha, NoV, NoLs);
        float3 Fs   = F_Schlick(F0, float3(1,1,1), VoHs);
        float3 specs = Ds * Viss * Fs;
        float3 diffs = EvalFrostbiteDiffuse(diffColor, roughness, NoV, NoLs, VoHs);

        Lo += (diffs + specs) * sl.color * NoLs * atten;
    }

    // ── IBL ambient (Falcor split-sum pattern) ──────────────────────────
    // @ref Karis 2013 "Real Shading in Unreal Engine 4" — split-sum IBL
    // @ref Falcor StandardBSDF — F_SchlickRoughness for env Fresnel
    //
    // IBL (indirect lighting) is NOT attenuated by directional shadow.
    // @ref UE5 DeferredLightingCommon.ush — IndirectLighting is independent of shadow.
    // Only AO affects indirect; shadows only affect direct lights.
    float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB) * skyLightIntensity;
    float3 ambient = float3(0, 0, 0);
    {
        // ── Diffuse irradiance: SH evaluation → cubemap fallback ─────────
        // @ref Ramamoorthi & Hanrahan 2001 — L2 SH is sufficient for diffuse irradiance
        float3 irradiance = EvalSH_L2(N, skyIrradianceSH);

#ifdef UZ_BINDLESS
        if (IsSHZero(skyIrradianceSH) && gBufIndices.iblIrradianceSrvIdx != kSentinelSrvIndex) {
            TextureCube<float4> irrTex = ResourceDescriptorHeap[gBufIndices.iblIrradianceSrvIdx];
            irradiance = irrTex.SampleLevel(linearSampler, WorldToCubemap(N), 0).rgb;
        }
#endif
        // ── Radiance Cache probe grid: spatially-varying diffuse GI ──────
        // @ref McGuire 2017 "Real-Time GI using Precomputed Light Field Probes"
        // Trilinear interpolation of 8 surrounding probes' L2 SH coefficients.
#ifdef UZ_BINDLESS
        if (_rcEnabled != 0 && _rcProbesSrv != 0xFFFFFFFFu) {
            StructuredBuffer<float4> probeSH = ResourceDescriptorHeap[_rcProbesSrv];

            // Map world position to probe grid normalized coordinates [0,1]
            float3 gridOrigin = float3(_rcOriginX, _rcOriginY, _rcOriginZ);
            float  gridSize   = _rcExtent * 2.0;
            float3 uvw = (worldPos - gridOrigin) / gridSize;
            uvw = saturate(uvw);

            // Grid dimensions (uniform: probesPerAxis^3)
            float  ppa  = float(_rcProbesPerAxis);
            float3 cell = uvw * (ppa - 1.0);
            int3   c0   = int3(floor(cell));
            float3 frac3 = cell - float3(c0);
            int3   c1   = min(c0 + 1, int3(int(_rcProbesPerAxis) - 1,
                                            int(_rcProbesPerAxis) - 1,
                                            int(_rcProbesPerAxis) - 1));

            // Trilinear: fetch 8 corner probes, evaluate SH, blend
            float3 rcIrrad = float3(0,0,0);
            [unroll]
            for (uint corner = 0; corner < 8; ++corner) {
                int3 idx3 = int3(
                    (corner & 1) ? c1.x : c0.x,
                    (corner & 2) ? c1.y : c0.y,
                    (corner & 4) ? c1.z : c0.z);
                float w = ((corner & 1) ? frac3.x : (1.0 - frac3.x))
                        * ((corner & 2) ? frac3.y : (1.0 - frac3.y))
                        * ((corner & 4) ? frac3.z : (1.0 - frac3.z));

                uint probeLinear = uint(idx3.x)
                                 + uint(idx3.y) * _rcProbesPerAxis
                                 + uint(idx3.z) * _rcProbesPerAxis * _rcProbesPerAxis;
                uint base = probeLinear * 7u; // 7 float4 per probe

                // Load 7 packed SH float4s and evaluate
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
            }

            // Blend radiance cache with global SH: RC overrides when available
            float rcIntensity = _rcGiIntensity;
            irradiance = lerp(irradiance, rcIrrad, rcIntensity);
        }
#endif

        // skyLightMinAmbient provides a floor on irradiance so fully occluded
        // interior pixels still receive some ambient light.
        irradiance = max(irradiance, skyLightMinAmbient);

        // Our SH coefficients are pre-convolved with cosine lobe (UE5 pattern),
        // so EvalSH_L2 returns irradiance E directly. No /PI needed — our
        // Frostbite diffuse BRDF already accounts for this.
        // (Three.js multiplies by PI then divides by PI — net identity)
        float3 cosineIrradiance = irradiance;

        // ── Specular: prefiltered env + BRDF LUT (DFG) ──────────────────
        // @ref Three.js — reflection vector blended with normal for rough surfaces
        float3 R = reflect(-V, N);
        float r4 = roughness * roughness * roughness * roughness;
        R = normalize(lerp(R, N, r4));

        // @ref Three.js PMREMGenerator roughnessToMip — piecewise non-linear mapping
        // Three.js CubeUV PMREM maps roughness to LOD index using breakpoints:
        //   r=0→LOD0, r=0.21→LOD4, r=0.305→LOD5, r=0.4→LOD6, r=0.8→LOD7, r=1.0→LOD8
        // Our cubemap extracted from PMREM has LOD 0→mip 0, etc. The PMREM atlas
        // stores each LOD at progressively higher roughness prefilter levels.
        // Since the atlas encodes roughness non-linearly, we remap to match:
        // @ref Filament perceptualRoughnessToLod: r*(2-r) — quadratic, similar shape
        // Scale by 1.2 to push roughness=0.4 from mip 4.5 closer to mip 5.4 (face=18px).
        float maxMip = float(prefilteredMipLevels - 1);
        float mipLevel = clamp(roughness * (2.0 - roughness) * maxMip * 1.2, 0.0, maxMip);
        float3 radiance = float3(0, 0, 0);
        float2 envBRDF = float2(0, 0);
#ifdef UZ_BINDLESS
        if (gBufIndices.iblPrefilteredSrvIdx != kSentinelSrvIndex) {
            TextureCube<float4> prefTex = ResourceDescriptorHeap[gBufIndices.iblPrefilteredSrvIdx];
            radiance = prefTex.SampleLevel(linearSampler, WorldToCubemap(R), mipLevel).rgb;
        }
#endif
        // Analytical DFG approximation — replaces GPU-baked BRDF LUT with the
        // closed-form fit from Karis 2014. No texture lookup needed. This is the
        // same approach used by Three.js, Filament, and Frostbite.
        // @ref Karis 2014 "Physically Based Shading" — analytical DFG approximation
        // @ref Three.js WebGLPrograms DFGApprox — production validation
        // @ref Karis 2014 "Physically Based Shading" — analytical DFG approximation
        // @ref Three.js WebGLPrograms DFGApprox — production validation
        {
            float a = roughness;
            float4 r = float4(-1.0, -0.0275, -0.572, 0.022) * a + float4(1.0, 0.0425, 1.04, -0.04);
            float ab = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.z;
            envBRDF = float2(ab, r.y * a + r.w);
            // Clamp to physical range — the analytical fit can exceed 1.0 at
            // very low roughness, causing overbright specular rings on chrome.
            envBRDF.x = saturate(envBRDF.x);
            envBRDF.y = max(envBRDF.y, 0.0);
        }

        // ── Split dielectric/metallic multi-scattering (Three.js pattern) ──
        // @ref Fdez-Aguera 2019 "A Multiple-Scattering Microfacet Model"
        // @ref Three.js computeMultiscattering — separate F0 per material type
        float3 dielectricF0 = float3(0.04, 0.04, 0.04);
        float3 metallicF0   = baseColor;

        // Dielectric path (F0 = 0.04)
        float3 FssEss_d = dielectricF0 * envBRDF.x + envBRDF.y;
        float  Ess_d    = envBRDF.x + envBRDF.y;
        float  Ems_d    = 1.0 - Ess_d;
        float3 Favg_d   = dielectricF0 + (1.0 - dielectricF0) * 0.047619;
        float3 Fms_d    = FssEss_d * Favg_d / max(1.0 - Ems_d * Favg_d, 0.001);

        // Metallic path (F0 = baseColor)
        float3 FssEss_m = metallicF0 * envBRDF.x + envBRDF.y;
        float  Ess_m    = Ess_d; // same DFG integral
        float  Ems_m    = Ems_d;
        float3 Favg_m   = metallicF0 + (1.0 - metallicF0) * 0.047619;
        float3 Fms_m    = FssEss_m * Favg_m / max(1.0 - Ems_m * Favg_m, 0.001);

        // Blend by metallic factor
        float3 singleScatter = lerp(FssEss_d, FssEss_m, metallic);
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

        // Diffuse: energy-conserving using DIELECTRIC scattering only
        // @ref Three.js RE_IndirectSpecular — diffuse *= (1.0 - totalScatteringDielectric)
        float3 totalScatterDielectric = FssEss_d + Fms_d * Ems_d;
        float3 diffuseAmbient = (1.0 - metallic) * cosineIrradiance * baseColor
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

        // SSGI: add screen-space indirect diffuse if available
#ifdef UZ_BINDLESS
        if (ssgiSrvIndex != kSentinelSrvIndex) {
            Texture2D<float4> ssgiTex = ResourceDescriptorHeap[ssgiSrvIndex];
            float3 ssgiColor = ssgiTex.Load(int3(px, 0)).rgb;
            if (any(ssgiColor > 0.001))
                diffuseAmbient += (1.0 - metallic) * ssgiColor * baseColor;
        }
#else
        {
            float3 ssgiColor = ssgiTextureT12.Load(int3(px, 0)).rgb;
            if (any(ssgiColor > 0.001))
                diffuseAmbient += (1.0 - metallic) * ssgiColor * baseColor;
        }
#endif

        // @ref Three.js MeshStandardMaterial — metallic mirrors use pure radiance
        // without DFG scaling, multi-scatter, or specular occlusion. This avoids
        // the warm ring artifact caused by envBRDF angular variation and irradiance
        // bleed at grazing angles.
        if (metallic > 0.95 && roughness < 0.1) {
            ambient = radiance * metallicF0 * skyTint;
        } else {
            ambient = (diffuseAmbient * ao + specularAmbient * specOcclusion) * skyTint;
        }
    }

    // ── Environment MIS (temporally accumulated specular) ───────────────
#ifdef UZ_BINDLESS
    if (gBufIndices.envMisSrvIdx != kSentinelSrvIndex) {
        Texture2D<float4> envMisTex = ResourceDescriptorHeap[gBufIndices.envMisSrvIdx];
        float3 envMIS = envMisTex.Load(int3(px, 0)).rgb;
        ambient += envMIS;
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
    if (transmission > 0.0 && gBufIndices.iblPrefilteredSrvIdx != kSentinelSrvIndex) {
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
        reflectedColor = prefTexT.SampleLevel(linearSampler, WorldToCubemap(R_glass), roughness * float(prefilteredMipLevels - 1)).rgb;
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
        float transmitMip = max(roughness, 0.15) * float(prefilteredMipLevels - 1);
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
    }

    // ── Aerial Perspective ───────────────────────────────────────────────
    // @ref Hillaire 2020 "A Scalable Sky and Atmosphere Rendering Technique"
    // Sample precomputed 32x32x16 AP volume: RGB = inscattering, A = mean transmittance.
    // Applied after all lighting to attenuate distant objects and add atmospheric haze.
    float3 color = Lo + ambient + emissive;


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
    }
#endif

    return float4(max(color, 0.0), 1.0);
}
