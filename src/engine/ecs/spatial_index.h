#pragma once

// ISpatialIndex — abstract interface for spatial queries on entity AABBs.
//
// Implementations provide fast broadphase queries (sphere, frustum, raycast)
// backed by an acceleration structure (BVH, grid, etc.).

#include "engine/core/math.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace uz {

// Result of a successful raycast against a spatial index.
struct RaycastHit {
    uint32_t entity_id;
    float    distance;
    Vec3     hit_point;
};

// Pure virtual interface for spatial indexing of entity AABBs.
//
// All query methods append results to the caller's vector (never clear it).
// This allows composing multiple queries without extra allocations.
class ISpatialIndex {
public:
    virtual ~ISpatialIndex() noexcept = default;

    // Insert or update an entity's bounding box.
    virtual void insert(uint32_t entity_id, Aabb bounds) = 0;

    // Remove an entity from the index. No-op if not present.
    virtual void remove(uint32_t entity_id) = 0;

    // Append entity IDs whose AABBs overlap the given sphere.
    virtual void query_sphere(Vec3 center, float radius,
                              std::vector<uint32_t>& results) const = 0;

    // Append entity IDs whose AABBs are not fully outside the frustum.
    // planes: 6 planes, each as (a, b, c, d) where ax + by + cz + d >= 0 is inside.
    virtual void query_frustum(const float planes[6][4],
                               std::vector<uint32_t>& results) const = 0;

    // Cast a ray and return the nearest hit within max_dist.
    // Returns nullopt if nothing is hit.
    [[nodiscard]]
    virtual std::optional<RaycastHit> raycast(Vec3 origin, Vec3 dir,
                                              float max_dist) const = 0;

    // Remove all entities. After clear(), count() == 0.
    virtual void clear() = 0;

    // Number of entities currently in the index.
    [[nodiscard]]
    virtual size_t count() const = 0;
};

} // namespace uz
