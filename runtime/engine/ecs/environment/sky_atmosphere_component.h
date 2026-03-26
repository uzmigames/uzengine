#pragma once

/// UzEngine SkyAtmosphereComponent — ECS data for physical atmosphere rendering.
///
/// Follows industry-standard SkyAtmosphereComponent parameter conventions:
/// - All distances in KILOMETERS (Equivalent: BottomRadiusKm, AtmosphereHeightKm, etc.)
/// - Scattering coefficients in km^-1 (not m^-1)
/// - Planet center is placed at (0, -bottomRadiusKm, 0) in sky-space so that
///   world Z=0 corresponds to sea level (Z-up world)
///
/// # Sun Parameters
/// Sun direction, color, and intensity are driven by a `DirectionalLightComponent`
/// with `atmosphereSunLight=true`. The `sunIlluminance`, `sunAngularRadius`, and
/// `sunDiskIntensity` fields are **fallback defaults** used when no linked light
/// exists. Prefer setting up a DirectionalLight with `atmosphereSunLight=true`.
///
/// # Sky Light
/// IBL ambient intensity is controlled by `SkyLightComponent`, not by this
/// component. The legacy `skyLightIntensity`/`skyLightColor` fields are
/// deprecated and ignored when a SkyLightComponent exists.
///
/// # Default Values
/// Earth-like atmosphere. Standard SkyAtmosphereComponent defaults:
/// - BottomRadius = 6360 km, AtmosphereHeight = 60 km
/// - Rayleigh: (5.802, 13.558, 33.1) * 1e-3 km^-1, scale height 8 km
/// - Mie: 3.996e-3 km^-1, anisotropy 0.8, scale height 1.2 km
///
/// # Usage
/// ```cpp
/// auto sky = world.CreateEntity();
/// auto& atmo = world.AddComponent<SkyAtmosphereComponent>(sky);
/// // Earth-like defaults — just add a DirectionalLight with atmosphereSunLight=true.
/// ```

#include "vector3.h"

namespace uz {

/// Physical atmosphere parameters for the sky rendering pass.
///
/// All distance fields are in **kilometers** to match standard sky atmosphere
/// and avoid floating-point precision issues at planetary scale.
/// Scattering coefficients are in km^-1.
struct SkyAtmosphereComponent {
    // ── Planet geometry ────────────────────────────────────────────────────

    /// Planet radius in km from center to sea level.
    /// Default: 6360.0 km (Earth). World Z=0 maps to sea level.
    float bottomRadiusKm = 6360.0f;

    /// Atmosphere thickness in km above sea level.
    /// Default: 60.0 km. Top of atmosphere = bottomRadiusKm + atmosphereHeightKm.
    float atmosphereHeightKm = 60.0f;

    // ── Rayleigh scattering ────────────────────────────────────────────────

    /// Rayleigh scattering coefficients in km^-1 at sea level.
    /// Default: (5.802, 13.558, 33.1) * 1e-3 km^-1.
    /// Controls the blue color of the sky — higher blue coefficient = bluer sky.
    Vec3  rayleighScattering = {5.802e-3f, 13.558e-3f, 33.1e-3f};

    /// Artistic multiplier for Rayleigh scattering. Default: 1.0.
    /// Equivalent: RayleighScatteringScale. Scale > 1 = more vivid blue.
    float rayleighScatteringScale = 1.0f;

    /// Rayleigh scale height in km — density halves every this many km.
    /// Default: 8.0 km (RayleighExponentialDistribution).
    float rayleighScaleHeightKm = 8.0f;

    // ── Mie scattering ─────────────────────────────────────────────────────

    /// Mie scattering coefficient in km^-1 at sea level.
    /// Default: 3.996e-3 km^-1. Controls haze and sun glow intensity.
    float mieScattering = 3.996e-3f;

    /// Artistic multiplier for Mie scattering. Default: 1.0.
    /// Equivalent: MieScatteringScale. Scale > 1 = more haze/glow.
    float mieScatteringScale = 1.0f;

    /// Mie absorption coefficient in km^-1.
    /// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — Mie absorption term
    /// Controls how much light is absorbed (not scattered) by aerosols.
    float mieAbsorption = 4.44e-4f;

    /// Artistic multiplier for Mie absorption. Default: 1.0.
    /// Equivalent: MieAbsorptionScale.
    float mieAbsorptionScale = 1.0f;

