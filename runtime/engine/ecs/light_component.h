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
    ///
    /// Values outside [0, 1] are physically valid for HDR lights.
    Vec3 color{1.f, 1.f, 1.f};

    /// Intensity multiplier applied to the light color. Default: 1.0.
    ///
    /// Units are lux for directional lights, candela for point/spot lights.
    float intensity = 1.0f;

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
    /// Effective attenuation radius in world units.
    ///
    /// Light contribution becomes zero at or beyond this distance.
    float range = 10.0f;

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
class SpotLightComponent : public LightComponent {
public:
    /// Effective attenuation radius in world units.
    float range = 10.0f;

    /// Inner cone half-angle in radians.
    ///
    /// Within this angle from the light axis the intensity is at full `intensity`.
    float innerAngle = 0.3f;

    /// Outer cone half-angle in radians.
    ///
    /// Beyond this angle from the light axis the intensity drops to zero.
    /// Must be >= `innerAngle`.
    float outerAngle = 0.5f;

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
