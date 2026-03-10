#include <gtest/gtest.h>
#include "uz_frame_array.h"

using namespace uz;

TEST(FrameArrayTest, EmptyOnConstruction) {
    FrameArray<int, 8> arr;
    EXPECT_TRUE(arr.IsEmpty());
    EXPECT_EQ(arr.Count(), 0u);
}

TEST(FrameArrayTest, PushAndAccess) {
    FrameArray<int, 8> arr;
    arr.Push(10);
    arr.Push(20);
    arr.Push(30);
    EXPECT_EQ(arr.Count(), 3u);
    EXPECT_EQ(arr[0], 10);
    EXPECT_EQ(arr[1], 20);
    EXPECT_EQ(arr[2], 30);
}

TEST(FrameArrayTest, Emplace) {
    struct Pair {
        int a, b;
        Pair(int a, int b) : a(a), b(b) {}
    };
    FrameArray<Pair, 4> arr;
    arr.Emplace(1, 2);
    EXPECT_EQ(arr[0].a, 1);
    EXPECT_EQ(arr[0].b, 2);
}

TEST(FrameArrayTest, Clear) {
    FrameArray<int, 8> arr;
    arr.Push(1);
    arr.Push(2);
    arr.Clear();
    EXPECT_TRUE(arr.IsEmpty());
    EXPECT_EQ(arr.Count(), 0u);

    // Can push again after clear
    arr.Push(3);
    EXPECT_EQ(arr[0], 3);
}

TEST(FrameArrayTest, RangeFor) {
    FrameArray<int, 8> arr;
    arr.Push(1);
    arr.Push(2);
    arr.Push(3);
    int sum = 0;
    for (int v : arr) sum += v;
    EXPECT_EQ(sum, 6);
}

TEST(FrameArrayTest, FullCapacity) {
    FrameArray<int, 4> arr;
    arr.Push(1);
    arr.Push(2);
    arr.Push(3);
    arr.Push(4);
    EXPECT_TRUE(arr.IsFull());
    EXPECT_EQ(arr.GetCapacity(), 4u);
}

TEST(FrameArrayTest, DataPointer) {
    FrameArray<int, 4> arr;
    EXPECT_EQ(arr.Data(), nullptr);
    arr.Push(42);
    EXPECT_NE(arr.Data(), nullptr);
    EXPECT_EQ(*arr.Data(), 42);
}

struct DestructorCounter {
    static int destroyed;
    ~DestructorCounter() { ++destroyed; }
};
int DestructorCounter::destroyed = 0;

TEST(FrameArrayTest, DestructorsCalledOnClear) {
    DestructorCounter::destroyed = 0;
    FrameArray<DestructorCounter, 4> arr;
    arr.Emplace();
    arr.Emplace();
    arr.Emplace();
    arr.Clear();
    EXPECT_EQ(DestructorCounter::destroyed, 3);
}
