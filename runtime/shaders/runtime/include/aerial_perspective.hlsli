/// Aerial perspective helper functions for atmosphere ray-marching.
///
/// @ref Bruneton 2017 "Precomputed Atmospheric Scattering"
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
///
/// Provides AP_-prefixed wrappers around standard atmosphere math so that
/// aerial_perspective_volume_cs.hlsl can include a single header.
/// The underlying math is identical to sky_atmosphere_ps.hlsl.
#ifndef AERIAL_PERSPECTIVE_HLSLI
#define AERIAL_PERSPECTIVE_HLSLI

#include "units.hlsli"
#include "math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// World-space (cm) to sky-space (km) conversion.
static const float AP_WORLD_TO_KM = UZ_CM_TO_KM;

/// Small offset to keep the camera above the planet surface (km).
static const float AP_PLANET_RADIUS_OFFSET_KM = UZ_PLANET_RADIUS_OFFSET_KM;

/// Epsilon for extinction denominator to avoid division by zero.
static const float AP_EXT_EPS = UZ_EXTINCTION_EPS;

/// Mie extinction-to-scattering ratio.
/// @ref Bruneton 2017 — Mie extinction ≈ 1.11 × Mie scattering.
static const float AP_MIE_EXTINCTION_RATIO = 1.11f;

// ─────────────────────────────────────────────────────────────────────────────
// Phase Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Rayleigh phase function: (3/16π)(1 + cos²θ).
/// @ref Bruneton 2017 eq. 2
float AP_RayleighPhase(float cosTheta) {
    return (3.0f / (16.0f * PI)) * (1.0f + cosTheta * cosTheta);
}

/// Henyey-Greenstein phase function for Mie scattering.
/// @ref Henyey & Greenstein 1941
float AP_MiePhase(float cosTheta, float g) {
    float g2    = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f / (4.0f * PI)) * (1.0f - g2) / (denom * sqrt(denom));
}

// ─────────────────────────────────────────────────────────────────────────────
// Density
// ─────────────────────────────────────────────────────────────────────────────

/// Exponential density profile: exp(-altitude / scaleHeight).
/// @ref Bruneton 2017 eq. 1
float AP_DensityAtAltitude(float altitudeKm, float scaleHeightKm) {
    return exp(-max(0.0f, altitudeKm) / scaleHeightKm);
}

// ─────────────────────────────────────────────────────────────────────────────
// Geometry
// ─────────────────────────────────────────────────────────────────────────────

/// Ray-sphere intersection. Returns (tNear, tFar).
/// tNear < 0 means ray origin is inside sphere.
/// Returns (-1, -1) if no intersection.
float2 AP_RaySphereIntersect(float3 origin, float3 dir, float3 center, float radius) {
    float3 oc   = origin - center;
    float  b    = dot(oc, dir);
    float  c    = dot(oc, oc) - radius * radius;
    float  disc = b * b - c;
    if (disc < 0.0f) return float2(-1.0f, -1.0f);
    float sq = sqrt(disc);
    return float2(-b - sq, -b + sq);
}

// ─────────────────────────────────────────────────────────────────────────────
// Transmittance LUT Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Maps (viewHeight, viewZenithCosAngle) to UV for the transmittance LUT.
/// @ref Bruneton 2017 — non-linear parameterization concentrating precision near horizon.
void AP_TransmittanceLutParamsToUv(float viewHeight, float viewZenithCosAngle,
    float bRadius, float tRadius, out float2 uv)
{
    float H   = sqrt(max(0.0f, tRadius * tRadius - bRadius * bRadius));
    float rho = sqrt(max(0.0f, viewHeight * viewHeight - bRadius * bRadius));

    float discriminant = viewHeight * viewHeight
        * (viewZenithCosAngle * viewZenithCosAngle - 1.0f)
        + tRadius * tRadius;
    float d = max(0.0f, (-viewHeight * viewZenithCosAngle + sqrt(max(0.0f, discriminant))));

    float dMin = tRadius - viewHeight;
    float dMax = rho + H;
    float uU   = (d - dMin) / (dMax - dMin);
    float uV   = rho / H;

    uv = float2(uU, uV);
}

/// Samples the precomputed transmittance LUT for a given position and direction.
/// @ref Bruneton 2017 "Precomputed Atmospheric Scattering"
float3 AP_SampleTransmittanceLut(Texture2D<float4> lut, SamplerState samp,
    float3 samplePos, float3 queryDir,
    float bRadius, float tRadius)
{
    float viewHeight = length(samplePos);
    float viewZenithCosAngle = dot(normalize(samplePos), queryDir);

    float2 uv;
    AP_TransmittanceLutParamsToUv(viewHeight, viewZenithCosAngle, bRadius, tRadius, uv);
    return lut.SampleLevel(samp, uv, 0).rgb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Scattering LUT Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Samples the precomputed multi-scattering LUT for aerial perspective.
/// The LUT is parameterized by sun zenith cosine (U) and normalized height (V).
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
/// @constants UV mapping: x = dot(up,sunDir)*0.5+0.5, y = (h-bR)/(tR-bR)
/// @deviations NONE
float3 AP_SampleMultiScatteringLut(Texture2D<float4> lut, SamplerState samp,
    float3 samplePos, float3 sunDir_, float bRadius, float tRadius)
{
    float3 upVec = normalize(samplePos);
    float viewZenithCos = dot(upVec, sunDir_);
    float h = length(samplePos);
    float2 uv = saturate(float2(
        viewZenithCos * 0.5f + 0.5f,
        (h - bRadius) / (tRadius - bRadius)
    ));
    return lut.SampleLevel(samp, uv, 0).rgb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Deferred Lighting Integration (stub for Step 8)
// ─────────────────────────────────────────────────────────────────────────────

/// Applies aerial perspective to a lit pixel. Returns float4(inscatter.rgb, transmittance).
/// Currently returns identity (no AP applied) — will be implemented at Step 8.
float4 ComputeAerialPerspective(float3 wp, float3 cam, float3 sd, float si,
    float br, float tr, float3 rb, float rsh, float mb, float msh, float ma,
    float rs, float ms, Texture2D<float4> lut, SamplerState s) {
    return float4(0, 0, 0, 1);
}

#endif
