/// UzEngine RenderWorld — GPU-optimized parallel representation of the game world.
///
/// Game code describes WHAT to render (components). RenderWorld decides HOW
/// (draw calls, sorting). The two worlds are decoupled by a single sync point.
///
/// # MVP Scope
/// Phase 1: single-thread, no culling, no LOD. Just proxy storage + draw call list.

#pragma once

#include "vector3.h"
#include "mat4.h"
#include "log.h"

#include <cstdint>
#include <vector>

namespace uz {

UZ_DECLARE_LOG_CATEGORY(RenderWorld);

// ─────────────────────────────────────────────────────────────────────────────
// RenderProxy Types
// ─────────────────────────────────────────────────────────────────────────────

/// Handle to a render proxy in the RenderWorld. Generational for safety.
///
/// A handle becomes stale when the proxy it refers to is destroyed and the slot
/// is reused. Stale handles are detected by comparing the stored generation
/// against the slot's current generation.
struct RenderProxyHandle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    /// Returns true if this handle has been initialized (non-sentinel index).
    ///
    /// A valid handle is NOT a guarantee the proxy is still alive — use
    /// `RenderWorld::GetProxy()` to perform a full generation check.
    [[nodiscard]] bool IsValid() const { return index != UINT32_MAX; }
};

/// Type of render proxy — determines draw call generation behavior.
enum class RenderProxyType : uint8_t {
    StaticMesh,       ///< Static mesh geometry (opaque or masked).
    DirectionalLight, ///< Infinite-distance directional light (sun/moon).
    PointLight,       ///< Omnidirectional point light with range falloff.
    SpotLight,        ///< Cone-shaped spot light with inner/outer angles.
};

/// POD render proxy — flat data for GPU submission, no virtual dispatch.
///
/// Stored in a dense pool inside RenderWorld. Type-specific data is accessed
/// via the union. Designed for linear iteration and cache-friendly processing.
///
/// # Union Safety
/// Always write through the union member that matches `type` before reading.
/// The default constructor zeroes the union to avoid undefined reads.
struct RenderProxy {
    RenderProxyHandle handle;
    RenderProxyType type = RenderProxyType::StaticMesh;

    // ── Transform ──

    /// World-space transform matrix for this proxy.
    Mat4 worldMatrix;

    // ── Visibility ──

    /// Whether this proxy contributes to any render passes.
    bool visible = true;

    /// Whether this proxy casts shadows into shadow maps.
    bool castShadows = true;

    /// Bitmask identifying which render layers this proxy belongs to.
    uint32_t renderLayer = 1;

    // ── Type-Specific Data ──

    union {
        struct {
            /// Opaque index into the engine mesh resource table.
            uint32_t meshId;
            /// Opaque index into the engine material resource table.
            uint32_t materialId;
        } staticMesh;

        struct {
            /// Luminous intensity in candela (cd).
            float intensity;
            /// Linear RGB color of the emitted light.
            Vec3 color;
            /// World-space unit direction (directional lights only).
            Vec3 direction;
            /// Effective radius in world units (point and spot lights).
            float range;
            /// Inner cone half-angle in radians (spot lights only).
            float innerAngle;
            /// Outer cone half-angle in radians (spot lights only).
            float outerAngle;
            /// Color temperature in Kelvin (only when useTemperature=true).
            float temperature;
            /// Whether to apply blackbody temperature tint.
            bool useTemperature;
            /// Specular highlight multiplier (0-1).
            float specularScale;
            /// Diffuse contribution multiplier (0-1).
            float diffuseScale;
            /// Sphere radius for area light approximation (point/spot lights).
            float sourceRadius;
            /// Custom falloff exponent (when useInverseSquaredFalloff=false).
            float falloffExponent;
            /// Use physically-correct inverse square law attenuation.
            bool useInverseSquaredFalloff;
        } light;
    };

    /// Constructs a zero-initialized proxy. The union is zeroed to prevent
    /// undefined reads before type-specific fields are written.
    RenderProxy() { memset(&staticMesh, 0, sizeof(staticMesh)); }
};

/// Descriptor used to create a new render proxy via `RenderWorld::CreateProxy`.
///
/// After creation, use `RenderWorld::GetProxy()` to populate type-specific
/// union fields (e.g., `staticMesh.meshId`, `light.intensity`).
struct RenderProxyDesc {
    RenderProxyType type = RenderProxyType::StaticMesh;
    Mat4 worldMatrix;
    bool visible = true;
    bool castShadows = true;
    uint32_t renderLayer = 1;
};

// ─────────────────────────────────────────────────────────────────────────────
// RenderWorld
// ─────────────────────────────────────────────────────────────────────────────

