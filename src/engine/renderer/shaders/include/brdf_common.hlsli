// brdf_common.hlsli -- Shared BRDF functions for forward and deferred paths
//
// References:
//   UE5 -- BRDF.ush, ShadingModels.ush
//   Filament -- https://google.github.io/filament/Filament.md.html
//   Godot -- scene_forward_lights_inc.glsl

#ifndef BRDF_COMMON_HLSLI
#define BRDF_COMMON_HLSLI

static const float PI = 3.14159265359;

// pow5 helper (UE5 Pow5)
float Pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

// GGX / Trowbridge-Reitz NDF
// [Walter et al. 2007]
float D_GGX(float a2, float NoH) {
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}

// Joint Smith term approximation for GGX
// [Heitz 2014] -- returns G / (4*NoL*NoV)
float Vis_SmithJointApprox(float a2, float NoV, float NoL) {
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (Vis_SmithV + Vis_SmithL + 1e-5);
}

// Schlick Fresnel with UE5 f90 clamp
float3 F_Schlick(float3 F0, float VoH) {
    float Fc = Pow5(1.0 - VoH);
    float f90 = saturate(50.0 * F0.g);
    return f90 * Fc + (1.0 - Fc) * F0;
}

// Fresnel with roughness (for ambient/IBL)
float3 F_SchlickRoughness(float3 F0, float VoH, float roughness) {
    float Fc = Pow5(1.0 - VoH);
    float3 F90 = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
    return F0 + (F90 - F0) * Fc;
}

// Burley Diffuse [Disney 2012]
float3 Diffuse_Burley(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH) {
    float FD90 = 0.5 + 2.0 * VoH * VoH * Roughness;
    float FdV = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoV);
    float FdL = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoL);
    return DiffuseColor * ((1.0 / PI) * FdV * FdL);
}

// EnvBRDF Approximation [Lazarov 2013]
float3 EnvBRDFApprox(float3 SpecularColor, float Roughness, float NoV) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4( 1.0,  0.0425,  1.04, -0.04);
    float4 r = Roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
    float f90 = saturate(50.0 * SpecularColor.g);
    return SpecularColor * AB.x + f90 * AB.y;
}

// Specular Occlusion [Lagarde 2014, Jimenez 2016]
float SpecularOcclusion(float NoV, float ao, float roughness) {
    return saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

// Full Cook-Torrance + Burley diffuse for a single analytical light
float3 EvaluateDirectLight(
    float3 N, float3 V, float NoV,
    float3 DiffuseColor, float3 F0, float a2, float roughness,
    float3 lightDirection, float3 lColor, float lIntensity)
{
    float3 L = normalize(-lightDirection);
    float  NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0) return float3(0.0, 0.0, 0.0);

    float3 H = normalize(V + L);
    float  NoH = saturate(dot(N, H));
    float  VoH = saturate(dot(V, H));

    float  D   = D_GGX(a2, NoH);
    float  Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    float3 F   = F_Schlick(F0, VoH);
    float3 specular = D * Vis * F;

    float3 diffuse = Diffuse_Burley(DiffuseColor, roughness, NoV, NoL, VoH);

    return (diffuse + specular) * lColor * lIntensity * NoL;
}

// ============================================================================
// Octahedral Normal Encoding/Decoding (10-bit precision in RGB10A2)
// [Meyer et al. 2010, "On Floating-Point Normal Vectors"]
// ============================================================================

float2 OctEncode(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return n.xy * 0.5 + 0.5;
}

float3 OctDecode(float2 f) {
    f = f * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

// ============================================================================
// PCF Shadow Sampling -- orthographic light-space reconstruction
// ============================================================================

float SampleShadowCommon(
    float3 worldPos, float3 worldNormal,
    float3 lightDirIn, float shadowBiasIn, float shadowOrthoSizeIn, float shadowLightDistIn,
    Texture2D shadowMapTex, SamplerState shadowSampler)
{
    float3 lightFwd = normalize(lightDirIn);
    float3 L_dir    = normalize(-lightFwd);
    float  NdotL_s  = saturate(dot(worldNormal, L_dir));
    float  sinTheta = sqrt(saturate(1.0 - NdotL_s * NdotL_s));
    float  tanTheta = sinTheta / max(NdotL_s, 1e-3);
    float  normalOffsetScale = clamp(tanTheta * 0.015, 0.0, 0.04);
    worldPos = worldPos + worldNormal * normalOffsetScale;

    float3 refUp  = (abs(lightFwd.y) > 0.99) ? float3(1, 0, 0) : float3(0, 1, 0);
    float3 lRight = normalize(cross(refUp, lightFwd));
    float3 lUp    = cross(lightFwd, lRight);

    float3 lightEye = -lightFwd * shadowLightDistIn;
    float3 relPos   = worldPos - lightEye;

    float lx = dot(relPos, lRight);
    float ly = dot(relPos, lUp);
    float lz = dot(relPos, lightFwd);

    float2 shadowUV = float2(lx, -ly) / shadowOrthoSizeIn * 0.5 + 0.5;
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0)
        return 1.0;

    float shadowFar    = shadowLightDistIn * 2.0;
    float currentDepth = (lz - 0.1) / (shadowFar - 0.1);

    float shadow = 0.0;
    float2 texelSize = 1.0 / float2(2048.0, 2048.0);

    static const float2 poissonDisk[16] = {
        float2(-0.94201624, -0.39906216), float2( 0.94558609, -0.76890725),
        float2(-0.09418410, -0.92938870), float2( 0.34495938,  0.29387760),
        float2(-0.91588581,  0.45771432), float2(-0.81544232, -0.87912464),
        float2(-0.38277543,  0.27676845), float2( 0.97484398,  0.75648379),
        float2( 0.44323325, -0.97511554), float2( 0.53742981, -0.47373420),
        float2(-0.26496911, -0.41893023), float2( 0.79197514,  0.19090188),
        float2(-0.24188840,  0.99706507), float2(-0.81409955,  0.91437590),
        float2( 0.19984126,  0.78641367), float2( 0.14383161, -0.14100790)
    };

    float penumbra = clamp(currentDepth * 3.5, 0.5, 2.5);

    [unroll]
    for (int i = 0; i < 16; ++i) {
        float closestDepth = shadowMapTex.Sample(shadowSampler,
            shadowUV + poissonDisk[i] * texelSize * penumbra).r;
        shadow += (currentDepth - shadowBiasIn > closestDepth) ? 0.0 : 1.0;
    }
    return shadow / 16.0;
}

#endif // BRDF_COMMON_HLSLI
