#pragma once

// BvhSpatialIndex — BVH-based implementation of ISpatialIndex.
//
// Uses a top-down median-split BVH stored as a flat array of nodes.
// Lazy rebuild: removals are deferred and trigger a full rebuild when the
// fraction of removed entries exceeds 25%.

#include "engine/ecs/spatial_index.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace uz {

// ----------------------------------------------------------------------------
// AABB helper functions (inline, no .cpp needed)
// ----------------------------------------------------------------------------

// Returns true if the AABB contains the given point.
[[nodiscard]] inline bool aabb_contains_point(const Aabb& box, Vec3 p) noexcept {
    return p.x >= box.min.x && p.x <= box.max.x &&
           p.y >= box.min.y && p.y <= box.max.y &&
           p.z >= box.min.z && p.z <= box.max.z;
}

// Returns true if the AABB overlaps a sphere (center, radius).
[[nodiscard]] inline bool aabb_intersects_sphere(const Aabb& box, Vec3 center,
                                                  float radius) noexcept {
    // Compute squared distance from sphere center to nearest point on AABB.
    float dist_sq = 0.0f;

    auto clamp_axis = [](float val, float lo, float hi) -> float {
        if (val < lo) { float d = lo - val; return d * d; }
        if (val > hi) { float d = val - hi; return d * d; }
        return 0.0f;
    };

    dist_sq += clamp_axis(center.x, box.min.x, box.max.x);
    dist_sq += clamp_axis(center.y, box.min.y, box.max.y);
    dist_sq += clamp_axis(center.z, box.min.z, box.max.z);

    return dist_sq <= radius * radius;
}

// Returns true if two AABBs overlap.
[[nodiscard]] inline bool aabb_intersects_aabb(const Aabb& a, const Aabb& b) noexcept {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

// Classify an AABB against a plane (a, b, c, d).
// Returns: +1 if fully on the positive side (ax+by+cz+d >= 0 for all corners),
//          -1 if fully on the negative side,
//           0 if straddling.
[[nodiscard]] inline int aabb_vs_plane(const Aabb& box,
                                        float a, float b, float c, float d) noexcept {
    // P-vertex: the corner most aligned with the plane normal.
    // N-vertex: the opposite corner.
    const float px = (a >= 0.0f) ? box.max.x : box.min.x;
    const float py = (b >= 0.0f) ? box.max.y : box.min.y;
    const float pz = (c >= 0.0f) ? box.max.z : box.min.z;

    const float nx = (a >= 0.0f) ? box.min.x : box.max.x;
    const float ny = (b >= 0.0f) ? box.min.y : box.max.y;
    const float nz = (c >= 0.0f) ? box.min.z : box.max.z;

    // If the P-vertex is on the negative side, the whole box is behind.
    if (a * px + b * py + c * pz + d < 0.0f) return -1;

    // If the N-vertex is on the positive side, the whole box is in front.
    if (a * nx + b * ny + c * nz + d >= 0.0f) return 1;

    return 0; // Straddling
}

// Ray-AABB intersection using the slab method.
// Returns the distance to the nearest intersection, or -1.0f if no hit.
// inv_dir = 1.0f / dir (precomputed by caller for efficiency).
[[nodiscard]] inline float aabb_ray_intersect(const Aabb& box, Vec3 origin,
                                               Vec3 inv_dir, float max_dist) noexcept {
    const float t1x = (box.min.x - origin.x) * inv_dir.x;
    const float t2x = (box.max.x - origin.x) * inv_dir.x;
    const float t1y = (box.min.y - origin.y) * inv_dir.y;
    const float t2y = (box.max.y - origin.y) * inv_dir.y;
    const float t1z = (box.min.z - origin.z) * inv_dir.z;
    const float t2z = (box.max.z - origin.z) * inv_dir.z;

    float tmin = std::fmin(t1x, t2x);
    float tmax = std::fmax(t1x, t2x);
    tmin = std::fmax(tmin, std::fmin(t1y, t2y));
    tmax = std::fmin(tmax, std::fmax(t1y, t2y));
    tmin = std::fmax(tmin, std::fmin(t1z, t2z));
    tmax = std::fmin(tmax, std::fmax(t1z, t2z));

    // tmax < 0: box is behind the ray origin.
    // tmin > tmax: no intersection.
    // tmin > max_dist: intersection too far.
    if (tmax < 0.0f || tmin > tmax || tmin > max_dist) return -1.0f;

    // If tmin < 0, the ray starts inside the box; use 0 as the entry point.
    return (tmin >= 0.0f) ? tmin : 0.0f;
}

// ----------------------------------------------------------------------------
// BVH node
// ----------------------------------------------------------------------------

struct BvhNode {
    Aabb     bounds;
    int32_t  left;       // -1 for leaf nodes
    int32_t  right;      // -1 for leaf nodes
    uint32_t entity_id;  // Only meaningful for leaf nodes (0 for internal)
};

// ----------------------------------------------------------------------------
// BvhSpatialIndex
// ----------------------------------------------------------------------------

class BvhSpatialIndex final : public ISpatialIndex {
public:
    BvhSpatialIndex() noexcept = default;
    ~BvhSpatialIndex() noexcept override = default;

    // Non-copyable, movable.
    BvhSpatialIndex(const BvhSpatialIndex&) = delete;
    BvhSpatialIndex& operator=(const BvhSpatialIndex&) = delete;
    BvhSpatialIndex(BvhSpatialIndex&&) noexcept = default;
    BvhSpatialIndex& operator=(BvhSpatialIndex&&) noexcept = default;

    void insert(uint32_t entity_id, Aabb bounds) override;
    void remove(uint32_t entity_id) override;

    void query_sphere(Vec3 center, float radius,
                      std::vector<uint32_t>& results) const override;

    void query_frustum(const float planes[6][4],
                       std::vector<uint32_t>& results) const override;

    [[nodiscard]]
    std::optional<RaycastHit> raycast(Vec3 origin, Vec3 dir,
                                      float max_dist) const override;

    void clear() override;

    [[nodiscard]]
    size_t count() const override;

    // Access the internal node array (for testing / debugging).
    [[nodiscard]] const std::vector<BvhNode>& nodes() const noexcept { return nodes_; }

private:
    // Source data: entity ID -> AABB (the "leaf list").
    struct Entry {
        uint32_t entity_id;
        Aabb     bounds;
    };

    std::vector<Entry>   entries_;
    std::vector<BvhNode> nodes_;
    size_t               removed_count_ = 0;
    int32_t              root_          = -1;

    // Build the BVH from entries_. Called after insert/remove when needed.
    void rebuild();

    // Recursive top-down build. Returns index of the created node in nodes_.
    int32_t build_recursive(std::vector<int32_t>& indices, int32_t begin, int32_t end);

    // Recursive traversal helpers.
    void query_sphere_recursive(int32_t node_idx, Vec3 center, float radius,
                                std::vector<uint32_t>& results) const;

    void query_frustum_recursive(int32_t node_idx, const float planes[6][4],
                                 std::vector<uint32_t>& results) const;

    void raycast_recursive(int32_t node_idx, Vec3 origin, Vec3 inv_dir,
                           float max_dist, float& nearest_dist,
                           RaycastHit& nearest_hit, bool& has_hit) const;
};

} // namespace uz
