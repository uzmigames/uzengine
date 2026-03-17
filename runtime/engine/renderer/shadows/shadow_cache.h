/// UzEngine Static Shadow Cache — UE5 Exact Parity
///
/// Caches shadow depth maps for static geometry to eliminate redundant shadow
/// rendering when only dynamic objects move. UE5 implements this in
/// ShadowSetup.cpp:3869-3954 (cache validation) and ScenePrivate.h:2685-2722
/// (cached shadow map data).
///
/// # Architecture Overview
///
/// UE5 separates shadow rendering into 4 modes per cascade:
/// 1. **Uncached** — Render all geometry every frame (fallback).
/// 2. **StaticPrimitivesOnly** — Render only static meshes. Cached across frames.
///    Re-rendered only when the cache is invalidated (light direction change,
///    cascade parameters change, static geometry changes).
/// 3. **MovablePrimitivesOnly** — Render only dynamic meshes. Every frame.
/// 4. **CSMScrolling** — GPU copy overlap when camera moves, render only new edges.
///
/// The shadow cache manager tracks per-cascade cached depth textures, validates
/// them against the current frame's cascade parameters, and allocates/releases
/// GPU resources within a VRAM budget.
///
/// # Compositing
///
/// When shadow caching is active, the pixel shader samples both the static
/// (cached) and dynamic (per-frame) shadow atlases and takes the minimum
/// (most shadowed) result. This is correct because a pixel is in shadow if
/// it is occluded by either static OR dynamic geometry.
///
/// # Thread Safety
///
/// NOT thread-safe. Must be called from the render thread only.
///
/// # UE5 Reference
/// - EShadowDepthCacheMode: ShadowRendering.h:183-189
/// - FProjectedShadowInfo cache fields: ScenePrivate.h:2685-2722
/// - Cache validation: ShadowSetup.cpp:3869-3954
/// - Cache config CVars: ShadowSetup.cpp:69-100

#pragma once

#include "shadow_cascade.h"
#include "rhi_types.h"
#include "vector3.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace uz {

class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheMode — UE5 EShadowDepthCacheMode parity
// ─────────────────────────────────────────────────────────────────────────────

/// Shadow depth cache mode — UE5 EShadowDepthCacheMode parity.
///
/// Determines how a cascade's shadow depth rendering is handled this frame.
/// The scene renderer checks this mode per cascade to decide whether to skip
/// the static geometry pass.
///
/// Source: UE5 ShadowRendering.h:183-189.
enum class ShadowCacheMode : uint8_t {
    Uncached,                ///< Render all geometry every frame (fallback).
    StaticPrimitivesOnly,    ///< Render only static meshes. Cached across frames.
    MovablePrimitivesOnly,   ///< Render only dynamic meshes. Every frame.
    CSMScrolling             ///< GPU copy overlap + render new edges only.
};

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheConfig — system configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the shadow cache system.
///
/// Controls global enable, VRAM budget, and CSM scrolling thresholds.
/// These map to UE5 CVars in ShadowSetup.cpp:69-100.
///
/// # Defaults
/// - `enabled = true`: Shadow caching is ready to use but requires explicit
///   activation in the scene renderer (ShadowDataCB::useShadowCache must be
///   set to 1 by the caller).
/// - `cacheBudgetMB = 150`: Reasonable for 4 cascades at 2048x2048 D32Float
///   (each cached texture is ~16 MB, total = ~64 MB for 4 cascades).
/// - `scrollingOverlapThreshold = 0.75`: Minimum UV overlap ratio before
///   CSM scrolling falls back to full re-render.
struct ShadowCacheConfig {
    bool enabled = true;                          ///< Enable shadow caching globally.
    uint32_t cacheBudgetMB = 150;                 ///< Max VRAM for cached shadow targets (MB).
    float scrollingOverlapThreshold = 0.75f;      ///< Min overlap ratio for CSM scrolling (0-1).
};

/// Maximum number of extra static shadow subjects allowed during CSM scrolling
/// before falling back to a full static re-render.
/// @source UE5 ShadowSetup.cpp:406-409 — CVarCSMScrollingMaxExtraStaticShadowSubjects (default 5)
constexpr uint32_t kMaxCSMScrollingExtraStaticSubjects = 5;

