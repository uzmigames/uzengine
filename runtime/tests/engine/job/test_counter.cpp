/// Unit tests for uz::Counter — initial state, Reset, Decrement, IsComplete,
/// Wait (immediate return), Wait (blocking), and multiple concurrent waiters.

#include "counter.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Initial state
// ─────────────────────────────────────────────────────────────────────────────

TEST(Counter, Default_CountIsZero_IsComplete) {
    Counter c;
    EXPECT_TRUE(c.IsComplete());
}

TEST(Counter, Default_Wait_ReturnsImmediately) {
    Counter c;
    // Must not block — if it blocks the test will time out.
    c.Wait();
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// Reset
// ─────────────────────────────────────────────────────────────────────────────

TEST(Counter, Reset_SetsCount_IsCompleteReturnsFalse) {
    Counter c;
    c.Reset(3);
    EXPECT_FALSE(c.IsComplete());
}

TEST(Counter, Reset_WithOne_IsCompleteReturnsFalse) {
    Counter c;
    c.Reset(1);
    EXPECT_FALSE(c.IsComplete());
}

TEST(Counter, Reset_WithZero_IsCompleteReturnsTrue) {
    // Resetting to zero means immediately complete.
    Counter c;
    c.Reset(1);
    EXPECT_FALSE(c.IsComplete());
    c.Reset(0);
    EXPECT_TRUE(c.IsComplete());
}

// ─────────────────────────────────────────────────────────────────────────────
// Decrement
// ─────────────────────────────────────────────────────────────────────────────

TEST(Counter, Decrement_DecreasesCount) {
    Counter c;
    c.Reset(3);
    EXPECT_FALSE(c.IsComplete());
    c.Decrement();
    EXPECT_FALSE(c.IsComplete());
    c.Decrement();
    EXPECT_FALSE(c.IsComplete());
}

TEST(Counter, Decrement_ToZero_IsCompleteReturnsTrue) {
    Counter c;
    c.Reset(1);
    EXPECT_FALSE(c.IsComplete());
    c.Decrement();
    EXPECT_TRUE(c.IsComplete());
}

TEST(Counter, Decrement_MultipleSteps_ReachesZero) {
    Counter c;
    constexpr uint32_t kCount = 5;
    c.Reset(kCount);
    for (uint32_t i = 0; i < kCount; ++i) {
        EXPECT_FALSE(c.IsComplete()) << "Should not be complete at step " << i;
        c.Decrement();
    }
    EXPECT_TRUE(c.IsComplete());
}

// ─────────────────────────────────────────────────────────────────────────────
// Wait — blocks until zero
// ─────────────────────────────────────────────────────────────────────────────

TEST(Counter, Wait_AlreadyComplete_ReturnsImmediately) {
    Counter c;
    // Default count = 0, so Wait must return without blocking.
    auto start = std::chrono::steady_clock::now();
    c.Wait();
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::milliseconds(50));
}

TEST(Counter, Wait_BlocksUntilDecremented_ToZero) {
    Counter c;
    c.Reset(1);

    std::atomic<bool> workerStarted{false};
    std::atomic<bool> waitReturned{false};

    // Worker thread decrements after a short delay
    std::thread worker([&] {
        workerStarted.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        c.Decrement();
    });

    // Wait for worker to signal it has started before calling Wait()
    while (!workerStarted.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    c.Wait(); // Must not return before Decrement()
    waitReturned.store(true, std::memory_order_release);

    worker.join();

    EXPECT_TRUE(waitReturned.load());
    EXPECT_TRUE(c.IsComplete());
}

TEST(Counter, Wait_BlocksForMultipleDecrements) {
    constexpr uint32_t kJobs = 4;
    Counter c;
    c.Reset(kJobs);

    std::vector<std::thread> threads;
    threads.reserve(kJobs);
    for (uint32_t i = 0; i < kJobs; ++i) {
        threads.emplace_back([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            c.Decrement();
        });
    }

    c.Wait();
    EXPECT_TRUE(c.IsComplete());

    for (auto& t : threads) {
        t.join();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple concurrent waiters
// ─────────────────────────────────────────────────────────────────────────────

TEST(Counter, MultipleWaiters_AllUnblockWhenZero) {
    Counter c;
    c.Reset(1);

    constexpr int kWaiters = 4;
    std::atomic<int> unblockedCount{0};

    std::vector<std::thread> waiters;
    waiters.reserve(kWaiters);
    for (int i = 0; i < kWaiters; ++i) {
        waiters.emplace_back([&] {
            c.Wait();
            unblockedCount.fetch_add(1, std::memory_order_release);
        });
    }

    // Give waiters time to enter Wait()
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    c.Decrement(); // Triggers notify_all

    for (auto& t : waiters) {
        t.join();
    }

    EXPECT_EQ(unblockedCount.load(), kWaiters);
    EXPECT_TRUE(c.IsComplete());
}

// ─────────────────────────────────────────────────────────────────────────────
// Reuse after Wait
// ─────────────────────────────────────────────────────────────────────────────

TEST(Counter, Reuse_AfterWait_WorksCorrectly) {
    Counter c;

    // First batch
    c.Reset(2);
    c.Decrement();
    c.Decrement();
    c.Wait();
    EXPECT_TRUE(c.IsComplete());

    // Second batch — counter must be reusable
    c.Reset(3);
    EXPECT_FALSE(c.IsComplete());
    c.Decrement();
    c.Decrement();
    c.Decrement();
    c.Wait();
    EXPECT_TRUE(c.IsComplete());
}

// ─────────────────────────────────────────────────────────────────────────────
// Concurrent Decrement — no lost decrements
// ─────────────────────────────────────────────────────────────────────────────

TEST(Counter, ConcurrentDecrements_AllCountedCorrectly) {
    constexpr uint32_t kThreads = 8;
    Counter c;
    c.Reset(kThreads);

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (uint32_t i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] { c.Decrement(); });
    }

    c.Wait();
    EXPECT_TRUE(c.IsComplete());

    for (auto& t : threads) {
        t.join();
    }
}
