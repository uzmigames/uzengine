/// UzEngine BRDF building blocks.
///
/// Microfacet NDF, masking-shadowing, Fresnel, and utility conversions
/// used by the specular, clearcoat, and standard material evaluation headers.
/// Every function matches the Falcor StandardBSDF reference (BSD-licensed).

#ifndef UZ_MATERIAL_BRDF_HLSLI
#define UZ_MATERIAL_BRDF_HLSLI

#include "../common/constants.hlsli"
#include "../common/math.hlsli"

// ─── Normal Distribution Function ────────────────────────────────────────────

/// Trowbridge-Reitz (GGX) normal distribution function.
/// @ref Walter et al. 2007 "Microfacet Models for Refraction through Rough Surfaces"
/// @ref Trowbridge & Reitz 1975 "Average irregularity representation of a rough surface"
/// @constants None (uses PI from constants.hlsli)
/// @deviations NONE
///
/// @param alpha   Roughness alpha (roughness^2, clamped >= kMinGGXAlpha).
/// @param NdotH   Cosine of angle between surface normal and half-vector.
/// @return        NDF value D(h).
float D_GGX(float alpha, float NdotH)
{
    float a2 = alpha * alpha;
    float d = ((NdotH * a2 - NdotH) * NdotH + 1.0f);
    return a2 / (d * d * PI);
}

// ─── Smith Masking-Shadowing ─────────────────────────────────────────────────

/// Lambda function for the Smith GGX masking-shadowing term.
/// @ref Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
/// @constants None
/// @deviations NONE
///
/// @param alphaSq  Squared roughness alpha (alpha^2).
/// @param cosTheta Cosine of angle between direction and normal.
/// @return         Lambda value.
float Lambda_GGX(float alphaSq, float cosTheta)
{
    if (cosTheta <= 0.0f) return 0.0f;
    float cos2 = cosTheta * cosTheta;
    float tan2 = max(1.0f - cos2, 0.0f) / cos2;
    return 0.5f * (-1.0f + sqrt(1.0f + alphaSq * tan2));
}

/// Smith G1 for GGX — single-direction masking.
/// @ref Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
/// @constants None
/// @deviations NONE
///
/// @param alphaSq  Squared roughness alpha.
/// @param cosTheta Cosine of angle between direction and normal.
/// @return         G1 masking value in [0, 1].
float G1_SmithGGX(float alphaSq, float cosTheta)
{
    if (cosTheta <= 0.0f) return 0.0f;
    float cos2 = cosTheta * cosTheta;
    float tan2 = max(1.0f - cos2, 0.0f) / cos2;
    return 2.0f / (1.0f + sqrt(1.0f + alphaSq * tan2));
}

/// Height-correlated Smith masking-shadowing for GGX.
/// @ref Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs" Eq. 99
/// @constants None
/// @deviations NONE
///
/// @param alpha Roughness alpha.
/// @param NdotV Cosine of angle between normal and view direction.
/// @param NdotL Cosine of angle between normal and light direction.
/// @return      G(v,l) masking-shadowing value.
float G_SmithGGXCorrelated(float alpha, float NdotV, float NdotL)
{
    float a2 = alpha * alpha;
    float lambdaV = Lambda_GGX(a2, NdotV);
    float lambdaL = Lambda_GGX(a2, NdotL);
    return 1.0f / (1.0f + lambdaV + lambdaL);
}

// ─── Visibility Function ─────────────────────────────────────────────────────

/// Smith GGX correlated visibility function V = G / (4 * NdotV * NdotL).
/// Uses the optimized form that avoids computing G separately.
/// @ref Hammon 2017 "PBR Diffuse Lighting for GGX+Smith Microsurfaces"
/// @constants None
/// @deviations NONE
///
/// @param alpha Roughness alpha.
/// @param NdotV Cosine of angle between normal and view direction.
/// @param NdotL Cosine of angle between normal and light direction.
/// @return      Visibility term V.
float V_SmithGGXCorrelated(float alpha, float NdotV, float NdotL)
{
    float a2 = alpha * alpha;
    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0f - a2) + a2);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0f - a2) + a2);
    return 0.5f / max(GGXV + GGXL, 1e-7f);
}

