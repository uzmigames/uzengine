/// LightComponent — base class for all light types.
///
/// Extends `PrimitiveComponent` with common light properties (color, intensity)
/// and concrete subclasses for directional, point, and spot lights.
///
/// # Proxy Data Flow
/// Light state is pushed into the proxy via `OnProxyCreated()` at creation
/// time. To update color or intensity after creation, call `SyncLightData()`
/// explicitly (or rely on the scene sync system calling `SyncToRenderWorld()`
/// each frame, which re-calls `SyncLightData()` via the subclass).

#pragma once

#include "primitive_component.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// LightComponent — shared light state
// ─────────────────────────────────────────────────────────────────────────────

/// Base class for all light components.
///
/// Provides shared color and intensity properties, and a protected helper
/// `SyncLightData()` that writes these values into the render proxy's `light`
/// union. Subclasses call this inside `OnProxyCreated()` and any per-frame
/// update path.
///
/// # Inheritance
/// - `DirectionalLightComponent` — infinite directional light
/// - `PointLightComponent`       — omnidirectional point light
/// - `SpotLightComponent`        — cone-shaped spot light
/// - `RectLightComponent`        — rectangular area light (LTC)
class LightComponent : public PrimitiveComponent {
public:
    /// Linear-space RGB color of the emitted light. Default: white (1, 1, 1).
    /// Values outside [0, 1] are physically valid for HDR lights.
    Vec3 color{1.f, 1.f, 1.f};

    /// Intensity multiplier applied to the light color. Default: 10.0.
    /// Units are lux for directional lights, candela for point/spot lights.
    /// Default: 10 lux (approximates clear-sky midday sun after exposure).
    float intensity = 10.0f;

    /// When true, `temperature` is used to tint the light color via blackbody radiation.
    /// Equivalent: bUseTemperature. Default: false.
    bool useTemperature = false;

    /// Blackbody color temperature in Kelvin. Only used when `useTemperature` is true.
    /// 3000K = warm/candle, 6500K = daylight white (D65), 10000K = cool/blue.
    /// Default: 6500.0.
    float temperature = 6500.f;

    /// Non-physical multiplier for specular highlights from this light. Default: 1.0.
    /// 0 = no specular, 1 = normal. Equivalent: SpecularScale.
    float specularScale = 1.0f;

    /// Non-physical multiplier for diffuse contribution from this light. Default: 1.0.
    /// Useful for artistic control. Not in reference engines but common in other engines.
    float diffuseScale = 1.0f;

    /// Multiplier for volumetric scattering contribution. Default: 1.0.
    /// Equivalent: VolumetricScatteringIntensity.
    float volumetricScatteringIntensity = 1.0f;

    /// Multiplier for indirect/GI contribution. Default: 1.0.
    /// Equivalent: IndirectLightingIntensity.
    float indirectLightingIntensity = 1.0f;

    /// Per-light shadow depth bias multiplier. Default: 0.5.
    /// Applied on top of the global CSM depth bias: `finalBias = csmBias * shadowBias`.
    /// Equivalent: ShadowBias (range 0.0–10.0). Source: LightComponent.h:105.
    float shadowBias = 0.5f;

    /// Per-light shadow slope-scaled bias multiplier. Default: 0.5.
    /// Applied on top of the global CSM slope bias: `finalSlopeBias = csmSlopeBias * shadowSlopeBias`.
    /// Equivalent: ShadowSlopeBias (range 0.0–10.0). Source: LightComponent.h:115.
    float shadowSlopeBias = 0.5f;

    // ── Contact Shadows ─────────────────────────────────────────────────────

    /// @ref Ubisoft 2015 "Screen-Space Contact Shadows" — per-light contact shadow controls
    /// Whether the contact shadow ray length is in world-space units (true) or
    /// screen-space fraction (false). Default: false (screen-space).
    bool contactShadowLengthInWorldSpace = false;

    /// @ref Ubisoft 2015 "Screen-Space Contact Shadows" — casting intensity
    /// Shadow intensity for surfaces that cast contact shadows. Default: 1.0.
    float contactShadowCastingIntensity = 1.0f;