// ─────────────────────────────────────────────────────────────────────────────
// CSMScrollingData — per-cascade scrolling parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Per-cascade CSM scrolling parameters computed during UpdateCache.
///
/// Contains the UV overlap regions and culling planes needed to perform
/// the GPU copy of the overlapping region and selective re-rendering of
/// the exposed edge strips.
///
/// @source UE5 ShadowSetup.cpp:5600-5668 — CSMScrolling data computation
/// @source UE5 ShadowDepthRendering.cpp:1000-1035 — GPU copy using overlap UVs
struct CSMScrollingData {
    /// UV region on the cached (old) shadow map that overlaps with the new map.
    /// Format: (minU, minV, maxU, maxV) in [0,1] normalized texture coordinates.
    /// @source UE5 ShadowSetup.cpp:5629-5632 — OverlappedUVOnCachedShadowMap
    float overlappedUVOnCached[4] = {0.f, 0.f, 1.f, 1.f};

    /// UV region on the current (new) shadow map that corresponds to the overlap.
    /// Format: (minU, minV, maxU, maxV) in [0,1] normalized texture coordinates.
    /// @source UE5 ShadowSetup.cpp:5636-5639 — OverlappedUVOnCurrentShadowMap
    float overlappedUVOnCurrent[4] = {0.f, 0.f, 1.f, 1.f};

    /// Z offset in light space between cached and current shadow maps.
    /// Used by the scrolling shader to reconstruct depth values after the copy.
    /// @source UE5 ShadowSetup.cpp:5616 — CSMScrollingZOffset
    float zOffset = 0.f;

    /// Culling planes for the non-overlapped edge strips (up to 4: top, bottom, left, right).
    /// Each plane is defined by a point and a normal in world space.
    /// Only static primitives that intersect these planes need re-rendering.
    /// @source UE5 ShadowSetup.cpp:5641-5667 — CSMScrollingExtraCullingPlanes
    struct CullingPlane {
        Vec3 point;   ///< A point on the plane (world space).
        Vec3 normal;  ///< Outward-facing normal of the plane.
    };
    CullingPlane cullingPlanes[4];
    uint32_t cullingPlaneCount = 0;

    /// Overlap area ratio: area of intersection / area of cached cascade.
    /// If > scrollingOverlapThreshold, scrolling is used; otherwise full re-render.
    float overlapRatio = 0.f;

    /// True if scrolling data is valid and should be used.
    bool active = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheKey — snapshot for invalidation comparison
// ─────────────────────────────────────────────────────────────────────────────

/// Snapshot of the shadow initializer at cache time.
///
/// Captures the parameters that, if changed, require re-rendering the static
/// shadow map for a cascade. Each field is compared with epsilon tolerance
/// to avoid unnecessary invalidation from floating-point drift.
///
/// Source: UE5 ShadowSetup.cpp:3869-3954 (CompareShadowVolume).
struct ShadowCacheKey {
    Vec3 lightDirection;       ///< Light direction when cached.
    Vec3 lightUp;              ///< Light up vector when cached.
    Vec3 cascadeCenter;        ///< World-space cascade center (PreShadowTranslation equivalent).
    float subjectRadius;       ///< Cascade bounding sphere radius.
    float minSubjectZ;         ///< Min Z extent in light space.
    float maxSubjectZ;         ///< Max Z extent in light space.
    float projectionScale;     ///< Ortho projection scale (2 * radius).
    uint32_t resolution;       ///< Shadow map resolution.
};

// ─────────────────────────────────────────────────────────────────────────────
// CachedShadowMapData — per-cascade cache entry
// ─────────────────────────────────────────────────────────────────────────────

/// Cached shadow map data for a single cascade.
///
/// Holds the GPU depth texture containing the rendered static geometry shadow,
/// the key used for invalidation comparison, and bookkeeping metadata.
///
/// Source: UE5 ScenePrivate.h:2685-2722.
struct CachedShadowMapData {
    RHITextureHandle cachedTexture;  ///< Cached depth texture (static geometry only).
    uint32_t srvIndex = UINT32_MAX;  ///< SRV heap index for the cached texture.
    ShadowCacheKey key;              ///< Initializer snapshot for invalidation.
    uint32_t lastUsedFrame = 0;      ///< Frame number when last validated/used.
    bool valid = false;              ///< True if cache is valid and can be reused.

