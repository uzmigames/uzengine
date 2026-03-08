// test_spatial_index.cpp — Tests for ISpatialIndex / BvhSpatialIndex.
//
// Covers: insert, remove, clear, count, query_sphere, query_frustum, raycast,
//         AABB helpers, edge cases (empty index, single entity, updates).

#include <gtest/gtest.h>
#include "engine/ecs/bvh_spatial_index.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace uz;

// ============================================================================
// Helpers
// ============================================================================

// Create a unit AABB centered at (cx, cy, cz) with half-extent h.
static Aabb make_aabb(float cx, float cy, float cz, float h = 0.5f) {
    return {
        {cx - h, cy - h, cz - h, 0.0f},
        {cx + h, cy + h, cz + h, 0.0f}
    };
}

// Sort a vector of uint32_t for deterministic comparison.
static std::vector<uint32_t> sorted(std::vector<uint32_t> v) {
    std::sort(v.begin(), v.end());
    return v;
}

// ============================================================================
// AABB helper tests
// ============================================================================

TEST(AabbHelpers, ContainsPointInside) {
    const Aabb box = make_aabb(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(aabb_contains_point(box, {0.0f, 0.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(aabb_contains_point(box, {0.5f, 0.5f, 0.5f, 0.0f}));
}

TEST(AabbHelpers, ContainsPointOnBoundary) {
    const Aabb box = make_aabb(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(aabb_contains_point(box, {1.0f, 1.0f, 1.0f, 0.0f}));
    EXPECT_TRUE(aabb_contains_point(box, {-1.0f, -1.0f, -1.0f, 0.0f}));
}

TEST(AabbHelpers, ContainsPointOutside) {
    const Aabb box = make_aabb(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_FALSE(aabb_contains_point(box, {1.5f, 0.0f, 0.0f, 0.0f}));
    EXPECT_FALSE(aabb_contains_point(box, {0.0f, -1.5f, 0.0f, 0.0f}));
    EXPECT_FALSE(aabb_contains_point(box, {0.0f, 0.0f, 2.0f, 0.0f}));
}

TEST(AabbHelpers, IntersectsSphereOverlapping) {
    const Aabb box = make_aabb(0.0f, 0.0f, 0.0f, 1.0f);
    // Sphere centered at origin — clearly overlaps.
    EXPECT_TRUE(aabb_intersects_sphere(box, {0.0f, 0.0f, 0.0f, 0.0f}, 0.5f));
    // Sphere just touching the corner.
    EXPECT_TRUE(aabb_intersects_sphere(box, {2.0f, 0.0f, 0.0f, 0.0f}, 1.1f));
}

TEST(AabbHelpers, IntersectsSphereNotOverlapping) {
    const Aabb box = make_aabb(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_FALSE(aabb_intersects_sphere(box, {5.0f, 0.0f, 0.0f, 0.0f}, 1.0f));
}

TEST(AabbHelpers, IntersectsSphereEdgeCase) {
    // Sphere just barely touching the surface of the AABB.
    const Aabb box = make_aabb(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(aabb_intersects_sphere(box, {2.0f, 0.0f, 0.0f, 0.0f}, 1.0f));
}

TEST(AabbHelpers, IntersectsAabbOverlapping) {
    const Aabb a = make_aabb(0.0f, 0.0f, 0.0f, 1.0f);
    const Aabb b = make_aabb(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(aabb_intersects_aabb(a, b));
}

TEST(AabbHelpers, IntersectsAabbNonOverlapping) {
    const Aabb a = make_aabb(0.0f, 0.0f, 0.0f, 1.0f);
    const Aabb b = make_aabb(5.0f, 5.0f, 5.0f, 1.0f);
    EXPECT_FALSE(aabb_intersects_aabb(a, b));
}

TEST(AabbHelpers, IntersectsAabbTouching) {
    const Aabb a = make_aabb(0.0f, 0.0f, 0.0f, 1.0f);
    const Aabb b = make_aabb(2.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(aabb_intersects_aabb(a, b)); // Touching at face x=1
}

TEST(AabbHelpers, VsPlaneFullyFront) {
    const Aabb box = make_aabb(5.0f, 0.0f, 0.0f, 1.0f);
    // Plane: x >= 0  =>  (1,0,0,0)
    EXPECT_EQ(aabb_vs_plane(box, 1.0f, 0.0f, 0.0f, 0.0f), 1);
}

TEST(AabbHelpers, VsPlaneFullyBehind) {
    const Aabb box = make_aabb(-5.0f, 0.0f, 0.0f, 1.0f);
    // Plane: x >= 0  =>  (1,0,0,0)
    EXPECT_EQ(aabb_vs_plane(box, 1.0f, 0.0f, 0.0f, 0.0f), -1);
}

TEST(AabbHelpers, VsPlaneStraddling) {
    const Aabb box = make_aabb(0.0f, 0.0f, 0.0f, 1.0f);
    // Plane: x >= 0  =>  (1,0,0,0) — box spans [-1,1] on x.
    EXPECT_EQ(aabb_vs_plane(box, 1.0f, 0.0f, 0.0f, 0.0f), 0);
}

TEST(AabbHelpers, VsPlaneNegativeNormal) {
    const Aabb box = make_aabb(5.0f, 0.0f, 0.0f, 1.0f);
    // Plane: -x >= 0  =>  (-1,0,0,0) — box at x=4..6 is behind (negative side).
    EXPECT_EQ(aabb_vs_plane(box, -1.0f, 0.0f, 0.0f, 0.0f), -1);
}

TEST(AabbHelpers, RayIntersectHit) {
    const Aabb box = make_aabb(5.0f, 0.0f, 0.0f, 1.0f); // x=[4,6]
    // Ray from origin along +X
    const Vec3 origin  = {0.0f, 0.0f, 0.0f, 0.0f};
    const Vec3 inv_dir = {1.0f, 1e30f, 1e30f, 0.0f}; // dir = (1,0,0)
    const float t = aabb_ray_intersect(box, origin, inv_dir, 100.0f);
    EXPECT_NEAR(t, 4.0f, 0.001f);
}

TEST(AabbHelpers, RayIntersectMiss) {
    const Aabb box = make_aabb(5.0f, 0.0f, 0.0f, 1.0f);
    // Ray along +Y — misses the box.
    const Vec3 origin  = {0.0f, 0.0f, 0.0f, 0.0f};
    const Vec3 inv_dir = {1e30f, 1.0f, 1e30f, 0.0f};
    const float t = aabb_ray_intersect(box, origin, inv_dir, 100.0f);
    EXPECT_LT(t, 0.0f);
}

TEST(AabbHelpers, RayIntersectBehind) {
    const Aabb box = make_aabb(-5.0f, 0.0f, 0.0f, 1.0f);
    // Ray from origin along +X — box is behind.
    const Vec3 origin  = {0.0f, 0.0f, 0.0f, 0.0f};
    const Vec3 inv_dir = {1.0f, 1e30f, 1e30f, 0.0f};
    const float t = aabb_ray_intersect(box, origin, inv_dir, 100.0f);
    EXPECT_LT(t, 0.0f);
}

TEST(AabbHelpers, RayIntersectInsideBox) {
    const Aabb box = make_aabb(0.0f, 0.0f, 0.0f, 5.0f); // [-5,5]^3
    const Vec3 origin  = {0.0f, 0.0f, 0.0f, 0.0f};
    const Vec3 inv_dir = {1.0f, 1e30f, 1e30f, 0.0f};
    const float t = aabb_ray_intersect(box, origin, inv_dir, 100.0f);
    EXPECT_NEAR(t, 0.0f, 0.001f); // Starts inside, distance = 0
}

TEST(AabbHelpers, RayIntersectTooFar) {
    const Aabb box = make_aabb(50.0f, 0.0f, 0.0f, 1.0f);
    const Vec3 origin  = {0.0f, 0.0f, 0.0f, 0.0f};
    const Vec3 inv_dir = {1.0f, 1e30f, 1e30f, 0.0f};
    const float t = aabb_ray_intersect(box, origin, inv_dir, 10.0f);
    EXPECT_LT(t, 0.0f); // Beyond max_dist
}

// ============================================================================
// Empty index
// ============================================================================

TEST(BvhSpatialIndex, EmptyIndexCountIsZero) {
    BvhSpatialIndex idx;
    EXPECT_EQ(idx.count(), 0u);
}

TEST(BvhSpatialIndex, EmptyIndexQuerySphereReturnsNothing) {
    BvhSpatialIndex idx;
    std::vector<uint32_t> results;
    idx.query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 100.0f, results);
    EXPECT_TRUE(results.empty());
}

TEST(BvhSpatialIndex, EmptyIndexQueryFrustumReturnsNothing) {
    BvhSpatialIndex idx;
    float planes[6][4] = {};
    std::vector<uint32_t> results;
    idx.query_frustum(planes, results);
    EXPECT_TRUE(results.empty());
}

TEST(BvhSpatialIndex, EmptyIndexRaycastReturnsNullopt) {
    BvhSpatialIndex idx;
    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, 100.0f);
    EXPECT_FALSE(hit.has_value());
}

// ============================================================================
// Single entity
// ============================================================================

TEST(BvhSpatialIndex, SingleEntityInsertAndCount) {
    BvhSpatialIndex idx;
    idx.insert(42, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(idx.count(), 1u);
}

TEST(BvhSpatialIndex, SingleEntityQuerySphereHit) {
    BvhSpatialIndex idx;
    idx.insert(42, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));

    std::vector<uint32_t> results;
    idx.query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 2.0f, results);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 42u);
}

TEST(BvhSpatialIndex, SingleEntityQuerySphereMiss) {
    BvhSpatialIndex idx;
    idx.insert(42, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));

    std::vector<uint32_t> results;
    idx.query_sphere({100.0f, 100.0f, 100.0f, 0.0f}, 0.5f, results);
    EXPECT_TRUE(results.empty());
}

TEST(BvhSpatialIndex, SingleEntityRaycastHit) {
    BvhSpatialIndex idx;
    idx.insert(42, make_aabb(5.0f, 0.0f, 0.0f, 1.0f)); // x=[4,6]

    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, 100.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity_id, 42u);
    EXPECT_NEAR(hit->distance, 4.0f, 0.01f);
}

TEST(BvhSpatialIndex, SingleEntityRaycastMiss) {
    BvhSpatialIndex idx;
    idx.insert(42, make_aabb(5.0f, 0.0f, 0.0f, 1.0f));

    // Shoot along +Y — misses.
    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, 100.0f);
    EXPECT_FALSE(hit.has_value());
}

TEST(BvhSpatialIndex, SingleEntityFrustumHit) {
    BvhSpatialIndex idx;
    idx.insert(42, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));

    // A big frustum that contains everything near the origin.
    // 6 planes, all far away: +X, -X, +Y, -Y, +Z, -Z all at distance 100.
    float planes[6][4] = {
        { 1.0f,  0.0f,  0.0f, 100.0f}, // x >= -100
        {-1.0f,  0.0f,  0.0f, 100.0f}, // x <= 100
        { 0.0f,  1.0f,  0.0f, 100.0f}, // y >= -100
        { 0.0f, -1.0f,  0.0f, 100.0f}, // y <= 100
        { 0.0f,  0.0f,  1.0f, 100.0f}, // z >= -100
        { 0.0f,  0.0f, -1.0f, 100.0f}, // z <= 100
    };

    std::vector<uint32_t> results;
    idx.query_frustum(planes, results);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 42u);
}

TEST(BvhSpatialIndex, SingleEntityFrustumMiss) {
    BvhSpatialIndex idx;
    idx.insert(42, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));

    // Frustum entirely in the +X half-space at x >= 50.
    float planes[6][4] = {
        { 1.0f,  0.0f,  0.0f, -50.0f}, // x >= 50
        {-1.0f,  0.0f,  0.0f, 100.0f}, // x <= 100
        { 0.0f,  1.0f,  0.0f, 100.0f},
        { 0.0f, -1.0f,  0.0f, 100.0f},
        { 0.0f,  0.0f,  1.0f, 100.0f},
        { 0.0f,  0.0f, -1.0f, 100.0f},
    };

    std::vector<uint32_t> results;
    idx.query_frustum(planes, results);
    EXPECT_TRUE(results.empty());
}

