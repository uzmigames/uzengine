/// Shared IBL (Image-Based Lighting) helper functions.
///
/// Provides low-discrepancy sampling, GGX importance sampling, and
/// roughness-aware Fresnel for split-sum IBL integration.
///
/// Used by brdf_lut_cs.hlsl, irradiance_cs.hlsl, and prefilter_cs.hlsl.
///
/// # References
/// - Karis 2013, "Real Shading in Unreal Engine 4"
/// - Hammersley & Handscomb 1964, "Monte Carlo Methods"

#ifndef IBL_HLSLI
#define IBL_HLSLI

#include "pbr_common.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Low-Discrepancy Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Van der Corput radical inverse in base 2 using bit reversal.
///
/// Mirrors the binary representation of `bits` around the decimal point
/// to produce a value in [0, 1). Used as the second coordinate in the
/// Hammersley sequence.
///
/// # Parameters
/// - bits: unsigned integer input.
///
/// # Returns
/// Radical inverse value in [0, 1).
float RadicalInverseVdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f; // 1 / 0x100000000
}

/// Hammersley 2D low-discrepancy sequence point.
///
/// Produces a quasi-random 2D sample in [0,1)^2. The x coordinate is a
/// simple linear ramp (i/N), while y uses the Van der Corput radical
/// inverse for better coverage than pseudo-random sampling.
///
/// # Parameters
/// - i: sample index in [0, N).
/// - N: total number of samples.
///
/// # Returns
/// 2D sample point in [0, 1)^2.
float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverseVdC(i));
}

// ─────────────────────────────────────────────────────────────────────────────
// GGX Importance Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Importance-samples the GGX NDF to generate a half-vector.
///
/// Maps a 2D uniform sample (Xi) to a direction on the hemisphere weighted
/// by the GGX distribution for the given roughness. The returned vector is
/// in world space, oriented around the surface normal N.
///
/// # Parameters
/// - Xi:        2D uniform random sample in [0, 1)^2 (e.g., from Hammersley).
/// - N:         macrosurface normal (normalised).
/// - roughness: perceptual roughness in [0, 1].
///
/// # Returns
/// World-space half-vector H sampled proportional to D_GGX(H).
float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a = roughness * roughness;

    // Spherical coordinates from the GGX inverse CDF
    float phi      = 2.0f * PI * Xi.x;
    float cosTheta = sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

    // Tangent-space half-vector
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // Build TBN from N — construct an arbitrary tangent frame
    float3 up = (abs(N.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f)
                                     : float3(1.0f, 0.0f, 0.0f);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    // Transform from tangent space to world space
    return normalize(T * H.x + B * H.y + N * H.z);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fresnel (IBL variant)
// ─────────────────────────────────────────────────────────────────────────────

/// Fresnel-Schlick with roughness attenuation for IBL ambient lighting.
///
/// Unlike the standard FresnelSchlick (which approaches 1.0 at grazing
/// angles regardless of roughness), this variant caps the maximum Fresnel
/// reflectance based on roughness. This prevents overly bright rim lighting
/// on rough materials under environment illumination.
///
/// # Parameters
/// - cosTheta:  dot(N, V), clamped to [0, 1].
/// - F0:        reflectance at normal incidence.
/// - roughness: perceptual roughness in [0, 1].
///
/// # Returns
/// Fresnel reflectance attenuated by roughness.
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float3 maxF = max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0);
    return F0 + (maxF - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cubemap Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a face index + 2D UV to a world-space direction for cubemap sampling.
///
/// Face order follows DX12 / D3D convention:
///   0 = +X, 1 = -X, 2 = +Y, 3 = -Y, 4 = +Z, 5 = -Z
///
/// # Parameters
/// - faceIndex: cubemap face [0, 5].
/// - uv:        normalised UV in [0, 1]^2 within that face.
///
/// # Returns
/// Unnormalised world-space direction vector.
float3 CubeFaceDirection(uint faceIndex, float2 uv) {
    // Remap UV from [0,1] to [-1,1]
    float u = uv.x * 2.0f - 1.0f;
    float v = uv.y * 2.0f - 1.0f;

    // DX cubemap convention: +Y is up, V is flipped for top/bottom
    float3 dir;
    switch (faceIndex) {
        case 0: dir = float3( 1.0f,   -v,   -u); break; // +X
        case 1: dir = float3(-1.0f,   -v,    u); break; // -X
        case 2: dir = float3(   u,  1.0f,    v); break; // +Y
        case 3: dir = float3(   u, -1.0f,   -v); break; // -Y
        case 4: dir = float3(   u,   -v,  1.0f); break; // +Z
        case 5: dir = float3(  -u,   -v, -1.0f); break; // -Z
        default: dir = float3(0.0f, 0.0f, 1.0f); break;
    }
    return dir;
}

#endif // IBL_HLSLI
