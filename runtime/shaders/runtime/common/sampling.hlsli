/// UzEngine common sampling utilities.
///
/// Low-discrepancy sequences, hemisphere/sphere/disk/cone sampling,
/// and GGX importance sampling for physically-based rendering.

#ifndef UZ_COMMON_SAMPLING_HLSLI
#define UZ_COMMON_SAMPLING_HLSLI

#include "constants.hlsli"
#include "math.hlsli"

// ─── Low-discrepancy sequences ────────────────────────────────────────────

/// Radical inverse in base 2 (Van der Corput sequence).
/// @ref Van der Corput 1935 "Verteilungsfunktionen I & II"
///
/// Reverses the bits of a 32-bit integer and maps to [0, 1).
/// Used as the second dimension of the Hammersley sequence.
float RadicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f; // 1.0 / 0x100000000
}

/// Hammersley 2D quasi-random sequence point.
/// @ref Hammersley 1960 "Monte Carlo Methods"
///
/// Returns the i-th point of an N-point Hammersley sequence.
/// First dimension is uniform (i/N), second is Van der Corput.
float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverseVdC(i));
}

// ─── Disk sampling ────────────────────────────────────────────────────────

/// Concentric disk mapping — maps [0,1]^2 to the unit disk with low
/// distortion by treating the square as two triangular halves.
/// @ref Shirley & Chiu 1997 "A Low Distortion Map Between Disk and Square"
float2 ConcentricSampleDisk(float2 u)
{
    // Remap to [-1, 1]
    float2 offset = 2.0f * u - 1.0f;

    // Handle degenerate case at origin
    if (abs(offset.x) < FLT_EPSILON && abs(offset.y) < FLT_EPSILON)
        return float2(0.0f, 0.0f);

    float theta, r;
    if (abs(offset.x) > abs(offset.y))
    {
        r = offset.x;
        theta = M_PI_2 * 0.5f * (offset.y / offset.x); // pi/4 * (y/x)
    }
    else
    {
        r = offset.y;
        theta = M_PI_2 - M_PI_2 * 0.5f * (offset.x / offset.y); // pi/2 - pi/4 * (x/y)
    }
    return float2(r * cos(theta), r * sin(theta));
}

// ─── Hemisphere / sphere / cone sampling ──────────────────────────────────

/// Cosine-weighted hemisphere sampling via Malley's method.
/// @ref Pharr & Humphreys 2023 "Physically Based Rendering" (4th ed.)
///      Section 13.6.1 "Cosine-Weighted Hemisphere Sampling"
///
/// Projects a uniform disk sample upward onto the hemisphere.
/// The resulting direction has PDF = cos(theta) / pi.
/// Returns a direction in tangent space (Z-up = normal direction).
float3 SampleCosineHemisphere(float2 u, out float pdf)
{
    // Concentric disk mapping for the XY components
    float2 d = ConcentricSampleDisk(u);
    float z = SafeSqrt(1.0f - d.x * d.x - d.y * d.y);
    pdf = z * M_1_PI; // cos(theta) / pi
    return float3(d.x, d.y, z);
}

/// Uniform sphere sampling — maps [0,1]^2 to the full unit sphere.
/// @ref Pharr & Humphreys 2023 "Physically Based Rendering" (4th ed.)
///      Section 13.6.1
///
/// PDF = 1 / (4 * pi)
float3 SampleUniformSphere(float2 u)
{
    float z = 1.0f - 2.0f * u.x;
    float r = SafeSqrt(1.0f - z * z);
    float phi = M_2PI * u.y;
    return float3(r * cos(phi), r * sin(phi), z);
}

/// Uniform sampling within a cone of half-angle acos(cosThetaMax).
/// @ref Pharr & Humphreys 2023 "Physically Based Rendering" (4th ed.)
///      Section 13.6.4
///
/// Returns a direction in tangent space (Z = cone axis).
/// PDF = 1 / (2 * pi * (1 - cosThetaMax))
float3 SampleUniformCone(float2 u, float cosThetaMax)
{
    float cosTheta = (1.0f - u.x) + u.x * cosThetaMax;
    float sinTheta = SafeSqrt(1.0f - cosTheta * cosTheta);
    float phi = M_2PI * u.y;
    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

// ─── GGX importance sampling ─────────────────────────────────────────────

/// Importance-sample the GGX NDF — returns a half vector H in tangent space.
/// @ref Walter et al. 2007 "Microfacet Models for Refraction through Rough
///      Surfaces"
///
/// Uses the standard inverse-CDF approach for the GGX distribution:
///   cos^2(theta) = (1 - u.x) / (1 + (alpha^2 - 1) * u.x)
///
/// roughness: perceptual roughness (alpha = roughness^2 is the GGX alpha).
/// Returns the sampled half vector H in tangent space (Z-up).
float3 ImportanceSampleGGX(float2 u, float roughness)
{
    float alpha = roughness * roughness;
    float a2 = alpha * alpha;

    float cosTheta = SafeSqrt((1.0f - u.x) / (1.0f + (a2 - 1.0f) * u.x));
    float sinTheta = SafeSqrt(1.0f - cosTheta * cosTheta);
    float phi = M_2PI * u.y;

    // Tangent-space half vector (Z-up)
    return float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

/// Sample the GGX visible normal distribution function (VNDF) using the
/// spherical cap method. Returns a microfacet normal (half vector) in
/// tangent space that is visible from the given incident direction.
///
/// @ref Dupuy & Benyoub 2023 "Sampling Visible GGX Normals with Spherical
///      Caps"
///
/// This method provides lower variance than the standard NDF sampling by
/// only sampling normals visible from the view direction (wi), which avoids
/// wasting samples on back-facing microfacets.
///
/// alpha: isotropic GGX roughness (alpha = roughness^2)
/// wi:    incident direction in tangent space (Z-up), must be normalized
/// u:     2D uniform random sample in [0,1]^2
///
/// Returns the sampled half vector H in tangent space. The caller is
/// responsible for computing the VNDF PDF (requires D_GGX from brdf.hlsli).
float3 SampleGGX_VNDF(float alpha, float3 wi, float2 u)
{
    // Transform the view direction to the hemisphere configuration
    // (stretch the ellipsoid into a hemisphere)
    float3 Vh = normalize(float3(alpha * wi.x, alpha * wi.y, wi.z));

    // Sample a spherical cap in the positive hemisphere
    float phi = M_2PI * u.x;
    float z = mad((1.0f - u.y), (1.0f + Vh.z), -Vh.z);
    float sinTheta = SafeSqrt(1.0f - z * z);
    float x = sinTheta * cos(phi);
    float y = sinTheta * sin(phi);

    // Compute the microfacet normal on the hemisphere
    float3 h_hemi = float3(x, y, z) + Vh;

    // Transform back from hemisphere to the ellipsoid configuration
    return normalize(float3(alpha * h_hemi.x, alpha * h_hemi.y, max(h_hemi.z, 0.0f)));
}

#endif // UZ_COMMON_SAMPLING_HLSLI
