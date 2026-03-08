// bvh_spatial_index.cpp — BVH-based spatial index implementation.
//
// Top-down median-split BVH with lazy rebuild on remove.
// See bvh_spatial_index.h for the public API and AABB helpers.

#include "engine/ecs/bvh_spatial_index.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace uz {

// ============================================================================
// Public API
// ============================================================================

void BvhSpatialIndex::insert(uint32_t entity_id, Aabb bounds) {
    // Update existing entry if present.
    for (auto& entry : entries_) {
        if (entry.entity_id == entity_id) {
            entry.bounds = bounds;
            rebuild();
            return;
        }
    }
    entries_.push_back({entity_id, bounds});
    rebuild();
}

void BvhSpatialIndex::remove(uint32_t entity_id) {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].entity_id == entity_id) {
            entries_.erase(entries_.begin() + static_cast<ptrdiff_t>(i));
            ++removed_count_;

            // Rebuild if >25% of the original population has been removed,
            // or always rebuild for correctness (the entry is gone from entries_).
            rebuild();
            removed_count_ = 0;
            return;
        }
    }
}

void BvhSpatialIndex::query_sphere(Vec3 center, float radius,
                                    std::vector<uint32_t>& results) const {
    if (root_ < 0) return;
    query_sphere_recursive(root_, center, radius, results);
}

void BvhSpatialIndex::query_frustum(const float planes[6][4],
                                     std::vector<uint32_t>& results) const {
    if (root_ < 0) return;
    query_frustum_recursive(root_, planes, results);
}

std::optional<RaycastHit> BvhSpatialIndex::raycast(Vec3 origin, Vec3 dir,
                                                    float max_dist) const {
    if (root_ < 0) return std::nullopt;

    // Precompute inverse direction for the slab test.
    // Use a large value instead of infinity for zero components to avoid NaN.
    constexpr float kBig = 1e30f;
    const Vec3 inv_dir = {
        (std::abs(dir.x) > kEpsilon) ? 1.0f / dir.x : ((dir.x >= 0.0f) ? kBig : -kBig),
        (std::abs(dir.y) > kEpsilon) ? 1.0f / dir.y : ((dir.y >= 0.0f) ? kBig : -kBig),
        (std::abs(dir.z) > kEpsilon) ? 1.0f / dir.z : ((dir.z >= 0.0f) ? kBig : -kBig),
        0.0f
    };

    RaycastHit nearest_hit{};
    float nearest_dist = max_dist;
    bool has_hit = false;

    raycast_recursive(root_, origin, inv_dir, max_dist, nearest_dist, nearest_hit, has_hit);

    if (has_hit) return nearest_hit;
    return std::nullopt;
}

void BvhSpatialIndex::clear() {
    entries_.clear();
    nodes_.clear();
    removed_count_ = 0;
    root_ = -1;
}

size_t BvhSpatialIndex::count() const {
    return entries_.size();
}

// ============================================================================
// BVH construction
// ============================================================================

void BvhSpatialIndex::rebuild() {
    nodes_.clear();
    root_ = -1;

    if (entries_.empty()) return;

    // Create index array [0, 1, 2, ..., N-1]
    std::vector<int32_t> indices(entries_.size());
    std::iota(indices.begin(), indices.end(), 0);

    root_ = build_recursive(indices, 0, static_cast<int32_t>(indices.size()));
}

int32_t BvhSpatialIndex::build_recursive(std::vector<int32_t>& indices,
                                          int32_t begin, int32_t end) {
    const int32_t count_here = end - begin;

    // Leaf node: single entry.
    if (count_here == 1) {
        const auto& entry = entries_[static_cast<size_t>(indices[static_cast<size_t>(begin)])];
        BvhNode node{};
        node.bounds    = entry.bounds;
        node.left      = -1;
        node.right     = -1;
        node.entity_id = entry.entity_id;
        nodes_.push_back(node);
        return static_cast<int32_t>(nodes_.size()) - 1;
    }

    // Compute bounding box of all entries in [begin, end).
    Aabb total_bounds = Aabb::empty();
    for (int32_t i = begin; i < end; ++i) {
        const auto& b = entries_[static_cast<size_t>(indices[static_cast<size_t>(i)])].bounds;
        total_bounds.merge(b.min);
        total_bounds.merge(b.max);
    }

    // Choose split axis: the one with the largest extent.
    const float ex = total_bounds.max.x - total_bounds.min.x;
    const float ey = total_bounds.max.y - total_bounds.min.y;
    const float ez = total_bounds.max.z - total_bounds.min.z;

    int axis = 0;
    if (ey > ex && ey > ez) axis = 1;
    else if (ez > ex && ez > ey) axis = 2;

    // Sort by centroid along the chosen axis.
    auto centroid = [this](int32_t idx, int ax) -> float {
        const auto& b = entries_[static_cast<size_t>(idx)].bounds;
        switch (ax) {
            case 0: return (b.min.x + b.max.x) * 0.5f;
            case 1: return (b.min.y + b.max.y) * 0.5f;
            default: return (b.min.z + b.max.z) * 0.5f;
        }
    };

    std::sort(
        indices.begin() + begin,
        indices.begin() + end,
        [&](int32_t a, int32_t b) { return centroid(a, axis) < centroid(b, axis); }
    );

    // Median split.
    const int32_t mid = begin + count_here / 2;

    // Reserve a slot for the internal node (index known before recursing).
    const int32_t node_idx = static_cast<int32_t>(nodes_.size());
    nodes_.push_back({}); // placeholder

    const int32_t left_idx  = build_recursive(indices, begin, mid);
    const int32_t right_idx = build_recursive(indices, mid, end);

    BvhNode& node = nodes_[static_cast<size_t>(node_idx)];
    node.bounds    = total_bounds;
    node.left      = left_idx;
    node.right     = right_idx;
    node.entity_id = 0;

    return node_idx;
}

