/// Unit tests for DecalManager.
///
/// Covers: Initialize/Shutdown lifecycle (including double-init, shutdown of
/// uninitialized), IsInitialized flag, SpawnDecal (valid asset returns non-null),
/// SpawnDecal on uninitialized manager, SpawnDecal on exhausted pool, Update(dt)
/// ticks all decals, Cleanup removes expired decals, GetActiveCount and
/// GetPoolAvailable accuracy, GetSortedDecals ordering, GetAtlasOccupancy,
/// FrustumCull (inside/outside frustum), multiple spawn/update/cleanup cycles,
/// and rotation alignment to surface normal.
///
/// DecalManager::Initialize accepts nullptr device -- no GPU hardware needed.

#include <gtest/gtest.h>

#include "engine/math/mat4.h"
#include "engine/math/vector3.h"
#include "engine/vfx/decals/decal_asset.h"
#include "engine/vfx/decals/decal_component.h"
#include "engine/vfx/decals/decal_manager.h"

#include <cmath>
#include <cstdint>
#include <string>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Frustum helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a wide-open frustum (90-degree FOV) centered on a camera looking
/// down +Z from position (0, 0, -10). Any point within ~50 units of the origin
/// on the +Z side passes the sphere test.
Mat4 MakeWideOpenFrustum() {
    const Mat4 view = Mat4::LookAt(Vec3{0.0f, 0.0f, -10.0f},
                                   Vec3{0.0f, 0.0f,   0.0f},
                                   Vec3{0.0f, 1.0f,   0.0f});
    const float fov  = 1.5707963f; // pi/2  — 90 degree vertical FOV
    const Mat4  proj = Mat4::Perspective(fov, 1.0f, 0.1f, 1000.0f);
    return view * proj;
}

/// Returns a frustum pointing strictly away from the world origin
/// (camera at z=-10000, looking toward z=-20000). A sphere at the origin
/// will be entirely behind this frustum's near plane.
Mat4 MakeFrustumAwayFromOrigin() {
    const Mat4 view = Mat4::LookAt(Vec3{0.0f, 0.0f, -10000.0f},
                                   Vec3{0.0f, 0.0f, -20000.0f},
                                   Vec3{0.0f, 1.0f,    0.0f});
    const float fov  = 0.5f; // narrow
    const Mat4  proj = Mat4::Perspective(fov, 1.0f, 1.0f, 100.0f);
    return view * proj;
}

// ─────────────────────────────────────────────────────────────────────────────
// Minimal valid .uzdecal JSON for test assets
// ─────────────────────────────────────────────────────────────────────────────

constexpr const char* kValidDecalJson = R"({
    "format": "uzdecal",
    "version": 1,
    "material": "materials/test_decal.uzmat",
    "blend_mode": "Opaque",
    "lifetime": 10.0,
    "fade_duration": 2.0,
    "normal_intensity": 0.8,
    "roughness_override": -1.0,
    "sort_order": 0,
    "half_extents": [0.5, 0.5, 0.2]
})";

/// Loads a DecalAsset from the standard valid JSON string.
std::unique_ptr<DecalAsset> MakeTestAsset() {
    return DecalAsset::LoadFromString(kValidDecalJson);
}

/// Default surface normal pointing straight up.
const Vec3 kUpNormal{0.0f, 1.0f, 0.0f};

/// A world-space hit position used across spawn tests.
const Vec3 kHitPos{10.0f, 0.0f, 5.0f};

} // anonymous namespace

// =============================================================================
// Lifecycle — Initialize / Shutdown
// =============================================================================

TEST(DecalManager, IsInitialized_FalseBeforeInitialize) {
    DecalManager mgr;
    EXPECT_FALSE(mgr.IsInitialized());
}

TEST(DecalManager, IsInitialized_TrueAfterInitialize) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 100, 512);
    EXPECT_TRUE(mgr.IsInitialized());
    mgr.Shutdown(nullptr);
}

TEST(DecalManager, IsInitialized_FalseAfterShutdown) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 100, 512);
    mgr.Shutdown(nullptr);
    EXPECT_FALSE(mgr.IsInitialized());
}