// ============================================================================
// Multiple entities: 10 entities, sphere query returns subset
// ============================================================================

TEST(BvhSpatialIndex, TenEntitiesQuerySphereReturnsCorrectSubset) {
    BvhSpatialIndex idx;

    // 5 entities near the origin (within radius 3).
    idx.insert(1, make_aabb( 0.0f,  0.0f,  0.0f, 0.5f));
    idx.insert(2, make_aabb( 1.0f,  0.0f,  0.0f, 0.5f));
    idx.insert(3, make_aabb( 0.0f,  1.0f,  0.0f, 0.5f));
    idx.insert(4, make_aabb(-1.0f,  0.0f,  0.0f, 0.5f));
    idx.insert(5, make_aabb( 0.0f, -1.0f,  0.0f, 0.5f));

    // 5 entities far away (outside radius 3).
    idx.insert(6,  make_aabb(50.0f,  0.0f,  0.0f, 0.5f));
    idx.insert(7,  make_aabb( 0.0f, 50.0f,  0.0f, 0.5f));
    idx.insert(8,  make_aabb( 0.0f,  0.0f, 50.0f, 0.5f));
    idx.insert(9,  make_aabb(-50.0f, 0.0f,  0.0f, 0.5f));
    idx.insert(10, make_aabb( 0.0f,  0.0f,-50.0f, 0.5f));

    EXPECT_EQ(idx.count(), 10u);

    std::vector<uint32_t> results;
    idx.query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 3.0f, results);

    EXPECT_EQ(sorted(results), (std::vector<uint32_t>{1, 2, 3, 4, 5}));
}