/// GPU-optimized world containing all active render proxies.
///
/// Proxies are stored in a generational slot pool. Destroyed slots are recycled
/// to avoid unbounded growth. Generational handles prevent use-after-free bugs
/// when a slot is reused for a new proxy.
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the main thread (MVP).
///
/// # Lifecycle
/// ```cpp
/// RenderWorld world;
/// auto handle = world.CreateProxy(desc);
/// world.SetTransform(handle, newMatrix);
/// // ... render loop: world.GetProxies() ...
/// world.DestroyProxy(handle);
/// ```
class RenderWorld {
public:
    RenderWorld() = default;
    ~RenderWorld() = default;

    /// Non-copyable — proxy pool is not cheaply copyable.
    RenderWorld(const RenderWorld&) = delete;
    RenderWorld& operator=(const RenderWorld&) = delete;

    /// Movable.
    RenderWorld(RenderWorld&&) = default;
    RenderWorld& operator=(RenderWorld&&) = default;

    // ── Proxy Lifecycle ──

    /// Creates a new render proxy and returns its generational handle.
    ///
    /// Reuses a freed slot when available; otherwise allocates a new slot.
    /// The returned handle is immediately valid.
    ///
    /// # Parameters
    /// - `desc`: Descriptor specifying type, transform, visibility, and render layer.
    ///
    /// # Returns
    /// A valid `RenderProxyHandle`. Never returns an invalid handle.
    [[nodiscard]] RenderProxyHandle CreateProxy(const RenderProxyDesc& desc);

    /// Destroys a proxy and releases its slot for reuse.
    ///
    /// The slot's generation is bumped, invalidating all existing handles to it.
    /// No-op if the handle is invalid or stale (already destroyed).
    ///
    /// # Parameters
    /// - `handle`: Handle returned by a prior `CreateProxy` call.
    void DestroyProxy(RenderProxyHandle handle);

    /// Returns a mutable pointer to the proxy for the given handle.
    ///
    /// Use this to populate type-specific union fields after creation, or to
    /// mutate proxy state. Performs a full generation check on every call.
    ///
    /// # Returns
    /// - Non-null pointer if `handle` is live.
    /// - `nullptr` if `handle` is invalid or stale.
    [[nodiscard]] RenderProxy* GetProxy(RenderProxyHandle handle);

    /// Returns a const pointer to the proxy for the given handle.
    ///
    /// # Returns
    /// - Non-null pointer if `handle` is live.
    /// - `nullptr` if `handle` is invalid or stale.
    [[nodiscard]] const RenderProxy* GetProxy(RenderProxyHandle handle) const;

    // ── Proxy Updates ──

    /// Updates the world-space transform matrix of a proxy.
    ///
    /// No-op and logs a warning if the handle is invalid or stale.
    ///
    /// # Parameters
    /// - `handle`: Handle to the proxy to update.
    /// - `worldMatrix`: New world-space transform.
    void SetTransform(RenderProxyHandle handle, const Mat4& worldMatrix);

    /// Sets the visibility of a proxy.
    ///
    /// Invisible proxies are excluded from all render passes.
    /// No-op and logs a warning if the handle is invalid or stale.
    ///
    /// # Parameters
    /// - `handle`: Handle to the proxy to update.
    /// - `visible`: True to make the proxy visible, false to hide it.
    void SetVisible(RenderProxyHandle handle, bool visible);

    // ── Queries ──

    /// Returns a const reference to the internal proxy slot array.
    ///
    /// Includes both live and freed slots — callers must check `proxy.handle.IsValid()`
    /// and that the handle's generation matches the slot. Prefer iterating only
    /// live proxies via the generation check.
    ///
    /// This is the hot path for the render loop. No copy is made.
    [[nodiscard]] const std::vector<RenderProxy>& GetProxies() const { return m_proxies; }

    /// Returns the number of currently live (non-destroyed) proxies.
    ///
    /// O(1) — tracked via a counter, not computed by iteration.
    [[nodiscard]] size_t ProxyCount() const;

    /// Destroys all proxies and resets the pool to empty state.
    ///
    /// All existing handles become invalid after this call.
    void Clear();

private:
    /// Slot array — may contain freed slots with stale generation.
    std::vector<RenderProxy> m_proxies;

    /// Per-slot generation counter. Incremented on destroy to invalidate handles.
    std::vector<uint32_t> m_generations;

    /// Indices of freed slots available for reuse.
    std::vector<uint32_t> m_freeSlots;

    /// Live proxy count — kept in sync with create/destroy for O(1) ProxyCount().
    size_t m_liveCount = 0;

    /// Returns true if `handle` refers to a currently live proxy.
    ///
    /// Checks both bounds and generation match.
    [[nodiscard]] bool IsHandleValid(RenderProxyHandle handle) const;
};

} // namespace uz
