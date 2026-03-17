/// Shared PBR functions for Cook-Torrance BRDF evaluation.
///
/// Used by both the forward pass (mesh_ps.hlsl) and deferred lighting pass
/// (deferred_lighting_ps.hlsl). Implements GGX NDF, Smith geometry, and
/// Schlick-Fresnel from the metallic-roughness workflow.
///
/// # References
/// - Karis 2013, "Real Shading in Unreal Engine 4"
/// - Walter et al. 2007, "Microfacet Models for Refraction"

#ifndef PBR_COMMON_HLSLI
#define PBR_COMMON_HLSLI

#include "math_util.hlsli"

static const float PI = 3.14159265359f;

/// Minimum roughness clamped before squaring to alpha, preventing a perfectly
/// smooth (singular) specular lobe that collapses to a delta function on GPU.
static const float MIN_ROUGHNESS = 0.04f;

// ─────────────────────────────────────────────────────────────────────────────
// ACES Filmic Tonemapping
// ─────────────────────────────────────────────────────────────────────────────

/// ACES filmic tonemapping curve (Narkowicz 2015 polynomial fit).
///
/// @source Narkowicz 2015 "ACES Filmic Tone Mapping Curve" — polynomial approximation
/// @constants a=2.51, b=0.03, c=2.43, d=0.59, e=0.14 (Narkowicz 2015)
/// @deviations UE5 uses a full ACES RRT+ODT pipeline with color space transforms
///   (sRGB→AP0→AP1), glow module, red modifier, and custom S-curve with 5 artist
///   parameters (FilmSlope/Toe/Shoulder/BlackClip/WhiteClip) in TonemapCommon.ush:105.
///   Full UE5 FilmToneMap requires ~200 lines of ACES color space math + CB parameters.
///   This Narkowicz fit is a single-function approximation that produces visually similar
///   results for the default ACES settings (Slope=0.91, Toe=0.53, Shoulder=0.23).
///
/// Maps HDR linear radiance to [0,1] LDR with a filmic S-curve that provides
/// better highlight rolloff and color preservation than per-channel Reinhard.
///
/// # Parameters
/// - x: linear HDR color (unbounded positive).
///
/// # Returns
/// Tonemapped LDR color in [0, 1].
float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ─────────────────────────────────────────────────────────────────────────────
// PBR Helper Functions
// ─────────────────────────────────────────────────────────────────────────────

/// GGX / Trowbridge-Reitz Normal Distribution Function.
///
/// Describes the statistical distribution of microfacet normals relative to
/// the macrosurface normal. A higher alpha (rougher surface) produces a wider,
/// dimmer highlight; a lower alpha produces a tight, bright specular peak.
///
/// D(h) = alpha^2 / (PI * ((N.H)^2 * (alpha^2 - 1) + 1)^2)
///
/// # Parameters
/// - NdotH: dot product of macrosurface normal N and half-vector H, in [0,1].
/// - alpha: perceptual roughness squared (r^2), in [MIN_ROUGHNESS^2, 1].
///
/// # Returns
/// Specular lobe density in [0, +inf). Integrates to 1 over the hemisphere.
float DistributionGGX(float NdotH, float alpha) {
    float a2    = alpha * alpha;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * denom * denom);
}

/// Schlick Fresnel approximation.
///
/// Computes the fraction of light reflected (vs. refracted) at a microfacet
/// surface. At grazing angles (cosTheta -> 0) the reflectance approaches 1
/// regardless of material.
///
/// F(v, h) = F0 + (1 - F0) * (1 - V.H)^5
///
/// # Parameters
/// - cosTheta: dot(V, H) — cosine of angle between view and half-vector.
/// - F0:       reflectance at normal incidence (0 deg angle of incidence).
///             Dielectric typical value: (0.04, 0.04, 0.04).
///             Metals: use the albedo color as F0.
///
/// # Returns
/// float3 Fresnel reflectance in [F0, 1].
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * Pow5(saturate(1.0f - cosTheta));
}

