/// PrimitiveComponent — base for all visible scene components.
///
/// Extends `SceneComponent` with a render proxy lifecycle tied to the
/// `RenderWorld`. Any component that contributes geometry, lights, or other
/// GPU-visible data to the scene should inherit from `PrimitiveComponent`.
///
/// # Proxy Lifecycle
/// The proxy is created lazily on the first call to `SyncToRenderWorld()`.
/// It is the caller's responsibility to call `SyncToRenderWorld()` each frame
/// (or each time state changes) and to destroy the proxy by calling
/// `DestroyProxy()` on the `RenderWorld` before this component is destroyed.
///
/// In MVP, proxy destruction is handled by `RenderWorld::Clear()` on scene
/// teardown. Per-component destroy is not yet wired into the ECS lifecycle.
///
/// # Thread Safety
/// All methods must be called from the main thread only.

#pragma once

#include "scene_component.h"
#include "render_world.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PrimitiveComponent
// ─────────────────────────────────────────────────────────────────────────────

/// Base class for all components that create a render proxy in the `RenderWorld`.
///
/// Subclasses declare their proxy type via `GetProxyType()` and populate
/// type-specific proxy data in `OnProxyCreated()`. The base class handles
/// proxy creation, transform sync, and visibility sync automatically.
///
/// # Examples
/// ```cpp
/// // Sync all primitives each frame:
/// for (auto& prim : scene.GetPrimitives()) {
///     prim.SyncToRenderWorld(renderWorld);
/// }
/// ```
class PrimitiveComponent : public SceneComponent {
public:
    /// Destructor. Does NOT destroy the render proxy — that is handled by
    /// `RenderWorld::Clear()` or an explicit `renderWorld.DestroyProxy()` call.
    ~PrimitiveComponent() override = default;

    // ── Render State ──────────────────────────────────────────────────────────

    /// Whether this component contributes to any render pass.
    ///
    /// When false, the proxy is hidden in the RenderWorld. Geometry still
    /// exists and can be made visible again without recreating the proxy.
    bool visible = true;

    /// Whether this component casts shadows into shadow maps.
    ///
    /// Ignored for light proxies (lights never cast shadows onto themselves).
    bool castShadows = true;

    /// Bitmask identifying which render layers this proxy belongs to.
    ///
    /// Used for layer-based render pass filtering (e.g., exclude UI from
    /// the main scene pass). Default layer 1 = "Default".
    uint32_t renderLayer = 1;

    /// Minimum draw distance in meters. Object is culled if camera is closer.
    /// UE5: MinDrawDistance. 0 = no minimum (always visible when close).
    float minDrawDistance = 0.f;

    /// Maximum draw distance in meters. Object is culled if camera is farther.
    /// UE5: LDMaxDrawDistance. 0 = unlimited (no distance culling).
    float maxDrawDistance = 0.f;

    /// Whether this component can receive decal projections.
    /// UE5: bReceivesDecals. Default: true.
    bool receivesDecals = true;

    /// Whether this component renders in the main opaque/translucent pass.
    /// UE5: bRenderInMainPass. Default: true.
    bool renderInMainPass = true;

    /// Whether this component renders in the depth-only prepass.
    /// UE5: bRenderInDepthPass. Default: true.
    bool renderInDepthPass = true;

    // ── Sync ──────────────────────────────────────────────────────────────────

    /// Synchronizes this component's state to its render proxy.
    ///
    /// On first call, creates the proxy via `RenderWorld::CreateProxy()` and
    /// invokes `OnProxyCreated()` for type-specific initialization. On
    /// subsequent calls, updates the world transform and visibility.
    ///
    /// Must be called from the main thread. Typically called once per frame
    /// by the scene sync system.
    ///
    /// # Parameters
    /// - `renderWorld`: The `RenderWorld` that owns the proxy pool.
    void SyncToRenderWorld(RenderWorld& renderWorld);

    // ── Proxy Access (for render system use only) ─────────────────────────────

    /// Returns the proxy handle for this component.
    ///
    /// # Returns
    /// A valid handle after the first `SyncToRenderWorld()` call.
    /// An invalid handle (default-constructed) before the first sync.
    /// The handle may become stale if the proxy was destroyed externally.
    [[nodiscard]] RenderProxyHandle GetProxyHandle() const noexcept { return m_proxyHandle; }

protected:
    /// Returns the `RenderProxyType` this component creates.
    ///
    /// Called once during proxy creation. Subclasses must implement this
    /// to declare the type of proxy they represent.
    ///
    /// # Returns
    /// The `RenderProxyType` enum value matching this component's role.
    [[nodiscard]] virtual RenderProxyType GetProxyType() const = 0;

    /// Called immediately after the proxy is created for the first time.
    ///
    /// Subclasses override this to write type-specific union fields into
    /// the proxy (e.g., `staticMesh.meshId`, `light.intensity`).
    ///
    /// # Parameters
    /// - `renderWorld`: The `RenderWorld` that owns the proxy.
    /// - `handle`: The freshly created proxy handle. Guaranteed valid.
    virtual void OnProxyCreated(RenderWorld& renderWorld, RenderProxyHandle handle) {
        // Default: no type-specific data to initialize.
        (void)renderWorld;
        (void)handle;
    }

    /// Generational handle to this component's proxy in the `RenderWorld`.
    ///
    /// Invalid (default-constructed) until `SyncToRenderWorld()` is first called.
    RenderProxyHandle m_proxyHandle;
};

} // namespace uz
