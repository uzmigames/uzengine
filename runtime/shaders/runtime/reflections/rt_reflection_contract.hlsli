// Shared CPU/shader contract of the hardware ray-traced reflection pass: the stage 1 mirror ray and
// fallback mask, the stage 2 glossy lobe, probe weighting and hit radiance, and the stage 3
// no-screen-trace mode (the pass owns the reflection buffer). Scalar math only, so
// the gtests (tests/engine/renderer/test_rt_reflection.cpp) evaluate the same expressions the shader
// runs.
#ifndef UZ_RT_REFLECTION_CONTRACT
#define UZ_RT_REFLECTION_CONTRACT

#ifdef __cplusplus
#include "vector3.h"
#include <cmath>
#include <cstdint>
namespace uz::rtreflection {
using float3 = Vec3;
using uint = uint32_t;
inline float ReflectionSqrt(float x) { return std::sqrt(x); }
inline float ReflectionMax(float a, float b) { return a > b ? a : b; }
inline float ReflectionSaturate(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
#else
float ReflectionSqrt(float x) { return sqrt(x); }
float ReflectionMax(float a, float b) { return max(a, b); }
float ReflectionSaturate(float x) { return saturate(x); }
#endif

static const float kReflectionRayTMin = 1.0f;
static const float kReflectionSurfaceBias = 2.0f;

// ─── Stage 2 ──────────────────────────────────────────────────────────────────────────────────

// Root-constant flag bits (`RTReflectionRootConstants::flags`).
static const uint kReflectionFlagGlossy      = 1u;   // r.RayTracing.Reflections.Glossy
static const uint kReflectionFlagIndirect    = 2u;   // r.RayTracing.Reflections.Indirect
static const uint kReflectionFlagHitDistance = 4u;   // r.RayTracing.Reflections.Denoise: write the ray t
// Stage 3: no screen trace this frame (SSR off, or r.SSR.Trace 0) — the pass owns the buffer.
static const uint kReflectionFlagNoScreenTrace = 8u;
// Phase D stage 1: the surface radiance cache is bound (`surfaceCacheParamsSrv`, DWORD 16) — a
// readable cell replaces the whole hit radiance, a miss shades inline and updates the cell.
static const uint kReflectionFlagSurfaceCache = 16u;
// Rough reflections stage 1 (r.RayTracing.Reflections.Rough): every roughness is eligible, the
// glossy lobe is sampled, the texel carries the full blend weight (no SSR roughness fade) and the
// sample's radiance is weighted by its VNDF throughput over the split-sum the lighting applies
// (`ReflectionRoughSampleWeight`).
static const uint kReflectionFlagRough = 32u;

// Below this roughness the lobe is a mirror: the screen trace's MIRROR_THRESHOLD
// (ssr_trace_cs.hlsl) and the cutoff below which the SSR spatial denoise passes texels through.
static const float kReflectionMirrorRoughness = 0.04f;
// GGX alpha floor of the deferred BRDF (constants.hlsli kMinGGXAlpha, brdf.hlsli RoughnessToAlpha).
static const float kReflectionMinGGXAlpha = 0.0064f;
static const float kReflectionPi = 3.14159265358979323846f;

float ReflectionDot(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// incident and normal are unit vectors; incident points toward the surface. With a sampled
// microfacet normal in place of the surface normal this is the glossy ray.
float3 ReflectionDirection(float3 incident, float3 normal) {
    float d = incident.x * normal.x + incident.y * normal.y + incident.z * normal.z;
    return incident - normal * (2.0f * d);
}

bool ReflectionEligible(float deviceZ, float roughness, float maxRoughness) {
    return deviceZ > 0.0001f && roughness <= maxRoughness;
}

// Use the current screen trace, not temporally reprojected history, to avoid
// retaining a reflection after its source has left the screen.
bool ReflectionNeedsRT(float deviceZ, float roughness, float maxRoughness,
                       float screenConfidence) {
    return ReflectionEligible(deviceZ, roughness, maxRoughness)
        && screenConfidence <= 0.001f;
}

bool ReflectionHitUsable(bool triangleHit, float surfaceDistance, float normalLengthSquared) {
    return triangleHit && surfaceDistance < 1e8f && normalLengthSquared > 0.5f;
}

// The ray-traced roughness cutoff: r.RayTracing.Reflections.MaxRoughness when set, else the SSR
// cutoff the stage 1 pass used (so an unset CVar keeps the stage 1 range and blend weights).
float ReflectionMaxRoughness(float rtMaxRoughness, float ssrMaxRoughness) {
    return rtMaxRoughness > 0.0f ? rtMaxRoughness : ssrMaxRoughness;
}

// Stage 2: a glossy (GGX-VNDF sampled) ray instead of the mirror vector — only when opted in and
// the surface is rougher than a mirror.
bool ReflectionUsesGlossyLobe(uint flags, float roughness) {
    return (flags & (kReflectionFlagGlossy | kReflectionFlagRough)) != 0u && roughness > kReflectionMirrorRoughness;
}

// ─── Rough reflections stage 1 ─────────────────────────────────────────────────────────────────

bool ReflectionRoughMode(uint flags) {
    return (flags & kReflectionFlagRough) != 0u;
}

// The roughness cutoff the pass runs with: every surface in rough mode, else the stage 2 cutoff.
float ReflectionRoughMaxRoughness(bool rough, float maxRoughness) {
    return rough ? 1.0f : maxRoughness;
}

// Blend weight of a traced texel in the lighting's `lerp(radiance, ssr.rgb, ssr.a)`. Stages 1-3:
// the screen trace's roughness fade (1 below half the cutoff, 0 at it) times r.SSR.Intensity — at
// the cutoff 1.0 the fade still drops a roughness-0.71 floor to 0.58, and the remaining 0.42 goes to
// the layer underneath. Rough mode: the ray is a sample of the whole lobe at every roughness, so it
// owns the layer (no fade).
float ReflectionBlendWeight(uint flags, float roughness, float roughnessMaskScale, float intensity) {
    if (ReflectionRoughMode(flags)) return intensity;
    return ReflectionSaturate(roughness * roughnessMaskScale + 2.0f) * intensity;
}

// Smith GGX Lambda, G1 and the height-correlated G2 (brdf.hlsli Lambda_GGX / G1_SmithGGX /
// G_SmithGGXCorrelated, the path tracer's terms) on alpha^2.
float ReflectionLambdaGGX(float alphaSq, float cosTheta) {
    float cos2 = cosTheta * cosTheta;
    float tan2 = ReflectionMax(1.0f - cos2, 0.0f) / ReflectionMax(cos2, 1e-8f);
    return 0.5f * (-1.0f + ReflectionSqrt(1.0f + alphaSq * tan2));
}

// Weight of one glossy sample relative to the lobe's mean: the path tracer's VNDF reflection
// throughput F(V,H) G2(V,L) / G1(V) (D and the cosine cancel the pdf) over the split-sum
// F0 * A + F90 * B the lighting multiplies the reflection buffer by (EnvironmentBRDFApprox).
// The lighting then evaluates E[L * F G2/G1], the path tracer's estimator, instead of
// E[L] * E[F G2/G1]: at grazing view on a rough lobe the dark near-horizon directions carry little
// throughput (small G1(L)) and the bright high-elevation ones most of it, so the unweighted mean
// undercounts. A sample below the horizon has G2 = 0 and weighs nothing (the path tracer drops it).
// f0 is a scalar proxy (0.04 dielectric, 1 metal: the weight's shape, not the tint the lighting
// applies), F90 = 1 (F_Schlick).
// @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals" — VNDF weight G2 / G1
float ReflectionRoughSampleWeight(float alpha, float NdotV, float NdotL, float VdotH, float f0,
                                  float envA, float envB) {
    if (NdotL <= 0.0f || NdotV <= 0.0f) return 0.0f;
    const float a2 = alpha * alpha;
    const float lambdaV = ReflectionLambdaGGX(a2, NdotV);
    const float lambdaL = ReflectionLambdaGGX(a2, NdotL);
    const float g2OverG1 = (1.0f + lambdaV) / (1.0f + lambdaV + lambdaL);
    const float c = 1.0f - ReflectionSaturate(VdotH);
    const float c2 = c * c;
    const float fresnel = f0 + (1.0f - f0) * (c2 * c2 * c);
    const float splitSum = ReflectionMax(f0 * envA + envB, 1e-4f);
    return fresnel * g2OverG1 / splitSum;
}

// Scalar F0 proxy of the weight: the dielectric 4 % to a metal's 1 by the G-buffer metallic.
float ReflectionRoughF0(float metallic) {
    return 0.04f + 0.96f * ReflectionSaturate(metallic);
}

// GGX alpha of the reflection lobe: the deferred BRDF's conversion (roughness^2, floored), so the
// sampled lobe is the lobe the lighting's split-sum integrates.
float ReflectionGlossyAlpha(float roughness) {
    return ReflectionMax(roughness * roughness, kReflectionMinGGXAlpha);
}

// Density over reflected directions of the glossy ray: the GGX distribution of visible normals
// reflected about the sampled microfacet normal, D(H) G1(V) / (4 N.V) — the path tracer's
// SampleGGX pdf (pathtracer_shared.hlsli). Zero outside the lobe's support (H below the surface,
// V below the surface or behind the microfacet).
// @ref Heitz 2018 "Sampling the GGX Distribution of Visible Normals", eq. 17 + 19
float ReflectionVndfPdf(float alpha, float NdotH, float NdotV, float VdotH) {
    if (NdotH <= 0.0f || NdotV <= 0.0f || VdotH <= 0.0f) return 0.0f;
    float a2 = alpha * alpha;
    float d  = (NdotH * a2 - NdotH) * NdotH + 1.0f;
    float D  = a2 / (d * d * kReflectionPi);
    float cos2 = NdotV * NdotV;
    float tan2 = ReflectionMax(1.0f - cos2, 0.0f) / cos2;
    float G1 = 2.0f / (1.0f + ReflectionSqrt(1.0f + a2 * tan2));
    return D * G1 / (4.0f * NdotV);
}

// Facing weight of a radiance-cache probe for a receiver: the squared cosine between the receiver
// normal and the direction to the probe, as the deferred lighting and the SDF reflections weight
// probes (one behind the receiver's tangent plane does not light it).
float ReflectionProbeFacingWeight(float3 normal, float3 toProbe) {
    float d2 = ReflectionDot(toProbe, toProbe);
    if (d2 <= 0.0f) return 1.0f;
    float c = ReflectionSaturate(ReflectionDot(normal, toProbe) / ReflectionSqrt(d2));
    return c * c;
}

// Radiance leaving the hit toward the reflector: the Lambert sun term (sun colour x intensity x
// N.L x shadow-ray visibility, over pi) plus the probe term — the radiance cache stores E / pi,
// so it adds to the sun term without another 1/pi (sdf_reflections.hlsli ShadeSdfReflectionHit).
// A hit in the sun's shadow is lit by the probe term alone.
float3 ReflectionHitRadiance(float3 albedo, float3 sunRadiance, float3 probeIrradianceOverPi) {
    return albedo * (sunRadiance / kReflectionPi) + albedo * probeIrradianceOverPi;
}

// ─── Stage 3 ──────────────────────────────────────────────────────────────────────────────────

// With no screen trace this frame the pass is the only writer of the reflection buffer the
// lighting blends, so it writes every texel: the ray's radiance where it traces, zero weight where
// it does not (an ineligible surface, a ray into the surface, a hit without attributes — the
// lighting keeps its prefiltered probe / sky there), and the screen confidence that hands a texel
// to the ray is zero everywhere, so every eligible texel is traced:
// ReflectionNeedsRT(z, r, max, 0) == ReflectionEligible(z, r, max). With the flag clear the pass is
// the stage 1/2 pass: it reads the screen trace and fills only its holes.
bool ReflectionOwnsOutput(uint flags) {
    return (flags & kReflectionFlagNoScreenTrace) != 0u;
}

#ifdef __cplusplus
} // namespace uz::rtreflection
#endif
#endif