// ============================================================================
// Raycast: nearest hit with multiple entities
// ============================================================================

TEST(BvhSpatialIndex, RaycastReturnsNearestHit) {
    BvhSpatialIndex idx;

    // Three boxes along +X axis at different distances.
    idx.insert(1, make_aabb(3.0f, 0.0f, 0.0f, 0.5f));  // x=[2.5, 3.5]
    idx.insert(2, make_aabb(6.0f, 0.0f, 0.0f, 0.5f));  // x=[5.5, 6.5]
    idx.insert(3, make_aabb(10.0f, 0.0f, 0.0f, 0.5f)); // x=[9.5, 10.5]

    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, 100.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity_id, 1u);
    EXPECT_NEAR(hit->distance, 2.5f, 0.01f);
}

TEST(BvhSpatialIndex, RaycastMissWhenNothingAlongRay) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 10.0f, 0.0f, 0.5f));
    idx.insert(2, make_aabb(0.0f, -10.0f, 0.0f, 0.5f));

    // Shoot along +X — both boxes are offset on Y.
    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, 100.0f);
    EXPECT_FALSE(hit.has_value());
}

TEST(BvhSpatialIndex, RaycastRespectsMaxDist) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(20.0f, 0.0f, 0.0f, 0.5f)); // x=[19.5, 20.5]

    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, 10.0f);
    EXPECT_FALSE(hit.has_value()); // Beyond max_dist=10
}