    /// @ref Ubisoft 2015 "Screen-Space Contact Shadows" — non-casting intensity
    /// Shadow intensity for surfaces that do not cast contact shadows. Default: 0.0.
    float contactShadowNonCastingIntensity = 0.0f;

protected:
    /// Writes `color` and `intensity` into the proxy's `light` union.
    ///
    /// Must be called after the proxy exists (i.e., inside or after
    /// `OnProxyCreated()`). No-op if the handle is stale.
    ///
    /// # Arguments
    /// - `renderWorld`: The `RenderWorld` that owns the proxy.
    void SyncLightData(RenderWorld& renderWorld);
};

// ─────────────────────────────────────────────────────────────────────────────
// DirectionalLightComponent
// ─────────────────────────────────────────────────────────────────────────────

/// Directional light — uniform illumination from an infinite distance.
///
/// The light direction is derived from the component's world rotation.
/// The forward axis in the engine's coordinate system is `Vec3::Forward()`
/// (X-axis, Z-up world). Rotate this component to aim the light.
///
/// Directional lights have no position; only rotation affects shading.
///
/// # Examples
/// ```cpp
/// DirectionalLightComponent sun;
/// // Point the sun 45 degrees down from horizontal:
/// sun.SetRotation(Quat::FromAxisAngle(Vec3::Right(), XM_PIDIV4));
/// Vec3 dir = sun.GetDirection(); // world-space forward
/// ```
class DirectionalLightComponent : public LightComponent {
public:
    /// When true, this directional light drives the SkyAtmosphereComponent sun.
    /// Direction, color, and intensity are passed to the atmosphere rendering.
    /// Default: true — this directional light drives the atmosphere sun disk.
    bool atmosphereSunLight = true;

    /// Which atmosphere sun slot this light occupies (0=primary sun, 1=moon).
    /// Equivalent: AtmosphereSunLightIndex. Default: 0.
    int atmosphereSunLightIndex = 0;

    /// Angular diameter of the sun disk in degrees (visual size in the sky).
    /// Equivalent: LightSourceAngle. Default: 0.5357 deg (≈ Earth sun).
    /// Default 0.5357° = the sun's angular diameter (UE5 `LightSourceAngle`
    /// default). 1.2° was a soft-shadow artistic choice inherited from the
    /// pre-port Sponza calibration; scenes wanting it set `light_source_angle`.
    float lightSourceAngle = 0.5357f;

    /// Soft edge angle of the sun disk in degrees.
    /// Equivalent: LightSourceSoftAngle. Default: 0.0.
    float lightSourceSoftAngle = 0.0f;

    /// Number of CSM cascades (0 = no shadows, 1-4 = cascade count).
    /// Equivalent: DynamicShadowCascades. Default: 4.
    int shadowCascades = 4;

    /// Cascade distribution exponent. 1.0 = linear split, >1 = more near camera.
    /// Default: 3.0 — exponential cascade split distribution (PSSM convention).
    float cascadeDistributionExponent = 3.0f;

    /// Cascade transition fade fraction (0-1). Smooth blend between cascades.
    /// Equivalent: CascadeTransitionFraction. Default: 0.1.
    float cascadeTransitionFraction = 0.1f;

    /// Shadow distance fadeout fraction (0-1). Fade at the far extent of CSM.
    /// Equivalent: ShadowDistanceFadeoutFraction. Default: 0.1.
    float shadowDistanceFadeoutFraction = 0.1f;

    /// Maximum CSM shadow distance in world units (cm).
    /// Default: 40000 cm (400 m) — maximum CSM coverage distance.
    float dynamicShadowDistance = 40000.f;

    /// Screen-space contact shadow ray length (0 = disabled).
    /// Equivalent: ContactShadowLength. Default: 0.
    float contactShadowLength = 0.f;

    /// Number of far shadow cascades beyond the main CSM distance (0 = disabled).
    /// Far cascades cover [dynamicShadowDistance, farShadowDistance] with lower
    /// geometry detail (LOD bias). Equivalent: FarShadowCascadeCount. Default: 0.
    /// Default: 0 (far shadows disabled).
    uint32_t farShadowCascadeCount = 0;

    /// Maximum far shadow distance in centimeters. Only used when
    /// farShadowCascadeCount > 0. Equivalent: FarShadowDistance. Default: 300000 (3km).
    /// Default: 300000 cm (3 km).
    float farShadowDistance = 300000.0f;

