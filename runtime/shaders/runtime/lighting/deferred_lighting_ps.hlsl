/// UzEngine deferred lighting — simplified Falcor-style pipeline.
/// GBuffer read → directional NdotL → point lights → IBL → HDR output.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4"
/// @ref Lagarde 2014 "Moving Frostbite to PBR"

#include "../common/constants.hlsli"
#include "../common/encoding.hlsli"
#include "../material/brdf.hlsli"
#include "../material/diffuse.hlsli"
#include "light_types.hlsli"
#include "shadow.hlsli"

// ─── Root constants at b0 ──────────────────────────────────────────────────

#ifdef UZ_BINDLESS
struct GBufferIndices {
    uint albedoIdx;
    uint normalIdx;
    uint depthIdx;
    uint positionIdx;
    uint emissiveIdx;
    uint envMisSrvIdx;  ///< Temporally-accumulated env MIS specular (kSentinelSrvIndex = disabled).
};
ConstantBuffer<GBufferIndices> gBufIndices : register(b0);
#else
Texture2D<float4> gbufferAlbedo    : register(t0);
Texture2D<float4> gbufferNormal    : register(t1);
Texture2D<float>  gbufferDepth     : register(t2);
Texture2D<float4> gbufferPositionWS : register(t4);
Texture2D<float4> gbufferEmissive  : register(t10);
#endif

// ─── PerFrameCB at b1 (768 bytes) ─────────────────────────────────────────

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    float4 timeParams;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
    row_major float4x4 invViewProj;
    float  nearPlane, farPlane;
    uint   irradianceSrvIndex, prefilteredSrvIndex;
    uint   brdfLutSrvIndex, prefilteredMipLevels;
    float  skyLightIntensity, skyLightColorR, skyLightColorG, skyLightColorB;
    float  skyLightMinAmbient, exposure;
    float4 skyIrradianceSH[7];
    uint   shadowDebugMode;
    // rest of 768 bytes not needed — padding implicit
};

// LightingCB at b2 via light_types.hlsli (already declared above)

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
    float  ao        = rt0.a;
    float3 N         = OctDecode(rt1.rg);
    float  metallic  = rt1.b;
    float  roughness = max(rt1.a, 0.04);
    float  alpha     = roughness * roughness;

    float3 F0         = ComputeF0(baseColor, metallic);
    float3 diffColor  = baseColor * (1.0 - metallic);

    float3 V   = normalize(cameraPos.xyz - worldPos);
    float  NoV = max(dot(N, V), 1e-4);

    // ── Directional light + CSM shadows ─────────────────────────────────
    float  viewDepth = length(cameraPos.xyz - worldPos);
    float3 sunL = normalize(lightDir.xyz);
    float  sunNoL = max(dot(N, sunL), 0.0);
    float  shadow = CascadeShadow(worldPos, viewDepth);

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
    float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB) * skyLightIntensity;
    float3 ambient = float3(0, 0, 0);
    {
        // Fresnel for IBL — uses roughness-modulated F to avoid overly reflective rough surfaces
        float s = 1.0 - roughness;
        float3 kS_env = F0 + (max(float3(s, s, s), F0) - F0) * Pow5(1.0 - NoV);
        float3 kD_env = (1.0 - kS_env) * (1.0 - metallic);

        // Diffuse irradiance from cubemap
        float3 irradiance = ambientColor.xyz; // fallback
#ifdef UZ_BINDLESS
        if (irradianceSrvIndex != kSentinelSrvIndex) {
            TextureCube<float4> irrTex = ResourceDescriptorHeap[irradianceSrvIndex];
            irradiance = irrTex.SampleLevel(clampSampler, N, 0).rgb;
        }
#endif
        irradiance = max(irradiance, skyLightMinAmbient);
        // Lambertian BRDF = albedo / PI. The irradiance cubemap integrates
        // cos-weighted radiance over the hemisphere, producing values in
        // units of irradiance (W/m²). Dividing by PI converts to exitant
        // radiance for a Lambertian surface.
        float3 diffuseAmbient = kD_env * irradiance * baseColor;

        // Specular: prefiltered env + BRDF LUT
        float3 R = reflect(-V, N);
        float mipLevel = roughness * float(prefilteredMipLevels - 1);
        float3 prefilteredColor = float3(0, 0, 0);
        float2 envBRDF = float2(1, 0);
#ifdef UZ_BINDLESS
        if (prefilteredSrvIndex != kSentinelSrvIndex) {
            TextureCube<float4> prefTex = ResourceDescriptorHeap[prefilteredSrvIndex];
            prefilteredColor = prefTex.SampleLevel(clampSampler, R, mipLevel).rgb;
        }
        if (brdfLutSrvIndex != kSentinelSrvIndex) {
            Texture2D<float4> brdfTex = ResourceDescriptorHeap[brdfLutSrvIndex];
            envBRDF = brdfTex.SampleLevel(clampSampler, float2(NoV, roughness), 0).rg;
        }
#endif
        float3 specularAmbient = prefilteredColor * (F0 * envBRDF.x + envBRDF.y);

        // Roughness attenuation — for very rough dielectrics the BRDF LUT bias
        // term produces visible specular from intense HDR environments (sun hotspot).
        // Attenuate specular contribution for rough non-metallic surfaces.
        // @ref Fdez-Aguera 2019 "A Multiple-Scattering Microfacet Model"
        float roughAtten = saturate(1.0 - roughness * roughness * (1.0 - metallic));
        specularAmbient *= roughAtten;

        // Horizon occlusion — prevent reflections below surface
        // @ref Lagarde 2012 "Horizon Occlusion for Normal Mapped Reflections"
        float horizon = saturate(1.0 + dot(R, N));
        specularAmbient *= horizon * horizon;

        // Attenuate specular IBL in shadowed areas — prevents bright reflections
        // appearing inside shadow regions (sun specular leaking through IBL)
        specularAmbient *= shadow;

        ambient = (diffuseAmbient * ao + specularAmbient) * skyTint;
    }

    // ── Environment MIS (temporally accumulated specular) ───────────────
#ifdef UZ_BINDLESS
    if (gBufIndices.envMisSrvIdx != kSentinelSrvIndex) {
        Texture2D<float4> envMisTex = ResourceDescriptorHeap[gBufIndices.envMisSrvIdx];
        float3 envMIS = envMisTex.Load(int3(px, 0)).rgb;
        ambient += envMIS;
    }
#endif

    // ── Final ─────────────────────────────────────────────────────────────
    float3 color = Lo + ambient + emissive;
    return float4(max(color, 0.0), 1.0);
}
