#include "uz_free_list.h"

#include <gtest/gtest.h>

using namespace uz;

struct TestItem {
    int value;
    static int alive;
    TestItem() : value(0) { ++alive; }
    explicit TestItem(int v) : value(v) { ++alive; }
    ~TestItem() { --alive; }
};
int TestItem::alive = 0;

class FreeListTest : public ::testing::Test {
  protected:
    void SetUp() override { TestItem::alive = 0; }
};

TEST_F(FreeListTest, AllocAndFree) {
    FreeList<TestItem, 4> fl;
    TestItem* a = fl.Alloc(10);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->value, 10);
    EXPECT_EQ(fl.Count(), 1u);
    EXPECT_EQ(TestItem::alive, 1);

    fl.Free(a);
    EXPECT_EQ(fl.Count(), 0u);
    EXPECT_EQ(TestItem::alive, 0);
}

TEST_F(FreeListTest, FillToCapacity) {
    FreeList<TestItem, 4> fl;
    TestItem* a = fl.Alloc(1);
    TestItem* b = fl.Alloc(2);
    TestItem* c = fl.Alloc(3);
    TestItem* d = fl.Alloc(4);
    EXPECT_TRUE(fl.IsFull());
    EXPECT_EQ(fl.Count(), 4u);

    TestItem* e = fl.Alloc(5);
    EXPECT_EQ(e, nullptr); // full

    fl.Free(b);
    EXPECT_EQ(fl.Count(), 3u);
    EXPECT_FALSE(fl.IsFull());

    TestItem* f = fl.Alloc(6);
    EXPECT_NE(f, nullptr);
    EXPECT_EQ(f->value, 6);
    EXPECT_EQ(fl.Count(), 4u);

    fl.Free(a);
    fl.Free(c);
    fl.Free(d);
    fl.Free(f);
}

TEST_F(FreeListTest, IndexOf) {
    FreeList<TestItem, 8> fl;
    TestItem* a = fl.Alloc(10);
    TestItem* b = fl.Alloc(20);
    size_t idxA = fl.IndexOf(a);
    size_t idxB = fl.IndexOf(b);
    EXPECT_NE(idxA, idxB);
    EXPECT_TRUE(fl.IsAlive(idxA));
    EXPECT_TRUE(fl.IsAlive(idxB));

    fl.Free(a);
    EXPECT_FALSE(fl.IsAlive(idxA));
    fl.Free(b);
}

TEST_F(FreeListTest, AtAccess) {
    FreeList<TestItem, 4> fl;
    TestItem* a = fl.Alloc(42);
    size_t idx = fl.IndexOf(a);
    EXPECT_EQ(fl.At(idx)->value, 42);
    fl.Free(a);
}

TEST_F(FreeListTest, DestructorCleansUp) {
    {
        FreeList<TestItem, 4> fl;
        fl.Alloc(1);
        fl.Alloc(2);
        fl.Alloc(3);
        EXPECT_EQ(TestItem::alive, 3);
    }
    EXPECT_EQ(TestItem::alive, 0);
}