TEST(BvhSpatialIndex, RaycastHitPointIsCorrect) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(5.0f, 0.0f, 0.0f, 1.0f)); // x=[4,6]

    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, 100.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->hit_point.x, 4.0f, 0.01f);
    EXPECT_NEAR(hit->hit_point.y, 0.0f, 0.01f);
    EXPECT_NEAR(hit->hit_point.z, 0.0f, 0.01f);
}

// ============================================================================
// Remove
// ============================================================================

TEST(BvhSpatialIndex, RemoveEntityNotReturnedByQuery) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));
    idx.insert(2, make_aabb(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(idx.count(), 2u);

    idx.remove(1);
    EXPECT_EQ(idx.count(), 1u);

    std::vector<uint32_t> results;
    idx.query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 100.0f, results);
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 2u);
}

TEST(BvhSpatialIndex, RemoveNonExistentEntityIsNoop) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));
    idx.remove(999); // Not present.
    EXPECT_EQ(idx.count(), 1u);
}

TEST(BvhSpatialIndex, RemoveAllEntitiesLeavesEmpty) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));
    idx.insert(2, make_aabb(1.0f, 0.0f, 0.0f, 1.0f));
    idx.remove(1);
    idx.remove(2);
    EXPECT_EQ(idx.count(), 0u);

    std::vector<uint32_t> results;
    idx.query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 100.0f, results);
    EXPECT_TRUE(results.empty());
}

