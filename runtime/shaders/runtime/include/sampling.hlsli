/// Shared sampling library — low-discrepancy sequences and distribution sampling.
///
/// Consolidates sampling primitives used across IBL baking, PCSS shadows,
/// SSGI tracing, and volumetric rendering into a single header.
///
/// # Functions
/// - RadicalInverseVdC, Hammersley — quasi-random sequences
/// - UniformSampleDiskConcentricApprox — concentric disk mapping
/// - CosineSampleHemisphere — cosine-weighted hemisphere sampling
/// - UniformSampleSphere — uniform sphere sampling
/// - UniformSampleCone — uniform cone sampling
/// - SampleTriangle — uniform triangle barycentric sampling
/// - VonMisesFisherSample — directional distribution sampling
///
/// @ref Shirley & Chiu 1997 "A Low Distortion Map Between Disk and Square"
/// @ref Hammersley & Handscomb 1964 — quasi-Monte Carlo sequences
/// @ref Fisher 1953 — von Mises-Fisher distribution

#ifndef SAMPLING_HLSLI
#define SAMPLING_HLSLI

#include "math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Quasi-Random Sequences
// ─────────────────────────────────────────────────────────────────────────────

/// Radical inverse in base 2 using bit reversal.
/// @ref Van der Corput 1935 — radical inverse sequence
/// @ref Hammersley & Handscomb 1964 — quasi-Monte Carlo integration
float RadicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

/// Hammersley 2D quasi-random point set.
/// Returns (i/N, RadicalInverse(i)) for stratified sampling.
/// @ref Hammersley & Handscomb 1964
float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverseVdC(i));
}

// ─────────────────────────────────────────────────────────────────────────────
// Disk Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Approximate concentric square-to-disk mapping.
/// @ref Reynolds 2017 "Square/Disc Mappings"
/// @constants sqrt(2.0), sqrt(0.5)
float2 UniformSampleDiskConcentricApprox(float2 E) {
    float2 sf = E * sqrt(2.0f) - sqrt(0.5f);
    float2 sq = sf * sf;
    float root = sqrt(2.0f * max(sq.x, sq.y) - min(sq.x, sq.y));
    if (sq.x > sq.y) {
        sf.x = sf.x > 0 ? root : -root;
    } else {
        sf.y = sf.y > 0 ? root : -root;
    }
    return sf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hemisphere / Sphere / Cone Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Cosine-weighted hemisphere sample in tangent space (+Z = normal).
/// Returns a unit direction where the probability density is cos(theta)/PI.
/// @ref Shirley & Chiu 1997 "A Low Distortion Map Between Disk and Square"
/// @ref Malley's method — project uniform disk sample to hemisphere
float3 CosineSampleHemisphere(float2 u) {
    float r = sqrt(u.x);
    float phi = M_2PI * u.y;
    float x = r * cos(phi);
    float y = r * sin(phi);
    float z = sqrt(max(0.0f, 1.0f - u.x));
    return float3(x, y, z);
}

/// Uniform sample on the unit sphere surface.
/// @ref Marsaglia 1972 — uniform sphere sampling via cosTheta + phi
float3 UniformSampleSphere(float2 u) {
    float z   = 1.0f - 2.0f * u.x;
    float r   = sqrt(max(0.0f, 1.0f - z * z));
    float phi = M_2PI * u.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

/// Uniform sample within a cone of half-angle acos(cosThetaMax) around +Z.
/// @ref Pharr, Jakob, Humphreys 2016 "Physically Based Rendering" — Section 13.6.3
float3 UniformSampleCone(float2 u, float cosThetaMax) {
    float cosTheta = (1.0f - u.x) + u.x * cosThetaMax;
    float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
    float phi = M_2PI * u.y;
    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

// ─────────────────────────────────────────────────────────────────────────────
// Triangle Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Uniform sample on a triangle, returning barycentric coordinates (u, v).
/// The third coordinate is w = 1 - u - v.
/// @ref Pharr, Jakob, Humphreys 2016 "Physically Based Rendering" — Section 13.6.5
float2 SampleTriangle(float2 u) {
    float su0 = sqrt(u.x);
    return float2(1.0f - su0, u.y * su0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Von Mises-Fisher Distribution
// ─────────────────────────────────────────────────────────────────────────────

/// Samples a direction from the von Mises-Fisher distribution centered on mu
/// with concentration parameter kappa. Higher kappa = more concentrated.
/// Returns a unit vector. For kappa=0, returns a uniform sphere sample.
/// @ref Fisher 1953 "Dispersion on a Sphere"
/// @ref Jakob 2012 "Numerically stable sampling of the von Mises Fisher distribution"
float3 VonMisesFisherSample(float3 mu, float kappa, float2 u) {
    // For very small kappa, fall back to uniform sphere
    if (kappa < 1e-5f) return UniformSampleSphere(u);

    // Sample cosTheta from the vMF marginal distribution
    // cosTheta = 1 + (1/kappa) * ln(u.x + (1-u.x) * exp(-2*kappa))
    float w = 1.0f + (1.0f / kappa) * log(u.x + (1.0f - u.x) * exp(-2.0f * kappa));
    w = clamp(w, -1.0f, 1.0f);

    float sinTheta = sqrt(max(0.0f, 1.0f - w * w));
    float phi = M_2PI * u.y;

    // Local sample around +Z
    float3 localDir = float3(sinTheta * cos(phi), sinTheta * sin(phi), w);

    // Rotate from +Z to mu using branchlessONB
    float3 t, b;
    branchlessONB(mu, t, b);
    return t * localDir.x + b * localDir.y + mu * localDir.z;
}

#endif