    /// Bitset tracking which primitive indices were rendered into the cached shadow map.
    ///
    /// Indexed by draw command index (position in the DrawList). When CSM scrolling
    /// mode is active, this bitset determines which static primitives are already
    /// present in the cached atlas, so only new or edge-strip primitives need
    /// re-rendering.
    ///
    /// @source UE5 ScenePrivate.h:2697 — StaticShadowSubjectPersistentPrimitiveIdMap
    std::vector<bool> staticPrimitiveMap;

    /// Count of extra static shadow subjects added during CSM scrolling this frame.
    /// If this exceeds `kMaxCSMScrollingExtraStaticSubjects`, fall back to full re-render.
    /// @source UE5 ShadowSetup.cpp:406-409 — CVarCSMScrollingMaxExtraStaticShadowSubjects
    uint32_t lastFrameExtraStaticShadowSubjects = 0;

    /// Light-space center (PreShadowTranslation equivalent) when cached.
    /// Used by CSM scrolling to compute the overlap between old and new cascade bounds.
    /// @source UE5 ScenePrivate.h:2696 — PreShadowTranslation
    Vec3 cachedPreShadowTranslation;
};

// ─────────────────────────────────────────────────────────────────────────────
// LightShadowCache — per-light cache container
// ─────────────────────────────────────────────────────────────────────────────

/// Per-light shadow cache — holds cached data for all cascades of a single light.
///
/// Uses a fixed-size array sized to `kMaxShadowCascades` (8) to avoid heap
/// allocation on the hot path. `cascadeCount` tracks how many entries are active.
struct LightShadowCache {
    std::array<CachedShadowMapData, kMaxShadowCascades> cascades;
    std::array<CSMScrollingData, kMaxShadowCascades> scrollingData;
    uint32_t cascadeCount = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCacheManager — manages all cached shadow maps
// ─────────────────────────────────────────────────────────────────────────────

/// Shadow cache manager — manages all cached shadow maps.
///
/// Tracks VRAM budget, invalidation, and provides the two-pass rendering
/// interface (static vs dynamic separation). Each light gets a
/// `LightShadowCache` with per-cascade entries. The manager allocates
/// D32Float textures on demand and releases them when the budget is exceeded
/// or a light is removed.
///
/// # Usage
/// ```cpp
/// ShadowCacheManager cache;
/// cache.Init(device, config);
///
/// // Per-frame, per-cascade:
/// ShadowCacheMode mode = cache.UpdateCache(
///     lightId, cascadeIdx, cascade, lightDir, lightUp, resolution, frameNumber);
///
/// if (mode == ShadowCacheMode::StaticPrimitivesOnly) {
///     // Static cache is valid — skip re-rendering static geometry
///     RHITextureHandle cached = cache.GetCachedTexture(lightId, cascadeIdx);
/// }
///
/// cache.Shutdown();
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread only.
class ShadowCacheManager {
public:
    /// Initializes the cache manager with a device and configuration.
    ///
    /// # Arguments
    /// * `device` — RHI device for texture creation/destruction. Must outlive the manager.
    /// * `config` — Shadow cache configuration.
    void Init(IRHIDevice* device, const ShadowCacheConfig& config);

    /// Shuts down and releases all cached textures.
    ///
    /// Must be called before the RHI device is released.
    void Shutdown();

    /// Builds a cache key from current cascade data for invalidation comparison.
    ///
    /// Extracts the parameters that determine shadow map content from the
    /// cascade's view-projection matrix and configuration. Two keys can be
    /// compared with `IsCacheValid()` to determine if a cached shadow map
    /// is still usable.
    ///
    /// # Arguments
    /// * `cascade` — Current frame's computed cascade data.
    /// * `lightDir` — Normalized light direction (direction light shines toward).
    /// * `lightUp` — Light's up vector used to build the light-space basis.
    /// * `resolution` — Shadow map resolution per cascade in texels.
    ///
    /// # Returns
    /// A `ShadowCacheKey` capturing all parameters that affect shadow content.
    [[nodiscard]] static ShadowCacheKey BuildKey(
        const ShadowCascade& cascade,
        const Vec3& lightDir,
        const Vec3& lightUp,
        uint32_t resolution);

