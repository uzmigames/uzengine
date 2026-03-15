/// PrimitiveComponent implementation — render proxy creation and sync.
///
/// Proxy creation is deferred to the first `SyncToRenderWorld()` call so that
/// `PrimitiveComponent` subclasses can be constructed and configured without
/// requiring an active `RenderWorld`. This matches the ECS pattern where
/// components are created before the world is fully initialized.

#include "primitive_component.h"

#include "log.h"

UZ_DEFINE_LOG_CATEGORY(Primitive, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PrimitiveComponent
// ─────────────────────────────────────────────────────────────────────────────

void PrimitiveComponent::SyncToRenderWorld(RenderWorld& renderWorld) {
    // Create proxy on first sync — deferred to avoid needing a RenderWorld
    // pointer at construction time.
    if (!m_proxyHandle.IsValid()) {
        RenderProxyDesc desc;
        desc.type        = GetProxyType();
        desc.worldMatrix = GetWorldMatrix();
        desc.visible     = visible;
        desc.castShadows = castShadows;
        desc.renderLayer = renderLayer;

        m_proxyHandle = renderWorld.CreateProxy(desc);
        UZ_LOG(Primitive, Trace, "Created render proxy: index={}", m_proxyHandle.index);

        OnProxyCreated(renderWorld, m_proxyHandle);
    }

    // Sync transform and visibility every frame — transforms may have changed
    // since the last tick without this component knowing (parent hierarchy).
    renderWorld.SetTransform(m_proxyHandle, GetWorldMatrix());
    renderWorld.SetVisible(m_proxyHandle, visible);

    // Sync castShadows and renderLayer directly into the proxy — RenderWorld
    // does not expose dedicated setters for these fields in MVP.
    RenderProxy* proxy = renderWorld.GetProxy(m_proxyHandle);
    if (proxy) {
        proxy->castShadows = castShadows;
        proxy->renderLayer = renderLayer;
    }
}

} // namespace uz
