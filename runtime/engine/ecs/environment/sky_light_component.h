/// UzEngine SkyLightComponent — ECS data for image-based ambient lighting.
///
/// Equivalent to UE5's USkyLightComponent. Controls the intensity and tint
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

#pragma once

#include "vector3.h"

#include <cstdint>

namespace uz {

/// Source type for the sky light cubemap.
///
/// Determines where the IBL cubemap data comes from.
enum class ESkyLightSourceType : uint8_t {
    /// Capture the scene's sky (SkyAtmosphere + clouds) into a cubemap at runtime.
    /// This is the default and most common mode for outdoor scenes.
    CapturedScene = 0,

    /// Use a user-specified cubemap texture (e.g., HDR environment map).
    /// Useful for indoor scenes or when artistic control over ambient is needed.
    SpecifiedCubemap = 1,
};

/// Image-based ambient lighting parameters.
///
/// Controls how the IBL (irradiance + prefiltered specular) cubemap
/// contributes to scene lighting. The IBL data is captured from the
/// sky atmosphere or loaded from a specified cubemap.
///
/// # UE5 Parity
/// Maps to USkyLightComponent parameters. Key differences:
/// - UzEngine uses centimeters (UE5 parity: 1 unit = 1 cm).
/// - SH coefficients are computed internally, not exposed.
struct SkyLightComponent {
    // ── Intensity & Color ─────────────────────────────────────────────────

    /// Overall sky light intensity multiplier.
    /// Scales both diffuse and specular IBL contributions.
    /// UE5 default: 1.0. Typical outdoor range: 1.0 to 5.0.
    float intensity = 1.0f;

    /// Sky light tint color (linear).
    /// Multiplied with the IBL result. (1,1,1) = neutral.
    /// Use cool tints (0.9, 0.95, 1.0) for blue-ish ambient fill.
    Vec3 lightColor = {1.0f, 1.0f, 1.0f};

    /// Indirect lighting intensity — scales only the diffuse IBL contribution.
    /// UE5: IndirectLightingIntensity. Separate from `intensity` to allow
    /// independent control of fill light vs specular reflections.
    float indirectLightingIntensity = 1.0f;

    // ── Source ─────────────────────────────────────────────────────────────

    /// Where the IBL cubemap data comes from.
    ESkyLightSourceType sourceType = ESkyLightSourceType::CapturedScene;

    /// Whether to recapture the sky cubemap every frame (true) or only once (false).
    /// UE5: RealTimeCapture. Enable for dynamic time-of-day.
    bool realTimeCapture = false;

    /// Capture resolution per cubemap face (power of 2).
    /// UE5 default: 128. Higher = sharper reflections but more GPU cost.
    uint32_t cubemapResolution = 128;

    // ── Lower Hemisphere ──────────────────────────────────────────────────

    /// When true, the lower hemisphere (below horizon) is replaced with a
    /// solid color. Physically accurate for outdoor scenes where the ground
    /// blocks sky light from below. UE5 default: true.
    bool lowerHemisphereIsBlack = true;

    /// Solid color for the lower hemisphere when `lowerHemisphereIsBlack` is true.
    /// UE5 default: (0, 0, 0). Use dark green for grass-lit interiors, etc.
    Vec3 lowerHemisphereColor = {0.0f, 0.0f, 0.0f};

    // ── Occlusion ─────────────────────────────────────────────────────────

    /// Maximum influence distance for AO/DFAO occlusion (cm, UE5 parity).
    /// Objects beyond this distance from occluders receive full sky light.
    /// UE5 default: 1000 cm.
    float occlusionMaxDistance = 1000.0f;

    /// Minimum occlusion value (0-1). Prevents fully black shadows in areas
    /// where the SkyLight SH cannot fully reach. UE5 default: 0, but UE5's
    /// SkyLight captures the full sky hemisphere including clouds which provides
    /// strong indirect fill. Our procedural sky SH may be weaker, so default
    /// to 0.1 to match visual parity with UE5's default scene appearance.
    /// @source UE5 SkyLightComponent.h — MinOcclusion
    float minOcclusion = 0.1f;

    /// Tint color applied to occluded areas.
    /// (0,0,0) = pure black in AO. Use (0.05, 0.05, 0.1) for colored fill.
    Vec3 occlusionTint = {0.0f, 0.0f, 0.0f};

    /// Exponent for the occlusion falloff curve. Higher = sharper AO.
    /// UE5: OcclusionExponent. Default: 1.0.
    float occlusionExponent = 1.0f;

    // ── Control ───────────────────────────────────────────────────────────

    /// Whether this sky light is enabled.
    bool enabled = true;
};

} // namespace uz
