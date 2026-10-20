// Shared CPU/shader contract of the hardware ray-traced reflection pass: the stage 1 mirror ray and
// fallback mask, and the stage 2 glossy lobe, probe weighting and hit radiance. Scalar math only, so
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
    return (flags & kReflectionFlagGlossy) != 0u && roughness > kReflectionMirrorRoughness;
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

#ifdef __cplusplus
} // namespace uz::rtreflection
#endif
#endif
