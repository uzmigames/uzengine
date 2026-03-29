#pragma once

/// UzEngine SkyLightComponent — ECS data for image-based ambient lighting.
///
/// Equivalent to standard sky light component. Controls the intensity and tint
/// of the IBL (Image-Based Lighting) ambient contribution derived from the
/// sky cubemap. Without this component, IBL ambient defaults to intensity=1.
///
/// Only the first entity with this component is used (singleton semantics
/// via first-found gathering in SceneRenderer).
///
/// # Relationship to Other Components
/// - **SkyAtmosphereComponent**: Defines the physical sky model (Rayleigh/Mie
///   scattering). The sky cubemap captured from the atmosphere is the SOURCE
///   of the IBL data that SkyLightComponent controls.
/// - **DirectionalLightComponent**: When `atmosphereSunLight=true`, the
///   directional light drives the sun direction/color in the sky model.
/// - **SkyLightComponent**: Controls HOW MUCH of the IBL ambient reaches
///   the scene — intensity, tint, lower hemisphere handling, and AO.
///
/// # Usage
/// ```cpp
/// auto entity = world.CreateEntity();
/// auto& skyLight = world.AddComponent<SkyLightComponent>(entity);
/// skyLight.intensity = 2.0f;  // Boost ambient fill in shadowed areas
/// skyLight.lightColor = {0.9f, 0.95f, 1.0f};  // Cool sky tint
/// ```

#include "vector3.h"

#include <cstdint>

namespace uz {

/// Source type for the sky light cubemap.
///
/// Determines where the IBL cubemap data comes from.
enum class ESkyLightSourceType : uint8_t {
    CapturedScene = 0,    ///< Capture the scene's sky (SkyAtmosphere + clouds) into a cubemap at runtime. Default and most common mode for outdoor scenes.
    SpecifiedCubemap = 1, ///< Use a user-specified cubemap texture (e.g., HDR environment map). Useful for indoor scenes or artistic control.
};

/// Image-based ambient lighting parameters.
///
/// Controls how the IBL (irradiance + prefiltered specular) cubemap
/// contributes to scene lighting. The IBL data is captured from the
/// sky atmosphere or loaded from a specified cubemap.
///
/// # Parity
/// Maps to USkyLightComponent parameters. Key differences:
/// - UzEngine uses centimeters (standard: 1 unit = 1 cm).
/// - SH coefficients are computed internally, not exposed.
struct SkyLightComponent {
    // ── Intensity & Color ─────────────────────────────────────────────────

    /// Overall sky light intensity multiplier.
    /// Scales both diffuse and specular IBL contributions.
    /// Default: 1.0. Typical outdoor range: 1.0 to 5.0.
    float intensity = 1.0f;

    /// Sky light tint color (linear).
    /// Multiplied with the IBL result. (1,1,1) = neutral.
    /// Use cool tints (0.9, 0.95, 1.0) for blue-ish ambient fill.
    Vec3 lightColor = {1.0f, 1.0f, 1.0f};

    /// Indirect lighting intensity — scales only the diffuse IBL contribution.
    /// Equivalent: IndirectLightingIntensity. Separate from `intensity` to allow
    /// independent control of fill light vs specular reflections.
    float indirectLightingIntensity = 1.0f;

    // ── Source ─────────────────────────────────────────────────────────────

    /// Where the IBL cubemap data comes from.
    ESkyLightSourceType sourceType = ESkyLightSourceType::CapturedScene;

    /// Whether to recapture the sky cubemap every frame (true) or only once (false).
    /// Equivalent: RealTimeCapture. Enable for dynamic time-of-day.
    bool realTimeCapture = false;

    /// Capture resolution per cubemap face (power of 2).
    /// Default: 128. Higher = sharper reflections but more GPU cost.
    uint32_t cubemapResolution = 128;

    // ── Lower Hemisphere ──────────────────────────────────────────────────

    /// When true, the lower hemisphere (below horizon) is replaced with a
    /// solid color. Physically accurate for outdoor scenes where the ground
    /// blocks sky light from below. Default: true.
    bool lowerHemisphereIsBlack = true;

    /// Solid color for the lower hemisphere when `lowerHemisphereIsBlack` is true.
    /// Default: (0, 0, 0). Use dark green for grass-lit interiors, etc.
    Vec3 lowerHemisphereColor = {0.0f, 0.0f, 0.0f};

    // ── Occlusion ─────────────────────────────────────────────────────────

    /// Maximum influence distance for AO/DFAO occlusion (cm, standard).
    /// Objects beyond this distance from occluders receive full sky light.
    /// Default: 1000 cm.
    float occlusionMaxDistance = 1000.0f;

    /// Minimum occlusion value (0-1). Prevents fully black shadows in areas
    /// where the SkyLight SH cannot fully reach.
    /// Default: 0.0 (fully occluded areas receive no skylight).
    float minOcclusion = 0.0f;

    /// Tint color applied to occluded areas.
    /// (0,0,0) = pure black in AO. Use (0.05, 0.05, 0.1) for colored fill.
    Vec3 occlusionTint = {0.0f, 0.0f, 0.0f};

    /// Exponent for the occlusion falloff curve. Higher = sharper AO.
    /// Equivalent: OcclusionExponent. Default: 1.0.
    float occlusionExponent = 1.0f;

    // ── Cloud Ambient Occlusion ────────────────────────────────────────────

    /// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
    /// Enable cloud-based ambient occlusion on the sky light. Default: true.
    bool cloudAmbientOcclusion = true;

    /// @ref Hillaire 2020 — cloud AO strength
    /// Strength of the cloud ambient occlusion effect [0,1]. Default: 0.5.
    float cloudAmbientOcclusionStrength = 0.5f;

    /// @ref Hillaire 2020 — cloud AO extent
    /// World-space extent of the cloud AO map in kilometers. Default: 64.0 km.
    float cloudAmbientOcclusionExtent = 64.0f;

    /// @ref Hillaire 2020 — cloud AO map resolution scale
    /// Multiplier for the cloud AO map resolution. Default: 1.0 (full resolution).
    float cloudAmbientOcclusionMapResolutionScale = 1.0f;

    /// @ref Hillaire 2020 — cloud AO aperture scale
    /// Controls the cone aperture for cloud AO sampling. Default: 0.0.
    float cloudAmbientOcclusionApertureScale = 0.0f;

    // ── Distance & Capture ──────────────────────────────────────────────────

    /// @ref IBL sky light — sky distance threshold
    /// Maximum distance at which geometry contributes to the sky light cubemap capture
    /// in centimeters. Geometry beyond this distance is treated as sky. Default: 150000.0 cm.
    float skyDistanceThreshold = 150000.0f;

    /// @ref IBL sky light — occlusion contrast
    /// Contrast adjustment for sky light occlusion. Higher = sharper AO transitions. Default: 0.2.
    float occlusionContrast = 0.2f;

    /// @ref IBL sky light — capture emissive only
    /// When true, the sky cubemap capture only includes emissive surfaces (no direct lighting).
    /// Useful for specific artistic effects. Default: false.
    bool captureEmissiveOnly = false;

    /// @ref IBL sky light — source cubemap rotation angle
    /// Rotation angle for the specified cubemap source in degrees. Only used when
    /// sourceType is SpecifiedCubemap. Default: 0.0 degrees.
    float sourceCubemapAngle = 0.0f;

    // ── Control ───────────────────────────────────────────────────────────

    /// Whether this sky light is enabled.
    bool enabled = true;
};

} // namespace uz
