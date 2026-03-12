/// Unit tests for the profiler module — Tracy scope instrumentation,
/// stat counter accumulation, and zero-overhead no-op behaviour when
/// profiling is disabled at compile time.
///
/// Extended with thread-safety tests for StatCounter concurrent access
/// and macro expansion tests for nested scope instrumentation.
#include "profiler.h"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

UZ_DEFINE_STAT(Rendering, TestDrawCalls);
UZ_DEFINE_STAT(Rendering, TestConcurrentCounter);
UZ_DEFINE_STAT(Rendering, TestMacroCounter);

// ─────────────────────────────────────────────────────────────────────────────
// StatCounter — basic increment / reset
// ─────────────────────────────────────────────────────────────────────────────

TEST(Profiler, StatCounterIncrementAndReset) {
    Stat_Rendering_TestDrawCalls.Reset();
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 0);

    Stat_Rendering_TestDrawCalls.Increment();
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 1);

    Stat_Rendering_TestDrawCalls.Increment(9);
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 10);

    Stat_Rendering_TestDrawCalls.Reset();
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// StatCounter — thread safety
// ─────────────────────────────────────────────────────────────────────────────

TEST(Profiler, StatCounter_ConcurrentIncrement_FinalCountIsExact) {
    constexpr int kThreads         = 4;
    constexpr int kIncrementsEach  = 10000;

    Stat_Rendering_TestConcurrentCounter.Reset();

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([] {
            for (int j = 0; j < kIncrementsEach; ++j) {
                Stat_Rendering_TestConcurrentCounter.Increment();
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(Stat_Rendering_TestConcurrentCounter.Get(),
              static_cast<int64_t>(kThreads) * kIncrementsEach);
}

TEST(Profiler, StatCounter_ResetDuringConcurrentAccess_NoCorruption) {
    // Start threads incrementing continuously; perform a reset mid-way.
    // After all threads finish, the counter must be non-negative — the
    // atomic operations may not linearise perfectly around the reset, but
    // memory must not be corrupted (negative values would indicate UB/tearing).
    constexpr int kThreads        = 4;
    constexpr int kIncrementsEach = 5000;

    Stat_Rendering_TestConcurrentCounter.Reset();
    std::atomic<bool> go{false};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&go] {
            while (!go.load(std::memory_order_acquire)) { /* spin */ }
            for (int j = 0; j < kIncrementsEach; ++j) {
                Stat_Rendering_TestConcurrentCounter.Increment();
            }
        });
    }

    go.store(true, std::memory_order_release);

    // Reset once while threads are live — no crash, no corruption.
    Stat_Rendering_TestConcurrentCounter.Reset();

    for (auto& t : threads) {
        t.join();
    }

    // Counter could be anywhere from 0 up to kThreads*kIncrementsEach
    // depending on timing — what matters is no negative (corruption).
    EXPECT_GE(Stat_Rendering_TestConcurrentCounter.Get(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// StatCounter — UZ_STAT_INCREMENT / UZ_STAT_GET / UZ_STAT_RESET macros
// ─────────────────────────────────────────────────────────────────────────────

TEST(Profiler, StatMacros_IncrementGetReset_WorkCorrectly) {
    UZ_STAT_RESET(Rendering, TestMacroCounter);
    EXPECT_EQ(UZ_STAT_GET(Rendering, TestMacroCounter), 0);

    // UZ_STAT_INCREMENT(Group, Name, delta) expands to
    // Stat_Group_Name.Increment(delta + 0). Use explicit delta=1 because
    // the zero-arg form expands to Increment(+0) = Increment(0) by the
    // variadic macro's __VA_ARGS__ + 0 pattern.
    UZ_STAT_INCREMENT(Rendering, TestMacroCounter, 1);
    EXPECT_EQ(UZ_STAT_GET(Rendering, TestMacroCounter), 1);

    UZ_STAT_INCREMENT(Rendering, TestMacroCounter, 4);
    EXPECT_EQ(UZ_STAT_GET(Rendering, TestMacroCounter), 5);

    UZ_STAT_RESET(Rendering, TestMacroCounter);
    EXPECT_EQ(UZ_STAT_GET(Rendering, TestMacroCounter), 0);
}

TEST(Profiler, StatCounter_NegativeDelta_DecreasesValue) {
    Stat_Rendering_TestDrawCalls.Reset();
    Stat_Rendering_TestDrawCalls.Increment(10);
    Stat_Rendering_TestDrawCalls.Increment(-3);
    EXPECT_EQ(Stat_Rendering_TestDrawCalls.Get(), 7);
}

// ─────────────────────────────────────────────────────────────────────────────
// UZ_PROFILE_SCOPE / UZ_PROFILE_FUNCTION — macro expansion & nesting
// ─────────────────────────────────────────────────────────────────────────────

TEST(Profiler, MacrosCompileAndRunWithoutCrash) {
    { UZ_PROFILE_SCOPE(TestScope); }
    { UZ_PROFILE_FUNCTION(); }
    UZ_PROFILE_FRAME_MARK();
}

TEST(Profiler, ProfileScope_NestedScopes_CompileAndRunWithoutCrash) {
    // Three levels of nesting: ensures macro does not name-clash at the
    // same nesting depth and that RAII destruction order is correct.
    {
        UZ_PROFILE_SCOPE(OuterScope);
        {
            UZ_PROFILE_SCOPE(MiddleScope);
            {
                UZ_PROFILE_SCOPE(InnerScope);
                // Intentional empty innermost scope.
            }
        }
    }
    // If we reach here, no crash, no stack corruption.
    SUCCEED();
}

TEST(Profiler, ProfileScope_SiblingScopes_DoNotNameClash) {
    // Two sibling scopes in the same block must not produce duplicate
    // identifiers — the macro uses __LINE__ to disambiguate.
    { UZ_PROFILE_SCOPE(SiblingA); }
    { UZ_PROFILE_SCOPE(SiblingB); }
    SUCCEED();
}

TEST(Profiler, ProfileFunction_InsideLambda_CompilesAndRuns) {
    auto work = [] {
        UZ_PROFILE_FUNCTION();
        // Simulate minimal work.
        volatile int x = 0;
        (void)x;
    };
    work();
    SUCCEED();
}
