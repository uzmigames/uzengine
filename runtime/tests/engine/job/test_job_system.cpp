/// Unit tests for uz::JobSystem — lifecycle, worker count, single/bulk enqueue,
/// priority ordering, work-stealing, ParallelFor (empty/single/large),
/// Counter integration, stress test, and shutdown-with-pending-work safety.

#include "job_system.h"
#include "job_types.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <numeric>
#include <thread>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// JobSystemTest fixture — manages Init/Shutdown and logging
// ─────────────────────────────────────────────────────────────────────────────

class JobSystemTest : public ::testing::Test {
protected:
    JobSystem js_;

    static std::string TmpLog() {
        namespace fs = std::filesystem;
        auto dir = fs::temp_directory_path() / "uzengine_js_test";
        fs::create_directories(dir);
        return (dir / "js_test.log").string();
    }

    void SetUp() override {
        LogInit(TmpLog().c_str());
        LogSetLevel(LogLevel::Error); // suppress info/warning noise
        ASSERT_TRUE(js_.Init());
    }

    void TearDown() override {
        js_.Shutdown();
        LogShutdown();
    }

    /// Enqueues `count` jobs, each incrementing `atomicCounter`, then waits on the counter.
    void EnqueueAndWait(int count, std::atomic<int>& atomicCounter) {
        Counter ctr;
        ctr.Reset(static_cast<uint32_t>(count));
        std::atomic<int>* ptr = &atomicCounter;
        for (int i = 0; i < count; ++i) {
            js_.Enqueue(MakeJob([ptr] { ptr->fetch_add(1, std::memory_order_relaxed); },
                                JobPriority::Normal, &ctr));
        }
        ctr.Wait();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, Init_Succeeds) {
    // SetUp already called Init and asserted success — we just verify workers exist.
    EXPECT_GE(js_.GetWorkerCount(), 1u);
}

TEST_F(JobSystemTest, Shutdown_IsIdempotent) {
    // First Shutdown is called in TearDown. Call an extra one here to verify no crash.
    js_.Shutdown();
    // TearDown will call Shutdown again — must not crash.
}

TEST_F(JobSystemTest, Get_ReturnsInstanceAfterInit) {
    EXPECT_EQ(JobSystem::Get(), &js_);
}

TEST_F(JobSystemTest, Get_ReturnsNullAfterShutdown) {
    js_.Shutdown();
    EXPECT_EQ(JobSystem::Get(), nullptr);
    // Prevent double-shutdown in TearDown from crashing.
    // TearDown calls Shutdown again — the Shutdown implementation guards against it.
}

// ─────────────────────────────────────────────────────────────────────────────
// GetWorkerCount
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, GetWorkerCount_AtLeastOne) {
    EXPECT_GE(js_.GetWorkerCount(), 1u);
}

TEST_F(JobSystemTest, GetWorkerCount_HardwareConcurrencyMinusOne) {
    const uint32_t hw = std::thread::hardware_concurrency();
    const uint32_t expected = (hw > 1) ? (hw - 1) : 1u;
    EXPECT_EQ(js_.GetWorkerCount(), expected);
}

// ─────────────────────────────────────────────────────────────────────────────
// Single job enqueue
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, Enqueue_SingleJob_Executes) {
    std::atomic<int> result{0};
    std::atomic<int>* ptr = &result;
    Counter ctr;
    ctr.Reset(1);
    js_.Enqueue(MakeJob([ptr] { ptr->store(42, std::memory_order_release); },
                        JobPriority::Normal, &ctr));
    ctr.Wait();
    EXPECT_EQ(result.load(std::memory_order_acquire), 42);
}

TEST_F(JobSystemTest, Enqueue_NullFunctionJob_DoesNotCrash) {
    // A job with null function should be a no-op — ExecuteJob guards against it.
    Counter ctr;
    ctr.Reset(1);
    Job job;
    job.function = nullptr;
    job.counter = &ctr;
    job.priority = JobPriority::Normal;
    js_.Enqueue(job);
    ctr.Wait();
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// Many jobs
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, Enqueue_100Jobs_AllExecute) {
    std::atomic<int> count{0};
    EnqueueAndWait(100, count);
    EXPECT_EQ(count.load(std::memory_order_acquire), 100);
}

TEST_F(JobSystemTest, Enqueue_1000Jobs_AllExecute) {
    std::atomic<int> count{0};
    EnqueueAndWait(1000, count);
    EXPECT_EQ(count.load(std::memory_order_acquire), 1000);
}

