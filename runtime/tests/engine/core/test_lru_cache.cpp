/// UzEngine LRUCache — Tests
///
/// Tests insertion, lookup, LRU eviction, budget enforcement, promotion
/// on Get, update-in-place, and Clear/Contains behavior.

#include "lru_cache.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// Basic operations
// ─────────────────────────────────────────────────────────────────────────────

TEST(LRUCache, Get_EmptyCache_ReturnsNullptr) {
    LRUCache<int, std::string> cache(1024);
    EXPECT_EQ(cache.Get(42), nullptr);
    EXPECT_EQ(cache.Get(0), nullptr);
    EXPECT_EQ(cache.Get(-1), nullptr);
}

TEST(LRUCache, Put_ThenGet_ReturnsValue) {
    LRUCache<int, std::string> cache(1024);
    cache.Put(1, "hello", 5);

    auto* val = cache.Get(1);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "hello");
}

TEST(LRUCache, Put_MultipleEntries_AllRetrievable) {
    LRUCache<int, std::string> cache(4096);
    cache.Put(10, "alpha", 100);
    cache.Put(20, "beta", 100);
    cache.Put(30, "gamma", 100);

    auto* a = cache.Get(10);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(*a, "alpha");

    auto* b = cache.Get(20);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(*b, "beta");

    auto* c = cache.Get(30);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(*c, "gamma");
}

// ─────────────────────────────────────────────────────────────────────────────
// Contains
// ─────────────────────────────────────────────────────────────────────────────