    /// Mie phase function anisotropy (Henyey-Greenstein g parameter).
    /// Range [-1, 1]. Default: 0.8 (forward scattering).
    float mieAnisotropy = 0.8f;

    /// Mie scale height in km — aerosol density profile.
    /// Default: 1.2 km (MieDensityExpScale).
    float mieScaleHeightKm = 1.2f;

    // ── Ground ─────────────────────────────────────────────────────────────

    /// Ground albedo — affects sky brightness near horizon.
    /// Default ground albedo ~0.4 linear (medium-grey Lambertian surface).
    Vec3 groundAlbedo = {0.4f, 0.4f, 0.4f};

    // ── Ozone (Other Absorption) ────────────────────────────────────────────

    /// Ozone absorption coefficients in km^-1 at peak density.
    /// @ref Bruneton 2008 — ozone absorption cross-section at peak density
    /// Standard atmosphere default: (0.000650, 0.001881, 0.000085) km^-1.
    /// Controls the blue-purple tint at high altitude due to ozone layer.
    Vec3 ozoneAbsorption = {0.000650f, 0.001881f, 0.000085f};

    /// Center altitude of the ozone tent distribution in km.
    /// @ref Standard atmosphere ozone profile — tent distribution center
    /// Default: 25.0 km (ozone layer peak altitude).
    float ozoneTipAltitude = 25.0f;

    /// Peak value of the ozone tent distribution (dimensionless density).
    /// @ref Standard atmosphere ozone profile — tent distribution peak density
    /// Default: 1.0 (normalized peak).
    float ozoneTipValue = 1.0f;

    /// Half-width of the ozone tent distribution in km.
    /// @ref Standard atmosphere ozone profile — tent distribution half-width
    /// Default: 15.0 km. Ozone density is 0 at tipAltitude +/- width.
    float ozoneWidth = 15.0f;

    // ── Multi-scattering ───────────────────────────────────────────────────

    /// Multiple scattering approximation factor. 0 = single scatter only,
    /// 1 = full multi-scatter (reference default). Reduces darkening at zenith.
    /// Equivalent: MultiScatteringFactor.
    float multiScatteringFactor = 1.0f;

    // ── Art direction ──────────────────────────────────────────────────────

    /// Per-channel sky luminance multiplier for artistic control.
    /// Equivalent: SkyLuminanceFactor. Default: (1,1,1) = physically accurate.
    Vec3 skyLuminanceFactor = {1.0f, 1.0f, 1.0f};

    /// Contribution of the atmosphere to exponential height fog.
    /// 0 = no contribution, 1 = full (reference default).
    /// Equivalent: HeightFogContribution.
    float heightFogContribution = 1.0f;

    // ── Transform Mode ────────────────────────────────────────────────────

    /// How the planet center is positioned relative to the world.
    /// Equivalent: TransformMode.
    enum class TransformMode : uint8_t {
        PlanetTopAtComponentTransform = 0,    ///< Planet center at (0, 0, -bottomRadiusKm * 1000). World origin = sea level.
        PlanetCenterAtComponentTransform = 1, ///< Planet center at component world position. Useful for space views.
    };
    TransformMode transformMode = TransformMode::PlanetTopAtComponentTransform;

    // ── Sun (fallback — prefer DirectionalLight with atmosphereSunLight) ──

    /// Fallback sun illuminance when no DirectionalLight has atmosphereSunLight=true.
    /// When a linked light exists, this field is ignored.
    Vec3  sunIlluminance = {20.f, 20.f, 20.f};

    /// Fallback sun angular radius in radians (~0.5 degrees for Earth's sun).
    /// When a linked DirectionalLight exists, lightSourceAngle is used instead.
    float sunAngularRadius = 0.00872665f;

    /// Fallback sun disk intensity multiplier.
    /// When a linked DirectionalLight exists, its intensity is used instead.
    float sunDiskIntensity = 1.0f;

    // ── Deprecated (use SkyLightComponent instead) ────────────────────────

    /// @deprecated Use SkyLightComponent::intensity instead.
    /// Kept for backward compatibility — ignored when a SkyLightComponent exists.
    float skyLightIntensity = 1.0f;

    /// @deprecated Use SkyLightComponent::lightColor instead.
    Vec3 skyLightColor = {1.0f, 1.0f, 1.0f};

    // ── Control ────────────────────────────────────────────────────────────

    /// Whether the sky atmosphere pass is enabled.
    bool enabled = true;
};

} // namespace uz