/// Disney Burley diffuse BRDF.
///
/// @source UE5 BRDF.ush:183-188 — Diffuse_Burley
/// @constants FD90 = 0.5 + 2 * VoH^2 * Roughness (BRDF.ush:185)
/// @deviations NONE
///
/// Adds roughness-dependent retroreflection at grazing angles compared to
/// plain Lambertian. At roughness=0 this reduces to Lambert diffuse.
///
/// # Parameters
/// - albedo:    surface base color (linear).
/// - roughness: perceptual roughness [0,1] (NOT alpha = roughness^2).
/// - NdotV:     saturated dot(N, V).
/// - NdotL:     saturated dot(N, L).
/// - VdotH:     saturated dot(V, H).
///
/// # Returns
/// Diffuse BRDF value (includes 1/PI normalization).
float3 DiffuseBurley(float3 albedo, float roughness, float NdotV, float NdotL, float VdotH) {
    float FD90 = 0.5f + 2.0f * VdotH * VdotH * roughness;
    float FdV = 1.0f + (FD90 - 1.0f) * Pow5(1.0f - NdotV);
    float FdL = 1.0f + (FD90 - 1.0f) * Pow5(1.0f - NdotL);
    return albedo * (1.0f / PI) * FdV * FdL;
}

/// Approximate joint Smith visibility term for GGX.
///
/// @source UE5 BRDF.ush:346-351 — Vis_SmithJointApprox
/// @constants NONE (pure formula)
/// @deviations NONE
///
/// Returns Vis = G / (4*NoV*NoL), so the specular BRDF is D*Vis*F.
/// This is a cheaper approximation of the exact height-correlated Smith
/// that UE5 uses everywhere for direct and IBL lighting.
///
/// # Parameters
/// - a2:   roughness alpha squared (roughness^4 in UE5 convention, or alpha^2).
/// - NoV:  saturated dot(N, V).
/// - NoL:  saturated dot(N, L).
///
/// # Returns
/// Visibility term in [0, +inf).
float Vis_SmithJointApprox(float a2, float NoV, float NoL) {
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0f - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0f - a) + a);
    return 0.5f * rcp(Vis_SmithV + Vis_SmithL);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiscatter Energy Compensation (Kulla-Conty / Turquin 2018)
// ─────────────────────────────────────────────────────────────────────────────

/// Analytic fit for GGX single-scattering directional albedo.
///
/// @source UE5 ShadingEnergyConservation.ush:58-63 — GGXEnergyLookup analytic fit
/// @constants E fit: pow(r, c/r) * ((r*c + 0.0266916) / (0.466495 + c))
///   (ShadingEnergyConservation.ush:60-61)
/// @constants Ef fit: Pow5(1-c) * pow(2.36651 * pow(c, 4.7703*r) + 0.0387332, r)
///   (ShadingEnergyConservation.ush:62-63)
/// @deviations Uses analytic fit (USE_ENERGY_CONSERVATION==2) instead of BRDF LUT
///   (USE_ENERGY_CONSERVATION==1). This matches UE5's non-LUT code path exactly.
///
/// Returns E (single-scattering directional albedo) and Ef (Fresnel-weighted
/// single-scattering albedo) as a float2. These represent the fraction of energy
/// accounted for by single-scattering; the remainder (1-E) is lost without
/// multiscatter compensation.
///
/// # Parameters
/// - roughness: perceptual roughness [0,1] (NOT alpha).
/// - NoV:       saturated dot(N, V) in [0,1].
///
/// # Returns
/// float2(E, Ef) where E = single-scatter albedo, Ef = Fresnel-weighted albedo.
float2 GGXEnergyLookup(float roughness, float NoV) {
    float r = roughness;
    float c = NoV;
    // Single-scattering directional albedo: fraction of energy from one bounce
    float E = 1.0f - saturate(pow(r, c / r) * ((r * c + 0.0266916f) / (0.466495f + c)));
    // Fresnel-weighted single-scattering albedo
    float Ef = Pow5(1.0f - c) * pow(2.36651f * pow(c, 4.7703f * r) + 0.0387332f, r);
    return float2(E, Ef);
}

