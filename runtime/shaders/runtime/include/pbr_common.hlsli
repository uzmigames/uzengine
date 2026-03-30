/// PBR BRDF functions — GGX + Smith + Schlick + Burley.
/// @ref Walter 2007, Heitz 2014, Burley 2012, Karis 2013
#ifndef PBR_COMMON_HLSLI
#define PBR_COMMON_HLSLI

#include "math_util.hlsli"

static const float MIN_ROUGHNESS = 0.045;

/// GGX NDF. @ref Walter 2007 eq.33
/// Returns 0 for alpha < 1e-3 (delta distribution / perfect mirror).
/// @ref Falcor SpecularMicrofacet.slang — delta guard for zero-roughness materials
float D_GGX(float NdotH, float alpha) {
    if (alpha < 1e-3) return 0.0;
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

/// Anisotropic GGX NDF.
/// @ref Walter 2007 "Microfacet Models for Refraction" — anisotropic extension
/// @ref Burley 2012 — anisotropic roughness parameterization
float D_GGX_Anisotropic(float NdotH, float TdotH, float BdotH, float alphaT, float alphaB) {
    float a2 = alphaT * alphaB;
    float3 d = float3(alphaB * TdotH, alphaT * BdotH, a2 * NdotH);
    float d2 = dot(d, d);
    float b2 = a2 / d2;
    return M_1_PI * a2 * b2 * b2;
}

/// Anisotropic height-correlated Smith visibility.
/// @ref Heitz 2014 — anisotropic Smith masking-shadowing
float V_SmithGGXCorrelated_Anisotropic(float alphaT, float alphaB,
    float TdotV, float BdotV, float TdotL, float BdotL,
    float NdotV, float NdotL)
{
    float lambdaV = NdotL * length(float3(alphaT * TdotV, alphaB * BdotV, NdotV));
    float lambdaL = NdotV * length(float3(alphaT * TdotL, alphaB * BdotL, NdotL));
    return 0.5 / max(lambdaV + lambdaL, 1e-7);
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

// ─────────────────────────────────────────────────────────────────────────────
// Generalized & Physical Fresnel
// ─────────────────────────────────────────────────────────────────────────────

/// Generalized Schlick Fresnel with tunable exponent and F90.
/// @ref Schlick 1994 — original uses exponent=5; tunable variant allows
///   artistic control (e.g., exponent=3 for softer falloff on cloth).
float F_SchlickGeneralized(float cosTheta, float f0, float f90, float exponent) {
    return f0 + (f90 - f0) * pow(saturate(1.0 - cosTheta), exponent);
}

/// Convert normal-incidence reflectance (F0) to index of refraction.
/// Assumes external medium is air (η=1). Inverse of IORToReflectance.
/// @ref PBR Book 2018 ch.8 — Fresnel equations
float ReflectanceToIOR(float reflectance) {
    float r = sqrt(max(reflectance, 0.0));
    return (1.0 + r) / max(1.0 - r, 1e-5);
}

/// Exact dielectric Fresnel reflectance (unpolarized).
/// Computes average of Rs and Rp using Snell's law.
/// Returns 1.0 for total internal reflection.
/// @ref PBR Book 2018 ch.8 — Fresnel equations for dielectrics
/// @constants eta = ηt/ηi (ratio of transmitted to incident IOR)
float F_FresnelDielectric(float eta, float cosThetaI) {
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);

    // Flip interface if ray enters from the denser medium
    if (cosThetaI < 0.0) {
        eta = 1.0 / eta;
        cosThetaI = -cosThetaI;
    }

    // Snell's law: sin²θt = sin²θi / η²
    float sin2ThetaT = (1.0 - cosThetaI * cosThetaI) / (eta * eta);

    // Total internal reflection
    if (sin2ThetaT >= 1.0) return 1.0;

    float cosThetaT = sqrt(max(0.0, 1.0 - sin2ThetaT));

    // Fresnel equations (Rs = s-polarized, Rp = p-polarized)
    float Rs = (cosThetaI - eta * cosThetaT) / (cosThetaI + eta * cosThetaT);
    float Rp = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);

    return 0.5 * (Rs * Rs + Rp * Rp);
}

/// Exact conductor Fresnel reflectance (unpolarized).
/// Uses complex index of refraction (η + ik) for metallic surfaces.
/// @ref Shirley 1997 "An Anisotropic Phong BRDF Model" — conductor Fresnel
/// @ref PBR Book 2018 ch.8 — Fresnel equations for conductors
/// @constants eta = complex IOR real part (per channel), k = extinction coefficient
float3 F_FresnelConductor(float3 eta, float3 k, float cosThetaI) {
    cosThetaI = clamp(cosThetaI, 0.0, 1.0);
    float cos2 = cosThetaI * cosThetaI;
    float sin2 = 1.0 - cos2;

    float3 eta2 = eta * eta;
    float3 k2   = k * k;

    float3 t0   = eta2 - k2 - sin2;
    float3 a2b2 = sqrt(max(t0 * t0 + 4.0 * eta2 * k2, 0.0));
    float3 a    = sqrt(max(0.5 * (a2b2 + t0), 0.0));

    // Rs (s-polarized)
    float3 t1 = a2b2 + cos2;
    float3 t2 = 2.0 * a * cosThetaI;
    float3 Rs = (t1 - t2) / (t1 + t2);

    // Rp (p-polarized)
    float3 t3 = a2b2 * cos2 + sin2 * sin2;
    float3 t4 = t2 * sin2;
    float3 Rp = Rs * (t3 - t4) / (t3 + t4);

    return 0.5 * (Rs + Rp);
}