TEST(DecalManager, Initialize_WithNullDevice_DoesNotCrash) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 50, 256);
    EXPECT_TRUE(mgr.IsInitialized());
    mgr.Shutdown(nullptr);
}

TEST(DecalManager, Initialize_AlreadyInitialized_IsNoOp_DoesNotCrash) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 50, 256);
    // Second Initialize must be a no-op (logged as warning).
    mgr.Initialize(nullptr, 100, 2048);
    // Pool size must remain as set in the first Initialize.
    EXPECT_EQ(mgr.GetPoolAvailable(), 50u)
        << "Double-Initialize must not change pool size";
    mgr.Shutdown(nullptr);
}

TEST(DecalManager, Shutdown_OnUninitializedManager_IsNoOp_DoesNotCrash) {
    DecalManager mgr;
    // Shutdown before Initialize must not crash.
    mgr.Shutdown(nullptr);
    EXPECT_FALSE(mgr.IsInitialized());
}

TEST(DecalManager, Shutdown_ClearsActiveDecals) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);
    mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    EXPECT_EQ(mgr.GetActiveCount(), 1u);

    mgr.Shutdown(nullptr);
    EXPECT_EQ(mgr.GetActiveCount(), 0u);
}

// =============================================================================
// Stats before initialization
// =============================================================================

TEST(DecalManager, GetActiveCount_BeforeInitialize_IsZero) {
    DecalManager mgr;
    EXPECT_EQ(mgr.GetActiveCount(), 0u);
}

TEST(DecalManager, GetPoolAvailable_BeforeInitialize_IsZero) {
    DecalManager mgr;
    EXPECT_EQ(mgr.GetPoolAvailable(), 0u);
}

TEST(DecalManager, GetAtlasOccupancy_BeforeInitialize_IsZero) {
    DecalManager mgr;
    EXPECT_FLOAT_EQ(mgr.GetAtlasOccupancy(), 0.0f);
}

// =============================================================================
// SpawnDecal — happy path
// =============================================================================

TEST(DecalManager, SpawnDecal_ValidAsset_ReturnsNonNull) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    EXPECT_NE(d, nullptr)
        << "SpawnDecal must return a non-null pointer for a valid asset";

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, SpawnDecal_CopiesAssetMaterialPath) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->materialPath, asset->materialPath);

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, SpawnDecal_CopiesAssetHalfExtents) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    ASSERT_NE(d, nullptr);
    EXPECT_FLOAT_EQ(d->halfExtents.x, asset->halfExtents.x);
    EXPECT_FLOAT_EQ(d->halfExtents.y, asset->halfExtents.y);
    EXPECT_FLOAT_EQ(d->halfExtents.z, asset->halfExtents.z);

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, SpawnDecal_CopiesAssetLifetime) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    ASSERT_NE(d, nullptr);
    EXPECT_FLOAT_EQ(d->lifetime, asset->lifetime);

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, SpawnDecal_LifetimeOverride_UsesOverride) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    const float override = 25.0f;
    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, kUpNormal, override);
    ASSERT_NE(d, nullptr);
    EXPECT_FLOAT_EQ(d->lifetime, override)
        << "SpawnDecal must use lifetimeOverride when > 0";

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, SpawnDecal_NegativeLifetimeOverride_UsesAssetLifetime) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, kUpNormal, -1.0f);
    ASSERT_NE(d, nullptr);
    EXPECT_FLOAT_EQ(d->lifetime, asset->lifetime)
        << "Negative lifetime override must fall back to asset lifetime";

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, SpawnDecal_SpawnedDecal_StartsFullyActive) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    ASSERT_NE(d, nullptr);
    EXPECT_TRUE(d->isActive);
    EXPECT_FLOAT_EQ(d->age, 0.0f);
    EXPECT_FLOAT_EQ(d->fadeAlpha, 1.0f);

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, SpawnDecal_PositionOffsetAlongNormal) {
    // SpawnDecal offsets the position by 0.01 along the surface normal.
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    const Vec3 hitPos{0.0f, 0.0f, 0.0f};
    const Vec3 normal{0.0f, 1.0f, 0.0f}; // unit up
    DecalComponent* d = mgr.SpawnDecal(*asset, hitPos, normal);
    ASSERT_NE(d, nullptr);

    // Position must be offset by 0.01 along the normal.
    EXPECT_NEAR(d->position.y, 0.01f, 1e-4f)
        << "SpawnDecal must offset position by 0.01 along surface normal";

    mgr.Shutdown(nullptr);
}

