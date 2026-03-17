/// Multi-light evaluation helpers for forward PBR rendering.
///
/// Defines the LightData struct and attenuation functions for directional,
/// point, and spot lights. Supports up to MAX_LIGHTS (8) dynamic lights
/// per frame.
///
/// # Light Types
/// - 0 = Directional: infinite distance, no attenuation, positionOrDir is direction toward light.
/// - 1 = Point:       omnidirectional, smooth distance attenuation with finite range.
/// - 2 = Spot:        cone-shaped, combines distance attenuation with angular falloff.
///
/// # Prerequisites
/// The including shader must declare pbr_common.hlsli (for BRDF functions)
/// and shadow_common.hlsli (for SampleShadowMap).

#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

// LightData and MAX_LIGHTS must be defined before including this file.
// See light_types.hlsli (included by the shader before this header).

// ─────────────────────────────────────────────────────────────────────────────
// Attenuation Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Smooth distance attenuation for point and spot lights.
///
/// @source UE5 DeferredLightingCommon.ush:265 — GetLocalLightAttenuation
/// @constants window = saturate(1 - (d/r)^4)^2 (DeferredLightingCommon.ush:265)
/// @deviations Includes 1/(d^2+1) inverse-square term for forward path (UE5 applies this via pre-multiplied light intensity on desktop deferred, but mobile uses the same formula: DeferredLightingCommon.ush:271)
///
/// Uses UE5-style inverse-square falloff with a smooth window function that
/// reaches exactly zero at the light's range. This avoids popping artifacts
/// when lights enter/exit the visible set.
///
///   window = saturate(1 - (d/r)^4)^2    (UE5: Square(saturate(1 - Square(d^2 * invR^2))))
///   atten  = window / (d^2 + 1)
///
/// The +1 in the denominator prevents the singularity at d=0 and provides
/// a natural intensity cap at the light center.
///
/// # Parameters
/// - dist:  distance from the shading point to the light source, in world units.
/// - range: light's attenuation radius. Must be > 0 for meaningful results.
///
/// # Returns
/// Attenuation factor in [0, 1]. Returns 0 if range <= 0.
float ComputePointAttenuation(float dist, float range) {
    if (range <= 0.0f) return 0.0f;

    float ratio  = dist / range;
    float ratio2 = ratio * ratio;
    float ratio4 = ratio2 * ratio2;
    float window = saturate(1.0f - ratio4);
    window *= window; // squared window for smooth falloff at range boundary

    // Inverse-square with +1 bias to avoid singularity at dist=0
    return window / (dist * dist + 1.0f);
}

/// Angular attenuation for spot lights (cone falloff).
///
/// Returns 1.0 inside the inner cone, 0.0 outside the outer cone, and a
/// smooth quadratic interpolation in the penumbra region between them.
///
/// # Parameters
/// - L:        normalized direction from shading point toward the light.
/// - spotDir:  normalized spot cone axis (direction the spot is pointing).
/// - innerCos: cosine of the inner half-angle (full intensity boundary).
/// - outerCos: cosine of the outer half-angle (zero intensity boundary).
///             Must satisfy outerCos <= innerCos (wider angle = smaller cosine).
///
/// # Returns
/// Spot factor in [0, 1].
float ComputeSpotFactor(float3 L, float3 spotDir, float innerCos, float outerCos) {
    // dot(-L, spotDir): angle between the reversed light direction and cone axis
    float cosAngle = dot(-L, spotDir);

    if (cosAngle >= innerCos) return 1.0f;  // inside inner cone — full intensity
    if (cosAngle <= outerCos) return 0.0f;  // outside outer cone — no light

    // Smooth penumbra: quadratic falloff between outer and inner cone edges
    float t = saturate((cosAngle - outerCos) / (innerCos - outerCos));
    return t * t;
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Light Accumulation
// ─────────────────────────────────────────────────────────────────────────────

/// Accumulates direct lighting from all active lights using Cook-Torrance BRDF.
///
/// Iterates over the light array, computes per-light direction and attenuation
/// (distance + spot cone), evaluates the full Cook-Torrance BRDF, and sums the
/// outgoing radiance. Shadow mapping is applied only to the first directional
/// light (index 0) when cascadeCount > 0.
///
/// # Parameters
/// - positionWS:  fragment world-space position.
/// - N:           world-space surface normal (normalized).
/// - V:           world-space view direction toward camera (normalized).
/// - NdotV:       pre-computed max(dot(N, V), 0.001) — constant across lights.
/// - albedo:      surface base color (linear).
/// - metallic:    metallic factor [0, 1].
/// - alpha:       roughness squared (PBR alpha parameter).
/// - F0:          reflectance at normal incidence.
/// - lightCount:  number of active lights (clamped to MAX_LIGHTS internally).
/// - lights:      array of LightData structs.
/// - cameraPos:   world-space camera position (for shadow view depth).
/// - cascadeCount: number of active shadow cascades (0 = shadows disabled).
///
/// # Returns
/// Total outgoing radiance from all direct light sources (before ambient/emissive).
float3 AccumulateDirectLighting(
    float3 positionWS, float3 N, float3 V, float NdotV,
    float3 albedo, float metallic, float alpha, float3 F0,
    uint lightCount, LightData lights[MAX_LIGHTS],
    float3 cameraPos, uint cascadeCount, float2 screenPos)
{
    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    for (uint i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        LightData light = lights[i];

        float3 L;
        float attenuation = 1.0f;

        if (light.lightType < 0.5f) {
            // ── Directional light ──
            // positionOrDir stores the direction TOWARD the light (pre-normalized)
            L = normalize(light.positionOrDir);

            // Shadow mapping only for the first directional light
            if (i == 0 && cascadeCount > 0) {
                float viewDepth = length(positionWS - cameraPos);
                attenuation = SampleShadowMap(positionWS, N, viewDepth, screenPos);
            }
        } else {
            // ── Point or Spot light ──
            float3 toLight = light.positionOrDir - positionWS;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001f); // safe normalize

            attenuation = ComputePointAttenuation(dist, light.range);

            if (light.lightType > 1.5f) {
                // Spot light — multiply distance atten by cone falloff
                attenuation *= ComputeSpotFactor(L, light.spotDirection,
                                                  light.innerConeCos, light.outerConeCos);
            }
        }

        // ── Per-light BRDF evaluation ──
        float3 H     = normalize(V + L);
        float  NdotL = max(dot(N, L), 0.001f);
        float  NdotH = saturate(dot(N, H));
        float  VdotH = saturate(dot(V, H));

        float  D = DistributionGGX(NdotH, alpha);
        float  G = GeometrySmith(NdotV, NdotL, alpha);
        float3 F = FresnelSchlick(VdotH, F0);

        // Cook-Torrance specular: D*G*F / (4 * N.V * N.L)
        float3 specular = (D * G * F) / max(4.0f * NdotV * NdotL, 0.001f);

        // Energy conservation: metals have no diffuse lobe
        float3 kS = F;
        float3 kD = (1.0f - kS) * (1.0f - metallic);
        float3 diffuse = kD * albedo / PI;

        Lo += (diffuse + specular) * light.color * NdotL * attenuation;
    }

    return Lo;
}

#endif // LIGHTING_HLSLI