    /// LOD bias applied to static meshes rendered in far shadow cascades.
    /// Higher values = coarser geometry = cheaper. Only active when
    /// farShadowCascadeCount > 0. Equivalent: GFarShadowStaticMeshLODBias. Default: 0.
    /// Default: 0 (no LOD bias for far shadow geometry).
    int32_t farShadowLodBias = 0;

    /// Tint color applied to the sun disk in the sky atmosphere.
    /// Equivalent: AtmosphereSunDiskColorScale. Default: (1,1,1).
    Vec3 atmosphereSunDiskColorScale = {1.f, 1.f, 1.f};

    // ── Cloud Shadows ───────────────────────────────────────────────────────

    /// @ref Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
    /// Whether this light casts shadows through volumetric clouds. Default: true.
    bool castCloudShadows = true;

    /// @ref Hillaire 2020 — cloud self-shadowing toggle
    /// Whether this light casts shadows onto the volumetric cloud layer itself. Default: true.
    bool castShadowsOnClouds = true;

    /// @ref Hillaire 2020 — atmospheric shadow toggle
    /// Whether cloud shadows darken the atmosphere (aerial perspective). Default: true.
    bool castShadowsOnAtmosphere = true;

    /// @ref Hillaire 2020 — cloud shadow overall strength
    /// Overall cloud shadow strength multiplier [0,1]. Default: 1.0.
    float cloudShadowStrength = 1.0f;

    /// @ref Hillaire 2020 — cloud shadow on atmosphere strength
    /// Strength of cloud shadows applied to atmosphere. Default: 1.0.
    float cloudShadowOnAtmosphereStrength = 1.0f;

    /// @ref Hillaire 2020 — cloud shadow on surface strength
    /// Strength of cloud shadows applied to opaque surfaces. Default: 1.0.
    float cloudShadowOnSurfaceStrength = 1.0f;

    /// @ref Hillaire 2020 — cloud shadow depth bias
    /// Depth bias for cloud shadow map sampling to reduce self-shadowing artifacts. Default: 0.0.
    float cloudShadowDepthBias = 0.0f;

    /// @ref Hillaire 2020 — cloud shadow map extent
    /// World-space extent of the cloud shadow map in kilometers. Default: 128.0 km.
    float cloudShadowExtent = 128.0f;

    /// @ref Hillaire 2020 — cloud shadow map resolution scale
    /// Multiplier for the cloud shadow map resolution. Default: 1.0 (full resolution).
    float cloudShadowMapResolutionScale = 1.0f;

    /// @ref Hillaire 2020 — cloud shadow ray sample count scale
    /// Multiplier for the number of ray-march samples when tracing cloud shadows. Default: 1.0.
    float cloudShadowRaySampleCountScale = 1.0f;

    /// @ref Hillaire 2020 — cloud scattered luminance scale
    /// Per-channel multiplier for the luminance scattered by clouds toward the light. Default: (1,1,1).
    Vec3 cloudScatteredLuminanceScale = {1.0f, 1.0f, 1.0f};

    // ── Light Shaft Occlusion ───────────────────────────────────────────────

    /// @ref Volumetric light shaft occlusion — standard screen-space light shaft technique
    /// Enable light shaft occlusion (screen-space radial blur from occluded sun). Default: true.
    bool enableLightShaftOcclusion = true;

    /// @ref Screen-space light shafts — occlusion mask darkness
    /// How dark the occlusion mask can get. 0 = fully transparent, 1 = fully opaque. Default: 0.5.
    float occlusionMaskDarkness = 0.5f;

    /// @ref Screen-space light shafts — occlusion depth range
    /// Maximum depth range for occlusion in centimeters. Default: 10000.0 cm.
    float occlusionDepthRange = 10000.0f;

    /// @ref Screen-space light shafts — override direction
    /// Manual light shaft direction override. (0,0,0) = use light direction. Default: (0,0,0).
    Vec3 lightShaftOverrideDirection = {0.0f, 0.0f, 0.0f};

    // ── Far Shadow & Miscellaneous ──────────────────────────────────────────