// =============================================================================
// SpawnDecal — uninitialized manager
// =============================================================================

TEST(DecalManager, SpawnDecal_BeforeInitialize_ReturnsNullptr) {
    DecalManager mgr;
    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    EXPECT_EQ(d, nullptr)
        << "SpawnDecal before Initialize must return nullptr";
}

// =============================================================================
// SpawnDecal — pool exhaustion
// =============================================================================

TEST(DecalManager, SpawnDecal_ExhaustedPool_ReturnsNullptr) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 2, 256); // very small pool

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    EXPECT_NE(mgr.SpawnDecal(*asset, kHitPos, kUpNormal), nullptr);
    EXPECT_NE(mgr.SpawnDecal(*asset, kHitPos, kUpNormal), nullptr);

    DecalComponent* overflow = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    EXPECT_EQ(overflow, nullptr)
        << "SpawnDecal must return nullptr when pool is exhausted";

    mgr.Shutdown(nullptr);
}

// =============================================================================
// GetActiveCount / GetPoolAvailable
// =============================================================================

TEST(DecalManager, GetActiveCount_IncreasesAfterSpawn) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);
    EXPECT_EQ(mgr.GetActiveCount(), 0u);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    EXPECT_EQ(mgr.GetActiveCount(), 1u);

    mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    EXPECT_EQ(mgr.GetActiveCount(), 2u);

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, GetPoolAvailable_DecreasesAfterSpawn) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);
    EXPECT_EQ(mgr.GetPoolAvailable(), 10u);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    EXPECT_EQ(mgr.GetPoolAvailable(), 9u);

    mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    EXPECT_EQ(mgr.GetPoolAvailable(), 8u);

    mgr.Shutdown(nullptr);
}

// =============================================================================
// Update(dt) — ticks all active decals
// =============================================================================

TEST(DecalManager, Update_TicksAllActiveDecals_AgeAdvances) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* d1 = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    DecalComponent* d2 = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    ASSERT_NE(d1, nullptr);
    ASSERT_NE(d2, nullptr);

    mgr.Update(1.0f);

    EXPECT_NEAR(d1->age, 1.0f, 1e-4f);
    EXPECT_NEAR(d2->age, 1.0f, 1e-4f);

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, Update_ExpiredDecals_AreRemovedFromActiveList) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    // Spawn a decal with a short lifetime.
    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, kUpNormal, 1.0f);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(mgr.GetActiveCount(), 1u);

    // Tick past the lifetime.
    mgr.Update(2.0f);

    EXPECT_EQ(mgr.GetActiveCount(), 0u)
        << "Expired decal must be removed from the active list after Update";

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, Update_BeforeInitialize_IsNoOp_DoesNotCrash) {
    DecalManager mgr;
    mgr.Update(0.016f);
    EXPECT_EQ(mgr.GetActiveCount(), 0u);
}

TEST(DecalManager, Update_ZeroDt_DoesNotExpireActiveDecals) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    EXPECT_EQ(mgr.GetActiveCount(), 1u);

    mgr.Update(0.0f);
    EXPECT_EQ(mgr.GetActiveCount(), 1u)
        << "Zero dt must not expire any decals";

    mgr.Shutdown(nullptr);
}

// =============================================================================
// Cleanup — removes expired / destroyed decals
// =============================================================================

TEST(DecalManager, Update_MarkForDestruction_RemovedInNextCleanup) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(mgr.GetActiveCount(), 1u);

    d->MarkForDestruction();
    // Update triggers Cleanup which returns destroyed decals to the pool.
    mgr.Update(0.0f);

    EXPECT_EQ(mgr.GetActiveCount(), 0u)
        << "MarkForDestruction + Update must remove the decal from the active list";

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, Update_PoolAvailable_IncreasesAfterExpiry) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 5, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    mgr.SpawnDecal(*asset, kHitPos, kUpNormal, 1.0f);
    EXPECT_EQ(mgr.GetPoolAvailable(), 4u);

    mgr.Update(2.0f); // past lifetime

    EXPECT_EQ(mgr.GetPoolAvailable(), 5u)
        << "Pool slot must be returned after decal expires";

    mgr.Shutdown(nullptr);
}

