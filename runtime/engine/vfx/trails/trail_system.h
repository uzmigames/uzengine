/// TrailSystem — manages all active trail components.
///
/// Owns the lifecycle of trails: registration, per-frame updates (point
/// addition, aging, geometry generation), and render data collection.
///
/// # Lifecycle
/// ```
/// TrailSystem trails;
/// TrailHandle h = trails.RegisterTrail(std::move(component));
///
/// // Each frame:
/// trails.Update(dt, positions, cameraPosition);
/// auto renderData = trails.GetRenderData();
/// // Pass renderData to AddTrailRenderPass
///
/// trails.UnregisterTrail(h);
/// ```
///
/// # Design
/// Each registered trail holds a `TrailComponent` (configuration + ring-buffer
/// state) and a `TrailMeshData` (GPU-ready vertices and indices). The mesh is
/// rebuilt every frame by `TrailMeshBuilder` from the current point buffer.
/// Only trails with at least 2 active points produce renderable geometry.
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the main/render thread.

#pragma once

#include "engine/vfx/trails/trail_component.h"
#include "engine/vfx/trails/trail_mesh_builder.h"
#include "engine/math/vector3.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TrailSystem — central manager for ribbon trail VFX
// ─────────────────────────────────────────────────────────────────────────────

/// Central manager that owns and updates all active ribbon trails.
///
/// Trails are registered by value (`TrailComponent&&`) and assigned an opaque
/// `TrailHandle`. Each frame, `Update()` advances the ring-buffer state for
/// every active trail and rebuilds GPU-ready mesh data via `TrailMeshBuilder`.
/// Callers retrieve per-trail render data from `GetRenderData()` and submit it
/// to `AddTrailRenderPass`.
///
/// Handles are stable for the lifetime of the trail; invalidated only after
/// `UnregisterTrail()` or `Clear()`.
///
/// # Ownership
/// - Owns all `TrailComponent` and `TrailMeshData` instances.
/// - Does NOT own GPU resources (no RHI device dependency).
/// - `GetRenderData()` returns non-owning pointers into owned storage; callers
///   must not store them beyond the next `Update()` call.
class TrailSystem {
public:
    // ── Handle Type ───────────────────────────────────────────────────────────

    /// Opaque handle for a registered trail.
    ///
    /// Returned by `RegisterTrail()`. Use to call `UnregisterTrail()`.
    /// `kInvalidHandle` is never returned by `RegisterTrail()`.
    using TrailHandle = uint32_t;

    /// Sentinel value indicating an invalid or unregistered trail handle.
    static constexpr TrailHandle kInvalidHandle = ~0u;

    // ── Construction ──────────────────────────────────────────────────────────

    /// Default constructor. The system is immediately usable; no separate
    /// Initialize() call is required.
    TrailSystem();
    ~TrailSystem();

    /// Non-copyable (owns trail component storage).
    TrailSystem(const TrailSystem&) = delete;
    TrailSystem& operator=(const TrailSystem&) = delete;

    /// Movable (transfers ownership of all internal storage).
    TrailSystem(TrailSystem&&) noexcept;
    TrailSystem& operator=(TrailSystem&&) noexcept;

    // ── Registration ──────────────────────────────────────────────────────────

    /// Registers a trail component and returns a stable handle.
    ///
    /// The component is moved into the system's internal storage. Point ring
    /// buffer state is preserved if the component already has accumulated
    /// points (e.g., pre-warmed trails).
    ///
    /// # Parameters
    /// - `component`: Trail configuration and ring-buffer state. Moved from.
    ///
    /// # Returns
    /// A valid `TrailHandle` (never `kInvalidHandle`). The handle is unique
    /// within this system until `UnregisterTrail()` is called for it.
    ///
    /// # Side Effects
    /// - Logs VFX/Debug on registration with the trail's material path.
    [[nodiscard]] TrailHandle RegisterTrail(TrailComponent&& component);

    /// Unregisters and destroys the trail with the given handle.
    ///
    /// If `handle` is `kInvalidHandle` or refers to an already-removed trail,
    /// the call is a no-op (logged as a VFX/Warning).
    ///
    /// # Parameters
    /// - `handle`: Handle returned by `RegisterTrail()`.
    ///
    /// # Side Effects
    /// - Removes the trail from the active set; its mesh data is destroyed.
    /// - Logs VFX/Debug on successful removal.
    void UnregisterTrail(TrailHandle handle);

    // ── Per-frame Update ───────────────────────────────────────────────────────

    /// Updates all active trails and rebuilds their ribbon geometry.
    ///
    /// For each registered trail:
    /// 1. Looks up the current world position from `positions` (if present).
    /// 2. Calls `TrailComponent::Tick(dt, newPosition)` to advance the ring
    ///    buffer (add new points, age existing points, expire old ones).
    /// 3. Calls `TrailMeshBuilder::Build(points, cameraPosition)` to
    ///    regenerate the camera-facing quad strip into the trail's `TrailMeshData`.
    ///
    /// Trails with no position entry in `positions` still age their existing
    /// ring-buffer points but do not add new ones.
    ///
    /// # Parameters
    /// - `dt`:             Frame delta time in seconds. Must be >= 0.
    /// - `positions`:      Map from handle to current world-space emitter position.
    ///                     Trails not present in this map are aged only.
    /// - `cameraPosition`: World-space camera position for billboard orientation.
    ///
    /// # Side Effects
    /// - Mutates all `TrailComponent` ring buffers and `TrailMeshData` instances.
    /// - Logs VFX/Debug once (on first call) with active trail count.
    void Update(float dt,
                const std::unordered_map<TrailHandle, Vec3>& positions,
                const Vec3& cameraPosition);

