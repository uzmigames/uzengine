#include <gtest/gtest.h>
#include "engine/scene/frame_snapshot.h"
#include "engine/core/arena.h"

#include <thread>

using namespace uz;

// ============================================================================
// DoubleBuffer tests
// ============================================================================

TEST(DoubleBufferTest, InitialState_FrontAndBackAreDefault) {
    DoubleBuffer<int> db;
    EXPECT_EQ(db.front(), 0);
    EXPECT_EQ(db.back(), 0);
}

TEST(DoubleBufferTest, WriteToBack_NotVisibleUntilSwap) {
    DoubleBuffer<int> db;
    db.back() = 42;

    // Consumer should still see 0 (old front)
    EXPECT_EQ(db.front(), 0);

    db.swap();

    // Now consumer should see 42
    EXPECT_EQ(db.front(), 42);
}

TEST(DoubleBufferTest, MultipleSwaps) {
    DoubleBuffer<int> db;

    db.back() = 1;
    db.swap();
    EXPECT_EQ(db.front(), 1);

    db.back() = 2;
    db.swap();
    EXPECT_EQ(db.front(), 2);

    db.back() = 3;
    db.swap();
    EXPECT_EQ(db.front(), 3);
}

TEST(DoubleBufferTest, SwapDoesNotClobberFront) {
    DoubleBuffer<int> db;

    db.back() = 10;
    db.swap();
    EXPECT_EQ(db.front(), 10);

    // Write new value but don't swap yet — front should still be 10
    db.back() = 20;
    EXPECT_EQ(db.front(), 10);
}

TEST(DoubleBufferTest, WithStruct) {
    DoubleBuffer<RenderFrameSnapshot> db;

    auto& snap = db.back();
    snap.frame_number = 100;
    snap.camera.fov_y = 90.0f;

    NodeSnapshot node;
    node.node_id = 1;
    node.position = {1.0f, 2.0f, 3.0f};
    snap.nodes.push_back(node);

    db.swap();

    const auto& front = db.front();
    EXPECT_EQ(front.frame_number, 100u);
    EXPECT_FLOAT_EQ(front.camera.fov_y, 90.0f);
    ASSERT_EQ(front.nodes.size(), 1u);
    EXPECT_EQ(front.nodes[0].node_id, 1u);
    EXPECT_FLOAT_EQ(front.nodes[0].position.x, 1.0f);
}

TEST(DoubleBufferTest, ProducerConsumerCrossThread) {
    DoubleBuffer<int> db;

    // Producer writes in a separate thread
    std::thread producer([&] {
        for (int i = 1; i <= 10; ++i) {
            db.back() = i;
            db.swap();
        }
    });
    producer.join();

    // After producer is done, consumer should see the last value
    EXPECT_EQ(db.front(), 10);
}

// ============================================================================
// RenderFrameSnapshot tests
// ============================================================================

TEST(RenderFrameSnapshotTest, DefaultState) {
    RenderFrameSnapshot snap;
    EXPECT_EQ(snap.frame_number, 0u);
    EXPECT_TRUE(snap.nodes.empty());
    EXPECT_TRUE(snap.lights.empty());
}

TEST(RenderFrameSnapshotTest, Clear_ResetsAll) {
    RenderFrameSnapshot snap;
    snap.frame_number = 42;
    snap.nodes.push_back({});
    snap.lights.push_back({});

    snap.clear();

    EXPECT_EQ(snap.frame_number, 0u);
    EXPECT_TRUE(snap.nodes.empty());
    EXPECT_TRUE(snap.lights.empty());
}

TEST(RenderFrameSnapshotTest, MultipleNodesAndLights) {
    RenderFrameSnapshot snap;
    snap.frame_number = 1;

    for (int i = 0; i < 100; ++i) {
        NodeSnapshot n;
        n.node_id = static_cast<uint32_t>(i);
        n.position = {static_cast<float>(i), 0, 0};
        snap.nodes.push_back(n);
    }

    LightSnapshot l;
    l.color = {1.0f, 0.5f, 0.0f};
    l.intensity = 2.0f;
    snap.lights.push_back(l);

    EXPECT_EQ(snap.nodes.size(), 100u);
    EXPECT_EQ(snap.lights.size(), 1u);
    EXPECT_FLOAT_EQ(snap.lights[0].intensity, 2.0f);
}

// ============================================================================
// Arena improvements tests — arena_new, ArenaScopeGuard, SmallVec
// ============================================================================

TEST(ArenaNewTest, ConstructsObjectOnArena) {
    alignas(64) uint8_t mem[1024];
    Arena arena;
    arena_init(&arena, mem, sizeof(mem), "test");

    struct Obj {
        int a;
        float b;
        Obj(int x, float y) : a(x), b(y) {}
    };

    Obj* obj = arena_new<Obj>(&arena, 42, 3.14f);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->a, 42);
    EXPECT_FLOAT_EQ(obj->b, 3.14f);
}

TEST(ArenaNewTest, DefaultConstructor) {
    alignas(64) uint8_t mem[1024];
    Arena arena;
    arena_init(&arena, mem, sizeof(mem), "test");

    struct Simple {
        int val = 99;
    };

    Simple* s = arena_new<Simple>(&arena);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->val, 99);
}

TEST(ArenaScopeGuardTest, RestoresOnScopeExit) {
    alignas(64) uint8_t mem[4096];
    Arena arena;
    arena_init(&arena, mem, sizeof(mem), "test");

    size_t initial = arena.offset;

    {
        ArenaScopeGuard guard(&arena);
        arena_alloc(&arena, 256);
        EXPECT_GT(arena.offset, initial);
    }

    // After guard goes out of scope, offset should be restored
    EXPECT_EQ(arena.offset, initial);
}

TEST(SmallVecTest, InlineStorage_NoSpill) {
    SmallVec<int, 8> vec;
    for (int i = 0; i < 8; ++i) {
        vec.push_back(i * 10);
    }

    EXPECT_EQ(vec.size(), 8u);
    EXPECT_FALSE(vec.empty());
    EXPECT_EQ(vec[0], 0);
    EXPECT_EQ(vec[7], 70);
}

TEST(SmallVecTest, SpillToHeap) {
    SmallVec<int, 4> vec;
    for (int i = 0; i < 100; ++i) {
        vec.push_back(i);
    }

    EXPECT_EQ(vec.size(), 100u);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(vec[static_cast<size_t>(i)], i);
    }
}

TEST(SmallVecTest, Clear_KeepsCapacity) {
    SmallVec<int, 4> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.clear();

    EXPECT_EQ(vec.size(), 0u);
    EXPECT_TRUE(vec.empty());

    // Can still push after clear
    vec.push_back(3);
    EXPECT_EQ(vec.size(), 1u);
    EXPECT_EQ(vec[0], 3);
}

TEST(SmallVecTest, RangeBasedFor) {
    SmallVec<int, 4> vec;
    vec.push_back(10);
    vec.push_back(20);
    vec.push_back(30);

    int sum = 0;
    for (int v : vec) {
        sum += v;
    }
    EXPECT_EQ(sum, 60);
}