// =============================================================================
// GetSortedDecals — ordering
// =============================================================================

TEST(DecalManager, GetSortedDecals_EmptyPool_ReturnsEmptyVector) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    const auto& sorted = mgr.GetSortedDecals(Vec3{0.0f, 0.0f, 0.0f});
    EXPECT_TRUE(sorted.empty());

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, GetSortedDecals_SingleDecal_ReturnsSingleEntry) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    const auto& sorted = mgr.GetSortedDecals(Vec3{0.0f, 0.0f, 0.0f});
    EXPECT_EQ(sorted.size(), 1u);

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, GetSortedDecals_SortOrderAscending) {
    // Lower sortOrder must appear first in the sorted list.
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* high = mgr.SpawnDecal(*asset, Vec3{0.0f, 0.0f, 0.0f}, kUpNormal);
    DecalComponent* low  = mgr.SpawnDecal(*asset, Vec3{0.0f, 0.0f, 0.0f}, kUpNormal);
    ASSERT_NE(high, nullptr);
    ASSERT_NE(low, nullptr);

    high->sortOrder = 10;
    low->sortOrder  = 1;

    const auto& sorted = mgr.GetSortedDecals(Vec3{0.0f, 100.0f, 0.0f});
    ASSERT_EQ(sorted.size(), 2u);
    EXPECT_LT(sorted[0]->sortOrder, sorted[1]->sortOrder)
        << "Decals must be sorted by sortOrder ascending";

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, GetSortedDecals_SameSortOrder_BackToFrontByDistance) {
    // When sortOrders are equal, the decal farther from the camera comes first.
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* close = mgr.SpawnDecal(*asset, Vec3{0.0f, 0.0f, 1.0f}, kUpNormal);
    DecalComponent* far   = mgr.SpawnDecal(*asset, Vec3{0.0f, 0.0f, 10.0f}, kUpNormal);
    ASSERT_NE(close, nullptr);
    ASSERT_NE(far, nullptr);

    // Override positions directly -- SpawnDecal adds a small offset, so set manually.
    close->position = Vec3{0.0f, 0.0f, 1.0f};
    far->position   = Vec3{0.0f, 0.0f, 10.0f};
    close->sortOrder = 0;
    far->sortOrder   = 0;

    const Vec3 camPos{0.0f, 0.0f, 0.0f};
    const auto& sorted = mgr.GetSortedDecals(camPos);
    ASSERT_EQ(sorted.size(), 2u);
    // Back-to-front: far decal must come first.
    EXPECT_EQ(sorted[0], far)
        << "Farther decal must appear first in back-to-front order";
    EXPECT_EQ(sorted[1], close);

    mgr.Shutdown(nullptr);
}

// =============================================================================
// GetAtlasOccupancy
// =============================================================================

TEST(DecalManager, GetAtlasOccupancy_AfterInitialize_IsZero) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 512);
    EXPECT_FLOAT_EQ(mgr.GetAtlasOccupancy(), 0.0f);
    mgr.Shutdown(nullptr);
}

// =============================================================================
// FrustumCull — inside and outside frustum
// =============================================================================

TEST(DecalManager, FrustumCull_BeforeInitialize_IsNoOp_DoesNotCrash) {
    DecalManager mgr;
    const Mat4 vp = MakeWideOpenFrustum();
    mgr.FrustumCull(vp); // must not crash
}

