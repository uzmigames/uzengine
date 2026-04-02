/// TrailSystem — Implementation
///
/// Per-frame trail lifecycle: point accumulation, age-based expiry, and
/// camera-facing ribbon mesh generation via TrailMeshBuilder.

#include "trail_system.h"

#include "engine/vfx/trails/trail_component.h"
#include "engine/vfx/trails/trail_mesh_builder.h"
#include "engine/math/vector3.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

#include <cmath>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

TrailSystem::TrailSystem() = default;
TrailSystem::~TrailSystem() = default;
TrailSystem::TrailSystem(TrailSystem&&) noexcept = default;
TrailSystem& TrailSystem::operator=(TrailSystem&&) noexcept = default;

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

TrailSystem::TrailHandle TrailSystem::RegisterTrail(TrailComponent&& component)
{
    const TrailHandle handle = nextHandle_++;

    UZ_LOG(VFX, Debug,
           "TrailSystem: registering trail handle={} material='{}'",
           handle,
           component.materialPath.c_str());

    // Search for a vacant (inactive) slot before growing the vector.
    // This avoids unbounded growth when trails are frequently registered
    // and unregistered at the same rate.
    for (auto& slot : trails_) {
        if (!slot.active) {
            slot.component = std::move(component);
            slot.meshData   = TrailMeshData{};
            slot.handle     = handle;
            slot.active     = true;
            return handle;
        }
    }

    // No free slot — append a new one.
    trails_.push_back(TrailInstance{
        .component = std::move(component),
        .meshData  = TrailMeshData{},
        .handle    = handle,
        .active    = true,
    });

    return handle;
}

