/// UzEngine clearcoat layer evaluation.
///
/// Dual-layer clear coat model for automotive paint, lacquered wood, etc.
/// The clearcoat is a smooth dielectric layer (IOR 1.5) on top of the
/// base material. The base layer is attenuated by (1 - Fc) where Fc is
/// the Fresnel reflectance of the clearcoat.

#ifndef UZ_MATERIAL_CLEARCOAT_HLSLI
#define UZ_MATERIAL_CLEARCOAT_HLSLI

#include "brdf.hlsli"
#include "specular.hlsli"

// ─── Clearcoat Constants ─────────────────────────────────────────────────────

/// Clearcoat index of refraction.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4" — common clear coat IOR
/// @constants kClearcoatIOR=1.5
static const float kClearcoatIOR = 1.5f;

/// Clearcoat F0 derived from IOR 1.5: ((1.5-1)/(1.5+1))^2 = 0.04.
/// @ref Karis 2013 "Real Shading in Unreal Engine 4"
/// @constants kClearcoatF0=0.04
static const float kClearcoatF0 = 0.04f;

// ─── Clearcoat Evaluation ────────────────────────────────────────────────────

/// Evaluate the clearcoat specular layer.
/// Returns the clearcoat contribution and outputs the Fresnel term so the
/// caller can attenuate the base layer by (1 - Fc).
///
/// @ref Karis 2013 "Real Shading in Unreal Engine 4"
/// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR"
/// @constants kClearcoatF0=0.04, kMinGGXAlpha=0.0064
/// @deviations NONE
///
/// Uses the same GGX NDF and Smith visibility as the base specular layer
/// but with the clearcoat's own roughness and a fixed F0 of 0.04.
///
/// @param clearcoatRoughness Perceptual roughness of the clearcoat layer.
/// @param NdotH              Cosine of angle between normal and half-vector.
/// @param NdotV              Cosine of angle between normal and view direction.
/// @param NdotL              Cosine of angle between normal and light direction.
/// @param VdotH              Cosine of angle between view and half-vector.
/// @param Fc                 [out] Clearcoat Fresnel reflectance (scalar). Caller
///                           attenuates base layer by (1 - Fc).
/// @return                   Clearcoat specular contribution (NOT multiplied by NdotL).
float EvalClearcoat(float clearcoatRoughness, float NdotH, float NdotV, float NdotL, float VdotH, out float Fc)
{
    float alpha = RoughnessToAlpha(clearcoatRoughness);
    float D = D_GGX(alpha, NdotH);
    float V = V_SmithGGXCorrelated(alpha, NdotV, NdotL);
    Fc = F_Schlick(kClearcoatF0, 1.0f, VdotH);
    return D * V * Fc;
}

#endif // UZ_MATERIAL_CLEARCOAT_HLSLI