TEST(DecalManager, FrustumCull_DecalAtOrigin_InsideWideFrustum_IsIncluded) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);
    // Spawn at world origin — should be inside the wide frustum.
    DecalComponent* d = mgr.SpawnDecal(*asset, Vec3{0.0f, 0.0f, 0.0f}, kUpNormal);
    ASSERT_NE(d, nullptr);
    d->position = Vec3{0.0f, 0.0f, 0.0f}; // reset offset for clarity

    mgr.FrustumCull(MakeWideOpenFrustum());
    const auto& sorted = mgr.GetSortedDecals(Vec3{0.0f, 0.0f, 0.0f});
    EXPECT_EQ(sorted.size(), 1u)
        << "Decal at origin must not be culled by a wide-open frustum";

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, FrustumCull_DecalAtOrigin_OutsideFrustumPointingAway_IsExcluded) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);
    DecalComponent* d = mgr.SpawnDecal(*asset, Vec3{0.0f, 0.0f, 0.0f}, kUpNormal);
    ASSERT_NE(d, nullptr);
    d->position = Vec3{0.0f, 0.0f, 0.0f};
    ASSERT_EQ(mgr.GetActiveCount(), 1u);

    mgr.FrustumCull(MakeFrustumAwayFromOrigin());
    const auto& sorted = mgr.GetSortedDecals(Vec3{0.0f, 0.0f, 0.0f});
    EXPECT_TRUE(sorted.empty())
        << "Decal at origin must be culled when frustum points away from origin";

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, FrustumCull_DoesNotReduceActiveCount) {
    // Culling is a visibility filter — active pool count must not change.
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);
    mgr.SpawnDecal(*asset, Vec3{0.0f, 0.0f, 0.0f}, kUpNormal);
    ASSERT_EQ(mgr.GetActiveCount(), 1u);

    mgr.FrustumCull(MakeFrustumAwayFromOrigin());

    EXPECT_EQ(mgr.GetActiveCount(), 1u)
        << "FrustumCull must not reduce the active count";

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, FrustumCull_AfterCull_UpdateStillTicksAllDecals) {
    // Even culled decals must age normally so they eventually expire.
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);
    DecalComponent* d = mgr.SpawnDecal(*asset, Vec3{0.0f, 0.0f, 0.0f}, kUpNormal, 1.0f);
    ASSERT_NE(d, nullptr);
    d->position = Vec3{0.0f, 0.0f, 0.0f};

    mgr.FrustumCull(MakeFrustumAwayFromOrigin()); // cull it
    // Sorted list is empty — but decal must still tick.
    mgr.Update(2.0f); // advance past 1s lifetime
    EXPECT_EQ(mgr.GetActiveCount(), 0u)
        << "Culled decal must still be expired and removed by Update";

    mgr.Shutdown(nullptr);
}

// =============================================================================
// SpawnDecal — rotation alignment to surface normal
// =============================================================================

TEST(DecalManager, SpawnDecal_UnitUpNormal_RotationIsNormalized) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);
    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, Vec3{0.0f, 1.0f, 0.0f});
    ASSERT_NE(d, nullptr);
    const float len = std::sqrt(d->rotation.x * d->rotation.x +
                                d->rotation.y * d->rotation.y +
                                d->rotation.z * d->rotation.z +
                                d->rotation.w * d->rotation.w);
    EXPECT_NEAR(len, 1.0f, 1e-4f)
        << "Rotation quaternion must be unit-length when surface normal is world up";
    mgr.Shutdown(nullptr);
}

TEST(DecalManager, SpawnDecal_DiagonalNormal_RotationIsNormalized) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);
    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    // Diagonal normal — exercises the general rotation path in AlignToSurface.
    const Vec3 diagNormal{0.577350f, 0.577350f, 0.577350f};
    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, diagNormal);
    ASSERT_NE(d, nullptr);
    const float len = std::sqrt(d->rotation.x * d->rotation.x +
                                d->rotation.y * d->rotation.y +
                                d->rotation.z * d->rotation.z +
                                d->rotation.w * d->rotation.w);
    EXPECT_NEAR(len, 1.0f, 1e-4f)
        << "Rotation quaternion must be unit-length for a diagonal surface normal";
    EXPECT_FALSE(std::isnan(d->rotation.x) || std::isnan(d->rotation.y) ||
                 std::isnan(d->rotation.z) || std::isnan(d->rotation.w))
        << "No NaN values allowed in rotation quaternion";
    mgr.Shutdown(nullptr);
}

