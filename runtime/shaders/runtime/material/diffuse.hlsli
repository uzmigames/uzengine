/// UzEngine diffuse BRDF models.
///
/// Lambertian and Frostbite (energy-renormalized Schlick) diffuse BRDFs.
/// The Frostbite model is the default for UzEngine's PBR material.

#ifndef UZ_MATERIAL_DIFFUSE_HLSLI
#define UZ_MATERIAL_DIFFUSE_HLSLI

#include "../common/constants.hlsli"
#include "../common/math.hlsli"
#include "brdf.hlsli"

// ─── Lambertian Diffuse ──────────────────────────────────────────────────────

/// Simple Lambertian diffuse: albedo / pi.
/// @ref Lambert 1760 "Photometria"
/// @constants M_1_PI (from constants.hlsli)
/// @deviations NONE
///
/// @param albedo  Surface diffuse color.
/// @return        Lambertian BRDF value (NOT multiplied by NdotL).
float3 EvalLambertDiffuse(float3 albedo)
{
    return albedo * M_1_PI;
}

// ─── Frostbite Diffuse ──────────────────────────────────────────────────────

/// Frostbite energy-renormalized diffuse BRDF.
/// Uses a roughness-dependent energy bias to compensate for energy lost
/// to the specular lobe at grazing angles. This produces a smoother
/// transition between rough and smooth surfaces than plain Lambertian.
///
/// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR"
/// @constants energyBias lerp(0.0, 0.5, roughness), energyFactor lerp(1.0, 1.0/1.51, roughness)
///            (Falcor FrostbiteDiffuseBRDF.slang evalWeight)
/// @deviations NONE
///
/// NOTE: The return value is f(wi,wo) already divided by PI. The final
/// multiplication by NdotL happens at the callsite, NOT inside this function.
///
/// @param albedo    Surface diffuse color.
/// @param roughness Perceptual roughness in [0, 1].
/// @param NdotV     Cosine of angle between normal and view direction.
/// @param NdotL     Cosine of angle between normal and light direction.
/// @param VdotH     Cosine of angle between view and half-vector.
/// @return          Diffuse BRDF value (NOT multiplied by NdotL).
float3 EvalFrostbiteDiffuse(float3 albedo, float roughness, float NdotV, float NdotL, float VdotH)
{
    float energyBias   = lerp(0.0f, 0.5f, roughness);
    float energyFactor = lerp(1.0f, 1.0f / 1.51f, roughness);

    float fd90 = energyBias + 2.0f * VdotH * VdotH * roughness;
    float fd0  = 1.0f;

    // Scalar Schlick for the light-scattering term
    float lightScatter = F_Schlick(fd0, fd90, NdotL);
    // Scalar Schlick for the view-scattering term
    float viewScatter  = F_Schlick(fd0, fd90, NdotV);

    return albedo * lightScatter * viewScatter * energyFactor * M_1_PI;
}

#endif // UZ_MATERIAL_DIFFUSE_HLSLI