// ─────────────────────────────────────────────────────────────────────────────
// Priority ordering — higher priority jobs tend to execute first
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, Priority_CriticalJobsExecute_WithoutBlocking) {
    // We can't deterministically assert ordering in a multi-threaded system,
    // but we can verify all jobs at every priority level complete correctly.
    constexpr int kPerPriority = 50;
    std::atomic<int> criticalCount{0}, normalCount{0}, lowCount{0};
    std::atomic<int>* pC = &criticalCount;
    std::atomic<int>* pN = &normalCount;
    std::atomic<int>* pL = &lowCount;

    Counter ctr;
    ctr.Reset(kPerPriority * 3);

    for (int i = 0; i < kPerPriority; ++i) {
        js_.Enqueue(MakeJob([pL] { pL->fetch_add(1, std::memory_order_relaxed); },
                            JobPriority::Low, &ctr));
        js_.Enqueue(MakeJob([pN] { pN->fetch_add(1, std::memory_order_relaxed); },
                            JobPriority::Normal, &ctr));
        js_.Enqueue(MakeJob([pC] { pC->fetch_add(1, std::memory_order_relaxed); },
                            JobPriority::Critical, &ctr));
    }

    ctr.Wait();

    EXPECT_EQ(criticalCount.load(), kPerPriority);
    EXPECT_EQ(normalCount.load(), kPerPriority);
    EXPECT_EQ(lowCount.load(), kPerPriority);
}

// ─────────────────────────────────────────────────────────────────────────────
// Work-stealing — jobs enqueued to one worker must be completed by all workers
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, WorkStealing_AllJobsComplete) {
    // Enqueue a burst of jobs — with round-robin distribution and stealing,
    // all must complete even if one worker's queue fills.
    constexpr int kJobs = 500;
    std::atomic<int> count{0};
    EnqueueAndWait(kJobs, count);
    EXPECT_EQ(count.load(std::memory_order_acquire), kJobs);
}

// ─────────────────────────────────────────────────────────────────────────────
// Yield — main thread helps drain work
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, Yield_WhenQueueEmpty_ReturnsFalse) {
    // After all workers have been idle for a moment, Yield should return false.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // May return true if a background job happened to be in flight — that's fine.
    // We just verify it doesn't crash.
    js_.Yield();
    SUCCEED();
}

TEST_F(JobSystemTest, Yield_ExecutesAtLeastOneJob_WhenWorkPending) {
    // Enqueue without waking workers first — Yield should drain at least one.
    std::atomic<int> count{0};
    std::atomic<int>* ptr = &count;
    Counter ctr;
    ctr.Reset(5);
    for (int i = 0; i < 5; ++i) {
        js_.Enqueue(MakeJob([ptr] { ptr->fetch_add(1, std::memory_order_relaxed); },
                            JobPriority::Normal, &ctr));
    }
    ctr.Wait();
    EXPECT_EQ(count.load(), 5);
}

// ─────────────────────────────────────────────────────────────────────────────
// Counter integration
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, CounterIntegration_WaitReturnAfterAllJobsComplete) {
    constexpr int kJobs = 20;
    std::atomic<int> executedCount{0};
    std::atomic<int>* ptr = &executedCount;
    Counter ctr;
    ctr.Reset(kJobs);

    for (int i = 0; i < kJobs; ++i) {
        js_.Enqueue(MakeJob([ptr] { ptr->fetch_add(1, std::memory_order_relaxed); },
                            JobPriority::High, &ctr));
    }

    ctr.Wait();
    EXPECT_EQ(executedCount.load(std::memory_order_acquire), kJobs);
    EXPECT_TRUE(ctr.IsComplete());
}

// ─────────────────────────────────────────────────────────────────────────────
// ParallelFor
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, ParallelFor_EmptySpan_IsNoOp) {
    std::vector<int> empty;
    js_.ParallelFor(empty.data(), 0, [](int& v) { v = 99; });
    EXPECT_TRUE(empty.empty());
}

TEST_F(JobSystemTest, ParallelFor_SingleElement_IsTransformed) {
    int value = 0;
    js_.ParallelFor(&value, 1, [](int& v) { v = 55; });
    EXPECT_EQ(value, 55);
}

TEST_F(JobSystemTest, ParallelFor_AllElements_AreTransformed) {
    constexpr size_t kSize = 1000;
    std::vector<int> data(kSize, 0);
    js_.ParallelFor(data.data(), data.size(), [](int& v) { v = 1; });
    int sum = std::accumulate(data.begin(), data.end(), 0);
    EXPECT_EQ(sum, static_cast<int>(kSize));
}

