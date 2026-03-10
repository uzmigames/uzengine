#include "uz_ring_buffer.h"

#include <gtest/gtest.h>

using namespace uz;

TEST(RingBufferTest, EmptyOnConstruction) {
    RingBuffer<int, 8> rb;
    EXPECT_TRUE(rb.Empty());
    EXPECT_EQ(rb.Size(), 0u);
}

TEST(RingBufferTest, PushAndPop) {
    RingBuffer<int, 8> rb;
    EXPECT_TRUE(rb.TryPush(42));
    EXPECT_EQ(rb.Size(), 1u);

    int out = 0;
    EXPECT_TRUE(rb.TryPop(out));
    EXPECT_EQ(out, 42);
    EXPECT_TRUE(rb.Empty());
}

TEST(RingBufferTest, FIFO_Order) {
    RingBuffer<int, 8> rb;
    for (int i = 0; i < 7; ++i) {
        EXPECT_TRUE(rb.TryPush(i));
    }

    for (int i = 0; i < 7; ++i) {
        int out = -1;
        EXPECT_TRUE(rb.TryPop(out));
        EXPECT_EQ(out, i);
    }
}

TEST(RingBufferTest, FullRejectsPush) {
    RingBuffer<int, 4> rb; // capacity 3 usable
    EXPECT_TRUE(rb.TryPush(1));
    EXPECT_TRUE(rb.TryPush(2));
    EXPECT_TRUE(rb.TryPush(3));
    EXPECT_TRUE(rb.Full());
    EXPECT_FALSE(rb.TryPush(4)); // should fail
}

TEST(RingBufferTest, EmptyRejectsPop) {
    RingBuffer<int, 4> rb;
    int out = 0;
    EXPECT_FALSE(rb.TryPop(out));
}

TEST(RingBufferTest, WrapAround) {
    RingBuffer<int, 4> rb;
    // Fill and drain multiple times to exercise wrap-around
    for (int round = 0; round < 5; ++round) {
        EXPECT_TRUE(rb.TryPush(round * 10 + 1));
        EXPECT_TRUE(rb.TryPush(round * 10 + 2));
        int out;
        EXPECT_TRUE(rb.TryPop(out));
        EXPECT_EQ(out, round * 10 + 1);
        EXPECT_TRUE(rb.TryPop(out));
        EXPECT_EQ(out, round * 10 + 2);
    }
}

TEST(RingBufferTest, Clear) {
    RingBuffer<int, 8> rb;
    rb.TryPush(1);
    rb.TryPush(2);
    rb.TryPush(3);
    rb.Clear();
    EXPECT_TRUE(rb.Empty());
    EXPECT_EQ(rb.Size(), 0u);
}

TEST(RingBufferTest, GetCapacity) {
    RingBuffer<int, 16> rb;
    EXPECT_EQ(rb.GetCapacity(), 15u); // power-of-2 minus 1
}

TEST(RingBufferTest, MoveSemantics) {
    struct MoveOnly {
        int value;
        bool moved = false;
        MoveOnly() : value(0) {}
        explicit MoveOnly(int v) : value(v) {}
        MoveOnly(MoveOnly&& other) noexcept : value(other.value), moved(false) {
            other.moved = true;
        }
        MoveOnly& operator=(MoveOnly&& other) noexcept {
            value = other.value;
            other.moved = true;
            return *this;
        }
        MoveOnly(const MoveOnly&) = default;
        MoveOnly& operator=(const MoveOnly&) = default;
    };

    RingBuffer<MoveOnly, 4> rb;
    MoveOnly item(42);
    EXPECT_TRUE(rb.TryPush(static_cast<MoveOnly&&>(item)));

    MoveOnly out;
    EXPECT_TRUE(rb.TryPop(out));
    EXPECT_EQ(out.value, 42);
}
