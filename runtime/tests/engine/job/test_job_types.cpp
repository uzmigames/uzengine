/// Unit tests for Job, JobPriority, and MakeJob — value types, inline data buffer,
/// priority field, counter field, and the MakeJob template helper.

#include "job_types.h"
#include "counter.h"

#include <atomic>
#include <gtest/gtest.h>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Job default state
// ─────────────────────────────────────────────────────────────────────────────

TEST(JobTypes, DefaultJob_HasNullFunction) {
    Job job;
    EXPECT_EQ(job.function, nullptr);
}

TEST(JobTypes, DefaultJob_HasNormalPriority) {
    Job job;
    EXPECT_EQ(job.priority, JobPriority::Normal);
}

TEST(JobTypes, DefaultJob_HasNullCounter) {
    Job job;
    EXPECT_EQ(job.counter, nullptr);
}

TEST(JobTypes, DefaultJob_DataBufferIsZero) {
    Job job;
    for (size_t i = 0; i < sizeof(job.data); ++i) {
        EXPECT_EQ(job.data[i], 0u) << "data[" << i << "] should be zero";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// MakeJob — basic construction
// ─────────────────────────────────────────────────────────────────────────────

TEST(JobTypes, MakeJob_FunctionIsNotNull) {
    auto job = MakeJob([] {});
    EXPECT_NE(job.function, nullptr);
}

TEST(JobTypes, MakeJob_DefaultPriorityIsNormal) {
    auto job = MakeJob([] {});
    EXPECT_EQ(job.priority, JobPriority::Normal);
}

TEST(JobTypes, MakeJob_DefaultCounterIsNull) {
    auto job = MakeJob([] {});
    EXPECT_EQ(job.counter, nullptr);
}

TEST(JobTypes, MakeJob_ExplicitPriority_IsStored) {
    auto job = MakeJob([] {}, JobPriority::Critical);
    EXPECT_EQ(job.priority, JobPriority::Critical);

    auto jobHigh = MakeJob([] {}, JobPriority::High);
    EXPECT_EQ(jobHigh.priority, JobPriority::High);

    auto jobLow = MakeJob([] {}, JobPriority::Low);
    EXPECT_EQ(jobLow.priority, JobPriority::Low);
}

TEST(JobTypes, MakeJob_CounterIsStored) {
    Counter counter;
    auto job = MakeJob([] {}, JobPriority::Normal, &counter);
    EXPECT_EQ(job.counter, &counter);
}

// ─────────────────────────────────────────────────────────────────────────────
// MakeJob — callable invocation
// ─────────────────────────────────────────────────────────────────────────────

TEST(JobTypes, MakeJob_InvokeFunction_ExecutesCallable) {
    int result = 0;
    int* ptr = &result;
    auto job = MakeJob([ptr] { *ptr = 42; });
    ASSERT_NE(job.function, nullptr);
    job.function(job.data);
    EXPECT_EQ(result, 42);
}

TEST(JobTypes, MakeJob_CapturesPointer_DataIsInlined) {
    // Verifies that data captured by pointer is copied into the inline buffer
    // and that calling the function via the data pointer works correctly.
    std::atomic<int> counter{0};
    std::atomic<int>* pCounter = &counter;
    auto job = MakeJob([pCounter] { pCounter->fetch_add(1); });
    job.function(job.data);
    EXPECT_EQ(counter.load(), 1);
}

TEST(JobTypes, MakeJob_MultipleCaptures_AllReachCallable) {
    int a = 0, b = 0;
    int* pa = &a;
    int* pb = &b;
    auto job = MakeJob([pa, pb] {
        *pa = 10;
        *pb = 20;
    });
    job.function(job.data);
    EXPECT_EQ(a, 10);
    EXPECT_EQ(b, 20);
}

TEST(JobTypes, MakeJob_CopiedJob_FunctionStillWorks) {
    // Copying a Job (as happens during queue enqueue) must preserve function/data.
    int result = 0;
    int* ptr = &result;
    auto original = MakeJob([ptr] { *ptr = 99; });
    Job copy = original; // value copy
    copy.function(copy.data);
    EXPECT_EQ(result, 99);
}

TEST(JobTypes, MakeJob_InvokedTwice_RunsTwice) {
    // Function should be re-invokable (idempotent invocation).
    int count = 0;
    int* ptr = &count;
    auto job = MakeJob([ptr] { ++(*ptr); });
    job.function(job.data);
    job.function(job.data);
    EXPECT_EQ(count, 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// JobPriority — enum values and ordering
// ─────────────────────────────────────────────────────────────────────────────

TEST(JobTypes, JobPriority_CriticalLessThanHigh) {
    EXPECT_LT(static_cast<uint8_t>(JobPriority::Critical),
              static_cast<uint8_t>(JobPriority::High));
}

TEST(JobTypes, JobPriority_HighLessThanNormal) {
    EXPECT_LT(static_cast<uint8_t>(JobPriority::High),
              static_cast<uint8_t>(JobPriority::Normal));
}

TEST(JobTypes, JobPriority_NormalLessThanLow) {
    EXPECT_LT(static_cast<uint8_t>(JobPriority::Normal),
              static_cast<uint8_t>(JobPriority::Low));
}

TEST(JobTypes, JobPriority_CountIs4) {
    EXPECT_EQ(static_cast<uint8_t>(JobPriority::Count), 4u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Job inline buffer size
// ─────────────────────────────────────────────────────────────────────────────

TEST(JobTypes, Job_DataBufferIs48Bytes) {
    EXPECT_EQ(sizeof(Job::data), 48u);
}

TEST(JobTypes, Job_DataBufferAlignedTo16) {
    Job job;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(job.data) % 16u, 0u);
}