void TrailSystem::UnregisterTrail(TrailHandle handle)
{
    if (handle == kInvalidHandle) {
        UZ_LOG(VFX, Warning, "TrailSystem: UnregisterTrail called with kInvalidHandle");
        return;
    }

    const size_t idx = FindInstanceIndex(handle);
    if (idx == trails_.size()) {
        UZ_LOG(VFX, Warning,
               "TrailSystem: UnregisterTrail -- handle={} not found or already removed",
               handle);
        return;
    }

    UZ_LOG(VFX, Debug,
           "TrailSystem: unregistering trail handle={} material='{}'",
           handle,
           trails_[idx].component.materialPath.c_str());

    // Mark inactive rather than erase so that other indices remain stable
    // within the same frame. The slot will be reused by the next RegisterTrail.
    trails_[idx].active = false;
    trails_[idx].meshData = TrailMeshData{};  // Free vertex/index memory early.
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame Update
// ─────────────────────────────────────────────────────────────────────────────

void TrailSystem::Update(float dt,
                         const std::unordered_map<TrailHandle, Vec3>& positions,
                         const Vec3& cameraPosition)
{
    UZ_PROFILE_SCOPE("VFX::TrailSystem::Update");

    // Log active trail count on first call for diagnostics.
    static bool loggedOnce = false;
    if (!loggedOnce) {
        UZ_LOG(VFX, Info,
               "TrailSystem::Update: first tick -- {} registered trail slots",
               trails_.size());
        loggedOnce = true;
    }

    for (auto& instance : trails_) {
        if (!instance.active) continue;

        // Resolve current position from the caller-provided map.
        // If this trail has no entry, pass a sentinel to Tick() to
        // signal that no new point should be added this frame.
        const auto posIt = positions.find(instance.handle);
        const bool hasPosition = (posIt != positions.end());

        if (hasPosition) {
            instance.component.Tick(dt, posIt->second);
        }
        // When no position is available, skip Tick entirely. Existing points
        // will not age this frame, which is acceptable for a single missed frame.
        // The alternative (adding a Tick(dt) overload) is not available on
        // TrailComponent.

        // Rebuild ribbon geometry from the current point ring buffer.
        // Build() requires a contiguous array, so copy the ring buffer into
        // a scratch vector first.
        const auto& pointBuffer = instance.component.GetPoints();
        const uint32_t pointCount = pointBuffer.Count();

        if (pointCount >= 2) {
            pointBuffer.CopyOrdered(orderedPointsScratch_);
            instance.meshData = meshBuilder_.Build(
                orderedPointsScratch_.data(),
                pointCount,
                cameraPosition,
                instance.component.widthStart,
                instance.component.widthEnd,
                instance.component.colorStart,
                instance.component.colorEnd,
                instance.component.totalDistance,
                instance.component.pointLifetime
            );
        } else {
            instance.meshData.Clear();
        }
    }

    lastCameraPosition_ = cameraPosition;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Data
// ─────────────────────────────────────────────────────────────────────────────

std::vector<TrailSystem::TrailRenderData> TrailSystem::GetRenderData() const
{
    std::vector<TrailRenderData> result;
    result.reserve(trails_.size());

    for (const auto& instance : trails_) {
        if (!instance.active) continue;
        if (instance.meshData.GetVertexCount() == 0) continue;  // No renderable geometry.

        // Compute the trail centroid as the average of the first and last point
        // positions for an approximate distance-to-camera value.
        const auto& points = instance.component.GetPoints();
        float distanceToCamera = 0.0f;
        if (points.Count() > 0) {
            // Average head and tail for a rough centre. The render pass uses this
            // only for back-to-front sorting, not precise z-ordering.
            const Vec3 head = points.GetPoint(0).position;
            const Vec3 tail = points.GetPoint(points.Count() - 1).position;
            const Vec3 centroid{
                (head.x + tail.x) * 0.5f,
                (head.y + tail.y) * 0.5f,
                (head.z + tail.z) * 0.5f,
            };

            // Compute Euclidean distance from centroid to the last known camera position.
            const float dx = centroid.x - lastCameraPosition_.x;
            const float dy = centroid.y - lastCameraPosition_.y;
            const float dz = centroid.z - lastCameraPosition_.z;
            distanceToCamera = std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        result.push_back(TrailRenderData{
            .vertices         = instance.meshData.vertices.data(),
            .vertexCount      = instance.meshData.GetVertexCount(),
            .indices          = instance.meshData.indices.data(),
            .indexCount       = instance.meshData.GetIndexCount(),
            .materialPath     = instance.component.materialPath.c_str(),
            .sortOrder        = instance.component.sortOrder,
            .distanceToCamera = distanceToCamera,
        });
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Component Access
// ─────────────────────────────────────────────────────────────────────────────

TrailComponent* TrailSystem::GetComponent(TrailHandle handle) noexcept
{
    const size_t idx = FindInstanceIndex(handle);
    if (idx >= trails_.size()) return nullptr;
    return &trails_[idx].component;
}

const TrailComponent* TrailSystem::GetComponent(TrailHandle handle) const noexcept
{
    const size_t idx = FindInstanceIndex(handle);
    if (idx >= trails_.size()) return nullptr;
    return &trails_[idx].component;
}

// ─────────────────────────────────────────────────────────────────────────────
// Stats
// ─────────────────────────────────────────────────────────────────────────────

uint32_t TrailSystem::GetActiveTrailCount() const noexcept
{
    uint32_t count = 0;
    for (const auto& instance : trails_) {
        if (instance.active) ++count;
    }
    return count;
}

uint32_t TrailSystem::GetTotalSegmentCount() const noexcept
{
    uint32_t total = 0;
    for (const auto& instance : trails_) {
        if (instance.active) {
            // Each ribbon segment is a quad (2 triangles) between adjacent points.
            // Segment count = pointCount - 1; clamp to 0 when < 2 points.
            const uint32_t pointCount = instance.component.GetPoints().Count();
            total += (pointCount > 1) ? (pointCount - 1) : 0;
        }
    }
    return total;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void TrailSystem::Clear()
{
    const uint32_t activeCount = GetActiveTrailCount();
    UZ_LOG(VFX, Info,
           "TrailSystem::Clear: removing {} active trails",
           activeCount);

    trails_.clear();
    nextHandle_ = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────────────────────

size_t TrailSystem::FindInstanceIndex(TrailHandle handle) const noexcept
{
    for (size_t i = 0; i < trails_.size(); ++i) {
        if (trails_[i].active && trails_[i].handle == handle) {
            return i;
        }
    }
    return trails_.size();  // Sentinel: not found.
}

} // namespace uz
