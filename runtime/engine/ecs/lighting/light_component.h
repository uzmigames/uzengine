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
class LightComponent : public PrimitiveComponent {
public:
    /// Linear-space RGB color of the emitted light. Default: white (1, 1, 1).
    /// Values outside [0, 1] are physically valid for HDR lights.
    Vec3 color{1.f, 1.f, 1.f};

    /// Intensity multiplier applied to the light color. Default: 1.0.
    /// Units are lux for directional lights, candela for point/spot lights.
    float intensity = 1.0f;

    /// When true, `temperature` is used to tint the light color via blackbody radiation.
    /// UE5: bUseTemperature. Default: false.
    bool useTemperature = false;

    /// Blackbody color temperature in Kelvin. Only used when `useTemperature` is true.
    /// 3000K = warm/candle, 6500K = daylight white (D65), 10000K = cool/blue.
    /// UE5 default: 6500.0.
    float temperature = 6500.f;

    /// Non-physical multiplier for specular highlights from this light. Default: 1.0.
    /// 0 = no specular, 1 = normal. UE5: SpecularScale.
    float specularScale = 1.0f;

    /// Non-physical multiplier for diffuse contribution from this light. Default: 1.0.
    /// Useful for artistic control. Not in UE5 but common in other engines.
    float diffuseScale = 1.0f;

    /// Multiplier for volumetric scattering contribution. Default: 1.0.
    /// UE5: VolumetricScatteringIntensity.
    float volumetricScatteringIntensity = 1.0f;

    /// Multiplier for indirect/GI contribution. Default: 1.0.
    /// UE5: IndirectLightingIntensity.
    float indirectLightingIntensity = 1.0f;

protected:
    /// Writes `color` and `intensity` into the proxy's `light` union.
    ///
    /// Must be called after the proxy exists (i.e., inside or after
    /// `OnProxyCreated()`). No-op if the handle is stale.
    ///
    /// # Parameters
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
    /// UE5: bAtmosphereSunLight. Default: false.
    bool atmosphereSunLight = false;

    /// Which atmosphere sun slot this light occupies (0=primary sun, 1=moon).
    /// UE5: AtmosphereSunLightIndex. Default: 0.
    int atmosphereSunLightIndex = 0;

    /// Angular diameter of the sun disk in degrees (visual size in the sky).
    /// UE5: LightSourceAngle. Default: 0.5357 deg (≈ Earth sun).
    float lightSourceAngle = 0.5357f;

    /// Soft edge angle of the sun disk in degrees.
    /// UE5: LightSourceSoftAngle. Default: 0.0.
    float lightSourceSoftAngle = 0.0f;

    /// Number of CSM cascades (0 = no shadows, 1-4 = cascade count).
    /// UE5: DynamicShadowCascades. Default: 4.
    int shadowCascades = 4;

    /// Cascade distribution exponent. 1.0 = linear split, >1 = more near camera.
    /// UE5: CascadeDistributionExponent. Default: 2.0.
    float cascadeDistributionExponent = 2.0f;

    /// Cascade transition fade fraction (0-1). Smooth blend between cascades.
    /// UE5: CascadeTransitionFraction. Default: 0.1.
    float cascadeTransitionFraction = 0.1f;

    /// Shadow distance fadeout fraction (0-1). Fade at the far extent of CSM.
    /// UE5: ShadowDistanceFadeoutFraction. Default: 0.1.
    float shadowDistanceFadeoutFraction = 0.1f;

    /// Maximum CSM shadow distance in world units. 0 = engine default.
    /// UE5: DynamicShadowDistanceMovableLight. Default: 0.
    float dynamicShadowDistance = 0.f;

    /// Screen-space contact shadow ray length (0 = disabled).
    /// UE5: ContactShadowLength. Default: 0.
    float contactShadowLength = 0.f;

    /// Tint color applied to the sun disk in the sky atmosphere.
    /// UE5: AtmosphereSunDiskColorScale. Default: (1,1,1).
    Vec3 atmosphereSunDiskColorScale = {1.f, 1.f, 1.f};

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
    /// # Parameters
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
    /// Effective attenuation radius in world units (meters).
    /// Light contribution becomes zero at or beyond this distance.
    /// UE5: AttenuationRadius. Default: 10.0 (1000 cm in UE5).
    float range = 10.0f;

    /// Use physically-correct inverse square falloff. Default: true.
    /// When false, uses `lightFalloffExponent` instead.
    /// UE5: bUseInverseSquaredFalloff. Default: true.
    bool useInverseSquaredFalloff = true;

    /// Custom falloff exponent when inverse squared is disabled.
    /// UE5: LightFalloffExponent. Default: 8.0.
    float lightFalloffExponent = 8.0f;

    /// Sphere radius for area light approximation (representative point technique).
    /// 0 = point light (no area). UE5: SourceRadius. Default: 0.
    float sourceRadius = 0.f;

    /// Soft edge sphere radius for smoother shadow penumbra.
    /// UE5: SoftSourceRadius. Default: 0.
    float softSourceRadius = 0.f;

    /// Tube/capsule length for elongated area light approximation.
    /// 0 = sphere only. UE5: SourceLength. Default: 0.
    float sourceLength = 0.f;

protected:
    /// Returns `RenderProxyType::PointLight`.
    [[nodiscard]] RenderProxyType GetProxyType() const override {
        return RenderProxyType::PointLight;
    }

    /// Writes color, intensity, and range into the newly created proxy.
    ///
    /// # Parameters
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
    /// UE5: InnerConeAngle. Default: 30.0 deg. Range: [0, outerAngleDeg].
    float innerAngleDeg = 30.0f;

    /// Outer cone half-angle in **degrees**. Zero intensity beyond this angle.
    /// UE5: OuterConeAngle. Default: 45.0 deg. Range: [innerAngleDeg, 90].
    float outerAngleDeg = 45.0f;

protected:
    /// Returns `RenderProxyType::SpotLight`.
    [[nodiscard]] RenderProxyType GetProxyType() const override {
        return RenderProxyType::SpotLight;
    }

    /// Writes color, intensity, range, and cone angles into the newly created proxy.
    ///
    /// # Parameters
    /// - `renderWorld`: The `RenderWorld` that owns the proxy.
    /// - `handle`: Freshly created proxy handle. Guaranteed valid.
    void OnProxyCreated(RenderWorld& renderWorld, RenderProxyHandle handle) override;
};

} // namespace uz