    /// Checks if a cached cascade is still valid (no parameters changed).
    ///
    /// Compares two cache keys with epsilon tolerance for floating-point fields.
    /// Direction vectors use a tight tolerance (1e-4), distances use a looser
    /// tolerance (1e-2), and integer fields (resolution) use exact comparison.
    ///
    /// Source: UE5 ShadowSetup.cpp:3869-3954.
    ///
    /// # Arguments
    /// * `cached` — The key stored when the shadow map was last rendered.
    /// * `current` — The key for the current frame's cascade.
    ///
    /// # Returns
    /// `true` if the cached shadow map can be reused without re-rendering.
    [[nodiscard]] static bool IsCacheValid(
        const ShadowCacheKey& cached,
        const ShadowCacheKey& current);

    /// Updates cache for a light's cascade. Returns the cache mode to use.
    ///
    /// This is the main per-frame entry point. For the specified light and
    /// cascade, it checks whether a valid cached texture exists, allocates
    /// a new one if needed, and determines the rendering mode.
    ///
    /// # Behavior
    /// - If caching is disabled (`config.enabled == false`), always returns `Uncached`.
    /// - If no cached texture exists or the cache key changed, allocates a D32Float
    ///   texture (if within budget) and returns `StaticPrimitivesOnly` so the caller
    ///   re-renders static geometry into the cached texture.
    /// - If the cache is valid (key matches), returns `MovablePrimitivesOnly` so the
    ///   caller only renders dynamic geometry.
    /// - If the VRAM budget is exceeded, returns `Uncached` (no caching for this cascade).
    ///
    /// # Arguments
    /// * `lightId` — Unique identifier for the light source.
    /// * `cascadeIndex` — Index of the cascade within the light's cascade array.
    /// * `cascade` — Current frame's computed cascade data.
    /// * `lightDir` — Normalized light direction (direction light shines toward).
    /// * `lightUp` — Light's up vector used to build the light-space basis.
    /// * `resolution` — Shadow map resolution per cascade in texels.
    /// * `currentFrame` — Current frame number for LRU tracking.
    ///
    /// # Returns
    /// The `ShadowCacheMode` to use for rendering this cascade.
    [[nodiscard]] ShadowCacheMode UpdateCache(
        uint32_t lightId,
        uint32_t cascadeIndex,
        const ShadowCascade& cascade,
        const Vec3& lightDir,
        const Vec3& lightUp,
        uint32_t resolution,
        uint32_t currentFrame);

    /// Gets the cached static shadow texture for compositing.
    ///
    /// # Arguments
    /// * `lightId` — Unique identifier for the light source.
    /// * `cascadeIndex` — Index of the cascade within the light's cascade array.
    ///
    /// # Returns
    /// The cached texture handle, or an invalid handle if no cache exists.
    [[nodiscard]] RHITextureHandle GetCachedTexture(
        uint32_t lightId,
        uint32_t cascadeIndex) const;

    /// Gets the SRV heap index for the cached static shadow texture.
    ///
    /// # Arguments
    /// * `lightId` — Unique identifier for the light source.
    /// * `cascadeIndex` — Index of the cascade within the light's cascade array.
    ///
    /// # Returns
    /// The SRV heap index, or `UINT32_MAX` if no cache exists.
    [[nodiscard]] uint32_t GetCachedTextureSrvIndex(
        uint32_t lightId,
        uint32_t cascadeIndex) const;

    /// Returns total VRAM used by all cached textures in bytes.
    [[nodiscard]] uint64_t GetTotalMemoryUsage() const { return m_totalMemoryBytes; }

    /// Returns true if VRAM budget is exceeded.
    [[nodiscard]] bool IsBudgetExceeded() const { return m_totalMemoryBytes > m_budgetBytes; }

    /// Returns the number of currently cached lights.
    [[nodiscard]] uint32_t GetCachedLightCount() const {
        return static_cast<uint32_t>(m_caches.size());
    }

    /// Returns the configuration.
    [[nodiscard]] const ShadowCacheConfig& GetConfig() const { return m_config; }

    /// Invalidates all cached shadow maps for a specific light.
    ///
    /// Call this when a light's properties change in a way that affects all
    /// cascades (e.g., light direction change, static geometry modification).
    ///
    /// # Arguments
    /// * `lightId` — Unique identifier for the light source.
    void InvalidateLight(uint32_t lightId);

    /// Invalidates all cached shadow maps for all lights.
    ///
    /// Call this when global static geometry changes (e.g., level load,
    /// static mesh added/removed).
    void InvalidateAll();

