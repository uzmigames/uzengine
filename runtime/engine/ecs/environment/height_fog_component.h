/// UzEngine HeightFogComponent — ECS data for exponential height fog.
///
/// Stores fog parameters for a distance-based exponential height fog pass.
/// Attached to a scene entity to enable fog rendering. Only the first entity
/// with this component is used (singleton semantics via first-found gathering).
///
/// # Fog Model
/// Exponential height fog with distance attenuation:
/// ```
/// density(h) = fogDensity * exp(-fogHeightFalloff * (h - baseHeight))
/// fog(d, h)  = 1 - exp(-integral of density along view ray)
/// opacity    = min(fog, maxOpacity)
/// ```
///
/// # Usage
/// ```cpp
/// auto entity = world.CreateEntity();
/// auto& fog = world.AddComponent<HeightFogComponent>(entity);
/// fog.color = {0.7f, 0.75f, 0.85f};
/// fog.density = 0.02f;
/// ```

#pragma once

#include "vector3.h"

namespace uz {

/// Exponential height fog parameters for the fog compositing pass.
///
/// The fog pass reads scene depth, reconstructs world position, computes
/// fog density along the view ray, and blends the fog color over the scene.
struct HeightFogComponent {
    /// Fog inscattering luminance in linear space.
    /// FogInscatteringLuminance default = black
    Vec3 color = {0.0f, 0.0f, 0.0f};

    /// Base fog density factor. Higher = thicker fog. Typical range: 0.001 to 0.1.
    float density = 0.02f;

    /// Height falloff coefficient. Controls how quickly fog thins with altitude.
    /// Higher = fog concentrated closer to baseHeight. Typical: 0.2 to 2.0.
    float heightFalloff = 0.2f;

    /// World-space height at which fog density is at its maximum (cm).
    float baseHeight = 0.f;

    /// Maximum fog opacity (0 to 1). Prevents fog from fully obscuring distant objects.
    float maxOpacity = 1.0f;

    /// Minimum distance from camera before fog begins. Objects closer are fog-free.
    float startDistance = 0.f;

    /// Maximum fog distance. Beyond this, fog contribution is zero.
    float fogCutoffDistance = 0.f;

    // ── Second Fog Layer ─────────────────────────────────────

    /// Second fog layer density. 0 = disabled (single layer only).
    float secondFogDensity = 0.f;

    /// Second fog layer height falloff coefficient.
    float secondFogHeightFalloff = 0.2f;

    /// Second fog layer base height in world units (cm).
    float secondFogBaseHeight = 0.f;

    // ── Directional Inscattering (sun-facing fog glow) ────────────────────

    /// Color of the directional inscattering (sun-facing fog glow).
    /// DirectionalInscatteringLuminance default = black
    Vec3 directionalInscatteringColor = {0.0f, 0.0f, 0.0f};

    /// Exponent controlling the tightness of the directional inscattering cone.
    /// Higher = tighter sun halo.
    /// DirectionalInscatteringExponent default = 4
    float directionalInscatteringExponent = 4.0f;

    /// Start distance for directional inscattering (cm from camera).
    /// default 10000.0 cm
    float directionalInscatteringStartDistance = 10000.f;

    // ── Volumetric Fog (placeholder — Phase 3+ implementation) ────────────

    /// Enable volumetric fog (requires compute-based froxel grid — P3+).
    bool enableVolumetricFog = false;

    // ── Control ───────────────────────────────────────────────────────────

    /// Whether the fog pass is enabled.
    bool enabled = true;
};

} // namespace uz
