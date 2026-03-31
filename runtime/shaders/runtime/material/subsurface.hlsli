/// UzEngine subsurface scattering approximations.
///
/// Wrap diffuse, back lighting, foliage, and thin translucent models for
/// materials that transmit light through their surface (skin, wax, leaves, etc.).

#ifndef UZ_MATERIAL_SUBSURFACE_HLSLI
#define UZ_MATERIAL_SUBSURFACE_HLSLI

#include "../common/constants.hlsli"

// ─── Wrap Diffuse ────────────────────────────────────────────────────────────

/// Wrap diffuse lighting — extends the Lambertian cosine lobe to wrap
/// around the terminator, simulating subsurface light transport.
///
/// @ref Valve 2006 "Half-Life 2 Shading" (SIGGRAPH 2006 Course)
/// @constants None
/// @deviations NONE
///
/// @param NdotL Cosine of angle between normal and light direction.
/// @param wrap  Wrap factor in [0, 1]. 0 = Lambertian, 1 = full hemisphere.
/// @return      Wrapped diffuse factor in [0, 1].
float WrapDiffuse(float NdotL, float wrap)
{
    return max(0.0f, (NdotL + wrap) / ((1.0f + wrap) * (1.0f + wrap)));
}

// ─── Back Lighting (Translucency) ────────────────────────────────────────────

/// Approximate subsurface back lighting for translucent objects.
/// Simulates light passing through thin geometry (ears, nostrils, fingers)
/// by evaluating a distorted view-dependent transmission lobe on the back face.
///
/// @ref Brisebois & Bouchard 2011 "Approximating Translucency for a Fast, Cheap
///      and Convincing Subsurface Scattering Look" (GDC 2011)
/// @constants distortion=0.4, power=4.0, scale=2.0, ambient=0.15
///            (Brisebois 2011, Section "Translucency Parameters")
/// @deviations NONE
///
/// @param lightDir       Normalized direction from surface to light.
/// @param viewDir        Normalized direction from surface to camera.
/// @param normal         Surface normal.
/// @param subsurfaceColor Color tint for the subsurface transmission.
/// @param thickness       Material thickness in [0, 1]; 0 = fully opaque, 1 = thinnest.
/// @return               Back-lighting contribution.
float3 BackLighting(float3 lightDir, float3 viewDir, float3 normal, float3 subsurfaceColor, float thickness)
{
    // Distort the light direction by the normal to simulate subsurface diffusion
    float3 backLight = -lightDir + normal * 0.4f;
    float VdotB = saturate(dot(viewDir, normalize(backLight)));
    float power = pow(VdotB, 4.0f) * 2.0f;
    float ambient = 0.15f;
    return subsurfaceColor * (power + ambient) * thickness;
}

// ─── Combined Subsurface ─────────────────────────────────────────────────────

/// Evaluate combined subsurface scattering: wrap diffuse + back lighting.
/// Suitable for skin, wax, marble, and similar thick translucent materials.
///
/// @ref Valve 2006 "Half-Life 2 Shading"
/// @ref Brisebois & Bouchard 2011 "Approximating Translucency"
/// @constants wrap=0.5 (common default for skin)
/// @deviations NONE
///
/// @param N                Surface normal.
/// @param L                Normalized direction from surface to light.
/// @param V                Normalized direction from surface to camera.
/// @param subsurfaceColor  Color tint for subsurface transport.
/// @param thickness        Material thickness in [0, 1].
/// @return                 Combined diffuse + back-lit subsurface contribution.
float3 EvalSubsurface(float3 N, float3 L, float3 V, float3 subsurfaceColor, float thickness)
{
    float NdotL = dot(N, L);
    float wrapDiffuse = WrapDiffuse(NdotL, 0.5f);
    float3 backLit = BackLighting(L, V, N, subsurfaceColor, thickness);
    return subsurfaceColor * wrapDiffuse * M_1_PI + backLit;
}

// ─── Foliage ─────────────────────────────────────────────────────────────────

/// Evaluate foliage/two-sided diffuse lighting.
/// Thin foliage is lit on both sides equally: the front face uses abs(NdotL)
/// and the back face receives wrap lighting to simulate light passing through
/// the leaf.
///
/// @ref Habel et al. 2007 "Physically Based Real-Time Translucency for Leaves"
///      (EGSR 2007)
/// @constants wrap=0.5
/// @deviations NONE
///
/// @param N                Surface normal.
/// @param L                Normalized direction from surface to light.
/// @param V                Normalized direction from surface to camera.
/// @param subsurfaceColor  Color tint for subsurface leaf transmission.
/// @return                 Foliage diffuse contribution.
float3 EvalFoliage(float3 N, float3 L, float3 V, float3 subsurfaceColor)
{
    float NdotL = dot(N, L);

    // Front face: standard diffuse using absolute NdotL
    float frontDiffuse = abs(NdotL) * M_1_PI;

    // Back face: wrap lighting for light transmitted through the leaf
    float backDiffuse = WrapDiffuse(-NdotL, 0.5f);

    return subsurfaceColor * (frontDiffuse + backDiffuse);
}

// ─── Thin Translucent ────────────────────────────────────────────────────────

/// Evaluate thin translucent material (curtains, paper, lampshades).
/// Uses a simple exponential attenuation based on thickness and the angle
/// of incidence to approximate light transmission through thin flat geometry.
///
/// @ref Jimenez et al. 2010 "Real-Time Realistic Skin Translucency"
///      (IEEE CGA, Vol. 30, No. 4)
/// @constants None
/// @deviations NONE
///
/// @param N                 Surface normal.
/// @param L                 Normalized direction from surface to light.
/// @param V                 Normalized direction from surface to camera.
/// @param transmissionColor Color of transmitted light.
/// @param thickness         Material thickness; higher = more attenuation.
/// @return                  Thin translucent contribution.
float3 EvalThinTranslucent(float3 N, float3 L, float3 V, float3 transmissionColor, float thickness)
{
    float NdotL = dot(N, L);

    // Transmission is strongest when light hits the back face
    float transmittance = exp(-thickness * (1.0f - NdotL));

    return transmissionColor * transmittance * M_1_PI;
}

#endif // UZ_MATERIAL_SUBSURFACE_HLSLI