    /// Evicts cached textures for lights not used since `olderThanFrame`.
    ///
    /// Frees VRAM from lights that are no longer visible. Called periodically
    /// (e.g., every 60 frames) to reclaim stale memory.
    ///
    /// # Arguments
    /// * `olderThanFrame` — Evict caches not used since this frame number.
    void EvictStale(uint32_t olderThanFrame);

    /// Records that a static primitive was rendered into the cached shadow map.
    ///
    /// Called during the static shadow pass for each draw command rendered.
    /// The bitset is later used by CSM scrolling to determine which primitives
    /// are already present in the overlap region.
    ///
    /// @source UE5 ShadowSetup.cpp:2364 — StaticShadowSubjectPersistentPrimitiveIdMap[...] = true
    ///
    /// # Arguments
    /// * `lightId` — Unique identifier for the light source.
    /// * `cascadeIndex` — Cascade index.
    /// * `primitiveIndex` — Index of the primitive in the draw list.
    /// * `totalPrimitives` — Total number of primitives (to size the bitset).
    void MarkStaticPrimitiveRendered(uint32_t lightId, uint32_t cascadeIndex,
                                     uint32_t primitiveIndex, uint32_t totalPrimitives);

    /// Checks if a static primitive is recorded in the cached shadow map.
    ///
    /// @source UE5 ShadowSetup.cpp:5002 — StaticShadowSubjectPersistentPrimitiveIdMap[...]
    ///
    /// # Returns
    /// `true` if the primitive was rendered into the cached shadow map.
    [[nodiscard]] bool IsStaticPrimitiveRendered(uint32_t lightId, uint32_t cascadeIndex,
                                                  uint32_t primitiveIndex) const;

    /// Retrieves the CSM scrolling data computed during UpdateCache for a cascade.
    ///
    /// Only valid when UpdateCache returned `CSMScrolling` for this cascade.
    ///
    /// # Returns
    /// Const reference to the scrolling data. Check `active` field before use.
    [[nodiscard]] const CSMScrollingData& GetScrollingData(uint32_t lightId,
                                                            uint32_t cascadeIndex) const;

    /// Sets shadow caching enabled/disabled at runtime.
    ///
    /// Equivalent to UE5's `r.Shadow.CacheEnabled` CVar.
    /// Takes effect on the next UpdateCache call.
    ///
    /// # Arguments
    /// * `enabled` — True to enable shadow caching.
    void SetEnabled(bool enabled) { m_config.enabled = enabled; }

    /// Returns whether shadow caching is currently enabled.
    [[nodiscard]] bool IsEnabled() const { return m_config.enabled; }

private:
    /// Allocates a D32Float cached depth texture at the specified resolution.
    ///
    /// # Returns
    /// True if allocation succeeded and is within budget.
    bool AllocateCachedTexture(CachedShadowMapData& data, uint32_t resolution);

    /// Releases a cached depth texture and updates memory tracking.
    void ReleaseCachedTexture(CachedShadowMapData& data);

    /// Computes the VRAM footprint of a single cached shadow map in bytes.
    [[nodiscard]] static uint64_t ComputeTextureMemory(uint32_t resolution);

    /// Computes scrolling overlap data between cached and current cascade bounds.
    ///
    /// Projects cascade centers into light space, computes AABB overlap, generates
    /// overlap UV regions and edge-strip culling planes.
    ///
    /// @source UE5 ShadowSetup.cpp:3893-3931 — overlap computation
    /// @source UE5 ShadowSetup.cpp:5600-5668 — UV + culling plane computation
    ///
    /// # Returns
    /// CSMScrollingData with overlap UVs, culling planes, and overlap ratio.
    [[nodiscard]] CSMScrollingData ComputeScrollingData(
        const ShadowCacheKey& cachedKey,
        const ShadowCacheKey& currentKey,
        const Vec3& cachedCenter,
        const Vec3& currentCenter,
        const Vec3& lightDir) const;

    IRHIDevice* m_device = nullptr;
    ShadowCacheConfig m_config;
    std::unordered_map<uint32_t, LightShadowCache> m_caches;
    uint64_t m_totalMemoryBytes = 0;
    uint64_t m_budgetBytes = 0;

    /// Default inactive scrolling data returned when no scrolling is active.
    static const CSMScrollingData s_inactiveScrollingData;
};

} // namespace uz