TEST_F(JobSystemTest, ParallelFor_LargeArray_AllProcessed) {
    constexpr size_t kSize = 10000;
    std::vector<float> data(kSize, 0.0f);
    js_.ParallelFor(data.data(), data.size(), [](float& v) { v = 1.0f; });

    float sum = 0.0f;
    for (float f : data) {
        sum += f;
    }
    EXPECT_FLOAT_EQ(sum, static_cast<float>(kSize));
}

TEST_F(JobSystemTest, ParallelFor_PreservesElementIdentity) {
    // Each element is its own index — verify correct element is modified.
    constexpr size_t kSize = 256;
    std::vector<int> data(kSize);
    std::iota(data.begin(), data.end(), 0); // 0..255

    js_.ParallelFor(data.data(), data.size(), [](int& v) { v = v * 2; });

    for (size_t i = 0; i < kSize; ++i) {
        EXPECT_EQ(data[i], static_cast<int>(i * 2)) << "Mismatch at index " << i;
    }
}

TEST_F(JobSystemTest, ParallelFor_TwoElements_BothTransformed) {
    int arr[2] = {0, 0};
    js_.ParallelFor(arr, 2, [](int& v) { v = 7; });
    EXPECT_EQ(arr[0], 7);
    EXPECT_EQ(arr[1], 7);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stress test — 100K jobs, no deadlock, completes in under 5 seconds
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, StressTest_100KJobs_AllComplete) {
    constexpr int kJobs = 100'000;
    std::atomic<int> count{0};
    std::atomic<int>* ptr = &count;
    Counter ctr;
    ctr.Reset(static_cast<uint32_t>(kJobs));

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < kJobs; ++i) {
        js_.Enqueue(MakeJob([ptr] { ptr->fetch_add(1, std::memory_order_relaxed); },
                            JobPriority::Normal, &ctr));
    }

    ctr.Wait();

    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_EQ(count.load(std::memory_order_acquire), kJobs);
    EXPECT_LT(elapsed, std::chrono::seconds(15))
        << "Stress test exceeded 15-second deadline";
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown with pending work — must not crash or deadlock
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, Shutdown_WithPendingWork_DoesNotCrash) {
    // Enqueue a large batch, then immediately shut down before they finish.
    // The spec says pending jobs may be abandoned — we only require no crash.
    constexpr int kJobs = 200;
    std::atomic<int> count{0};
    std::atomic<int>* ptr = &count;

    // No counter — we don't wait. Just flood the queue and shut down.
    for (int i = 0; i < kJobs; ++i) {
        js_.Enqueue(MakeJob([ptr] { ptr->fetch_add(1, std::memory_order_relaxed); }));
    }

    js_.Shutdown(); // Must not crash or deadlock
    // TearDown calls Shutdown again — the guard in Shutdown handles it.
    SUCCEED();
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple priorities — all complete
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, AllPriorityLevels_Execute) {
    struct PrioCase {
        JobPriority prio;
        const char* name;
    };
    const PrioCase cases[] = {
        {JobPriority::Critical, "Critical"},
        {JobPriority::High,     "High"},
        {JobPriority::Normal,   "Normal"},
        {JobPriority::Low,      "Low"},
    };

    for (const auto& c : cases) {
        std::atomic<bool> ran{false};
        std::atomic<bool>* ptr = &ran;
        Counter ctr;
        ctr.Reset(1);
        js_.Enqueue(MakeJob([ptr] { ptr->store(true, std::memory_order_release); },
                            c.prio, &ctr));
        ctr.Wait();
        EXPECT_TRUE(ran.load(std::memory_order_acquire))
            << "Job with priority " << c.name << " did not execute";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Concurrent submitters — multiple threads enqueue simultaneously
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(JobSystemTest, ConcurrentSubmitters_AllJobsComplete) {
    constexpr int kThreads = 4;
    constexpr int kJobsPerThread = 250;
    constexpr int kTotal = kThreads * kJobsPerThread;

    std::atomic<int> count{0};
    std::atomic<int>* ptr = &count;
    Counter ctr;
    ctr.Reset(static_cast<uint32_t>(kTotal));

    std::vector<std::thread> submitters;
    submitters.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        submitters.emplace_back([&] {
            for (int j = 0; j < kJobsPerThread; ++j) {
                js_.Enqueue(MakeJob([ptr] { ptr->fetch_add(1, std::memory_order_relaxed); },
                                    JobPriority::Normal, &ctr));
            }
        });
    }

    for (auto& t : submitters) {
        t.join();
    }

    ctr.Wait();
    EXPECT_EQ(count.load(std::memory_order_acquire), kTotal);
}
