/// LightComponent implementation — directional, point, and spot light proxies.
///
/// Light direction is derived from the component's local rotation via
/// `GetRotation() * Vec3::Forward()`. The engine uses a Z-up, X-forward
/// coordinate system, so `Vec3::Forward() == Vec3::UnitX()`.

#include "light_component.h"

#include "log.h"

UZ_DEFINE_LOG_CATEGORY(Light, Trace);

namespace uz {
using namespace DirectX;

// ─────────────────────────────────────────────────────────────────────────────
// LightComponent
// ─────────────────────────────────────────────────────────────────────────────

void LightComponent::SyncLightData(RenderWorld& renderWorld) {
    RenderProxy* proxy = renderWorld.GetProxy(m_proxyHandle);
    if (!proxy) {
        UZ_LOG(Light, Warning, "SyncLightData called with stale proxy handle (index={})",
               m_proxyHandle.index);
        return;
    }
    proxy->light.color     = color;
    proxy->light.intensity = intensity;
}

// ─────────────────────────────────────────────────────────────────────────────
// DirectionalLightComponent
// ─────────────────────────────────────────────────────────────────────────────

Vec3 DirectionalLightComponent::GetDirection() const {
    // Rotate the engine's forward axis (X) by the component's LOCAL rotation.
    // For a root component (no parent) this equals the world rotation.
    // For a hierarchical component the world orientation is embedded in the
    // world matrix, but we derive direction from local rotation to keep this
    // operation cheap and hierarchy-agnostic at the component level.
    // A proper world-space direction would require extracting rotation from
    // GetWorldMatrix(), which is deferred to Phase 2.
    const Vec3 dir = GetRotation() * Vec3::Forward();

    // Normalize to protect against floating-point drift in accumulated
    // rotations. The result should already be unit length for well-formed
    // quaternions, but we normalize defensively.
    const XMVECTOR xm  = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(&dir));
    const XMVECTOR norm = XMVector3Normalize(xm);
    Vec3 result;
    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&result), norm);
    return result;
}

void DirectionalLightComponent::OnProxyCreated(RenderWorld& renderWorld,
                                                RenderProxyHandle handle) {
    RenderProxy* proxy = renderWorld.GetProxy(handle);
    if (!proxy) {
        return;
    }
    proxy->light.color     = color;
    proxy->light.intensity = intensity;
    proxy->light.direction = GetDirection();
    // range, innerAngle, outerAngle are unused for directional lights.
    proxy->light.range      = 0.f;
    proxy->light.innerAngle = 0.f;
    proxy->light.outerAngle = 0.f;

    UZ_LOG(Light, Trace, "DirectionalLight proxy created: direction=({}, {}, {})",
           proxy->light.direction.x, proxy->light.direction.y, proxy->light.direction.z);
}

// ─────────────────────────────────────────────────────────────────────────────
// PointLightComponent
// ─────────────────────────────────────────────────────────────────────────────

void PointLightComponent::OnProxyCreated(RenderWorld& renderWorld,
                                          RenderProxyHandle handle) {
    RenderProxy* proxy = renderWorld.GetProxy(handle);
    if (!proxy) {
        return;
    }
    proxy->light.color      = color;
    proxy->light.intensity  = intensity;
    proxy->light.range      = range;
    // direction, innerAngle, outerAngle unused for point lights.
    proxy->light.direction  = Vec3{0.f, 0.f, 0.f};
    proxy->light.innerAngle = 0.f;
    proxy->light.outerAngle = 0.f;

    UZ_LOG(Light, Trace, "PointLight proxy created: range={}", range);
}

// ─────────────────────────────────────────────────────────────────────────────
// SpotLightComponent
// ─────────────────────────────────────────────────────────────────────────────

void SpotLightComponent::OnProxyCreated(RenderWorld& renderWorld,
                                         RenderProxyHandle handle) {
    RenderProxy* proxy = renderWorld.GetProxy(handle);
    if (!proxy) {
        return;
    }
    static constexpr float kDegToRad = 3.14159265358979323846f / 180.f;

    proxy->light.color      = color;
    proxy->light.intensity  = intensity;
    proxy->light.range      = range;
    proxy->light.innerAngle = innerAngleDeg * kDegToRad;
    proxy->light.outerAngle = outerAngleDeg * kDegToRad;
    // Spot direction follows the same convention as directional lights.
    proxy->light.direction  = GetRotation() * Vec3::Forward();

    UZ_LOG(Light, Trace, "SpotLight proxy created: range={}, inner={}deg, outer={}deg",
           range, innerAngleDeg, outerAngleDeg);
}

} // namespace uz
