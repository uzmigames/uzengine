/// UzEngine cloth/sheen BRDF.
///
/// Charlie NDF and Neubelt visibility for fabric/velvet sheen highlights.
/// Used by the kShadingModelCloth shading model in standard_material.hlsli.

#ifndef UZ_MATERIAL_SHEEN_HLSLI
#define UZ_MATERIAL_SHEEN_HLSLI

#include "../common/constants.hlsli"
#include "../common/math.hlsli"

// ─── Charlie NDF ─────────────────────────────────────────────────────────────

/// Charlie normal distribution function for cloth/sheen.
/// Models the soft, wide highlight characteristic of fabric materials
/// by using an inverted-Gaussian distribution on the sin^2 of the half-angle.
///
/// @ref Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF"
///      (SIGGRAPH 2017 Talk, Sony Pictures Imageworks)
/// @constants 0.0078125 = 1/128, floor for sin2h to avoid pow(0, x) (Falcor SheenBSDF.slang)
/// @deviations NONE
///
/// @param NdotH     Cosine of angle between normal and half-vector.
/// @param roughness Sheen roughness in [0, 1].
/// @return          NDF value.
float D_Charlie(float NdotH, float roughness)
{
    float invAlpha = 1.0f / roughness;
    float cos2h = NdotH * NdotH;
    float sin2h = max(1.0f - cos2h, 0.0078125f);
    return (2.0f + invAlpha) * pow(sin2h, invAlpha * 0.5f) / (2.0f * PI);
}

// ─── Neubelt Visibility ──────────────────────────────────────────────────────

/// Neubelt visibility function for cloth shading.
/// A simple analytic visibility term that accounts for the self-shadowing
/// of fabric fibers without requiring a full Smith derivation.
///
/// @ref Neubelt & Pettineo 2013 "Crafting a Next-Gen Material Pipeline for
///      The Order: 1886" (SIGGRAPH 2013 Course)
/// @constants None
/// @deviations NONE
///
/// @param NdotV Cosine of angle between normal and view direction.
/// @param NdotL Cosine of angle between normal and light direction.
/// @return      Visibility value (includes 1/(4*NdotL*NdotV) normalization).
float V_Neubelt(float NdotV, float NdotL)
{
    return 1.0f / (4.0f * (NdotL + NdotV - NdotL * NdotV));
}

// ─── Cloth Sheen Evaluation ──────────────────────────────────────────────────

/// Evaluate the cloth sheen BRDF: sheenColor * D_Charlie * V_Neubelt.
/// @ref Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF"
/// @ref Neubelt & Pettineo 2013 "Crafting a Next-Gen Material Pipeline for The Order: 1886"
/// @constants None (delegates to D_Charlie, V_Neubelt)
/// @deviations NONE
///
/// @param sheenColor     Color tint of the sheen highlight.
/// @param sheenRoughness Roughness of the sheen layer in [0, 1].
/// @param NdotH          Cosine of angle between normal and half-vector.
/// @param NdotV          Cosine of angle between normal and view direction.
/// @param NdotL          Cosine of angle between normal and light direction.
/// @return               Sheen BRDF contribution (NOT multiplied by NdotL).
float3 EvalClothSheen(float3 sheenColor, float sheenRoughness, float NdotH, float NdotV, float NdotL)
{
    float D = D_Charlie(NdotH, sheenRoughness);
    float V = V_Neubelt(NdotV, NdotL);
    return sheenColor * D * V;
}

#endif // UZ_MATERIAL_SHEEN_HLSLI
