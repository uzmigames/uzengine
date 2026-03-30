/// Sheen BRDF — fabric/velvet specular for cloth shading.
///
/// @ref Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF"
/// @ref Neubelt 2013 — simplified visibility term for cloth
#ifndef SHEEN_HLSLI
#define SHEEN_HLSLI

#include "math_util.hlsli"

/// Charlie sheen NDF — fabric/velvet specular distribution.
/// Uses sin(theta) instead of cos(theta) for fabric highlight at grazing angles.
/// @ref Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF"
/// @constants min sin²h = 2^(-14/2) to avoid zero
/// @deviations NONE
float D_Charlie(float roughness, float NoH) {
    float sin2h = max(1.0 - NoH * NoH, 0.0078125);
    float sinH  = sqrt(sin2h);
    float invR = 1.0 / max(roughness, 0.001);
    return (2.0 + invR) * pow(sinH, invR) / (2.0 * PI);
}

/// Neubelt visibility for cloth — simplified geometric term for sheen.
/// @ref Neubelt 2013, Estevez & Kulla 2017 "Production Friendly Microfacet Sheen BRDF"
/// @deviations NONE
float V_Neubelt(float NoV, float NoL) {
    return 1.0 / (4.0 * (NoL + NoV - NoL * NoV));
}

#endif