TEST(DecalManager, SpawnDecal_DownwardNormal_RotationIsNormalized_NoNaN) {
    // Normal pointing straight down — triggers the fallback antiparallel path.
    DecalManager mgr;
    mgr.Initialize(nullptr, 10, 256);
    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* d = mgr.SpawnDecal(*asset, kHitPos, Vec3{0.0f, -1.0f, 0.0f});
    ASSERT_NE(d, nullptr);
    const float len = std::sqrt(d->rotation.x * d->rotation.x +
                                d->rotation.y * d->rotation.y +
                                d->rotation.z * d->rotation.z +
                                d->rotation.w * d->rotation.w);
    EXPECT_NEAR(len, 1.0f, 1e-4f);
    EXPECT_FALSE(std::isnan(d->rotation.x) || std::isnan(d->rotation.y) ||
                 std::isnan(d->rotation.z) || std::isnan(d->rotation.w));
    mgr.Shutdown(nullptr);
}

// =============================================================================
// Multiple spawn/update/cleanup cycles
// =============================================================================

TEST(DecalManager, MultipleSpawnUpdateCycles_PoolReusedCorrectly) {
    // 5 rounds of: spawn 4 short-lived decals → update past expiry → verify pool reset.
    DecalManager mgr;
    mgr.Initialize(nullptr, 4, 256);

    for (int round = 0; round < 5; ++round) {
        // Use a very short lifetime so a single Update clears them all.
        const std::string json = R"({
            "format": "uzdecal", "version": 1,
            "material": "materials/cycle_test.uzmat",
            "lifetime": 0.05
        })";
        auto asset = DecalAsset::LoadFromString(json);
        ASSERT_NE(asset, nullptr) << "Round " << round;

        for (int i = 0; i < 4; ++i) {
            DecalComponent* d = mgr.SpawnDecal(
                *asset,
                Vec3{static_cast<float>(i), 0.0f, 0.0f},
                kUpNormal);
            EXPECT_NE(d, nullptr)
                << "Round " << round << " spawn " << i << " must succeed";
        }
        EXPECT_EQ(mgr.GetActiveCount(), 4u) << "Round " << round;
        EXPECT_EQ(mgr.GetPoolAvailable(), 0u) << "Round " << round;

        mgr.Update(1.0f); // expires all 4
        EXPECT_EQ(mgr.GetActiveCount(), 0u)
            << "Round " << round << ": all decals must expire after Update";
        EXPECT_EQ(mgr.GetPoolAvailable(), 4u)
            << "Round " << round << ": all slots must be returned to pool";
    }

    mgr.Shutdown(nullptr);
}

TEST(DecalManager, SpawnThenMarkForDestructionThenRespawn_WorksCorrectly) {
    DecalManager mgr;
    mgr.Initialize(nullptr, 2, 256);
    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);

    DecalComponent* d1 = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    DecalComponent* d2 = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    ASSERT_NE(d1, nullptr);
    ASSERT_NE(d2, nullptr);
    EXPECT_EQ(mgr.GetPoolAvailable(), 0u);

    // Mark both for immediate destruction.
    d1->MarkForDestruction();
    d2->MarkForDestruction();
    mgr.Update(0.0f); // Cleanup runs inside Update

    EXPECT_EQ(mgr.GetActiveCount(), 0u);
    EXPECT_EQ(mgr.GetPoolAvailable(), 2u);

    // Should be able to spawn two fresh decals again.
    DecalComponent* d3 = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    DecalComponent* d4 = mgr.SpawnDecal(*asset, kHitPos, kUpNormal);
    EXPECT_NE(d3, nullptr) << "First re-spawn must succeed after MarkForDestruction";
    EXPECT_NE(d4, nullptr) << "Second re-spawn must succeed after MarkForDestruction";

    mgr.Shutdown(nullptr);
}

// =============================================================================
// Move semantics
// =============================================================================

TEST(DecalManager, MoveConstructor_TransfersInitializedState) {
    DecalManager src;
    src.Initialize(nullptr, 20, 256);

    auto asset = MakeTestAsset();
    ASSERT_NE(asset, nullptr);
    src.SpawnDecal(*asset, kHitPos, kUpNormal);

    DecalManager dst(std::move(src));
    EXPECT_TRUE(dst.IsInitialized());
    EXPECT_EQ(dst.GetActiveCount(), 1u);
    EXPECT_FALSE(src.IsInitialized()) // NOLINT(bugprone-use-after-move)
        << "Moved-from manager must not be initialized";

    dst.Shutdown(nullptr);
}