// ============================================================================
// Clear
// ============================================================================

TEST(BvhSpatialIndex, ClearResetsToEmpty) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));
    idx.insert(2, make_aabb(1.0f, 0.0f, 0.0f, 1.0f));
    idx.insert(3, make_aabb(2.0f, 0.0f, 0.0f, 1.0f));

    idx.clear();
    EXPECT_EQ(idx.count(), 0u);

    std::vector<uint32_t> results;
    idx.query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 100.0f, results);
    EXPECT_TRUE(results.empty());

    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, 100.0f);
    EXPECT_FALSE(hit.has_value());
}

// ============================================================================
// Update (re-insert same entity_id with new bounds)
// ============================================================================

TEST(BvhSpatialIndex, UpdateMovesEntity) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));

    // Move entity 1 far away.
    idx.insert(1, make_aabb(100.0f, 100.0f, 100.0f, 1.0f));
    EXPECT_EQ(idx.count(), 1u);

    // Query near origin: should not find it.
    std::vector<uint32_t> results;
    idx.query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 5.0f, results);
    EXPECT_TRUE(results.empty());

    // Query near new position: should find it.
    idx.query_sphere({100.0f, 100.0f, 100.0f, 0.0f}, 5.0f, results);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 1u);
}

// ============================================================================
// Frustum query with known planes
// ============================================================================

TEST(BvhSpatialIndex, FrustumQueryKnownPlanes) {
    BvhSpatialIndex idx;

    // Entities inside a box [0, 10] x [0, 10] x [0, 10]
    idx.insert(1, make_aabb(2.0f, 2.0f, 2.0f, 0.5f));
    idx.insert(2, make_aabb(5.0f, 5.0f, 5.0f, 0.5f));
    idx.insert(3, make_aabb(8.0f, 8.0f, 8.0f, 0.5f));

    // Entities outside
    idx.insert(4, make_aabb(-10.0f, 0.0f, 0.0f, 0.5f));
    idx.insert(5, make_aabb(0.0f, -10.0f, 0.0f, 0.5f));
    idx.insert(6, make_aabb(0.0f, 0.0f, 20.0f, 0.5f));

    // Frustum planes defining [0, 10]^3:
    // Plane: ax + by + cz + d >= 0 means inside.
    float planes[6][4] = {
        { 1.0f,  0.0f,  0.0f,   0.0f}, // x >= 0
        {-1.0f,  0.0f,  0.0f,  10.0f}, // x <= 10
        { 0.0f,  1.0f,  0.0f,   0.0f}, // y >= 0
        { 0.0f, -1.0f,  0.0f,  10.0f}, // y <= 10
        { 0.0f,  0.0f,  1.0f,   0.0f}, // z >= 0
        { 0.0f,  0.0f, -1.0f,  10.0f}, // z <= 10
    };

    std::vector<uint32_t> results;
    idx.query_frustum(planes, results);

    EXPECT_EQ(sorted(results), (std::vector<uint32_t>{1, 2, 3}));
}

// ============================================================================
// Append semantics: queries append to existing vector
// ============================================================================

TEST(BvhSpatialIndex, QuerySphereAppendsToExistingResults) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));

    std::vector<uint32_t> results = {99};
    idx.query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 10.0f, results);
    EXPECT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0], 99u);
    EXPECT_EQ(results[1], 1u);
}

