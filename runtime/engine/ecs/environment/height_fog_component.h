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
#include "../core/dirty_flags.h"

namespace uz {

/// Exponential height fog parameters for the fog compositing pass.
///
/// The fog pass reads scene depth, reconstructs world position, computes
/// fog density along the view ray, and blends the fog color over the scene.
struct HeightFogComponent : public CBDirtyMixin {
    /// Fog inscattering luminance in linear space.
    /// @ref Hillaire 2016 — default white-ish for physically plausible haze
    Vec3 color = {0.45f, 0.55f, 0.65f};

    /// Base fog density factor. Higher = thicker fog. Typical range: 0.02 to 0.5.
    /// 0.15 = clearly visible fog at ~100m distance.
    float density = 0.15f;

    /// Height falloff coefficient. Controls how quickly fog thins with altitude.
    /// Works in conjunction with baseHeight: fog is densest at baseHeight and
    /// decays exponentially above it. With baseHeight=-5000 and falloff=0.2,
    /// fog is visible at ground level (Z=0) with gradual falloff upward.
    float heightFalloff = 0.2f;

    /// World-space height at which fog density is at its maximum (cm).
    /// -10 = just below ground. The exponential height fog integrates
    /// density along the view ray — rays going toward the ground pass
    /// through dense fog near baseHeight even if camera is above it.
    float baseHeight = -5000.f;

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
    /// @ref Hillaire 2016 — warm sun glow through fog
    Vec3 directionalInscatteringColor = {1.0f, 0.85f, 0.5f};

    /// Exponent controlling the tightness of the directional inscattering cone.
    /// Higher = tighter sun halo.
    /// DirectionalInscatteringExponent default = 4
    float directionalInscatteringExponent = 4.0f;

    /// Start distance for directional inscattering (cm from camera).
    /// default 10000.0 cm
    float directionalInscatteringStartDistance = 10000.f;

    // ── Volumetric Fog ────────────────────────────────────────────────────

    /// Enable volumetric fog (requires compute-based froxel grid).
    bool enableVolumetricFog = false;

    /// @ref Wronski 2014 "Volumetric Fog: Unified Compute Shader Based Solution"
    /// Scattering distribution (phase function asymmetry). Positive = forward scatter.
    /// Range [-1, 1]. Default: 0.2.
    float volumetricFogScatteringDistribution = 0.2f;

    /// @ref Wronski 2014 — volumetric fog albedo
    /// Single-scattering albedo of the volumetric fog medium. Default: white (1,1,1).
    Vec3 volumetricFogAlbedo = {1.0f, 1.0f, 1.0f};

    /// @ref Wronski 2014 — volumetric fog emissive
    /// Self-emissive color of the fog medium (linear RGB). Default: black (0,0,0).
    Vec3 volumetricFogEmissive = {0.0f, 0.0f, 0.0f};

    /// @ref Wronski 2014 — volumetric fog extinction scale
    /// Multiplier for the extinction coefficient of the volumetric fog. Default: 1.0.
    float volumetricFogExtinctionScale = 1.0f;

    /// @ref Wronski 2014 — volumetric fog maximum distance
    /// Maximum distance from camera for volumetric fog evaluation in centimeters. Default: 6400.0 cm.
    float volumetricFogDistance = 6400.0f;

    /// @ref Wronski 2014 — volumetric fog start distance
    /// Minimum distance from camera before volumetric fog begins in centimeters. Default: 0.0 cm.
    float volumetricFogStartDistance = 0.0f;

    /// @ref Wronski 2014 — volumetric fog near fade-in distance
    /// Distance over which volumetric fog fades in near the camera in centimeters. Default: 300.0 cm.
    float volumetricFogNearFadeInDistance = 300.0f;

    /// @ref Wronski 2014 — static lighting scattering intensity
    /// Multiplier for static (baked) lighting scattering contribution. Default: 1.0.
    float volumetricFogStaticLightingScatteringIntensity = 1.0f;

    /// @ref Height fog / volumetric fog — override inscattering colors
    /// When true, light colors are overridden by the fog inscattering colors for
    /// volumetric fog. Default: false.
    bool overrideLightColorsWithFogInscatteringColors = false;

    // ── Inscattering Texture ────────────────────────────────────────────────

    /// @ref Exponential height fog — cubemap inscattering
    /// Handle/ID of a cubemap texture used for inscattering color lookup. 0 = none.
    uint32_t inscatteringColorCubemapId = 0;

    /// @ref Exponential height fog — cubemap rotation angle
    /// Rotation angle for the inscattering cubemap in degrees. Default: 0.0.
    float inscatteringColorCubemapAngle = 0.0f;

    /// @ref Exponential height fog — inscattering texture tint
    /// Tint color applied to the inscattering cubemap (linear RGB). Default: (1,1,1).
    Vec3 inscatteringTextureTint = {1.0f, 1.0f, 1.0f};

    /// @ref Exponential height fog — fully directional inscattering color distance
    /// Distance at which inscattering color becomes fully directional (from cubemap) in centimeters.
    /// Default: 100000.0 cm.
    float fullyDirectionalInscatteringColorDistance = 100000.0f;

    /// @ref Exponential height fog — non-directional inscattering color distance
    /// Distance at which non-directional (ambient) inscattering starts blending in centimeters.
    /// Default: 50000.0 cm.
    float nonDirectionalInscatteringColorDistance = 50000.0f;

    // ── Atmosphere Contribution ─────────────────────────────────────────────

    /// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — sky atmosphere fog tint
    /// Per-channel scale for the sky atmosphere's ambient contribution to fog inscattering.
    /// Default: (1,1,1).
    Vec3 skyAtmosphereAmbientContributionColorScale = {1.0f, 1.0f, 1.0f};

    // ── Control ───────────────────────────────────────────────────────────

    /// Whether the fog pass is enabled.
    bool enabled = true;
};

} // namespace uz
