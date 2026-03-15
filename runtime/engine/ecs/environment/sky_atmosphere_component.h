/// UzEngine SkyAtmosphereComponent — ECS data for physical atmosphere rendering.
///
/// Follows UE5 SkyAtmosphereComponent parameter conventions:
/// - All distances in KILOMETERS (UE5: BottomRadiusKm, AtmosphereHeightKm, etc.)
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
/// Earth-like atmosphere. Matches UE5 SkyAtmosphereComponent defaults:
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

#pragma once

#include "vector3.h"

namespace uz {

/// Physical atmosphere parameters for the sky rendering pass.
///
/// All distance fields are in **kilometers** to match UE5 SkyAtmosphereComponent
/// and avoid floating-point precision issues at planetary scale.
/// Scattering coefficients are in km^-1.
struct SkyAtmosphereComponent {
    // ── Planet geometry ────────────────────────────────────────────────────

    /// Planet radius in km from center to sea level.
    /// UE5 default: 6360.0 km (Earth). World Z=0 maps to sea level.
    float bottomRadiusKm = 6360.0f;

    /// Atmosphere thickness in km above sea level.
    /// UE5 default: 60.0 km. Top of atmosphere = bottomRadiusKm + atmosphereHeightKm.
    float atmosphereHeightKm = 60.0f;

    // ── Rayleigh scattering ────────────────────────────────────────────────

    /// Rayleigh scattering coefficients in km^-1 at sea level.
    /// UE5 default: (5.802, 13.558, 33.1) * 1e-3 km^-1.
    /// Controls the blue color of the sky — higher blue coefficient = bluer sky.
    Vec3  rayleighScattering = {5.802e-3f, 13.558e-3f, 33.1e-3f};

    /// Artistic multiplier for Rayleigh scattering. Default: 1.0.
    /// UE5: RayleighScatteringScale. Scale > 1 = more vivid blue.
    float rayleighScatteringScale = 1.0f;

    /// Rayleigh scale height in km — density halves every this many km.
    /// UE5 default: 8.0 km (RayleighExponentialDistribution).
    float rayleighScaleHeightKm = 8.0f;

    // ── Mie scattering ─────────────────────────────────────────────────────

    /// Mie scattering coefficient in km^-1 at sea level.
    /// UE5 default: 3.996e-3 km^-1. Controls haze and sun glow intensity.
    float mieScattering = 3.996e-3f;

    /// Artistic multiplier for Mie scattering. Default: 1.0.
    /// UE5: MieScatteringScale. Scale > 1 = more haze/glow.
    float mieScatteringScale = 1.0f;

    /// Mie absorption coefficient in km^-1. UE5 default: 4.4e-3.
    /// Controls how much light is absorbed (not scattered) by aerosols.
    float mieAbsorption = 4.4e-3f;

    /// Artistic multiplier for Mie absorption. Default: 1.0.
    /// UE5: MieAbsorptionScale.
    float mieAbsorptionScale = 1.0f;

    /// Mie phase function anisotropy (Henyey-Greenstein g parameter).
    /// Range [-1, 1]. UE5 default: 0.8 (forward scattering).
    float mieAnisotropy = 0.8f;

    /// Mie scale height in km — aerosol density profile.
    /// UE5 default: 1.2 km (MieDensityExpScale).
    float mieScaleHeightKm = 1.2f;

    // ── Ground ─────────────────────────────────────────────────────────────

    /// Ground albedo — affects sky brightness near horizon.
    /// UE5 default: (0.3, 0.3, 0.3) (sRGB ~0.3 linear).
    Vec3 groundAlbedo = {0.3f, 0.3f, 0.3f};

    // ── Multi-scattering ───────────────────────────────────────────────────

    /// Multiple scattering approximation factor. 0 = single scatter only,
    /// 1 = full multi-scatter (UE5 default). Reduces darkening at zenith.
    /// UE5: MultiScatteringFactor.
    float multiScatteringFactor = 1.0f;

    // ── Art direction ──────────────────────────────────────────────────────

    /// Per-channel sky luminance multiplier for artistic control.
    /// UE5: SkyLuminanceFactor. Default: (1,1,1) = physically accurate.
    Vec3 skyLuminanceFactor = {1.0f, 1.0f, 1.0f};

    /// Contribution of the atmosphere to exponential height fog.
    /// 0 = no contribution, 1 = full (UE5 default).
    /// UE5: HeightFogContribution.
    float heightFogContribution = 1.0f;

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
