#include <gtest/gtest.h>
#include "uz_memory.h"
#include <utility>

namespace {

struct TestObj {
    int value;
    static int alive;
    TestObj(int v) : value(v) { ++alive; }
    ~TestObj() { --alive; }
};
int TestObj::alive = 0;

class MemoryTest : public ::testing::Test {
protected:
    void SetUp() override { TestObj::alive = 0; }
};

} // namespace

TEST_F(MemoryTest, OwnCreateAndDestroy) {
    {
        auto obj = uz::MakeOwn<TestObj>(42);
        EXPECT_TRUE(static_cast<bool>(obj));
        EXPECT_EQ(obj->value, 42);
        EXPECT_EQ(TestObj::alive, 1);
    }
    EXPECT_EQ(TestObj::alive, 0);
}

TEST_F(MemoryTest, OwnMoveTransfersOwnership) {
    auto a = uz::MakeOwn<TestObj>(10);
    auto b = std::move(a);
    EXPECT_FALSE(static_cast<bool>(a));
    EXPECT_TRUE(static_cast<bool>(b));
    EXPECT_EQ(b->value, 10);
    EXPECT_EQ(TestObj::alive, 1);
}

TEST_F(MemoryTest, OwnReleaseAndReset) {
    auto obj = uz::MakeOwn<TestObj>(5);
    TestObj* raw = obj.Release();
    EXPECT_FALSE(static_cast<bool>(obj));
    EXPECT_EQ(raw->value, 5);
    EXPECT_EQ(TestObj::alive, 1);
    delete raw;
    EXPECT_EQ(TestObj::alive, 0);
}

TEST_F(MemoryTest, RefImmutableBorrow) {
    auto owner = uz::MakeOwn<TestObj>(7);
    {
        uz::Ref<TestObj> r1(owner);
        EXPECT_EQ(r1->value, 7);
        uz::Ref<TestObj> r2(owner);
        EXPECT_EQ(r2->value, 7);
    }
    EXPECT_EQ(owner->value, 7);
}

TEST_F(MemoryTest, RefMutExclusiveBorrow) {
    auto owner = uz::MakeOwn<TestObj>(3);
    {
        uz::RefMut<TestObj> rm(owner);
        rm->value = 99;
    }
    EXPECT_EQ(owner->value, 99);
}

TEST_F(MemoryTest, RefMutMoveOnly) {
    auto owner = uz::MakeOwn<TestObj>(1);
    uz::RefMut<TestObj> rm1(owner);
    uz::RefMut<TestObj> rm2 = std::move(rm1);
    EXPECT_FALSE(static_cast<bool>(rm1));
    EXPECT_TRUE(static_cast<bool>(rm2));
    EXPECT_EQ(rm2->value, 1);
}
