/// Unit tests for uz::AccessTracker, uz::ReadAccess<T>, and uz::WriteAccess<T>.
///
/// Covers: default state, single-threaded acquire/release, operator overloads,
/// move semantics, multi-reader concurrency, sequential multi-thread scenarios,
/// and death-test conflict detection (write+write, write+read, read+write).
///
/// Death tests require a non-shipping debug build — UZ_CHECK calls std::abort().

#include "access_guard.h"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// AccessTracker — default state
// ─────────────────────────────────────────────────────────────────────────────

TEST(AccessTracker, Default_StateIsZero) {
    AccessTracker tracker;
    EXPECT_EQ(tracker.state.load(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// ReadAccess — single-threaded acquire / release
// ─────────────────────────────────────────────────────────────────────────────

TEST(ReadAccess, Constructor_IncrementsStateToOne) {
    AccessTracker tracker;
    int value = 42;

    ReadAccess<int> ra(&value, &tracker);

    EXPECT_EQ(tracker.state.load(), 1);
}

TEST(ReadAccess, Destructor_DecrementsStateBackToZero) {
    AccessTracker tracker;
    int value = 42;

    {
        ReadAccess<int> ra(&value, &tracker);
        EXPECT_EQ(tracker.state.load(), 1);
    }

    EXPECT_EQ(tracker.state.load(), 0);
}

TEST(ReadAccess, MultipleConcurrentReaders_StateEqualsN) {
    AccessTracker tracker;
    int value = 10;

    ReadAccess<int> r1(&value, &tracker);
    ReadAccess<int> r2(&value, &tracker);
    ReadAccess<int> r3(&value, &tracker);

    EXPECT_EQ(tracker.state.load(), 3);
}

TEST(ReadAccess, MultipleConcurrentReaders_DestDecrements) {
    AccessTracker tracker;
    int value = 10;

    {
        ReadAccess<int> r1(&value, &tracker);
        ReadAccess<int> r2(&value, &tracker);
        EXPECT_EQ(tracker.state.load(), 2);
    }

    EXPECT_EQ(tracker.state.load(), 0);
}

TEST(ReadAccess, OperatorStar_ReturnsConstRef) {
    AccessTracker tracker;
    int value = 99;

    ReadAccess<int> ra(&value, &tracker);

    EXPECT_EQ(*ra, 99);
    // Confirm the returned reference is const — only usable as const ref.
    const int& ref = *ra;
    EXPECT_EQ(ref, 99);
}

TEST(ReadAccess, OperatorArrow_ReturnsConstPointer) {
    struct Point { int x; int y; };
    AccessTracker tracker;
    Point p{3, 7};

    ReadAccess<Point> ra(&p, &tracker);

    EXPECT_EQ(ra->x, 3);
    EXPECT_EQ(ra->y, 7);
}

TEST(ReadAccess, Get_ReturnsConstPointer) {
    AccessTracker tracker;
    int value = 55;

    ReadAccess<int> ra(&value, &tracker);

    EXPECT_EQ(ra.get(), &value);
    // Confirm it is a const pointer.
    const int* p = ra.get();
    EXPECT_EQ(*p, 55);
}

// ─────────────────────────────────────────────────────────────────────────────
// ReadAccess — move semantics
// ─────────────────────────────────────────────────────────────────────────────

TEST(ReadAccess, MoveConstructor_SourceTrackerBecomesNull_DestWorksNormally) {
    AccessTracker tracker;
    int value = 7;

    ReadAccess<int> original(&value, &tracker);
    EXPECT_EQ(tracker.state.load(), 1);

    ReadAccess<int> moved(std::move(original));

    // Source no longer holds the tracker reference.
    EXPECT_EQ(original.tracker, nullptr); // NOLINT(bugprone-use-after-move)

    // Dest still reflects the one active reader.
    EXPECT_EQ(tracker.state.load(), 1);
    EXPECT_EQ(*moved, 7);
}

TEST(ReadAccess, MoveConstructor_OnlyOneDecrementOnDestroy) {
    AccessTracker tracker;
    int value = 7;

    {
        ReadAccess<int> original(&value, &tracker);
        ReadAccess<int> moved(std::move(original));
        EXPECT_EQ(tracker.state.load(), 1);
    } // both destructors run; only moved has a non-null tracker

    EXPECT_EQ(tracker.state.load(), 0);
}

TEST(ReadAccess, MoveAssignment_ReleasesCurrentAndTakesOwnership) {
    AccessTracker t1, t2;
    int v1 = 1, v2 = 2;

    ReadAccess<int> ra1(&v1, &t1);
    ReadAccess<int> ra2(&v2, &t2);

    EXPECT_EQ(t1.state.load(), 1);
    EXPECT_EQ(t2.state.load(), 1);

    // Assign ra1 from ra2: ra1 releases t1, takes ownership from ra2.
    ra1 = std::move(ra2);

    EXPECT_EQ(t1.state.load(), 0); // released
    EXPECT_EQ(t2.state.load(), 1); // still held by ra1 now
    EXPECT_EQ(ra2.tracker, nullptr); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(*ra1, 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// WriteAccess — single-threaded acquire / release
// ─────────────────────────────────────────────────────────────────────────────

TEST(WriteAccess, Constructor_SetsStateToMinusOne) {
    AccessTracker tracker;
    int value = 0;

    WriteAccess<int> wa(&value, &tracker);

    EXPECT_EQ(tracker.state.load(), -1);
}

TEST(WriteAccess, Destructor_ResetsStateToZero) {
    AccessTracker tracker;
    int value = 0;

    {
        WriteAccess<int> wa(&value, &tracker);
        EXPECT_EQ(tracker.state.load(), -1);
    }

    EXPECT_EQ(tracker.state.load(), 0);
}

TEST(WriteAccess, OperatorStar_ReturnsMutableRef) {
    AccessTracker tracker;
    int value = 0;

    WriteAccess<int> wa(&value, &tracker);
    *wa = 42;

    EXPECT_EQ(value, 42);
}

TEST(WriteAccess, OperatorArrow_ReturnsMutablePointer) {
    struct Counter { int n = 0; void inc() { ++n; } };
    AccessTracker tracker;
    Counter c;

    WriteAccess<Counter> wa(&c, &tracker);
    wa->inc();

    EXPECT_EQ(c.n, 1);
}

TEST(WriteAccess, Get_ReturnsMutablePointer) {
    AccessTracker tracker;
    int value = 10;

    WriteAccess<int> wa(&value, &tracker);

    EXPECT_EQ(wa.get(), &value);
    *wa.get() = 99;
    EXPECT_EQ(value, 99);
}

// ─────────────────────────────────────────────────────────────────────────────
// WriteAccess — move semantics
// ─────────────────────────────────────────────────────────────────────────────

TEST(WriteAccess, MoveConstructor_SourceTrackerBecomesNull_StateRemainsMinusOne) {
    AccessTracker tracker;
    int value = 0;

    WriteAccess<int> original(&value, &tracker);
    WriteAccess<int> moved(std::move(original));

    EXPECT_EQ(original.tracker, nullptr); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(tracker.state.load(), -1);
}

TEST(WriteAccess, MoveConstructor_OnlyOneReleaseOnDestroy) {
    AccessTracker tracker;
    int value = 0;

    {
        WriteAccess<int> original(&value, &tracker);
        WriteAccess<int> moved(std::move(original));
        EXPECT_EQ(tracker.state.load(), -1);
    }

    EXPECT_EQ(tracker.state.load(), 0);
}

TEST(WriteAccess, MoveAssignment_ReleasesCurrentAndTakesOwnership) {
    AccessTracker t1, t2;
    int v1 = 0, v2 = 0;

    WriteAccess<int> wa1(&v1, &t1);
    WriteAccess<int> wa2(&v2, &t2);

    EXPECT_EQ(t1.state.load(), -1);
    EXPECT_EQ(t2.state.load(), -1);

    wa1 = std::move(wa2);

    EXPECT_EQ(t1.state.load(), 0);  // wa1 released t1
    EXPECT_EQ(t2.state.load(), -1); // wa1 now holds t2
    EXPECT_EQ(wa2.tracker, nullptr); // NOLINT(bugprone-use-after-move)
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle — sequential reuse of the same tracker
// ─────────────────────────────────────────────────────────────────────────────

TEST(ReadAccess, SequentialAcquireRelease_StateAlwaysReturnsToZero) {
    AccessTracker tracker;
    int value = 0;

    for (int i = 0; i < 5; ++i) {
        {
            ReadAccess<int> ra(&value, &tracker);
            EXPECT_EQ(tracker.state.load(), 1) << "iteration " << i;
        }
        EXPECT_EQ(tracker.state.load(), 0) << "after iteration " << i;
    }
}

TEST(WriteAccess, SequentialAcquireRelease_StateAlwaysReturnsToZero) {
    AccessTracker tracker;
    int value = 0;

    for (int i = 0; i < 5; ++i) {
        {
            WriteAccess<int> wa(&value, &tracker);
            EXPECT_EQ(tracker.state.load(), -1) << "iteration " << i;
        }
        EXPECT_EQ(tracker.state.load(), 0) << "after iteration " << i;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Conflict detection — death tests (UZ_CHECK calls std::abort)
// ─────────────────────────────────────────────────────────────────────────────

/// WriteAccess while WriteAccess already active must abort.
TEST(AccessGuardDeathTest, WriteWhileWriteActive_Aborts) {
    AccessTracker tracker;
    int value = 0;

    ASSERT_DEATH(
        {
            WriteAccess<int> wa1(&value, &tracker);
            WriteAccess<int> wa2(&value, &tracker); // must abort
        },
        "" // any output / signal is acceptable
    );
}

/// WriteAccess while ReadAccess already active must abort.
TEST(AccessGuardDeathTest, WriteWhileReadActive_Aborts) {
    AccessTracker tracker;
    int value = 0;

    ASSERT_DEATH(
        {
            ReadAccess<int> ra(&value, &tracker);
            WriteAccess<int> wa(&value, &tracker); // must abort
        },
        ""
    );
}

/// ReadAccess while WriteAccess already active must abort.
TEST(AccessGuardDeathTest, ReadWhileWriteActive_Aborts) {
    AccessTracker tracker;
    int value = 0;

    ASSERT_DEATH(
        {
            WriteAccess<int> wa(&value, &tracker);
            ReadAccess<int> ra(&value, &tracker); // must abort
        },
        ""
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-threaded — multiple readers succeed concurrently
// ─────────────────────────────────────────────────────────────────────────────

TEST(ReadAccess, MultipleThreads_AllReadersSucceed_StateEqualsN) {
    constexpr int kReaders = 8;

    AccessTracker tracker;
    int value = 123;

    std::atomic<int> active_count{0};
    std::atomic<bool> all_holding{false};
    std::atomic<bool> release_flag{false};

    std::vector<std::thread> threads;
    threads.reserve(kReaders);

    for (int i = 0; i < kReaders; ++i) {
        threads.emplace_back([&] {
            ReadAccess<int> ra(&value, &tracker);
            EXPECT_EQ(*ra, 123);
            active_count.fetch_add(1, std::memory_order_release);
            // Spin until all readers are holding and release is signalled.
            while (!release_flag.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        });
    }

    // Wait until all kReaders have incremented active_count.
    while (active_count.load(std::memory_order_acquire) < kReaders) {
        std::this_thread::yield();
    }

    EXPECT_EQ(tracker.state.load(), kReaders);

    release_flag.store(true, std::memory_order_release);

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(tracker.state.load(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-threaded — sequential write → read → write from different threads
// ─────────────────────────────────────────────────────────────────────────────

TEST(AccessGuard, SequentialMultiThread_WriteReadWrite_AllSucceed) {
    AccessTracker tracker;
    int value = 0;

    // Thread 1: write 10
    std::thread t1([&] {
        WriteAccess<int> wa(&value, &tracker);
        *wa = 10;
    });
    t1.join();

    EXPECT_EQ(tracker.state.load(), 0);
    EXPECT_EQ(value, 10);

    // Thread 2: read and verify
    std::thread t2([&] {
        ReadAccess<int> ra(&value, &tracker);
        EXPECT_EQ(*ra, 10);
    });
    t2.join();

    EXPECT_EQ(tracker.state.load(), 0);

    // Thread 3: write 20
    std::thread t3([&] {
        WriteAccess<int> wa(&value, &tracker);
        *wa = 20;
    });
    t3.join();

    EXPECT_EQ(tracker.state.load(), 0);
    EXPECT_EQ(value, 20);
}
