/// PBR BRDF functions — GGX + Smith + Schlick + Burley.
/// @ref Walter 2007, Heitz 2014, Burley 2012, Karis 2013
#ifndef PBR_COMMON_HLSLI
#define PBR_COMMON_HLSLI

#include "math_util.hlsli"

static const float MIN_ROUGHNESS = 0.045;

/// GGX NDF. @ref Walter 2007 eq.33
float D_GGX(float NdotH, float alpha) {
    float a2 = alpha * alpha;
    float f = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * f * f);
}

/// Height-correlated Smith visibility (includes 1/4NLV denominator).
/// @ref Heitz 2014 eq.99, Lagarde 2014
float V_SmithGGXCorrelated(float a2, float NdotV, float NdotL) {
    float lV = NdotL * sqrt(a2 + NdotV * NdotV * (1.0 - a2));
    float lL = NdotV * sqrt(a2 + NdotL * NdotL * (1.0 - a2));
    return 0.5 / max(lV + lL, 1e-7);
}

/// Schlick Fresnel. @ref Schlick 1994
float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * Pow5(1.0 - cosTheta);
}

/// Schlick with F90. @ref Lagarde 2014
float3 F_SchlickF90(float cosTheta, float3 F0, float F90) {
    return F0 + (F90 - F0) * Pow5(1.0 - cosTheta);
}

/// Schlick with roughness for IBL. @ref Lagarde 2011
float3 F_SchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float s = 1.0 - roughness;
    return F0 + (max(float3(s, s, s), F0) - F0) * Pow5(1.0 - cosTheta);
}

/// F0 from metallic workflow. @ref Karis 2013
float3 ComputeF0(float specular, float metallic, float3 albedo) {
    float d = 0.08 * specular;
    return lerp(float3(d, d, d), albedo, metallic);
}

/// Disney Burley diffuse. @ref Burley 2012
float3 Fd_Burley(float3 albedo, float roughness, float NdotV, float NdotL, float LdotH) {
    float fd90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
    float ls = 1.0 + (fd90 - 1.0) * Pow5(1.0 - NdotL);
    float vs = 1.0 + (fd90 - 1.0) * Pow5(1.0 - NdotV);
    return albedo * (1.0 / PI) * ls * vs;
}

/// Combined BRDF. @ref Karis 2013
float3 EvaluateBRDF(float3 N, float3 V, float3 L, float3 radiance,
                    float3 albedo, float metallic, float alpha, float3 F0) {
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.001);
    float NdotV = max(abs(dot(N, V)), 0.001);
    float NdotH = saturate(dot(N, H));
    float LdotH = saturate(dot(L, H));

    float  D   = D_GGX(NdotH, alpha);
    float  Vis = V_SmithGGXCorrelated(alpha * alpha, NdotV, NdotL);
    float  f90 = saturate(50.0 * F0.g);
    float3 F   = F_SchlickF90(LdotH, F0, f90);
    float3 spec = D * Vis * F;

    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diff = kD * Fd_Burley(albedo, sqrt(alpha), NdotV, NdotL, LdotH);

    return (diff + spec) * radiance * NdotL;
}

/// BRDF with per-light scale. @ref Karis 2013
float3 EvaluateBRDFScaled(float3 N, float3 V, float3 L, float3 radiance,
                          float3 albedo, float metallic, float alpha, float3 F0,
                          float specScale, float diffScale) {
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.001);
    float NdotV = max(abs(dot(N, V)), 0.001);
    float NdotH = saturate(dot(N, H));
    float LdotH = saturate(dot(L, H));

    float  D   = D_GGX(NdotH, alpha);
    float  Vis = V_SmithGGXCorrelated(alpha * alpha, NdotV, NdotL);
    float  f90 = saturate(50.0 * F0.g);
    float3 F   = F_SchlickF90(LdotH, F0, f90);
    float3 spec = D * Vis * F;

    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diff = kD * Fd_Burley(albedo, sqrt(alpha), NdotV, NdotL, LdotH);

    return (diff * diffScale + spec * specScale) * radiance * NdotL;
}

/// Multi-bounce AO. @ref Jimenez 2016
float3 AOMultiBounce(float ao, float3 c) {
    float3 a = 2.0404 * c - 0.3324;
    float3 b = -4.7951 * c + 0.6417;
    float3 cc = 2.7552 * c + 0.6903;
    return max(ao, ((ao * a + b) * ao + cc) * ao);
}

/// ACES tonemap. @ref Narkowicz 2015
float3 ACESFilm(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

// Legacy aliases for old shaders that use these names
#define DistributionGGX D_GGX
#define VisibilitySmithGGX V_SmithGGXCorrelated
#define FresnelSchlick F_Schlick
#define FresnelSchlickRoughness F_SchlickRoughness
#define DiffuseDisneyBurley(a,r,v,l,h) Fd_Burley(a,r,v,l,h)

#endif