TEST(LRUCache, Contains_TrueForExisting_FalseForMissing) {
    LRUCache<int, int> cache(1024);
    cache.Put(1, 100, 10);
    cache.Put(2, 200, 10);

    EXPECT_TRUE(cache.Contains(1));
    EXPECT_TRUE(cache.Contains(2));
    EXPECT_FALSE(cache.Contains(3));
    EXPECT_FALSE(cache.Contains(0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Budget enforcement and eviction
// ─────────────────────────────────────────────────────────────────────────────

TEST(LRUCache, BudgetEnforcement_EvictsLRUItems) {
    // Budget = 300 bytes. Insert 3 items of 100 each = exactly at budget.
    // Then insert a 4th — the LRU (first inserted) should be evicted.
    LRUCache<int, std::string> cache(300);

    cache.Put(1, "first", 100);
    cache.Put(2, "second", 100);
    cache.Put(3, "third", 100);

    EXPECT_EQ(cache.EntryCount(), 3u);
    EXPECT_EQ(cache.CurrentBytes(), 300u);

    // Insert 4th — pushes over budget. Key 1 (LRU) should be evicted.
    cache.Put(4, "fourth", 100);

    EXPECT_EQ(cache.EntryCount(), 3u);
    EXPECT_EQ(cache.CurrentBytes(), 300u);

    EXPECT_EQ(cache.Get(1), nullptr) << "Key 1 should have been evicted (LRU)";
    ASSERT_NE(cache.Get(2), nullptr);
    EXPECT_EQ(*cache.Get(2), "second");
    ASSERT_NE(cache.Get(3), nullptr);
    EXPECT_EQ(*cache.Get(3), "third");
    ASSERT_NE(cache.Get(4), nullptr);
    EXPECT_EQ(*cache.Get(4), "fourth");
}

TEST(LRUCache, BudgetEnforcement_EvictsMultipleItems) {
    // Budget = 200, insert 4 items of 100 bytes each.
    // After inserting the 3rd, only the 2 most recent should survive.
    LRUCache<int, int> cache(200);

    cache.Put(1, 10, 100);
    cache.Put(2, 20, 100);
    EXPECT_EQ(cache.EntryCount(), 2u);
    EXPECT_EQ(cache.CurrentBytes(), 200u);

    cache.Put(3, 30, 100);
    EXPECT_EQ(cache.EntryCount(), 2u);
    EXPECT_EQ(cache.CurrentBytes(), 200u);
    EXPECT_EQ(cache.Get(1), nullptr) << "Key 1 should have been evicted";

    cache.Put(4, 40, 100);
    EXPECT_EQ(cache.EntryCount(), 2u);
    EXPECT_EQ(cache.CurrentBytes(), 200u);
    EXPECT_EQ(cache.Get(2), nullptr) << "Key 2 should have been evicted";

    auto* v3 = cache.Get(3);
    ASSERT_NE(v3, nullptr);
    EXPECT_EQ(*v3, 30);

    auto* v4 = cache.Get(4);
    ASSERT_NE(v4, nullptr);
    EXPECT_EQ(*v4, 40);
}

// ─────────────────────────────────────────────────────────────────────────────
// Get promotes entry (changes eviction order)
// ─────────────────────────────────────────────────────────────────────────────

TEST(LRUCache, Get_PromotesEntry_AffectsEvictionOrder) {
    // Budget = 300. Insert A(100), B(100), C(100). Then Get(A) to promote it.
    // Insert D(100) — B should be evicted (now LRU), NOT A.
    LRUCache<int, std::string> cache(300);

    cache.Put(1, "A", 100);
    cache.Put(2, "B", 100);
    cache.Put(3, "C", 100);

    // Promote key 1 by accessing it
    auto* promoted = cache.Get(1);
    ASSERT_NE(promoted, nullptr);
    EXPECT_EQ(*promoted, "A");

    // Insert new item — key 2 is now LRU
    cache.Put(4, "D", 100);

    EXPECT_EQ(cache.Get(2), nullptr) << "Key 2 should be evicted (LRU after promotion of 1)";
    ASSERT_NE(cache.Get(1), nullptr) << "Key 1 should survive (was promoted)";
    EXPECT_EQ(*cache.Get(1), "A");
    ASSERT_NE(cache.Get(3), nullptr);
    ASSERT_NE(cache.Get(4), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// CurrentBytes and EntryCount tracking
// ─────────────────────────────────────────────────────────────────────────────

TEST(LRUCache, CurrentBytes_TracksCorrectly) {
    LRUCache<int, int> cache(10000);

    EXPECT_EQ(cache.CurrentBytes(), 0u);

    cache.Put(1, 10, 100);
    EXPECT_EQ(cache.CurrentBytes(), 100u);

    cache.Put(2, 20, 250);
    EXPECT_EQ(cache.CurrentBytes(), 350u);

    cache.Put(3, 30, 50);
    EXPECT_EQ(cache.CurrentBytes(), 400u);
}

TEST(LRUCache, CurrentBytes_DecreasesAfterEviction) {
    LRUCache<int, int> cache(300);

    cache.Put(1, 10, 150);
    cache.Put(2, 20, 150);
    EXPECT_EQ(cache.CurrentBytes(), 300u);
    EXPECT_EQ(cache.EntryCount(), 2u);

    // This will evict key 1 (150 bytes)
    cache.Put(3, 30, 150);
    EXPECT_EQ(cache.CurrentBytes(), 300u);
    EXPECT_EQ(cache.EntryCount(), 2u);
}

TEST(LRUCache, EntryCount_TracksCorrectly) {
    LRUCache<std::string, int> cache(10000);

    EXPECT_EQ(cache.EntryCount(), 0u);

    cache.Put("a", 1, 10);
    EXPECT_EQ(cache.EntryCount(), 1u);

    cache.Put("b", 2, 10);
    EXPECT_EQ(cache.EntryCount(), 2u);

    cache.Put("c", 3, 10);
    EXPECT_EQ(cache.EntryCount(), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear
// ─────────────────────────────────────────────────────────────────────────────

TEST(LRUCache, Clear_ResetsEverything) {
    LRUCache<int, std::string> cache(4096);
    cache.Put(1, "a", 100);
    cache.Put(2, "b", 200);
    cache.Put(3, "c", 300);

    EXPECT_EQ(cache.EntryCount(), 3u);
    EXPECT_EQ(cache.CurrentBytes(), 600u);

    cache.Clear();

    EXPECT_EQ(cache.EntryCount(), 0u);
    EXPECT_EQ(cache.CurrentBytes(), 0u);
    EXPECT_FALSE(cache.Contains(1));
    EXPECT_FALSE(cache.Contains(2));
    EXPECT_FALSE(cache.Contains(3));
    EXPECT_EQ(cache.Get(1), nullptr);
    EXPECT_EQ(cache.Get(2), nullptr);
    EXPECT_EQ(cache.Get(3), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Update existing key
// ─────────────────────────────────────────────────────────────────────────────

TEST(LRUCache, Put_SameKey_UpdatesValue) {
    LRUCache<int, std::string> cache(1024);
    cache.Put(1, "original", 50);

    auto* val = cache.Get(1);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "original");

    // Update same key with new value and different size
    cache.Put(1, "updated", 75);

    val = cache.Get(1);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "updated");

    EXPECT_EQ(cache.EntryCount(), 1u);
    EXPECT_EQ(cache.CurrentBytes(), 75u);
}

TEST(LRUCache, Put_SameKey_UpdateSizeTrackedCorrectly) {
    LRUCache<int, int> cache(1024);

    cache.Put(1, 10, 100);
    cache.Put(2, 20, 100);
    EXPECT_EQ(cache.CurrentBytes(), 200u);

    // Update key 1 with a larger size
    cache.Put(1, 15, 300);
    EXPECT_EQ(cache.CurrentBytes(), 400u);
    EXPECT_EQ(cache.EntryCount(), 2u);

    // Verify updated value
    auto* v = cache.Get(1);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(*v, 15);
}

// ─────────────────────────────────────────────────────────────────────────────
// Single item exceeding budget
// ─────────────────────────────────────────────────────────────────────────────

TEST(LRUCache, SingleItem_ExceedsBudget_StillInserted) {
    LRUCache<int, std::string> cache(100);

    // Insert a small item first
    cache.Put(1, "small", 50);
    EXPECT_EQ(cache.EntryCount(), 1u);

    // Insert an item that exceeds the entire budget
    cache.Put(2, "huge", 500);

    // The huge item should be the only one remaining
    // (small evicted, huge kept because order_.size() > 1 check prevents
    // evicting the last item)
    EXPECT_EQ(cache.EntryCount(), 1u);
    EXPECT_EQ(cache.Get(1), nullptr) << "Small item should have been evicted";

    auto* huge = cache.Get(2);
    ASSERT_NE(huge, nullptr);
    EXPECT_EQ(*huge, "huge");
    EXPECT_EQ(cache.CurrentBytes(), 500u);
}

TEST(LRUCache, SingleItem_ExceedsBudget_InEmptyCache) {
    LRUCache<int, int> cache(10);

    // Insert item larger than budget into empty cache
    cache.Put(1, 42, 1000);

    EXPECT_EQ(cache.EntryCount(), 1u);
    EXPECT_EQ(cache.CurrentBytes(), 1000u);

    auto* val = cache.Get(1);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 42);
}

// ─────────────────────────────────────────────────────────────────────────────
// MaxBytes accessor
// ─────────────────────────────────────────────────────────────────────────────

TEST(LRUCache, MaxBytes_ReturnsConfiguredBudget) {
    LRUCache<int, int> cache(8192);
    EXPECT_EQ(cache.MaxBytes(), 8192u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Edge cases
// ─────────────────────────────────────────────────────────────────────────────

TEST(LRUCache, ZeroSizeItems_DoNotConsumeBytes) {
    LRUCache<int, int> cache(100);

    cache.Put(1, 10, 0);
    cache.Put(2, 20, 0);
    cache.Put(3, 30, 0);

    EXPECT_EQ(cache.CurrentBytes(), 0u);
    EXPECT_EQ(cache.EntryCount(), 3u);

    auto* v = cache.Get(2);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(*v, 20);
}

TEST(LRUCache, ManyEntries_CorrectEvictionOrder) {
    // Budget fits exactly 5 items of 100 bytes each.
    // Insert 10 items — only the last 5 should remain.
    LRUCache<int, int> cache(500);

    for (int i = 0; i < 10; ++i) {
        cache.Put(i, i * 100, 100);
    }

    EXPECT_EQ(cache.EntryCount(), 5u);
    EXPECT_EQ(cache.CurrentBytes(), 500u);

    // Keys 0-4 should be evicted
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(cache.Get(i), nullptr) << "Key " << i << " should have been evicted";
    }

    // Keys 5-9 should remain
    for (int i = 5; i < 10; ++i) {
        auto* val = cache.Get(i);
        ASSERT_NE(val, nullptr) << "Key " << i << " should still be in cache";
        EXPECT_EQ(*val, i * 100);
    }
}

TEST(LRUCache, Get_MissingKey_ReturnsNullptr_DoesNotModifyCache) {
    LRUCache<int, int> cache(1024);
    cache.Put(1, 10, 50);

    EXPECT_EQ(cache.Get(999), nullptr);
    EXPECT_EQ(cache.EntryCount(), 1u);
    EXPECT_EQ(cache.CurrentBytes(), 50u);
}

} // namespace uz::test
