/// UzEngine specular BRDF evaluation.
///
/// Convenience functions that combine the NDF, visibility, and Fresnel terms
/// into a single specular microfacet BRDF value.

#ifndef UZ_MATERIAL_SPECULAR_HLSLI
#define UZ_MATERIAL_SPECULAR_HLSLI

#include "brdf.hlsli"

// ─── Specular Evaluation ─────────────────────────────────────────────────────

/// Combine pre-computed D, V, and F terms into the specular BRDF.
/// @ref Cook & Torrance 1982 "A Reflectance Model for Computer Graphics"
/// @constants None
/// @deviations NONE
///
/// The microfacet specular BRDF is D * V * F where V already includes the
/// 1 / (4 * NdotV * NdotL) denominator (visibility form, not G form).
///
/// @param D  Normal distribution function value.
/// @param V  Visibility (= G / (4 * NdotV * NdotL)).
/// @param F  Fresnel reflectance (float3 for colored metals).
/// @return   Specular BRDF contribution (NOT multiplied by NdotL).
float3 EvalSpecular(float D, float V, float3 F)
{
    return D * V * F;
}

/// Evaluate specular microfacet BRDF from material parameters.
/// Internally computes D_GGX, V_SmithGGXCorrelated, and F_Schlick.
/// @ref Walter et al. 2007 "Microfacet Models for Refraction through Rough Surfaces"
/// @ref Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
/// @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-Based Rendering"
/// @constants None (delegates to D_GGX, V_SmithGGXCorrelated, F_Schlick)
/// @deviations NONE
///
/// @param alpha Roughness alpha (roughness^2, clamped >= kMinGGXAlpha).
/// @param NdotH Cosine of angle between normal and half-vector.
/// @param NdotV Cosine of angle between normal and view direction.
/// @param NdotL Cosine of angle between normal and light direction.
/// @param VdotH Cosine of angle between view and half-vector.
/// @param f0    Specular reflectance at normal incidence.
/// @return      Specular BRDF contribution (NOT multiplied by NdotL).
float3 EvalSpecularDVF(float alpha, float NdotH, float NdotV, float NdotL, float VdotH, float3 f0)
{
    float  D = D_GGX(alpha, NdotH);
    float  V = V_SmithGGXCorrelated(alpha, NdotV, NdotL);
    float3 F = F_Schlick(f0, VdotH);
    return EvalSpecular(D, V, F);
}

#endif // UZ_MATERIAL_SPECULAR_HLSLI
