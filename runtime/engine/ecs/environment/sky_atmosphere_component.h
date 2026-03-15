/// UzEngine SkyAtmosphereComponent — ECS data for physical atmosphere rendering.
///
/// Follows UE5 SkyAtmosphereComponent parameter conventions:
/// - All distances in KILOMETERS (UE5: BottomRadiusKm, AtmosphereHeightKm, etc.)
/// - Scattering coefficients in km^-1 (not m^-1)
/// - Planet center is placed at (0, -bottomRadiusKm, 0) in sky-space so that
///   world Y=0 corresponds to sea level
///
/// The renderer (SceneRenderer) converts world-space camera position (meters)
/// to sky-space (km) and applies the snap-to-surface safety margin (5m above
/// planet surface), following UE5's ComputeViewData() pattern.
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
/// // Defaults produce Earth-like blue sky with orange sunset.
/// atmo.sunIlluminance = {20.f, 20.f, 20.f};
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
    /// UE5 default: 6360.0 km (Earth). World Y=0 maps to sea level.
    float bottomRadiusKm = 6360.0f;

    /// Atmosphere thickness in km above sea level.
    /// UE5 default: 60.0 km. Top of atmosphere = bottomRadiusKm + atmosphereHeightKm.
    float atmosphereHeightKm = 60.0f;

    // ── Rayleigh scattering ────────────────────────────────────────────────

    /// Rayleigh scattering coefficients in km^-1 at sea level.
    /// UE5 default: (5.802, 13.558, 33.1) * 1e-3 km^-1.
    /// Controls the blue color of the sky — higher blue coefficient = bluer sky.
    Vec3  rayleighScattering = {5.802e-3f, 13.558e-3f, 33.1e-3f};

    /// Rayleigh scale height in km — density halves every this many km.
    /// UE5 default: 8.0 km (RayleighExponentialDistribution).
    float rayleighScaleHeightKm = 8.0f;

    // ── Mie scattering ─────────────────────────────────────────────────────

    /// Mie scattering coefficient in km^-1 at sea level.
    /// UE5 default: 3.996e-3 km^-1. Controls haze and sun glow intensity.
    float mieScattering = 3.996e-3f;

    /// Mie phase function anisotropy (Henyey-Greenstein g parameter).
    /// Range [-1, 1]. UE5 default: 0.8 (forward scattering).
    float mieAnisotropy = 0.8f;

    /// Mie scale height in km — aerosol density profile.
    /// UE5 default: 1.2 km (MieDensityExpScale).
    float mieScaleHeightKm = 1.2f;

    // ── Ground ─────────────────────────────────────────────────────────────

    /// Ground albedo — affects sky brightness near horizon.
    /// UE5 default: (0.4, 0.4, 0.4).
    Vec3 groundAlbedo = {0.4f, 0.4f, 0.4f};

    // ── Sun ────────────────────────────────────────────────────────────────

    /// Sun illuminance (radiance at top of atmosphere).
    /// UE5: driven by AtmosphericLight intensity. Typical: (20, 20, 20).
    Vec3  sunIlluminance = {20.f, 20.f, 20.f};

    /// Sun angular radius in radians.
    /// Earth: ~0.00872665 rad (~0.5 degrees).
    float sunAngularRadius = 0.00872665f;

    /// Intensity multiplier for the sun disk rendering.
    float sunDiskIntensity = 1.0f;

    /// Multiplier for IBL ambient lighting derived from the sky cubemap.
    /// Default 1.0 = physically accurate. Increase for brighter ambient.
    float skyLightIntensity = 1.0f;

    // ── Control ────────────────────────────────────────────────────────────

    /// Whether the sky atmosphere pass is enabled.
    bool enabled = true;
};

} // namespace uz
