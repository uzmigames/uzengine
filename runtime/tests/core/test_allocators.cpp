/// Unit tests for engine allocators — Linear, Pool, and Frame allocator
/// correctness, alignment guarantees, capacity limits, and reset semantics.
#include "allocators.h"

#include <gtest/gtest.h>

using namespace uz;

// ── LinearAllocator ──────────────────────────────────────────────────────────

TEST(LinearAllocatorTest, AllocAndReset) {
    LinearAllocator alloc("Test", 1024);
    void* a = alloc.Alloc(64);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(alloc.Used(), 64u);

    void* b = alloc.Alloc(128);
    ASSERT_NE(b, nullptr);
    EXPECT_GE(alloc.Used(), 192u);

    alloc.Reset();
    EXPECT_EQ(alloc.Used(), 0u);
}

TEST(LinearAllocatorTest, ReturnsNullOnOverflow) {
    LinearAllocator alloc("Tiny", 64);
    void* a = alloc.Alloc(32);
    EXPECT_NE(a, nullptr);
    void* b = alloc.Alloc(64); // exceeds remaining
    EXPECT_EQ(b, nullptr);
}

TEST(LinearAllocatorTest, Alignment) {
    LinearAllocator alloc("Align", 1024);
    alloc.Alloc(1); // offset = 1
    void* aligned = alloc.Alloc(16, 16);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(aligned) % 16, 0u);
}

TEST(LinearAllocatorTest, NewTyped) {
    LinearAllocator alloc("Typed", 1024);
    struct Foo {
        int x;
        float y;
    };
    Foo* f = alloc.New<Foo>();
    ASSERT_NE(f, nullptr);
    f->x = 42;
    f->y = 3.14f;
    EXPECT_EQ(f->x, 42);
}

TEST(LinearAllocatorTest, NameAndMetrics) {
    LinearAllocator alloc("Metrics", 256);
    EXPECT_STREQ(alloc.Name(), "Metrics");
    EXPECT_EQ(alloc.GetSize(), 256u);
    EXPECT_EQ(alloc.Remaining(), 256u);
    alloc.Alloc(100);
    EXPECT_EQ(alloc.Remaining(), 156u);
}

// ── PoolAllocator ────────────────────────────────────────────────────────────

struct PoolItem {
    int value;
    PoolItem() : value(0) {}
    explicit PoolItem(int v) : value(v) {}
};

TEST(PoolAllocatorTest, AllocAndFree) {
    PoolAllocator<PoolItem> pool("Test", 4);
    PoolItem* a = pool.Alloc(42);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->value, 42);
    EXPECT_EQ(pool.Count(), 1u);

    pool.Free(a);
    EXPECT_EQ(pool.Count(), 0u);
}

TEST(PoolAllocatorTest, FillToCapacity) {
    PoolAllocator<PoolItem> pool("Test", 3);
    PoolItem* a = pool.Alloc(1);
    PoolItem* b = pool.Alloc(2);
    PoolItem* c = pool.Alloc(3);
    EXPECT_EQ(pool.Count(), 3u);

    PoolItem* d = pool.Alloc(4);
    EXPECT_EQ(d, nullptr); // full

    pool.Free(b);
    PoolItem* e = pool.Alloc(5);
    EXPECT_NE(e, nullptr);
    EXPECT_EQ(e->value, 5);

    pool.Free(a);
    pool.Free(c);
    pool.Free(e);
}

TEST(PoolAllocatorTest, Contains) {
    PoolAllocator<PoolItem> pool("Test", 4);
    PoolItem* a = pool.Alloc(1);
    EXPECT_TRUE(pool.Contains(a));

    PoolItem stackItem(99);
    EXPECT_FALSE(pool.Contains(&stackItem));

    pool.Free(a);
}

TEST(PoolAllocatorTest, NameAndCapacity) {
    PoolAllocator<PoolItem> pool("VFX.Particles", 1024);
    EXPECT_STREQ(pool.Name(), "VFX.Particles");
    EXPECT_EQ(pool.GetCapacity(), 1024u);
}

// ── FrameAllocator ───────────────────────────────────────────────────────────

TEST(FrameAllocatorTest, AllocAndBeginFrame) {
    FrameAllocator alloc("Frame", 1024);
    void* a = alloc.Alloc(64);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(alloc.Used(), 64u);

    alloc.BeginFrame();
    EXPECT_EQ(alloc.Used(), 0u);

    // Can alloc again after reset
    void* b = alloc.Alloc(128);
    EXPECT_NE(b, nullptr);
}

TEST(FrameAllocatorTest, NewTyped) {
    FrameAllocator alloc("Frame", 1024);
    struct Bar {
        int x;
    };
    Bar* b = alloc.New<Bar>();
    ASSERT_NE(b, nullptr);
    b->x = 99;
    EXPECT_EQ(b->x, 99);
}
