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
// Deferred Lighting Integration — Inline Per-Pixel Aerial Perspective
// ─────────────────────────────────────────────────────────────────────────────

/// Number of ray-march steps for inline per-pixel aerial perspective.
/// Matches the volume CS step count for consistency.
static const int AP_INLINE_STEPS = 8;

/// Applies aerial perspective to a lit pixel via inline ray-marching.
///
/// Computes single-scattering + transmittance along the ray from camera to the
/// world-space fragment position. Returns float4(inscattering.rgb, meanTransmittance).
///
/// # Parameters
/// - wp:  World-space pixel position (cm).
/// - cam: Camera world position (cm).
/// - sd:  Normalised direction TOWARD the sun.
/// - si:  Sun illuminance intensity.
/// - br:  Planet bottom radius (km).
/// - tr:  Atmosphere top radius (km).
/// - rb:  Rayleigh scattering coefficients (km^-1).
/// - rsh: Rayleigh scale height (km).
/// - mb:  Mie scattering coefficient (km^-1).
/// - msh: Mie scale height (km).
/// - ma:  Mie HG anisotropy (typically 0.8).
/// - rs:  Artistic Rayleigh scattering multiplier.
/// - ms:  Artistic Mie scattering multiplier.
/// - lut: Precomputed transmittance LUT (256x64 RGBA16F).
/// - s:   Linear clamp sampler for LUT access.
///
/// # Returns
/// float4(inscattering.rgb, meanTransmittance).
/// Apply as: finalColor = litColor * result.a + result.rgb;
///
/// @ref Bruneton 2017 "Precomputed Atmospheric Scattering"
/// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
/// @constants AP_INLINE_STEPS=8, AP_MIE_EXTINCTION_RATIO=1.11
float4 ComputeAerialPerspective(float3 wp, float3 cam, float3 sd, float si,
    float br, float tr, float3 rb, float rsh, float mb, float msh, float ma,
    float rs, float ms, Texture2D<float4> lut, SamplerState s)
{
    // ── Convert camera and world position to sky-space (km) ──────────────
    float3 cameraSkyKm = cam * AP_WORLD_TO_KM + float3(0.0f, 0.0f, br);
    float3 pointSkyKm  = wp  * AP_WORLD_TO_KM + float3(0.0f, 0.0f, br);

    // Snap camera above planet surface
    float distToCenter  = length(cameraSkyKm);
    float minSafeRadius = br + AP_PLANET_RADIUS_OFFSET_KM;
    if (distToCenter < minSafeRadius) {
        cameraSkyKm = cameraSkyKm * (minSafeRadius / distToCenter);
    }

    // ── Ray setup ────────────────────────────────────────────────────────
    float3 rayVec = pointSkyKm - cameraSkyKm;
    float  rayLen = length(rayVec);

    // No atmosphere contribution for very close fragments
    if (rayLen < 0.001f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    float3 rayDir = rayVec / rayLen;

    // ── Clip ray to atmosphere sphere ────────────────────────────────────
    float2 atmoHit = AP_RaySphereIntersect(cameraSkyKm, rayDir,
                                            float3(0, 0, 0), tr);
    if (atmoHit.y < 0.0f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    float tStart   = max(atmoHit.x, 0.0f);
    float tEnd     = min(rayLen, atmoHit.y);
    float marchLen = tEnd - tStart;

    if (marchLen <= 0.0f)
        return float4(0.0f, 0.0f, 0.0f, 1.0f);

    // ── Phase functions ──────────────────────────────────────────────────
    float cosTheta = dot(rayDir, sd);
    float phaseR   = AP_RayleighPhase(cosTheta);
    float phaseM   = AP_MiePhase(cosTheta, ma);

    // ── Artistic scattering coefficients ─────────────────────────────────
    float3 artRayleighBeta = rb * rs;
    float  artMieBeta      = mb * ms;

    // ── 8-step ray march ─────────────────────────────────────────────────
    /// @ref Bruneton 2017 — single-scattering ray march with transmittance LUT
    float stepLenKm = marchLen / float(AP_INLINE_STEPS);
    float3 L          = float3(0, 0, 0);
    float3 Throughput = float3(1, 1, 1);

    for (int i = 0; i < AP_INLINE_STEPS; ++i) {
        float  t        = tStart + (float(i) + 0.5f) * stepLenKm;
        float3 sampleKm = cameraSkyKm + rayDir * t;
        float  altKm    = max(0.0f, length(sampleKm) - br);

        float densR = AP_DensityAtAltitude(altKm, rsh);
        float densM = AP_DensityAtAltitude(altKm, msh);

        // Extinction uses physical coefficients (must match transmittance LUT)
        float3 extR     = rb * densR;
        float3 extM     = mb * AP_MIE_EXTINCTION_RATIO * densM;
        float3 extTotal = extR + extM;

        float3 T_step = exp(-extTotal * stepLenKm);

        // Sun transmittance from LUT
        float3 sunT = AP_SampleTransmittanceLut(lut, s,
            sampleKm, sd, br, tr);

        // In-scattering with artistic coefficients
        float3 scatterR = artRayleighBeta * densR * phaseR;
        float3 scatterM = artMieBeta * densM * phaseM;
        float3 scatter  = sunT * (scatterR + scatterM);

        // Analytical step integral: Sint = scatter * (1 - T_step) / ext
        float3 Sint = (scatter - scatter * T_step) / max(extTotal, AP_EXT_EPS);

        L         += Throughput * Sint * si;
        Throughput *= T_step;
    }

    // Mean transmittance (average of RGB channels)
    /// @ref dot(Transmittance, float3(1/3, 1/3, 1/3))
    float meanTransmittance = dot(Throughput, float3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f));

    return float4(L, meanTransmittance);
}

#endif