    /// @ref PCSS / CSM — source angle factor
    /// Multiplier for the light source angle used in shadow penumbra calculation. Default: 1.0.
    float shadowSourceAngleFactor = 1.0f;

    /// @ref PSSM — cascade bias distribution
    /// Controls how shadow bias is distributed across cascades. Default: 1.0.
    float shadowCascadeBiasDistribution = 1.0f;

    /// @ref Distance Field Shadows — maximum shadow distance
    /// Maximum distance for distance field shadows in centimeters. 0 = disabled. Default: 0.0 cm.
    float distanceFieldShadowDistance = 0.0f;

    /// @ref Distance Field Shadows — trace distance
    /// Maximum ray trace distance for distance field shadow tracing in centimeters. Default: 100000.0 cm.
    float traceDistance = 100000.0f;

    /// @ref Forward rendering — shadow priority
    /// Priority for this light in forward rendering shadow allocation. Higher = more likely to get shadows. Default: 0.
    int32_t forwardShadingPriority = 0;

    /// Returns the world-space unit direction vector of this light.
    ///
    /// Computed by rotating `Vec3::Forward()` by the component's world
    /// rotation quaternion. The result is normalized.
    ///
    /// # Returns
    /// Normalized world-space direction. Never returns the zero vector.
    [[nodiscard]] Vec3 GetDirection() const;

protected:
    /// Returns `RenderProxyType::DirectionalLight`.
    [[nodiscard]] RenderProxyType GetProxyType() const override {
        return RenderProxyType::DirectionalLight;
    }

    /// Writes direction, color, and intensity into the newly created proxy.
    ///
    /// # Arguments
    /// - `renderWorld`: The `RenderWorld` that owns the proxy.
    /// - `handle`: Freshly created proxy handle. Guaranteed valid.
    void OnProxyCreated(RenderWorld& renderWorld, RenderProxyHandle handle) override;
};

// ─────────────────────────────────────────────────────────────────────────────
// PointLightComponent
// ─────────────────────────────────────────────────────────────────────────────

/// Point light — emits uniformly in all directions from a world position.
///
/// Attenuation falls off with distance up to `range`. The position is
/// taken from the component's world transform.
class PointLightComponent : public LightComponent {
public:
    /// Effective attenuation radius in world units (centimeters, standard).
    /// Light contribution becomes zero at or beyond this distance.
    /// Equivalent: AttenuationRadius. Default: 1000 cm (= 10 m).
    float range = 1000.0f;

    /// Use physically-correct inverse square falloff. Default: true.
    /// When false, uses `lightFalloffExponent` instead.
    /// Equivalent: bUseInverseSquaredFalloff. Default: true.
    bool useInverseSquaredFalloff = true;

    /// Custom falloff exponent when inverse squared is disabled.
    /// Equivalent: LightFalloffExponent. Default: 8.0.
    float lightFalloffExponent = 8.0f;

    /// Sphere radius for area light approximation (representative point technique).
    /// 0 = point light (no area). Equivalent: SourceRadius. Default: 0.
    float sourceRadius = 0.f;

    /// Soft edge sphere radius for smoother shadow penumbra.
    /// Equivalent: SoftSourceRadius. Default: 0.
    float softSourceRadius = 0.f;

    /// Tube/capsule length for elongated area light approximation.
    /// 0 = sphere only. Equivalent: SourceLength. Default: 0.
    float sourceLength = 0.f;

    /// When true, this point light casts cubemap shadow maps (6 faces) into
    /// the local shadow atlas. Each face is a 90-degree FOV perspective
    /// projection from the light position.
    /// Default: false (shadows must be explicitly enabled per light).
    bool castShadows = false;

    /// Per-light shadow depth bias for this point light's shadow maps.
    /// Applied as a constant depth offset before shadow comparison.
    /// Default: 0.5. Range: [0.0, 10.0].
    float pointShadowBias = 0.5f;

protected:
    /// Returns `RenderProxyType::PointLight`.
    [[nodiscard]] RenderProxyType GetProxyType() const override {
        return RenderProxyType::PointLight;
    }

