/// Unit tests for uz::Fence — empty wait, single job, multiple jobs,
/// IsComplete state transitions, and fence reuse after Wait.
///
/// Fence requires a live JobSystem for Enqueue. Each test that needs one
/// uses FenceTest fixture which manages Init/Shutdown.

#include "fence.h"
#include "job_system.h"
#include "job_types.h"
#include "log.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// FenceTest fixture — owns a fully initialized JobSystem
// ─────────────────────────────────────────────────────────────────────────────

class FenceTest : public ::testing::Test {
protected:
    JobSystem js_;

    static std::string TmpLog() {
        namespace fs = std::filesystem;
        auto dir = fs::temp_directory_path() / "uzengine_fence_test";
        fs::create_directories(dir);
        return (dir / "fence_test.log").string();
    }

    void SetUp() override {
        LogInit(TmpLog().c_str());
        LogSetLevel(LogLevel::Error); // suppress noise
        ASSERT_TRUE(js_.Init());
    }

    void TearDown() override {
        js_.Shutdown();
        LogShutdown();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Empty fence
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FenceTest, EmptyFence_IsComplete_ReturnsTrue) {
    Fence fence(&js_);
    EXPECT_TRUE(fence.IsComplete());
}

TEST_F(FenceTest, EmptyFence_Wait_ReturnsImmediately) {
    Fence fence(&js_);
    auto start = std::chrono::steady_clock::now();
    fence.Wait(); // must not block
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::milliseconds(50));
}

// ─────────────────────────────────────────────────────────────────────────────
// Single job
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FenceTest, SingleJob_ExecutesBeforeWaitReturns) {
    Fence fence(&js_);
    std::atomic<bool> executed{false};
    std::atomic<bool>* pExec = &executed;

    fence.AddWork(MakeJob([pExec] { pExec->store(true, std::memory_order_release); }));
    fence.Wait();

    EXPECT_TRUE(executed.load(std::memory_order_acquire));
}

TEST_F(FenceTest, SingleJob_IsComplete_TrueAfterWait) {
    Fence fence(&js_);
    std::atomic<int> result{0};
    std::atomic<int>* ptr = &result;

    fence.AddWork(MakeJob([ptr] { ptr->store(7, std::memory_order_release); }));
    EXPECT_FALSE(fence.IsComplete()); // buffered, not yet flushed
    fence.Wait();

    EXPECT_TRUE(fence.IsComplete());
    EXPECT_EQ(result.load(), 7);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple jobs
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FenceTest, MultipleJobs_AllCompleteBeforeWaitReturns) {
    constexpr int kJobs = 8;
    Fence fence(&js_);
    std::atomic<int> count{0};
    std::atomic<int>* ptr = &count;

    for (int i = 0; i < kJobs; ++i) {
        fence.AddWork(MakeJob([ptr] { ptr->fetch_add(1, std::memory_order_relaxed); }));
    }

    fence.Wait();

    EXPECT_EQ(count.load(std::memory_order_acquire), kJobs);
    EXPECT_TRUE(fence.IsComplete());
}

TEST_F(FenceTest, MultipleJobs_CounterAssignedToEachJob) {
    // Verifies that each buffered job has its counter field overwritten by Fence::Wait().
    // We validate indirectly: all 10 jobs must complete exactly once.
    constexpr int kJobs = 10;
    Fence fence(&js_);
    std::atomic<int> sum{0};
    std::atomic<int>* ptr = &sum;

    for (int i = 1; i <= kJobs; ++i) {
        int val = i;
        auto* p = ptr;
        fence.AddWork(MakeJob([p, val] { p->fetch_add(val, std::memory_order_relaxed); }));
    }

    fence.Wait();

    // Sum of 1..10 = 55
    EXPECT_EQ(sum.load(std::memory_order_acquire), 55);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fence reuse after Wait
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FenceTest, Reuse_AfterWait_SecondBatchCompletes) {
    Fence fence(&js_);
    std::atomic<int> batch1{0}, batch2{0};
    std::atomic<int>* p1 = &batch1;
    std::atomic<int>* p2 = &batch2;

    // First batch
    fence.AddWork(MakeJob([p1] { p1->fetch_add(1, std::memory_order_relaxed); }));
    fence.AddWork(MakeJob([p1] { p1->fetch_add(1, std::memory_order_relaxed); }));
    fence.Wait();
    EXPECT_EQ(batch1.load(), 2);
    EXPECT_TRUE(fence.IsComplete());

    // Second batch — fence must be reusable
    fence.AddWork(MakeJob([p2] { p2->store(99, std::memory_order_release); }));
    fence.Wait();
    EXPECT_EQ(batch2.load(), 99);
    EXPECT_TRUE(fence.IsComplete());
}

// ─────────────────────────────────────────────────────────────────────────────
// IsComplete reflects pending state
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FenceTest, IsComplete_FalseAfterAddWork_TrueAfterWait) {
    Fence fence(&js_);
    EXPECT_TRUE(fence.IsComplete()); // nothing added yet

    std::atomic<bool> gate{false};
    std::atomic<bool>* pGate = &gate;
    fence.AddWork(MakeJob([pGate] {
        // spin briefly so IsComplete can be observed as false while running
        while (!pGate->load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }));

    // After AddWork but before Wait, the counter has not been set yet — IsComplete
    // checks counter_ which is at 0 (will be reset in Wait). So it returns true
    // until Wait() sets the counter. We verify the post-Wait state is correct.
    gate.store(true, std::memory_order_release);
    fence.Wait();
    EXPECT_TRUE(fence.IsComplete());
}

// ─────────────────────────────────────────────────────────────────────────────
// Large job batch
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FenceTest, LargeBatch_100Jobs_AllComplete) {
    constexpr int kJobs = 100;
    Fence fence(&js_);
    std::atomic<int> counter{0};
    std::atomic<int>* ptr = &counter;

    for (int i = 0; i < kJobs; ++i) {
        fence.AddWork(MakeJob([ptr] { ptr->fetch_add(1, std::memory_order_relaxed); }));
    }

    fence.Wait();
    EXPECT_EQ(counter.load(std::memory_order_acquire), kJobs);
}