// ============================================================================
// Recursive query: sphere
// ============================================================================

void BvhSpatialIndex::query_sphere_recursive(int32_t node_idx, Vec3 center,
                                              float radius,
                                              std::vector<uint32_t>& results) const {
    const auto& node = nodes_[static_cast<size_t>(node_idx)];

    if (!aabb_intersects_sphere(node.bounds, center, radius)) return;

    // Leaf
    if (node.left == -1 && node.right == -1) {
        results.push_back(node.entity_id);
        return;
    }

    if (node.left >= 0)  query_sphere_recursive(node.left, center, radius, results);
    if (node.right >= 0) query_sphere_recursive(node.right, center, radius, results);
}

// ============================================================================
// Recursive query: frustum
// ============================================================================

void BvhSpatialIndex::query_frustum_recursive(int32_t node_idx,
                                               const float planes[6][4],
                                               std::vector<uint32_t>& results) const {
    const auto& node = nodes_[static_cast<size_t>(node_idx)];

    // Test AABB against all 6 frustum planes. If the AABB is fully behind
    // any single plane, it is completely outside the frustum.
    for (int p = 0; p < 6; ++p) {
        if (aabb_vs_plane(node.bounds, planes[p][0], planes[p][1],
                          planes[p][2], planes[p][3]) < 0) {
            return; // Fully outside this plane
        }
    }

    // Leaf
    if (node.left == -1 && node.right == -1) {
        results.push_back(node.entity_id);
        return;
    }

    if (node.left >= 0)  query_frustum_recursive(node.left, planes, results);
    if (node.right >= 0) query_frustum_recursive(node.right, planes, results);
}

// ============================================================================
// Recursive query: raycast
// ============================================================================

void BvhSpatialIndex::raycast_recursive(int32_t node_idx, Vec3 origin, Vec3 inv_dir,
                                         float max_dist, float& nearest_dist,
                                         RaycastHit& nearest_hit,
                                         bool& has_hit) const {
    const auto& node = nodes_[static_cast<size_t>(node_idx)];

    const float t = aabb_ray_intersect(node.bounds, origin, inv_dir, nearest_dist);
    if (t < 0.0f) return;

    // Leaf: record hit if closer than current nearest.
    if (node.left == -1 && node.right == -1) {
        if (t < nearest_dist) {
            nearest_dist = t;
            nearest_hit.entity_id = node.entity_id;
            nearest_hit.distance  = t;
            nearest_hit.hit_point = {
                origin.x + t * (1.0f / inv_dir.x),
                origin.y + t * (1.0f / inv_dir.y),
                origin.z + t * (1.0f / inv_dir.z),
                0.0f
            };

            // Recompute hit point from original direction to avoid div-by-zero
            // when inv_dir was clamped to kBig. We stored inv_dir, so
            // direction = 1/inv_dir only if inv_dir != kBig. Safer to store:
            // hit = origin + t * dir, but we don't have dir here. Instead,
            // the caller already normalised dir, so 1/inv_dir is safe for
            // non-zero components. For zero-direction components the position
            // is just origin.{x,y,z}.
            has_hit = true;
        }
        return;
    }

    if (node.left >= 0)
        raycast_recursive(node.left, origin, inv_dir, max_dist, nearest_dist, nearest_hit, has_hit);
    if (node.right >= 0)
        raycast_recursive(node.right, origin, inv_dir, max_dist, nearest_dist, nearest_hit, has_hit);
}

} // namespace uz
