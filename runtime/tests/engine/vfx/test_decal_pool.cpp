/// Unit tests for DecalPool.
///
/// Covers: construction (size, counts), AcquireDecal (returns non-null, increases
/// active count, resets to defaults), AcquireDecal on a full pool (nullptr),
/// ReturnDecal (decreases active count, increases available), ReturnDecal with
/// invalid/null pointer (no-op), Clear (resets all counts), multiple acquire/return
/// cycles, GetActiveCount/GetAvailableCount/GetPoolSize accuracy, and verification
/// that acquired decals are reset to default state.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "engine/vfx/decals/decal_pool.h"

#include <cstdint>
#include <vector>

using namespace uz;

// =============================================================================
// Construction
// =============================================================================

TEST(DecalPool, Constructor_PoolSize100_CorrectInitialCounts) {
    DecalPool pool(100);
    EXPECT_EQ(pool.GetPoolSize(), 100u);
    EXPECT_EQ(pool.GetActiveCount(), 0u);
    EXPECT_EQ(pool.GetAvailableCount(), 100u);
}

TEST(DecalPool, Constructor_PoolSize1_CorrectInitialCounts) {
    DecalPool pool(1);
    EXPECT_EQ(pool.GetPoolSize(), 1u);
    EXPECT_EQ(pool.GetActiveCount(), 0u);
    EXPECT_EQ(pool.GetAvailableCount(), 1u);
}

TEST(DecalPool, Constructor_DefaultPoolSize_IsNonZero) {
    DecalPool pool;
    EXPECT_GT(pool.GetPoolSize(), 0u);
    EXPECT_EQ(pool.GetActiveCount(), 0u);
    EXPECT_EQ(pool.GetAvailableCount(), pool.GetPoolSize());
}

// =============================================================================
// GetPoolSize / invariant: active + available == poolSize
// =============================================================================

TEST(DecalPool, Invariant_ActivePlusAvailable_EqualsPoolSize_AfterAcquire) {
    DecalPool pool(50);
    pool.AcquireDecal();
    pool.AcquireDecal();
    pool.AcquireDecal();
    EXPECT_EQ(pool.GetActiveCount() + pool.GetAvailableCount(), pool.GetPoolSize());
}

// =============================================================================
// AcquireDecal — happy path
// =============================================================================

TEST(DecalPool, AcquireDecal_ReturnsNonNullPointer) {
    DecalPool pool(10);
    DecalComponent* d = pool.AcquireDecal();
    ASSERT_NE(d, nullptr);
}

TEST(DecalPool, AcquireDecal_IncreasesActiveCount) {
    DecalPool pool(10);
    EXPECT_EQ(pool.GetActiveCount(), 0u);
    pool.AcquireDecal();
    EXPECT_EQ(pool.GetActiveCount(), 1u);
    pool.AcquireDecal();
    EXPECT_EQ(pool.GetActiveCount(), 2u);
}

TEST(DecalPool, AcquireDecal_DecreasesAvailableCount) {
    DecalPool pool(10);
    EXPECT_EQ(pool.GetAvailableCount(), 10u);
    pool.AcquireDecal();
    EXPECT_EQ(pool.GetAvailableCount(), 9u);
    pool.AcquireDecal();
    EXPECT_EQ(pool.GetAvailableCount(), 8u);
}

TEST(DecalPool, AcquireDecal_ReturnedDecal_IsResetToDefaults) {
    DecalPool pool(10);
    DecalComponent* first = pool.AcquireDecal();
    ASSERT_NE(first, nullptr);

    // Dirty the decal with non-default values.
    first->age = 99.0f;
    first->fadeAlpha = 0.0f;
    first->isActive = false;
    first->materialPath = "some/path.uzmat";
    first->lifetime = 0.5f;

    // Return it and re-acquire -- should get a clean slate.
    pool.ReturnDecal(first);
    DecalComponent* second = pool.AcquireDecal();
    ASSERT_NE(second, nullptr);

    // The re-acquired decal must have default values.
    EXPECT_FLOAT_EQ(second->age, 0.0f)
        << "Acquired decal must have default age";
    EXPECT_FLOAT_EQ(second->fadeAlpha, 1.0f)
        << "Acquired decal must have default fadeAlpha";
    EXPECT_TRUE(second->isActive)
        << "Acquired decal must be active by default";
    EXPECT_FLOAT_EQ(second->lifetime, 10.0f)
        << "Acquired decal must have default lifetime";
    EXPECT_TRUE(second->materialPath.empty())
        << "Acquired decal must have empty materialPath";
}

