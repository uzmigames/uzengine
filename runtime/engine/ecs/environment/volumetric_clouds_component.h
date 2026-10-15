/// UzEngine VolumetricCloudsComponent — ECS data for volumetric cloud rendering.
///
/// Stores all parameters for a volumetric cloud layer. Attached to a scene
/// entity to enable cloud rendering. Only the first entity with this component
/// is used (singleton semantics via first-found gathering).
///
/// @ref Schneider & Vos 2015 — "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
/// @ref Hillaire 2020 — "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
///
/// # Cloud Layer Model
/// Clouds occupy a spherical shell between two altitudes above sea level:
/// ```
/// innerRadius = planetRadius + layerBottomAltitudeKm
/// outerRadius = innerRadius  + layerHeightKm
/// ```
/// Density within the shell is sampled from 3D noise (Perlin-Worley shape +
/// Worley detail) modulated by a 2D weather map that controls per-texel
/// coverage, cloud type, and precipitation.
///
/// # Scattering Model
/// Single-scattering with multi-scatter energy approximation:
/// - Beer-Lambert extinction along view and light rays
/// - Dual-lobe Henyey-Greenstein phase function (phaseG, phaseG2, phaseBlend)
/// - Powder term for silver-lining at cloud edges
/// - Ambient contribution to prevent fully black cloud interiors
///
/// # Usage
/// ```cpp
/// auto entity = world.CreateEntity();
/// auto& clouds = world.AddComponent<VolumetricCloudsComponent>(entity);
/// clouds.coverage = 0.6f;  // partly cloudy
/// clouds.layerBottomAltitudeKm = 5.0f;
/// ```

#pragma once

#include "vector3.h"
#include "../core/dirty_flags.h"

#include <cstdint>

namespace uz {

/// Tracing max distance interpretation mode for volumetric clouds.
///
/// Controls whether the ray-march distance limit is measured from the cloud
/// layer entry point or from the camera position.
/// @ref Schneider & Vos 2015 "Real-Time Volumetric Cloudscapes of Horizon Zero Dawn"
enum class TracingMaxDistanceMode : uint8_t {
    /// Distance measured from the point where the view ray enters the cloud layer.
    DistanceFromCloudLayerEntryPoint = 0,
    /// Distance measured from the camera (point of view).
    DistanceFromPointOfView = 1,
};

/// Volumetric cloud layer configuration for the cloud ray-march pass.
///
/// All altitude and distance fields are in **kilometers** to match reference
/// VolumetricCloudComponent conventions and avoid floating-point precision
/// issues at planetary scale. Wind velocity is also in km/s.
struct VolumetricCloudsComponent : public CBDirtyMixin {
    // ── Cloud Layer Geometry ─────────────────────────────────────────────

    /// Bottom altitude of the cloud layer in km above sea level.
    /// Standard fair-weather cumulus base altitude. Default: 7.0 km.
    float layerBottomAltitudeKm = 7.0f;

    /// Height (thickness) of the cloud layer in km.
    /// Fair-weather cumulus vertical extent. Default: 6.0 km.
    float layerHeightKm = 6.0f;

    // ── Cloud Density ────────────────────────────────────────────────────

    /// Global cloud coverage [0,1]. 0 = clear sky, 1 = overcast.
    /// Multiplied into the weather map R channel during density sampling.
    float coverage = 0.35f;

    /// Cloud density multiplier. Higher values produce thicker, more opaque
    /// clouds. Scales the raw density after noise sampling.
    float density = 0.6f;

    /// Cloud type [0,1]. 0 = stratus (flat, layered), 1 = cumulus (puffy, towering).
    /// Controls the vertical density profile applied within the cloud shell.
    float cloudType = 0.5f;

    // ── Wind ─────────────────────────────────────────────────────────────

    /// Wind direction and speed in km/s (XY = horizontal direction, Z = unused).
    /// Applied as a UV offset to both the 3D shape noise and 2D weather map.
    Vec3 windVelocity = {0.005f, 0.002f, 0.0f};

    /// Wind speed multiplier for detail noise relative to shape noise.
    /// Creates the churning, turbulent appearance at cloud edges.
    float detailWindScale = 2.0f;

    // ── Absorption & Scattering ──────────────────────────────────────────

    /// Extinction coefficient for Beer-Lambert attenuation (km^-1).
    /// Higher values cause light to be absorbed more quickly inside clouds.
    /// @ref Beer-Lambert law — cloud extinction coefficient. Default: 0.01 km^-1.
    /// Lower values = brighter, more translucent clouds. Higher = darker, more opaque.
    float extinction = 0.01f;

    /// Scattering albedo [0,1]. Ratio of scattered light to total extinction.
    /// 1.0 = pure scattering (bright white clouds).
    /// 0.0 = pure absorption (dark, light-eating clouds).
    float scatteringAlbedo = 0.99f;

    /// Primary Henyey-Greenstein phase function eccentricity [-1,1].
    /// Positive = forward scattering (silver lining around sun).
    /// Negative = back scattering.
    float phaseG = 0.5f;

    /// Secondary phase function eccentricity for dual-lobe scattering.
    /// Typically negative to add subtle back-scatter illumination.
    float phaseG2 = -0.3f;

    /// Blend factor between primary and secondary phase functions [0,1].
    /// 0.0 = only phaseG, 1.0 = only phaseG2.
    float phaseBlend = 0.7f;