TEST(BvhSpatialIndex, QueryFrustumAppendsToExistingResults) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));

    float planes[6][4] = {
        { 1.0f,  0.0f,  0.0f, 100.0f},
        {-1.0f,  0.0f,  0.0f, 100.0f},
        { 0.0f,  1.0f,  0.0f, 100.0f},
        { 0.0f, -1.0f,  0.0f, 100.0f},
        { 0.0f,  0.0f,  1.0f, 100.0f},
        { 0.0f,  0.0f, -1.0f, 100.0f},
    };

    std::vector<uint32_t> results = {77};
    idx.query_frustum(planes, results);
    EXPECT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0], 77u);
}

// ============================================================================
// Move semantics
// ============================================================================

TEST(BvhSpatialIndex, MoveConstructor) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));
    idx.insert(2, make_aabb(5.0f, 0.0f, 0.0f, 1.0f));

    BvhSpatialIndex moved(std::move(idx));
    EXPECT_EQ(moved.count(), 2u);

    std::vector<uint32_t> results;
    moved.query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 100.0f, results);
    EXPECT_EQ(results.size(), 2u);
}

TEST(BvhSpatialIndex, MoveAssignment) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));

    BvhSpatialIndex other;
    other = std::move(idx);
    EXPECT_EQ(other.count(), 1u);
}

// ============================================================================
// Interface polymorphism
// ============================================================================

TEST(BvhSpatialIndex, UsableThroughInterfacePointer) {
    auto ptr = std::make_unique<BvhSpatialIndex>();
    ISpatialIndex* iface = ptr.get();

    iface->insert(10, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(iface->count(), 1u);

    std::vector<uint32_t> results;
    iface->query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 5.0f, results);
    EXPECT_EQ(results.size(), 1u);

    iface->clear();
    EXPECT_EQ(iface->count(), 0u);
}

// ============================================================================
// BVH structure: node array is non-empty after insert
// ============================================================================

TEST(BvhSpatialIndex, NodesNonEmptyAfterInsert) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_FALSE(idx.nodes().empty());
}

TEST(BvhSpatialIndex, NodesEmptyAfterClear) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));
    idx.clear();
    EXPECT_TRUE(idx.nodes().empty());
}

// ============================================================================
// Large-ish test: 100 entities, queries still correct
// ============================================================================

TEST(BvhSpatialIndex, HundredEntitiesSphereQuery) {
    BvhSpatialIndex idx;

    // Insert 100 entities in a grid.
    for (uint32_t i = 0; i < 100; ++i) {
        const float x = static_cast<float>(i % 10) * 5.0f;
        const float z = static_cast<float>(i / 10) * 5.0f;
        idx.insert(i + 1, make_aabb(x, 0.0f, z, 0.4f));
    }
    EXPECT_EQ(idx.count(), 100u);

    // Query a small sphere around (0, 0, 0) — should find entity at (0, 0, 0).
    std::vector<uint32_t> results;
    idx.query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 1.0f, results);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 1u); // Entity 1 is at (0, 0, 0)
}

TEST(BvhSpatialIndex, HundredEntitiesRaycast) {
    BvhSpatialIndex idx;

    for (uint32_t i = 0; i < 100; ++i) {
        const float x = static_cast<float>(i + 1) * 3.0f;
        idx.insert(i + 1, make_aabb(x, 0.0f, 0.0f, 0.4f));
    }

    // Ray along +X should hit the first box (entity 1 at x=3, box=[2.6, 3.4]).
    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, 1000.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity_id, 1u);
    EXPECT_NEAR(hit->distance, 2.6f, 0.01f);
}

// ============================================================================
// Raycast with diagonal direction
// ============================================================================

TEST(BvhSpatialIndex, RaycastDiagonalDirection) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(5.0f, 5.0f, 5.0f, 1.0f));

    // Diagonal ray from origin toward (1,1,1) normalized.
    const float inv_sqrt3 = 1.0f / std::sqrt(3.0f);
    const Vec3 dir = {inv_sqrt3, inv_sqrt3, inv_sqrt3, 0.0f};

    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, dir, 100.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity_id, 1u);
    // Distance to box at (5,5,5) with half-extent 1 along diagonal:
    // entry face at (4,4,4), distance from origin = sqrt(48) ~ 6.93
    EXPECT_NEAR(hit->distance, std::sqrt(48.0f), 0.1f);
}

