/// Unit tests for HandleMap — generational handle allocation, stale-handle
/// rejection after removal, iteration over live entries, and capacity growth.
#include "handle_map.h"

#include <gtest/gtest.h>

using namespace uz;

struct Resource {
    int id;
    static int alive;
    Resource() : id(0) { ++alive; }
    explicit Resource(int id) : id(id) { ++alive; }
    ~Resource() { --alive; }
};
int Resource::alive = 0;

class HandleMapTest : public ::testing::Test {
  protected:
    void SetUp() override { Resource::alive = 0; }
};

TEST_F(HandleMapTest, CreateAndGet) {
    HandleMap<Resource, 8> map;
    Handle<Resource> h = map.Create(42);
    EXPECT_TRUE(h.IsValid());
    EXPECT_EQ(map.Count(), 1u);

    Resource* r = map.Get(h);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->id, 42);
}

TEST_F(HandleMapTest, DestroyInvalidatesHandle) {
    HandleMap<Resource, 8> map;
    Handle<Resource> h = map.Create(1);
    map.Destroy(h);
    EXPECT_EQ(map.Get(h), nullptr);
    EXPECT_FALSE(map.IsValidHandle(h));
    EXPECT_EQ(map.Count(), 0u);
}

TEST_F(HandleMapTest, GenerationPreventsUseAfterFree) {
    HandleMap<Resource, 8> map;
    Handle<Resource> h1 = map.Create(1);
    map.Destroy(h1);

    Handle<Resource> h2 = map.Create(2);
    // h1 and h2 may share same index, but different generation
    EXPECT_EQ(map.Get(h1), nullptr); // stale handle
    Resource* r2 = map.Get(h2);
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(r2->id, 2);

    map.Destroy(h2);
}

TEST_F(HandleMapTest, FillToCapacity) {
    HandleMap<Resource, 4> map;
    Handle<Resource> h1 = map.Create(1);
    Handle<Resource> h2 = map.Create(2);
    Handle<Resource> h3 = map.Create(3);
    Handle<Resource> h4 = map.Create(4);
    EXPECT_EQ(map.Count(), 4u);

    Handle<Resource> h5 = map.Create(5);
    EXPECT_FALSE(h5.IsValid()); // full

    map.Destroy(h1);
    map.Destroy(h2);
    map.Destroy(h3);
    map.Destroy(h4);
}

TEST_F(HandleMapTest, InvalidHandleReturnsNull) {
    HandleMap<Resource, 4> map;
    Handle<Resource> invalid = Handle<Resource>::Invalid();
    EXPECT_EQ(map.Get(invalid), nullptr);
}

TEST_F(HandleMapTest, ForEach) {
    HandleMap<Resource, 8> map;
    map.Create(10);
    map.Create(20);
    map.Create(30);

    int sum = 0;
    int count = 0;
    map.ForEach([&](Handle<Resource>, Resource& r) {
        sum += r.id;
        ++count;
    });
    EXPECT_EQ(count, 3);
    EXPECT_EQ(sum, 60);
}

TEST_F(HandleMapTest, DestructorCleansUp) {
    {
        HandleMap<Resource, 4> map;
        map.Create(1);
        map.Create(2);
        EXPECT_EQ(Resource::alive, 2);
    }
    EXPECT_EQ(Resource::alive, 0);
}

TEST_F(HandleMapTest, DestroyAndReuseSlot) {
    HandleMap<Resource, 2> map;
    Handle<Resource> h1 = map.Create(1);
    map.Destroy(h1);

    Handle<Resource> h2 = map.Create(2);
    EXPECT_TRUE(h2.IsValid());
    EXPECT_NE(h2.generation, h1.generation); // generation bumped

    map.Destroy(h2);
}
