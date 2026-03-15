/// UzEngine SkyAtmosphereComponent — ECS data for physical atmosphere rendering.
///
/// Stores Rayleigh/Mie scattering parameters for a Hillaire 2020-style
/// single-scatter sky model. Attached to a scene entity to enable atmospheric
/// sky rendering. Only the first entity with this component is used (singleton
/// semantics via first-found gathering).
///
/// # Default Values
/// Earth-like atmosphere at sea level. Rayleigh and Mie coefficients match
/// the standard atmosphere model used by Bruneton, Hillaire, and most
/// real-time sky implementations.
///
/// # Usage
/// ```cpp
/// auto entity = world.CreateEntity();
/// world.AddComponent<SkyAtmosphereComponent>(entity);
/// // Defaults produce an Earth-like blue sky with orange sunset.
/// ```

#pragma once

#include "vector3.h"

namespace uz {

/// Physical atmosphere parameters for the sky rendering pass.
///
/// All distance units are in meters (matching the Bruneton convention).
/// The renderer converts to the appropriate coordinate space at render time.
struct SkyAtmosphereComponent {
    /// Rayleigh scattering coefficients (per meter) at sea level.
    /// Controls the blue/violet color of the sky. Higher values = more scattering.
    Vec3 rayleighScattering = {5.8e-6f, 13.5e-6f, 33.1e-6f};

    /// Rayleigh scale height in meters — density halves every this many meters.
    float rayleighScaleHeight = 8000.f;

    /// Mie scattering coefficient (per meter) at sea level.
    /// Controls haze and sun glow intensity.
    float mieScattering = 21e-6f;

    /// Mie phase function anisotropy factor (Henyey-Greenstein g parameter).
    /// Range [-1, 1]. Positive = forward scattering (sun glow). Typical: 0.76.
    float mieAnisotropy = 0.76f;

    /// Mie scale height in meters — aerosol density halves every this many meters.
    float mieScaleHeight = 1200.f;

    /// Ground albedo (reflectance) for multi-scatter approximation.
    /// Affects the brightness of the sky near the horizon.
    Vec3 groundAlbedo = {0.4f, 0.4f, 0.4f};

    /// Sun illuminance (radiance reaching the atmosphere top).
    /// Higher values = brighter sky and sun disk.
    Vec3 sunIlluminance = {20.f, 20.f, 20.f};

    /// Sun angular radius in radians. ~0.5 degrees for Earth's sun.
    float sunAngularRadius = 0.00872665f;

    /// Planet radius in meters (from center to sea level).
    float planetRadius = 6360000.f;

    /// Atmosphere thickness in meters (from sea level to atmosphere top).
    float atmosphereHeight = 100000.f;

    /// Intensity multiplier for the sun disk rendering.
    float sunDiskIntensity = 1.0f;

    /// Whether the sky atmosphere pass is enabled.
    bool enabled = true;
};

} // namespace uz
