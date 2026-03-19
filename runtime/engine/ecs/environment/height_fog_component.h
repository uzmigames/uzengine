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
    /// Fog color in linear space. Blended over the scene where fog is dense.
    /// UE5 default: (1, 1, 1) white. Tinted by inscattering color in practice.
    Vec3 color = {1.0f, 1.0f, 1.0f};

    /// Base fog density factor. Higher = thicker fog. Typical range: 0.001 to 0.1.
    /// UE5 default: 0.02 (FogDensity).
    float density = 0.02f;

    /// Height falloff coefficient. Controls how quickly fog thins with altitude.
    /// Higher = fog concentrated closer to baseHeight. Typical: 0.2 to 2.0.
    /// UE5 default: 0.2 (FogHeightFalloff).
    float heightFalloff = 0.2f;

    /// World-space height at which fog density is at its maximum (cm).
    float baseHeight = 0.f;

    /// Maximum fog opacity (0 to 1). Prevents fog from fully obscuring distant objects.
    /// UE5 default: 1.0 (FogMaxOpacity).
    float maxOpacity = 1.0f;

    /// Minimum distance from camera before fog begins. Objects closer are fog-free.
    /// UE5: StartDistance. Default: 0 (UE5), we use 10 for cleaner near-camera.
    float startDistance = 0.f;

    /// Maximum fog distance. Beyond this, fog contribution is zero.
    /// UE5: FogCutoffDistance. 0 = unlimited. Default: 0.
    float fogCutoffDistance = 0.f;

    // ── Second Fog Layer (UE5 parity) ─────────────────────────────────────

    /// Second fog layer density. 0 = disabled (single layer only).
    /// UE5: SecondFogData.FogDensity. Allows two fog layers at different heights.
    float secondFogDensity = 0.f;

    /// Second fog layer height falloff coefficient.
    /// UE5: SecondFogData.FogHeightFalloff. Default: 0.2.
    float secondFogHeightFalloff = 0.2f;

    /// Second fog layer base height in world units (cm, UE5 parity).
    /// UE5: SecondFogData.FogBaseHeight. Default: 0.
    float secondFogBaseHeight = 0.f;

    // ── Directional Inscattering (sun-facing fog glow) ────────────────────

    /// Color of the directional inscattering (sun-facing fog glow).
    /// UE5: DirectionalInscatteringColor. Default: warm orange (1, 0.8, 0.6).
    Vec3 directionalInscatteringColor = {1.0f, 0.8f, 0.6f};

    /// Exponent controlling the tightness of the directional inscattering cone.
    /// Higher = tighter sun halo. UE5: DirectionalInscatteringExponent. Default: 4.
    /// @source UE5 ExponentialHeightFogComponent.cpp — DirectionalInscatteringExponent=4
    float directionalInscatteringExponent = 4.0f;

    /// Start distance for directional inscattering (cm from camera).
    /// UE5: DirectionalInscatteringStartDistance. Default: 0.
    float directionalInscatteringStartDistance = 0.f;

    // ── Volumetric Fog (placeholder — Phase 3+ implementation) ────────────

    /// Enable volumetric fog (requires compute-based froxel grid — P3+).
    /// UE5: bEnableVolumetricFog. Default: false.
    bool enableVolumetricFog = false;

    // ── Control ───────────────────────────────────────────────────────────

    /// Whether the fog pass is enabled.
    bool enabled = true;
};

} // namespace uz