/// Computes multiscatter energy compensation terms for specular and diffuse.
///
/// @source UE5 ShadingEnergyConservationTemplate.ush:44-64 — ComputeFresnelEnergyTerms
/// @source UE5 ShadingEnergyConservationTemplate.ush:55 — Result.W = 1 + F0 * ((1-E.x)/E.x)
/// @source Turquin 2018 Eq.16 — non-reciprocal Kulla-Conty multiscatter
/// @constants luminance weights = (0.2126, 0.7152, 0.0722) (Rec.709 / sRGB)
///   (ShadingEnergyConservationTemplate.ush:63)
/// @deviations NONE
///
/// The single-scatter BRDF loses energy at higher roughness because it only
/// models one bounce. This function computes a compensation weight W that scales
/// the specular lobe to account for the missing multi-bounce energy.
///
/// It also computes the total specular albedo (fraction of energy reflected by
/// specular) so that the diffuse lobe can be attenuated accordingly to preserve
/// total energy.
///
/// # Parameters
/// - F0:        reflectance at normal incidence.
/// - roughness: perceptual roughness [0,1].
/// - NoV:       saturated dot(N, V).
///
/// # Returns
/// float4 where:
///   .xyz = specular energy compensation weight (multiply specular by this)
///   .w   = luminance of total specular albedo (diffuse *= 1 - w, metals excluded)
float4 ComputeEnergyCompensation(float3 F0, float roughness, float NoV) {
    float2 E = GGXEnergyLookup(roughness, NoV);

    // Multiscatter compensation weight: accounts for energy lost after first bounce
    // W = 1 + F0 * ((1 - E.x) / E.x)
    // When E.x is high (smooth surface), W ~ 1 (little lost energy).
    // When E.x is low (rough surface), W > 1 (significant compensation needed).
    float3 W = 1.0f + F0 * ((1.0f - E.x) / max(E.x, 0.001f));

    // Total specular albedo: energy reflected by specular (single + multi scatter)
    // Used to attenuate diffuse for overall energy preservation
    float3 specAlbedo = W * (E.x * F0 + E.y * (1.0f - F0));

    // Rec.709 luminance of specular albedo for scalar diffuse attenuation
    return float4(W, dot(specAlbedo, float3(0.2126f, 0.7152f, 0.0722f)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Cook-Torrance BRDF Evaluation
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates Cook-Torrance BRDF for a single light source.
///
/// @source UE5 ShadingModels.ush:220 — DefaultLitBxDF (D_GGX * Vis_SmithJointApprox * F_Schlick)
/// @source UE5 BRDF.ush:183 — Diffuse_Burley
/// @source UE5 ShadingEnergyConservationTemplate.ush:55 — multiscatter compensation
/// @constants F0_dielectric = 0.04 (BRDF.ush:209)
/// @deviations NONE
///
/// Computes the full outgoing radiance from a single analytic light, including
/// both diffuse (Burley) and specular (GGX) lobes with multiscatter energy
/// compensation (Kulla-Conty / Turquin 2018).
///
/// # Parameters
/// - N:         world-space surface normal (normalised).
/// - V:         world-space view direction (normalised, toward camera).
/// - L:         world-space light direction (normalised, toward light).
/// - radiance:  incoming light radiance (color * intensity * attenuation).
/// - albedo:    surface base color (linear).
/// - metallic:  metallic factor [0,1].
/// - alpha:     roughness^2 (PBR alpha parameter).
/// - F0:        reflectance at normal incidence.
///
/// # Returns
/// Outgoing radiance contribution from this light.
float3 EvaluateBRDF(float3 N, float3 V, float3 L, float3 radiance,
                    float3 albedo, float metallic, float alpha, float3 F0) {
    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.001f);
    float NdotV = max(dot(N, V), 0.001f);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    // Cook-Torrance specular BRDF: D * Vis * F
    // Vis_SmithJointApprox already includes the 1/(4*NoV*NoL) denominator
    float  D   = DistributionGGX(NdotH, alpha);
    float  Vis = Vis_SmithJointApprox(alpha, NdotV, NdotL);
    float3 F   = FresnelSchlick(VdotH, F0);

    float3 specular = D * Vis * F;

    // Multiscatter energy compensation (Kulla-Conty / Turquin 2018)
    // Recovers energy lost by single-scatter BRDF at higher roughness
    float roughness = sqrt(alpha);
    float4 energyComp = ComputeEnergyCompensation(F0, roughness, NdotV);
    specular *= energyComp.xyz;

    // Energy preservation: attenuate diffuse by total specular albedo luminance
    // Metals have no diffuse lobe (metallic=1 → kD=0)
    float3 kD = (1.0f - energyComp.w) * (1.0f - metallic);

    // Disney Burley diffuse (roughness-dependent retroreflection)
    float3 diffuse = kD * DiffuseBurley(albedo, roughness, NdotV, NdotL, VdotH);

    return (diffuse + specular) * radiance * NdotL;
}

#endif // PBR_COMMON_HLSLI