    /// Writes color, intensity, and range into the newly created proxy.
    ///
    /// # Arguments
    /// - `renderWorld`: The `RenderWorld` that owns the proxy.
    /// - `handle`: Freshly created proxy handle. Guaranteed valid.
    void OnProxyCreated(RenderWorld& renderWorld, RenderProxyHandle handle) override;
};

// ─────────────────────────────────────────────────────────────────────────────
// SpotLightComponent
// ─────────────────────────────────────────────────────────────────────────────

/// Spot light — cone-shaped emission from a world position along a direction.
///
/// The light direction follows the component's rotation (same convention as
/// `DirectionalLightComponent`). Intensity transitions from full inside
/// `innerAngle` to zero at `outerAngle`.
class SpotLightComponent : public PointLightComponent {
public:
    /// Inner cone half-angle in **degrees**. Full intensity inside this cone.
    /// Equivalent: InnerConeAngle. Default: 30.0 deg. Range: [0, outerAngleDeg].
    float innerAngleDeg = 30.0f;

    /// Outer cone half-angle in **degrees**. Zero intensity beyond this angle.
    /// Equivalent: OuterConeAngle. Default: 45.0 deg. Range: [innerAngleDeg, 90].
    float outerAngleDeg = 45.0f;

    /// When true, this spot light casts a shadow map into the local shadow atlas.
    /// Default: false (shadows must be explicitly enabled per light).
    bool castShadows = false;

    /// Per-light shadow depth bias for this spot light's shadow map.
    /// Applied as a constant depth offset before shadow comparison.
    /// Default: 0.5. Range: [0.0, 10.0].
    float spotShadowBias = 0.5f;

protected:
    /// Returns `RenderProxyType::SpotLight`.
    [[nodiscard]] RenderProxyType GetProxyType() const override {
        return RenderProxyType::SpotLight;
    }

    /// Writes color, intensity, range, and cone angles into the newly created proxy.
    ///
    /// # Arguments
    /// - `renderWorld`: The `RenderWorld` that owns the proxy.
    /// - `handle`: Freshly created proxy handle. Guaranteed valid.
    void OnProxyCreated(RenderWorld& renderWorld, RenderProxyHandle handle) override;
};

// ─────────────────────────────────────────────────────────────────────────────
// RectLightComponent
// ─────────────────────────────────────────────────────────────────────────────

/// Rectangular area light — a `width` x `height` emitting rectangle centred on the component's
/// world position, shaded analytically with Linearly Transformed Cosines (phase3a 3.33.0 stage 1).
///
/// Orientation follows the spot-light convention: the rectangle emits along the component's
/// forward axis (`rotation * Vec3::Forward()`); its width runs along `rotation * Vec3::Right()` and
/// its height along `rotation * Vec3::Up()`. `color * intensity` is the emitted RADIANCE of the
/// surface (an emissive quad with the same `emissiveIntensity` is the path-traced reference), so
/// the inverse-square falloff comes out of the integral; `range` only windows the light to zero.
/// Equivalent: UE5 RectLightComponent (SourceWidth / SourceHeight / AttenuationRadius).
/// Stage 1 casts no shadows (area-light shadows are a later stage).
/// @ref Heitz, Dupuy, Hill, Neubelt 2016 "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines"
class RectLightComponent : public LightComponent {
public:
    /// Full width of the rectangle along the component's right axis (cm). Default: 100.
    float width = 100.0f;

    /// Full height of the rectangle along the component's up axis (cm). Default: 100.
    float height = 100.0f;

    /// Attenuation radius (cm) measured from the rectangle's centre: the light fades to zero there
    /// with the point-light window. Default: 1000 cm (= 10 m).
    float range = 1000.0f;

    /// Emit from both faces. Default: false (one-sided, along the forward axis).
    bool twoSided = false;

protected:
    /// Returns `RenderProxyType::RectLight`.
    [[nodiscard]] RenderProxyType GetProxyType() const override {
        return RenderProxyType::RectLight;
    }

    /// Writes color, intensity, range and the emission direction into the newly created proxy.
    ///
    /// # Arguments
    /// - `renderWorld`: The `RenderWorld` that owns the proxy.
    /// - `handle`: Freshly created proxy handle. Guaranteed valid.
    void OnProxyCreated(RenderWorld& renderWorld, RenderProxyHandle handle) override;
};

} // namespace uz