    // ── Multi-Scatter Approximation ──────────────────────────────────────

    /// Sky irradiance scale for the height-dependent isotropic sky source.
    /// The legacy serialized name is retained; multiple scattering is evaluated
    /// separately using optical depth toward the sun.
    float ambientMultiplier = 6.0f;

    /// Optional Beer-powder attenuation. Zero disables the effect.
    float powderScale = 0.0f;
    float powderExponent = 2.0f;

    /// Oz/Kulla scattering orders including direct light, clamped to [1,8].
    uint32_t multiScatterOctaves = 8;

    // ── Ray March Parameters ─────────────────────────────────────────────

    /// Maximum ray march distance in km from the camera.
    /// Limit ray-march to 50 km for performance (beyond this clouds fade out).
    float tracingMaxDistanceKm = 50.0f;

    /// Number of primary ray march steps through the cloud shell.
    /// More steps = higher quality but more expensive. 64 is a good balance.
    uint32_t primarySteps = 48;

    /// Adaptive march distances in km. The shader always caps the iteration count.
    float minStepSize = 0.01f;
    float maxStepSize = 0.5f;
    float perspectiveStepScale = 1.006f;
    float jitterScale = 1.0f;

    /// Number of light march steps toward the sun at each primary step.
    /// Used to compute in-scattering and shadow self-occlusion. 6 is typical.
    uint32_t lightSteps = 6;

    // Beer shadow map configuration. Distances use cloud-space kilometers.
    uint32_t shadowCascadeCount = 2;
    uint32_t shadowResolution = 512;
    float shadowDistance = 50.0f;

    // ── Tracing Quality ────────────────────────────────────────────────────

    /// @ref Schneider & Vos 2015 — view ray sample count scale
    /// Multiplier for the number of primary ray-march samples during main view rendering.
    /// Lower values improve performance at the cost of quality. Default: 0.5.
    float viewSampleCountScale = 0.5f;

    /// @ref Schneider & Vos 2015 — reflection view sample count scale
    /// Multiplier for sample count when tracing clouds in reflections. Default: 0.1.
    float reflectionViewSampleCountScale = 0.1f;

    /// @ref Schneider & Vos 2015 — shadow view sample count scale
    /// Multiplier for sample count when tracing cloud shadows. Default: 0.5.
    float shadowViewSampleCountScale = 0.5f;

    /// @ref Schneider & Vos 2015 — shadow reflection sample count scale
    /// Multiplier for sample count when tracing cloud shadow reflections. Default: 0.1.
    float shadowReflectionViewSampleCountScale = 0.1f;

    /// @ref Schneider & Vos 2015 — shadow tracing distance
    /// Maximum distance for shadow ray tracing in kilometers. Default: 10.0 km.
    float shadowTracingDistance = 10.0f;

    /// @ref Schneider & Vos 2015 — early termination transmittance threshold
    /// Stop tracing when accumulated transmittance drops below this value. Default: 0.01.
    float stopTracingTransmittanceThreshold = 0.01f;

    // ── Aerial Perspective Scattering ───────────────────────────────────────

    /// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
    /// Distance from camera where Rayleigh scattering on clouds begins in kilometers. Default: 0.0 km.
    float aerialPerspectiveRayleighScatteringStartDistance = 0.0f;

    /// @ref Hillaire 2020 — Rayleigh scattering fade distance
    /// Distance over which Rayleigh scattering on clouds fades in, in kilometers. Default: 10.0 km.
    float aerialPerspectiveRayleighScatteringFadeDistance = 10.0f;

    /// @ref Hillaire 2020 — Mie scattering start distance
    /// Distance from camera where Mie scattering on clouds begins in kilometers. Default: 0.0 km.
    float aerialPerspectiveMieScatteringStartDistance = 0.0f;

    /// @ref Hillaire 2020 — Mie scattering fade distance
    /// Distance over which Mie scattering on clouds fades in, in kilometers. Default: 10.0 km.
    float aerialPerspectiveMieScatteringFadeDistance = 10.0f;

    // ── Sky Light Occlusion ─────────────────────────────────────────────────

    /// @ref Schneider & Vos 2015 — sky light cloud bottom occlusion
    /// Occlusion factor applied to sky light at the bottom of the cloud layer [0,1].
    /// Simulates reduced ambient light under thick clouds. Default: 0.2.
    float skyLightCloudBottomOcclusion = 0.2f;

    // ── Tracing Distance Mode ───────────────────────────────────────────────

    /// @ref Schneider & Vos 2015 — tracing max distance mode
    /// How the maximum tracing distance is interpreted. Default: DistanceFromCloudLayerEntryPoint.
    TracingMaxDistanceMode tracingMaxDistanceMode = TracingMaxDistanceMode::DistanceFromCloudLayerEntryPoint;

    /// @ref Schneider & Vos 2015 — tracing start max distance
    /// Maximum distance from the tracing origin (per tracingMaxDistanceMode) in kilometers.
    /// Default: 350.0 km.
    float tracingStartMaxDistance = 350.0f;

    /// @ref Schneider & Vos 2015 — tracing start distance from camera
    /// Minimum distance from camera before cloud tracing begins in kilometers.
    /// Default: 0.0 km.
    float tracingStartDistanceFromCamera = 0.0f;

    // ── Control ──────────────────────────────────────────────────────────

    /// Whether volumetric clouds are enabled for rendering.
    bool enabled = true;
};

} // namespace uz