// ─── Fresnel ─────────────────────────────────────────────────────────────────

/// Schlick Fresnel approximation (float3 with explicit f90).
/// @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-Based Rendering"
/// @constants None
/// @deviations NONE
///
/// @param f0       Reflectance at normal incidence.
/// @param f90      Reflectance at grazing incidence.
/// @param cosTheta Cosine of angle between view and half-vector.
/// @return         Fresnel reflectance.
float3 F_Schlick(float3 f0, float3 f90, float cosTheta)
{
    return f0 + (f90 - f0) * Pow5(max(1.0f - cosTheta, 0.0f));
}

/// Schlick Fresnel approximation (scalar with explicit f90).
/// @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-Based Rendering"
/// @constants None
/// @deviations NONE
float F_Schlick(float f0, float f90, float cosTheta)
{
    return f0 + (f90 - f0) * Pow5(max(1.0f - cosTheta, 0.0f));
}

/// Schlick Fresnel approximation (float3 convenience, f90 = 1).
/// @ref Schlick 1994 "An Inexpensive BRDF Model for Physically-Based Rendering"
/// @constants None
/// @deviations NONE
float3 F_Schlick(float3 f0, float cosTheta)
{
    return F_Schlick(f0, float3(1.0f, 1.0f, 1.0f), cosTheta);
}

/// Exact Fresnel for dielectric materials using Snell's law.
/// Handles total internal reflection when the incidence angle exceeds
/// the critical angle.
/// @ref Hecht 2002 "Optics" (4th ed.) — Fresnel equations
/// @constants None
/// @deviations NONE
///
/// @param eta       Ratio of refractive indices (n_i / n_t).
/// @param cosThetaI Cosine of the incidence angle (may be negative for backface).
/// @return          Fresnel reflectance in [0, 1].
float F_DielectricIOR(float eta, float cosThetaI)
{
    if (cosThetaI < 0.0f)
    {
        eta = 1.0f / eta;
        cosThetaI = -cosThetaI;
    }
    float sinThetaTSq = eta * eta * (1.0f - cosThetaI * cosThetaI);
    // Total internal reflection
    if (sinThetaTSq > 1.0f) return 1.0f;
    float cosThetaT = sqrt(1.0f - sinThetaTSq);
    float Rs = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);
    float Rp = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);
    return 0.5f * (Rs * Rs + Rp * Rp);
}

// ─── IOR / F0 Conversion Utilities ──────────────────────────────────────────

/// Convert index of refraction to F0 (normal-incidence reflectance).
/// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR"
/// @constants None
/// @deviations NONE
float IORToF0(float ior)
{
    float r = (ior - 1.0f) / (ior + 1.0f);
    return r * r;
}

/// Convert F0 (normal-incidence reflectance) to index of refraction.
/// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR"
/// @constants None
/// @deviations NONE
float F0ToIOR(float f0)
{
    float r = sqrt(max(f0, 0.0f));
    return (1.0f + r) / max(1.0f - r, 1e-7f);
}

/// Compute specular F0 from base color and metallic.
/// Dielectrics use 0.04 (typical for plastic/glass), metals use albedo.
/// @ref Burley 2012 "Physically Based Shading at Disney"
/// @constants kDielectricF0=0.04 (common dielectric reflectance)
/// @deviations NONE
float3 ComputeF0(float3 albedo, float metallic)
{
    return lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
}

/// Convert perceptual roughness to GGX alpha, clamped to kMinGGXAlpha.
/// @ref Burley 2012 "Physically Based Shading at Disney" — roughness remapping
/// @constants kMinGGXAlpha=0.0064 (from constants.hlsli)
/// @deviations NONE
float RoughnessToAlpha(float roughness)
{
    return max(roughness * roughness, kMinGGXAlpha);
}

#endif // UZ_MATERIAL_BRDF_HLSLI