    // ── Render Data ───────────────────────────────────────────────────────────

    /// Per-trail render data snapshot for submission to `AddTrailRenderPass`.
    ///
    /// All pointer fields are non-owning views into the system's internal
    /// storage. Valid only until the next `Update()`, `UnregisterTrail()`, or
    /// `Clear()` call.
    struct TrailRenderData {
        /// Pointer to the first vertex in the trail's ribbon mesh.
        /// `nullptr` if the trail has no renderable geometry (< 2 points).
        const TrailVertex* vertices = nullptr;

        /// Number of vertices in the ribbon mesh.
        uint32_t vertexCount = 0;

        /// Pointer to the first index in the trail's ribbon mesh (uint32_t).
        /// `nullptr` if `vertexCount == 0`.
        const uint32_t* indices = nullptr;

        /// Number of indices in the ribbon mesh.
        uint32_t indexCount = 0;

        /// Material asset path from the trail component.
        /// References the component's `std::string`; valid until next Update().
        const char* materialPath = nullptr;

        /// Rendering sort order from the trail component (lower = drawn first).
        int sortOrder = 0;

        /// Signed distance from the trail's centroid to the camera (metres).
        /// Used for back-to-front sorting in the render pass.
        float distanceToCamera = 0.0f;
    };

    /// Returns per-trail render data for all trails that have renderable geometry.
    ///
    /// Only trails with at least 2 active points (producing at least one
    /// ribbon quad) are included. The order within the returned vector is
    /// unspecified; the render pass sorts by `distanceToCamera`.
    ///
    /// # Returns
    /// Vector of `TrailRenderData` snapshots. Each element holds non-owning
    /// pointers into the system's internal storage -- do not hold past the
    /// next `Update()` call.
    [[nodiscard]] std::vector<TrailRenderData> GetRenderData() const;

    // ── Stats ─────────────────────────────────────────────────────────────────

    /// Returns the number of currently registered (active) trails.
    ///
    /// Inactive slots from `UnregisterTrail()` are NOT counted.
    [[nodiscard]] uint32_t GetActiveTrailCount() const noexcept;

    /// Returns a mutable pointer to the trail component for the given handle.
    ///
    /// Allows external systems (e.g., Lua bindings) to read or modify trail
    /// configuration after registration. The returned pointer is valid until the
    /// next `UnregisterTrail(handle)` or `Clear()` call.
    ///
    /// # Parameters
    /// - `handle`: Handle returned by `RegisterTrail()`.
    ///
    /// # Returns
    /// Non-null pointer if `handle` is valid and the trail is active, `nullptr`
    /// otherwise (invalid handle, already unregistered, or `kInvalidHandle`).
    [[nodiscard]] TrailComponent* GetComponent(TrailHandle handle) noexcept;

    /// Returns a const pointer to the trail component for the given handle.
    ///
    /// Const overload of `GetComponent(TrailHandle)`.
    [[nodiscard]] const TrailComponent* GetComponent(TrailHandle handle) const noexcept;

    /// Returns the total number of live trail points across all active trails.
    ///
    /// Sums `TrailComponent::GetPointCount()` for every registered trail.
    /// Useful for budget tracking and debug overlays.
    [[nodiscard]] uint32_t GetTotalSegmentCount() const noexcept;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /// Removes all registered trails and resets the handle counter.
    ///
    /// After this call, the system is in the same state as after default
    /// construction. No destructors are called for GPU resources because the
    /// system does not own any.
    ///
    /// # Side Effects
    /// - Logs VFX/Info with the trail count at the time of clearing.
    void Clear();

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Internal trail instance storage
    // ─────────────────────────────────────────────────────────────────────────

    /// Internal storage for one registered trail.
    struct TrailInstance {
        /// Owning trail component (configuration + ring buffer state).
        TrailComponent component;

        /// GPU-ready vertex/index mesh, rebuilt each frame by Update().
        TrailMeshData meshData;

        /// Unique handle assigned at registration time.
        TrailHandle handle = kInvalidHandle;

        /// False after `UnregisterTrail()`. The slot may be reused in a future
        /// registration if the vector is compacted.
        bool active = false;
    };

    // ── Helpers ───────────────────────────────────────────────────────────────

    /// Finds the index of the trail instance with the given handle.
    ///
    /// # Returns
    /// Index into `trails_` if found and active, or `trails_.size()` if not
    /// present or inactive.
    [[nodiscard]] size_t FindInstanceIndex(TrailHandle handle) const noexcept;

    // ── Storage ───────────────────────────────────────────────────────────────

    /// Flat array of trail instances (active and inactive slots).
    std::vector<TrailInstance> trails_;

    /// Monotonically increasing handle counter. Never reused.
    TrailHandle nextHandle_ = 0;

    /// Reusable mesh builder (holds no per-trail state between calls).
    TrailMeshBuilder meshBuilder_;

    /// Camera position from the most recent `Update()` call.
    /// Stored so that `GetRenderData()` can compute centroid-to-camera distances
    /// for back-to-front sorting without requiring a camera parameter.
    Vec3 lastCameraPosition_{};

    /// Scratch buffer reused across frames by `Update()` to hold contiguous
    /// point snapshots from ring buffers (avoids per-trail allocation).
    std::vector<TrailPoint> orderedPointsScratch_;
};

} // namespace uz