TEST(DecalPool, AcquireDecal_MultiplePointers_AreDistinct) {
    DecalPool pool(5);
    std::vector<DecalComponent*> ptrs;
    for (int i = 0; i < 5; ++i) {
        DecalComponent* d = pool.AcquireDecal();
        ASSERT_NE(d, nullptr);
        ptrs.push_back(d);
    }
    // All pointers must be unique.
    for (size_t i = 0; i < ptrs.size(); ++i) {
        for (size_t j = i + 1; j < ptrs.size(); ++j) {
            EXPECT_NE(ptrs[i], ptrs[j])
                << "Acquired decal pointers must be distinct";
        }
    }
}

// =============================================================================
// AcquireDecal — pool exhaustion
// =============================================================================

TEST(DecalPool, AcquireDecal_FullPool_ReturnsNullptr) {
    DecalPool pool(3);
    pool.AcquireDecal();
    pool.AcquireDecal();
    pool.AcquireDecal();
    EXPECT_EQ(pool.GetActiveCount(), 3u);
    EXPECT_EQ(pool.GetAvailableCount(), 0u);

    DecalComponent* overflow = pool.AcquireDecal();
    EXPECT_EQ(overflow, nullptr)
        << "AcquireDecal must return nullptr when pool is exhausted";
}

TEST(DecalPool, AcquireDecal_FullPool_CountsDoNotChange) {
    DecalPool pool(2);
    pool.AcquireDecal();
    pool.AcquireDecal();

    pool.AcquireDecal(); // attempt on full pool

    EXPECT_EQ(pool.GetActiveCount(), 2u);
    EXPECT_EQ(pool.GetAvailableCount(), 0u);
}

// =============================================================================
// ReturnDecal — happy path
// =============================================================================

TEST(DecalPool, ReturnDecal_DecreasesActiveCount) {
    DecalPool pool(10);
    DecalComponent* d = pool.AcquireDecal();
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(pool.GetActiveCount(), 1u);
    pool.ReturnDecal(d);
    EXPECT_EQ(pool.GetActiveCount(), 0u);
}

TEST(DecalPool, ReturnDecal_IncreasesAvailableCount) {
    DecalPool pool(10);
    DecalComponent* d = pool.AcquireDecal();
    ASSERT_NE(d, nullptr);
    const uint32_t availBefore = pool.GetAvailableCount();
    pool.ReturnDecal(d);
    EXPECT_EQ(pool.GetAvailableCount(), availBefore + 1u);
}

TEST(DecalPool, ReturnDecal_SlotBecomesReusable) {
    DecalPool pool(1);
    DecalComponent* first = pool.AcquireDecal();
    ASSERT_NE(first, nullptr);
    pool.ReturnDecal(first);
    // After return, acquiring again must succeed.
    DecalComponent* second = pool.AcquireDecal();
    EXPECT_NE(second, nullptr)
        << "After ReturnDecal, the slot must be available for re-acquisition";
}

// =============================================================================
// ReturnDecal — invalid pointer (no crash)
// =============================================================================

TEST(DecalPool, ReturnDecal_NullPointer_IsNoOp_DoesNotCrash) {
    DecalPool pool(10);
    pool.AcquireDecal();
    const uint32_t activeBefore = pool.GetActiveCount();
    const uint32_t availBefore = pool.GetAvailableCount();

    // Must not crash or modify counts.
    pool.ReturnDecal(nullptr);

    EXPECT_EQ(pool.GetActiveCount(), activeBefore);
    EXPECT_EQ(pool.GetAvailableCount(), availBefore);
}

TEST(DecalPool, ReturnDecal_OutOfRangePointer_IsNoOp_DoesNotCrash) {
    DecalPool pool(10);
    DecalComponent* valid = pool.AcquireDecal();
    ASSERT_NE(valid, nullptr);

    // A pointer far outside the pool's memory range.
    DecalComponent outsider{};
    const uint32_t activeBefore = pool.GetActiveCount();
    const uint32_t availBefore = pool.GetAvailableCount();

    pool.ReturnDecal(&outsider);

    EXPECT_EQ(pool.GetActiveCount(), activeBefore)
        << "Active count must not change for out-of-range pointer";
    EXPECT_EQ(pool.GetAvailableCount(), availBefore)
        << "Available count must not change for out-of-range pointer";
}

// =============================================================================
// Clear
// =============================================================================

TEST(DecalPool, Clear_ReturnsAllActive_CountsReset) {
    DecalPool pool(10);
    pool.AcquireDecal();
    pool.AcquireDecal();
    pool.AcquireDecal();
    EXPECT_EQ(pool.GetActiveCount(), 3u);

    pool.Clear();

    EXPECT_EQ(pool.GetActiveCount(), 0u);
    EXPECT_EQ(pool.GetAvailableCount(), 10u);
}