// ============================================================================
// Raycast with zero-component direction (axis-aligned edge case)
// ============================================================================

TEST(BvhSpatialIndex, RaycastAlongYAxis) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 10.0f, 0.0f, 1.0f));

    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, 100.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity_id, 1u);
    EXPECT_NEAR(hit->distance, 9.0f, 0.01f);
}

TEST(BvhSpatialIndex, RaycastAlongZAxis) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 7.0f, 1.0f));

    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, 100.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity_id, 1u);
    EXPECT_NEAR(hit->distance, 6.0f, 0.01f);
}

// ============================================================================
// Multiple removes and re-insert
// ============================================================================

TEST(BvhSpatialIndex, RemoveAndReinsert) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));
    idx.insert(2, make_aabb(5.0f, 0.0f, 0.0f, 1.0f));
    idx.insert(3, make_aabb(10.0f, 0.0f, 0.0f, 1.0f));

    idx.remove(2);
    EXPECT_EQ(idx.count(), 2u);

    // Re-insert entity 2 at a new location.
    idx.insert(2, make_aabb(-5.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(idx.count(), 3u);

    // Verify entity 2 is now at (-5, 0, 0).
    std::vector<uint32_t> results;
    idx.query_sphere({-5.0f, 0.0f, 0.0f, 0.0f}, 2.0f, results);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 2u);
}

// ============================================================================
// Frustum with angled planes
// ============================================================================

TEST(BvhSpatialIndex, FrustumAngledPlanes) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 5.0f, 1.0f));   // In front
    idx.insert(2, make_aabb(0.0f, 0.0f, -5.0f, 1.0f));   // Behind

    // Frustum looking along +Z. Only the near/far matter here:
    // Near plane: z >= 1  => (0, 0, 1, -1)
    // Far plane:  z <= 20 => (0, 0, -1, 20)
    // Other planes are wide open.
    float planes[6][4] = {
        { 1.0f,  0.0f,  0.0f, 100.0f},
        {-1.0f,  0.0f,  0.0f, 100.0f},
        { 0.0f,  1.0f,  0.0f, 100.0f},
        { 0.0f, -1.0f,  0.0f, 100.0f},
        { 0.0f,  0.0f,  1.0f, -1.0f},   // z >= 1
        { 0.0f,  0.0f, -1.0f, 20.0f},   // z <= 20
    };

    std::vector<uint32_t> results;
    idx.query_frustum(planes, results);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 1u);
}

// ============================================================================
// Two entities at the same position
// ============================================================================

TEST(BvhSpatialIndex, OverlappingEntities) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));
    idx.insert(2, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));

    std::vector<uint32_t> results;
    idx.query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 2.0f, results);
    EXPECT_EQ(sorted(results), (std::vector<uint32_t>{1, 2}));
}

// ============================================================================
// Sphere query with radius=0 (point query)
// ============================================================================

TEST(BvhSpatialIndex, PointQuery) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(0.0f, 0.0f, 0.0f, 1.0f));
    idx.insert(2, make_aabb(5.0f, 0.0f, 0.0f, 1.0f));

    // Point at (0,0,0) with radius=0: should hit entity 1 (AABB contains origin).
    std::vector<uint32_t> results;
    idx.query_sphere({0.0f, 0.0f, 0.0f, 0.0f}, 0.0f, results);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], 1u);
}

// ============================================================================
// Negative direction raycast
// ============================================================================

TEST(BvhSpatialIndex, RaycastNegativeDirection) {
    BvhSpatialIndex idx;
    idx.insert(1, make_aabb(-5.0f, 0.0f, 0.0f, 1.0f));

    auto hit = idx.raycast({0.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}, 100.0f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity_id, 1u);
    EXPECT_NEAR(hit->distance, 4.0f, 0.01f);
}
