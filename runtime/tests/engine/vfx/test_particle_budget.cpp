/// Unit tests for ParticleBudget.
///
/// Covers:
///   - Constructor: default (500K) and custom max
///   - Allocate(): full request, partial request, exhausted budget, zero request
///   - Return(): normal return, zero return, full release
///   - GetUsed / GetAvailable / GetMax / GetUsageRatio
///   - SetMax(): change budget mid-flight
///   - Thread safety: concurrent Allocate/Return from many threads

#include <gtest/gtest.h>

#include "particle_budget.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

/// Default constructor sets max to 500,000 particles.
TEST(ParticleBudget, DefaultConstructor_MaxIs500K) {
    ParticleBudget budget;
    EXPECT_EQ(budget.GetMax(), 500000u);
}

/// Default constructor starts with zero particles in use.
TEST(ParticleBudget, DefaultConstructor_UsedIsZero) {
    ParticleBudget budget;
    EXPECT_EQ(budget.GetUsed(), 0u);
}

/// Custom constructor sets the requested max.
TEST(ParticleBudget, CustomConstructor_MaxMatchesArgument) {
    ParticleBudget budget(100u);
    EXPECT_EQ(budget.GetMax(), 100u);
}

/// Custom constructor with max=1 is the minimum valid budget.
TEST(ParticleBudget, CustomConstructor_MaxOne_Accepted) {
    ParticleBudget budget(1u);
    EXPECT_EQ(budget.GetMax(), 1u);
    EXPECT_EQ(budget.GetUsed(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Allocate — success paths
// ─────────────────────────────────────────────────────────────────────────────

/// Allocate() returns true and grants the full requested amount when budget
/// has enough room.
TEST(ParticleBudget, Allocate_BelowMax_ReturnsTrueGrantsFull) {
    ParticleBudget budget(10000u);
    uint32_t granted = 0;

    const bool ok = budget.Allocate(1000u, granted);

    EXPECT_TRUE(ok);
    EXPECT_EQ(granted, 1000u);
}

/// After a successful Allocate(), GetUsed() reflects the allocation.
TEST(ParticleBudget, Allocate_Success_GetUsedIncreases) {
    ParticleBudget budget(10000u);
    uint32_t granted = 0;
    budget.Allocate(500u, granted);

    EXPECT_EQ(budget.GetUsed(), 500u);
}

/// Allocating exactly the max in one call is permitted.
TEST(ParticleBudget, Allocate_ExactlyMax_GrantsFull) {
    ParticleBudget budget(1000u);
    uint32_t granted = 0;

    const bool ok = budget.Allocate(1000u, granted);

    EXPECT_TRUE(ok);
    EXPECT_EQ(granted, 1000u);
    EXPECT_EQ(budget.GetUsed(), 1000u);
    EXPECT_EQ(budget.GetAvailable(), 0u);
}

/// When the request is larger than available, Allocate() grants only what is
/// available and returns true (partial grant).
TEST(ParticleBudget, Allocate_PartialGrant_WhenNearlyFull) {
    ParticleBudget budget(100u);
    uint32_t g1 = 0, g2 = 0;

    budget.Allocate(90u, g1);    // 10 remaining
    const bool ok = budget.Allocate(50u, g2); // can only give 10

    EXPECT_TRUE(ok) << "Partial grant must still return true";
    EXPECT_EQ(g2, 10u) << "Partial grant amount must equal remaining available";
    EXPECT_EQ(budget.GetUsed(), 100u);
    EXPECT_EQ(budget.GetAvailable(), 0u);
}

/// Multiple sequential Allocate() calls each consume their requested amount.
TEST(ParticleBudget, Allocate_MultipleSequential_SumsCorrectly) {
    ParticleBudget budget(1000u);
    uint32_t g = 0;

    budget.Allocate(100u, g);
    budget.Allocate(200u, g);
    budget.Allocate(300u, g);

    EXPECT_EQ(budget.GetUsed(), 600u);
    EXPECT_EQ(budget.GetAvailable(), 400u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Allocate — failure / exhausted
// ─────────────────────────────────────────────────────────────────────────────

/// Allocate() returns false and grants 0 when budget is fully exhausted.
TEST(ParticleBudget, Allocate_Exhausted_ReturnsFalse) {
    ParticleBudget budget(100u);
    uint32_t g1 = 0, g2 = 0;

    budget.Allocate(100u, g1);   // drain the budget
    const bool ok = budget.Allocate(1u, g2);

    EXPECT_FALSE(ok) << "Allocate() on exhausted budget must return false";
    EXPECT_EQ(g2, 0u) << "Granted count must be 0 when budget is exhausted";
}

/// Allocate() with requested=0 returns false immediately (no CAS needed).
TEST(ParticleBudget, Allocate_ZeroRequested_ReturnsFalse) {
    ParticleBudget budget(10000u);
    uint32_t granted = 99u;  // non-zero sentinel to detect mutation

    const bool ok = budget.Allocate(0u, granted);

    EXPECT_FALSE(ok) << "Allocate(0) must return false";
    EXPECT_EQ(granted, 0u) << "Allocated must be set to 0 for a zero request";
    EXPECT_EQ(budget.GetUsed(), 0u) << "GetUsed must remain 0 after zero request";
}

// ─────────────────────────────────────────────────────────────────────────────
// Return
// ─────────────────────────────────────────────────────────────────────────────

/// Return() decrements GetUsed() by exactly the returned amount.
TEST(ParticleBudget, Return_Normal_DecreasesUsed) {
    ParticleBudget budget(1000u);
    uint32_t granted = 0;
    budget.Allocate(500u, granted);

    budget.Return(200u);

    EXPECT_EQ(budget.GetUsed(), 300u);
    EXPECT_EQ(budget.GetAvailable(), 700u);
}

/// Returning the full allocation brings GetUsed() back to zero.
TEST(ParticleBudget, Return_FullAmount_UsedBecomesZero) {
    ParticleBudget budget(1000u);
    uint32_t granted = 0;
    budget.Allocate(800u, granted);

    budget.Return(800u);

    EXPECT_EQ(budget.GetUsed(), 0u);
    EXPECT_EQ(budget.GetAvailable(), 1000u);
}

/// Return(0) is a no-op — GetUsed() stays unchanged.
TEST(ParticleBudget, Return_ZeroCount_IsNoOp) {
    ParticleBudget budget(1000u);
    uint32_t granted = 0;
    budget.Allocate(100u, granted);

    budget.Return(0u);

    EXPECT_EQ(budget.GetUsed(), 100u)
        << "Return(0) must not change the used count";
}

/// After Allocate + full Return, another full Allocate succeeds again.
TEST(ParticleBudget, Return_ThenReallocate_Works) {
    ParticleBudget budget(100u);
    uint32_t g = 0;

    budget.Allocate(100u, g);
    budget.Return(100u);

    g = 0;
    const bool ok = budget.Allocate(100u, g);

    EXPECT_TRUE(ok) << "Full re-allocation after Return must succeed";
    EXPECT_EQ(g, 100u);

    budget.Return(100u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetAvailable / GetMax / GetUsageRatio
// ─────────────────────────────────────────────────────────────────────────────

/// GetAvailable() = GetMax() - GetUsed() for a fresh budget.
TEST(ParticleBudget, GetAvailable_Fresh_EqualToMax) {
    ParticleBudget budget(5000u);
    EXPECT_EQ(budget.GetAvailable(), 5000u);
}

/// GetAvailable() returns 0 when the budget is fully exhausted.
TEST(ParticleBudget, GetAvailable_Exhausted_IsZero) {
    ParticleBudget budget(200u);
    uint32_t g = 0;
    budget.Allocate(200u, g);

    EXPECT_EQ(budget.GetAvailable(), 0u);

    budget.Return(200u);
}

/// GetUsageRatio() is 0.0f on an empty budget.
TEST(ParticleBudget, GetUsageRatio_Empty_IsZero) {
    ParticleBudget budget(1000u);
    EXPECT_FLOAT_EQ(budget.GetUsageRatio(), 0.0f);
}

/// GetUsageRatio() is 1.0f when fully allocated.
TEST(ParticleBudget, GetUsageRatio_Full_IsOne) {
    ParticleBudget budget(1000u);
    uint32_t g = 0;
    budget.Allocate(1000u, g);

    EXPECT_FLOAT_EQ(budget.GetUsageRatio(), 1.0f);

    budget.Return(1000u);
}

/// GetUsageRatio() is approximately 0.5 at half-capacity.
TEST(ParticleBudget, GetUsageRatio_Half_IsPointFive) {
    ParticleBudget budget(1000u);
    uint32_t g = 0;
    budget.Allocate(500u, g);

    EXPECT_NEAR(budget.GetUsageRatio(), 0.5f, 1e-6f);

    budget.Return(500u);
}

// ─────────────────────────────────────────────────────────────────────────────
// SetMax
// ─────────────────────────────────────────────────────────────────────────────

/// SetMax() updates GetMax() to the new value.
TEST(ParticleBudget, SetMax_UpdatesGetMax) {
    ParticleBudget budget(1000u);
    budget.SetMax(2000u);
    EXPECT_EQ(budget.GetMax(), 2000u);
}

/// Increasing SetMax() immediately allows additional Allocate() calls.
TEST(ParticleBudget, SetMax_Increase_AllowsMoreAllocations) {
    ParticleBudget budget(100u);
    uint32_t g = 0;
    budget.Allocate(100u, g);

    budget.SetMax(200u);

    g = 0;
    const bool ok = budget.Allocate(50u, g);
    EXPECT_TRUE(ok) << "Allocation after SetMax increase must succeed";
    EXPECT_EQ(g, 50u);

    budget.Return(150u);
}

/// Decreasing SetMax() below current usage prevents further Allocate() calls.
TEST(ParticleBudget, SetMax_Decrease_BelowUsed_PreventsNewAllocations) {
    ParticleBudget budget(1000u);
    uint32_t g = 0;
    budget.Allocate(500u, g);

    budget.SetMax(300u);  // now max < used

    g = 0;
    const bool ok = budget.Allocate(1u, g);
    EXPECT_FALSE(ok) << "Allocation must fail when max is set below current usage";

    budget.Return(500u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Thread Safety — concurrent Allocate/Return
// ─────────────────────────────────────────────────────────────────────────────

/// Concurrent Allocate() calls from multiple threads must not over-allocate
/// beyond the maximum budget.
TEST(ParticleBudget, ThreadSafety_ConcurrentAllocate_NeverExceedsMax) {
    constexpr uint32_t kMax      = 100000u;
    constexpr uint32_t kPerThread = 1000u;
    constexpr int      kThreads  = 16;

    ParticleBudget budget(kMax);
    std::atomic<uint32_t> totalGranted{0};

    auto task = [&]() {
        uint32_t g = 0;
        if (budget.Allocate(kPerThread, g)) {
            totalGranted.fetch_add(g, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) threads.emplace_back(task);
    for (auto& t : threads) t.join();

    EXPECT_LE(totalGranted.load(), kMax)
        << "Total granted particles must never exceed the max budget";
    EXPECT_EQ(budget.GetUsed(), totalGranted.load())
        << "GetUsed() must equal sum of all successful grants";

    budget.Return(totalGranted.load());
    EXPECT_EQ(budget.GetUsed(), 0u);
}

/// Concurrent Allocate() and Return() calls from different threads must not
/// corrupt the internal counter — the final GetUsed() must match the expected
/// net allocation.
TEST(ParticleBudget, ThreadSafety_ConcurrentAllocateAndReturn_ConsistentState) {
    constexpr uint32_t kMax       = 200000u;
    constexpr uint32_t kAllocSize = 100u;
    constexpr int      kAllocThreads  = 8;
    constexpr int      kReturnThreads = 4;

    ParticleBudget budget(kMax);

    // Pre-allocate so Return threads have something to return.
    constexpr uint32_t kPreAlloc = kAllocSize * kReturnThreads;
    uint32_t preGranted = 0;
    budget.Allocate(kPreAlloc, preGranted);
    ASSERT_EQ(preGranted, kPreAlloc);

    std::atomic<uint32_t> totalNewGrants{0};

    auto allocTask = [&]() {
        uint32_t g = 0;
        if (budget.Allocate(kAllocSize, g)) {
            totalNewGrants.fetch_add(g, std::memory_order_relaxed);
        }
    };

    auto returnTask = [&]() {
        budget.Return(kAllocSize);
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < kAllocThreads;  ++i) threads.emplace_back(allocTask);
    for (int i = 0; i < kReturnThreads; ++i) threads.emplace_back(returnTask);
    for (auto& t : threads) t.join();

    // Expected used = preAlloc - returnedByReturnThreads + newGrants
    const uint32_t expectedUsed = preGranted
        - (kAllocSize * kReturnThreads)
        + totalNewGrants.load();

    EXPECT_EQ(budget.GetUsed(), expectedUsed)
        << "Final GetUsed() must equal net particle allocation after concurrent ops";

    budget.Return(expectedUsed);
    EXPECT_EQ(budget.GetUsed(), 0u);
}

/// Many threads each performing small Allocate+Return cycles must leave
/// GetUsed() at zero when all have finished.
TEST(ParticleBudget, ThreadSafety_AllocateReturnCycles_FinalUsedIsZero) {
    constexpr uint32_t kMax     = 50000u;
    constexpr int      kThreads = 10;
    constexpr int      kCycles  = 100;
    constexpr uint32_t kChunk   = 10u;

    ParticleBudget budget(kMax);

    auto task = [&]() {
        for (int c = 0; c < kCycles; ++c) {
            uint32_t g = 0;
            if (budget.Allocate(kChunk, g)) {
                budget.Return(g);
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) threads.emplace_back(task);
    for (auto& t : threads) t.join();

    EXPECT_EQ(budget.GetUsed(), 0u)
        << "After balanced Allocate/Return cycles GetUsed() must be 0";
}