TEST(DecalPool, Clear_OnEmptyPool_IsNoOp) {
    DecalPool pool(10);
    pool.Clear();
    EXPECT_EQ(pool.GetActiveCount(), 0u);
    EXPECT_EQ(pool.GetAvailableCount(), 10u);
}

TEST(DecalPool, Clear_AfterFullAcquire_MakesAllSlotsAvailableAgain) {
    DecalPool pool(5);
    for (int i = 0; i < 5; ++i) {
        ASSERT_NE(pool.AcquireDecal(), nullptr);
    }
    EXPECT_EQ(pool.GetAvailableCount(), 0u);

    pool.Clear();
    EXPECT_EQ(pool.GetAvailableCount(), 5u);

    // All slots should be acquirable again.
    for (int i = 0; i < 5; ++i) {
        EXPECT_NE(pool.AcquireDecal(), nullptr);
    }
}

// =============================================================================
// Multiple acquire/return cycles
// =============================================================================

TEST(DecalPool, MultipleAcquireReturnCycles_WorkCorrectly) {
    DecalPool pool(4);

    // Cycle 1: acquire all, return all.
    std::vector<DecalComponent*> batch1;
    for (int i = 0; i < 4; ++i) {
        batch1.push_back(pool.AcquireDecal());
    }
    for (auto* d : batch1) { pool.ReturnDecal(d); }
    EXPECT_EQ(pool.GetActiveCount(), 0u);
    EXPECT_EQ(pool.GetAvailableCount(), 4u);

    // Cycle 2: acquire 2, return 2, acquire 4 more.
    DecalComponent* a = pool.AcquireDecal();
    DecalComponent* b = pool.AcquireDecal();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    pool.ReturnDecal(a);
    pool.ReturnDecal(b);

    std::vector<DecalComponent*> batch2;
    for (int i = 0; i < 4; ++i) {
        DecalComponent* d = pool.AcquireDecal();
        EXPECT_NE(d, nullptr) << "Pool should have 4 available slots after returning 2";
        batch2.push_back(d);
    }
    EXPECT_EQ(pool.GetActiveCount(), 4u);
    EXPECT_EQ(pool.GetAvailableCount(), 0u);
}

// =============================================================================
// GetActiveDecals — reflects live set
// =============================================================================

TEST(DecalPool, GetActiveDecals_ReturnsCorrectPointers) {
    DecalPool pool(5);
    DecalComponent* d1 = pool.AcquireDecal();
    DecalComponent* d2 = pool.AcquireDecal();
    ASSERT_NE(d1, nullptr);
    ASSERT_NE(d2, nullptr);

    const auto& active = pool.GetActiveDecals();
    EXPECT_EQ(active.size(), 2u);

    bool foundD1 = false, foundD2 = false;
    for (const auto* p : active) {
        if (p == d1) foundD1 = true;
        if (p == d2) foundD2 = true;
    }
    EXPECT_TRUE(foundD1) << "d1 must be in the active list";
    EXPECT_TRUE(foundD2) << "d2 must be in the active list";
}

TEST(DecalPool, GetActiveDecals_AfterReturn_RemovedFromList) {
    DecalPool pool(5);
    DecalComponent* d1 = pool.AcquireDecal();
    DecalComponent* d2 = pool.AcquireDecal();
    ASSERT_NE(d1, nullptr);
    ASSERT_NE(d2, nullptr);

    pool.ReturnDecal(d1);

    const auto& active = pool.GetActiveDecals();
    EXPECT_EQ(active.size(), 1u);
    for (const auto* p : active) {
        EXPECT_NE(p, d1) << "Returned decal must not appear in active list";
    }
}

// =============================================================================
// Move semantics
// =============================================================================

TEST(DecalPool, MoveConstructor_TransfersOwnership) {
    DecalPool original(20);
    original.AcquireDecal();
    original.AcquireDecal();

    DecalPool moved(std::move(original));
    EXPECT_EQ(moved.GetPoolSize(), 20u);
    EXPECT_EQ(moved.GetActiveCount(), 2u);
    EXPECT_EQ(moved.GetAvailableCount(), 18u);
}

TEST(DecalPool, MoveAssignment_TransfersOwnership) {
    DecalPool src(15);
    src.AcquireDecal();

    DecalPool dst(5);
    dst = std::move(src);

    EXPECT_EQ(dst.GetPoolSize(), 15u);
    EXPECT_EQ(dst.GetActiveCount(), 1u);
    EXPECT_EQ(dst.GetAvailableCount(), 14u);
}