// ─────────────────────────────────────────────────────────────────────────────
// Common Metal IOR/k Constants (measured spectral data, RGB approximation)
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Palik 1998 "Handbook of Optical Constants of Solids"
/// Values at approximately 550nm (green), approximated for sRGB primaries.

static const float3 kGoldEta      = float3(0.18, 0.42, 1.37);
static const float3 kGoldK        = float3(3.42, 2.35, 1.77);

static const float3 kCopperEta    = float3(0.27, 0.68, 1.22);
static const float3 kCopperK      = float3(3.35, 2.49, 2.16);

static const float3 kAluminumEta  = float3(1.66, 0.88, 0.52);
static const float3 kAluminumK    = float3(9.22, 6.27, 4.84);

static const float3 kIronEta      = float3(2.95, 2.93, 2.59);
static const float3 kIronK        = float3(3.01, 2.95, 2.77);

static const float3 kSilverEta    = float3(0.16, 0.14, 0.13);
static const float3 kSilverK      = float3(3.80, 3.11, 2.47);

/// Disney Burley diffuse with Frostbite energy conservation.
/// @ref Burley 2012 "Physically Based Shading at Disney"
/// @ref Lagarde 2015 "Moving Frostbite to PBR" — energy conservation addendum
/// @constants energyFactor = lerp(1.0, 1.0/1.51, roughness)
///   Without this, rough surfaces are ~51% over-bright due to uncompensated
///   energy loss in the Fresnel retro-reflection terms.
float3 Fd_Burley(float3 albedo, float roughness, float NdotV, float NdotL, float LdotH) {
    float fd90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
    float ls = 1.0 + (fd90 - 1.0) * Pow5(1.0 - NdotL);
    float vs = 1.0 + (fd90 - 1.0) * Pow5(1.0 - NdotV);
    float energyFactor = lerp(1.0, 1.0 / 1.51, roughness);
    return albedo * M_1_PI * ls * vs * energyFactor;
}

/// Oren-Nayar diffuse for rough surfaces.
/// Full qualitative model with A and B terms. More physically accurate than
/// Burley for very rough dielectrics (clay, concrete, unfinished wood).
/// @ref Oren & Nayar 1994 "Generalization of Lambert's Reflectance Model"
/// @constants sigma = roughness (mapped from perceptual roughness)
///   A = 1 - 0.5 * sigma^2 / (sigma^2 + 0.33)
///   B = 0.45 * sigma^2 / (sigma^2 + 0.09)
float3 Fd_OrenNayar(float3 albedo, float roughness, float NdotV, float NdotL, float VdotL) {
    float sigma2 = roughness * roughness;
    float A = 1.0 - 0.5 * sigma2 / (sigma2 + 0.33);
    float B = 0.45 * sigma2 / (sigma2 + 0.09);

    // cos(phi_i - phi_o) = (VdotL - NdotV * NdotL) / (sin_theta_i * sin_theta_o)
    float sinThetaI = sqrt(max(0.0, 1.0 - NdotL * NdotL));
    float sinThetaO = sqrt(max(0.0, 1.0 - NdotV * NdotV));
    float maxCos = (sinThetaI * sinThetaO > 1e-4)
                 ? max(0.0, (VdotL - NdotV * NdotL) / (sinThetaI * sinThetaO))
                 : 0.0;

    // sin(alpha) * tan(beta) where alpha = max(theta_i, theta_o), beta = min
    float sinAlpha, tanBeta;
    if (NdotL > NdotV) {
        sinAlpha = sinThetaI;
        tanBeta  = sinThetaO / max(NdotV, 1e-4);
    } else {
        sinAlpha = sinThetaO;
        tanBeta  = sinThetaI / max(NdotL, 1e-4);
    }

    return albedo * M_1_PI * (A + B * maxCos * sinAlpha * tanBeta);
}

/// GGX Visible Normal Distribution Function sampling.
/// Samples half-vectors proportional to D(h) * G1(v,h) — drastically reduces
/// variance at grazing angles compared to standard NDF sampling.
/// @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals" (JCGT)
/// @constants Uses hemisphere clipping + stretch/unstretching for anisotropic alpha
float3 ImportanceSampleGGX_VNDF(float2 u, float3 V, float alphaX, float alphaY) {
    // Stretch the view vector so we are sampling the GGX distribution of
    // the stretched configuration (isotropic α=1)
    float3 Vh = normalize(float3(alphaX * V.x, alphaY * V.y, V.z));

    // Build orthonormal basis around Vh
    float lenSq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lenSq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) / sqrt(lenSq)
                             : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(Vh, T1);

    // Parameterization of the projected area (hemisphere clipping)
    float r = sqrt(u.x);
    float phi = M_2PI * u.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(max(0.0, 1.0 - t1 * t1)) + s * t2;

    // Reproject onto hemisphere
    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

    // Unstretch
    return normalize(float3(alphaX * Nh.x, alphaY * Nh.y, max(0.0, Nh.z)));
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
