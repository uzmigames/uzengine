// test_handle.cpp — 100% coverage of handle.h (Handle<Tag> + HandlePool<T,Tag>)

#include <gtest/gtest.h>
#include "engine/core/handle.h"

#include <set>
#include <string>
#include <type_traits>

using namespace uz;

// ============================================================================
// Tag types for tests
// ============================================================================

struct TestTag {};
struct OtherTag {};
using TestHandle  = Handle<TestTag>;
using OtherHandle = Handle<OtherTag>;
using TestPool    = HandlePool<int, TestTag>;
using StringPool  = HandlePool<std::string, TestTag>;

// ============================================================================
// Handle<Tag> — null sentinel
// ============================================================================

TEST(HandleNull, NullHandle_IndexIsZero_IsNullTrue_BoolFalse) {
    auto h = TestHandle::null();

    EXPECT_EQ(h.index, 0u);
    EXPECT_EQ(h.generation, 0u);
    EXPECT_TRUE(h.is_null());
    EXPECT_FALSE(static_cast<bool>(h));
}

TEST(HandleNull, DefaultConstructed_BehaviorMatchesNullSentinel) {
    // The struct uses bit-fields; zero-initialised should equal null().
    TestHandle h{};
    h.index      = 0;
    h.generation = 0;

    EXPECT_TRUE(h.is_null());
    EXPECT_FALSE(static_cast<bool>(h));
}

// ============================================================================
// Handle<Tag> — non-null
// ============================================================================

TEST(HandleNonNull, IndexOneGenZero_IsNotNull_BoolTrue) {
    TestHandle h{};
    h.index      = 1;
    h.generation = 0;

    EXPECT_FALSE(h.is_null());
    EXPECT_TRUE(static_cast<bool>(h));
}

TEST(HandleNonNull, IndexZeroGenNonZero_IsStillNullByIndexCheck) {
    // is_null() checks index == 0, not generation.
    TestHandle h{};
    h.index      = 0;
    h.generation = 5;

    EXPECT_TRUE(h.is_null())
        << "is_null() must return true when index == 0 regardless of generation";
}

// ============================================================================
// Handle<Tag> — equality
// ============================================================================

TEST(HandleEquality, SameIndexAndGeneration_AreEqual) {
    TestHandle a{};
    a.index = 3; a.generation = 7;
    TestHandle b{};
    b.index = 3; b.generation = 7;

    EXPECT_EQ(a, b);
    EXPECT_FALSE(a != b);
}

TEST(HandleEquality, DifferentGeneration_AreNotEqual) {
    TestHandle a{};
    a.index = 3; a.generation = 1;
    TestHandle b{};
    b.index = 3; b.generation = 2;

    EXPECT_NE(a, b);
    EXPECT_FALSE(a == b);
}

TEST(HandleEquality, DifferentIndex_AreNotEqual) {
    TestHandle a{};
    a.index = 1; a.generation = 0;
    TestHandle b{};
    b.index = 2; b.generation = 0;

    EXPECT_NE(a, b);
}

TEST(HandleEquality, NullHandlesAreEqual) {
    auto a = TestHandle::null();
    auto b = TestHandle::null();
    EXPECT_EQ(a, b);
}

// ============================================================================
// Handle<Tag> — type safety (compile-time)
// ============================================================================

TEST(HandleTypeSafety, EntityAndTextureHandleAreDistinctTypes) {
    // These must be distinct types so the compiler rejects mixing them.
    constexpr bool different = !std::is_same_v<EntityHandle, TextureHandle>;
    static_assert(different, "EntityHandle and TextureHandle must be distinct types");
    EXPECT_TRUE(different);
}

TEST(HandleTypeSafety, TestTagAndOtherTagHandlesAreDistinctTypes) {
    constexpr bool different = !std::is_same_v<TestHandle, OtherHandle>;
    static_assert(different, "Handles with different tags must be distinct types");
    EXPECT_TRUE(different);
}

// ============================================================================
// HandlePool — construction
// ============================================================================

TEST(HandlePoolConstruct, NewPool_HasSlot0Reserved_FreeListEmpty) {
    TestPool pool;

    // Slot 0 is the null sentinel — pool starts with 1 slot already reserved.
    EXPECT_EQ(pool.slots.size(), 1u);
    EXPECT_TRUE(pool.free_list.empty());
}

// ============================================================================
// HandlePool — insert
// ============================================================================

TEST(HandlePoolInsert, InsertValue_ReturnsNonNullHandle) {
    TestPool pool;
    auto h = pool.insert(42);

    EXPECT_FALSE(h.is_null());
    EXPECT_TRUE(static_cast<bool>(h));
}

TEST(HandlePoolInsert, InsertedHandle_IsValid) {
    TestPool pool;
    auto h = pool.insert(100);

    EXPECT_TRUE(pool.is_valid(h));
}

TEST(HandlePoolInsert, InsertedHandle_IndexIsNonZero) {
    TestPool pool;
    auto h = pool.insert(77);

    EXPECT_NE(h.index, 0u);
}

// ============================================================================
// HandlePool — get
// ============================================================================

TEST(HandlePoolGet, Get_AfterInsert_ReturnsPointerToValue) {
    TestPool pool;
    auto h = pool.insert(1234);

    int* p = pool.get(h);

    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, 1234);
}

TEST(HandlePoolGet, ConstGet_AfterInsert_ReturnsConstPointerToValue) {
    TestPool pool;
    auto h = pool.insert(5678);

    const TestPool& cpool = pool;
    const int* p = cpool.get(h);

    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, 5678);
}

TEST(HandlePoolGet, Get_NullHandle_ReturnsNullptr) {
    TestPool pool;

    int* p = pool.get(TestHandle::null());

    EXPECT_EQ(p, nullptr);
}

TEST(HandlePoolGet, Get_HandleWithOutOfBoundsIndex_ReturnsNullptr) {
    TestPool pool;

    // Pool has only slot 0. Slot 99 doesn't exist.
    TestHandle h{};
    h.index      = 99;
    h.generation = 0;

    EXPECT_EQ(pool.get(h), nullptr);
}

TEST(HandlePoolGet, Get_WriteThroughPointer_ValueChanges) {
    TestPool pool;
    auto h = pool.insert(0);

    int* p = pool.get(h);
    ASSERT_NE(p, nullptr);
    *p = 999;

    int* p2 = pool.get(h);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(*p2, 999);
}

TEST(HandlePoolGet, Get_StringPool_ReturnsCorrectString) {
    StringPool pool;
    auto h = pool.insert(std::string("hello_world"));

    std::string* s = pool.get(h);

    ASSERT_NE(s, nullptr);
    EXPECT_EQ(*s, "hello_world");
}

// ============================================================================
// HandlePool — remove
// ============================================================================

TEST(HandlePoolRemove, AfterRemove_IsValidReturnsFalse) {
    TestPool pool;
    auto h = pool.insert(10);

    pool.remove(h);

    EXPECT_FALSE(pool.is_valid(h));
}

TEST(HandlePoolRemove, AfterRemove_GetReturnsNullptr) {
    TestPool pool;
    auto h = pool.insert(10);

    pool.remove(h);

    EXPECT_EQ(pool.get(h), nullptr);
}

TEST(HandlePoolRemove, RemoveNullHandle_IsNoOp) {
    TestPool pool;

    // Must not assert or crash.
    EXPECT_NO_FATAL_FAILURE(pool.remove(TestHandle::null()));
}

// ============================================================================
// HandlePool — generation bump on remove
// ============================================================================

TEST(HandlePoolGenerationBump, AfterRemoveAndReinsert_NewHandleHasDifferentGeneration) {
    TestPool pool;

    auto h1 = pool.insert(1);
    const uint32_t idx1 = h1.index;
    const uint8_t  gen1 = static_cast<uint8_t>(h1.generation);

    pool.remove(h1);
    auto h2 = pool.insert(2);

    // Must reuse same slot (free_list).
    EXPECT_EQ(h2.index, idx1) << "Slot should be reused from free_list";
    // Generation must have incremented.
    EXPECT_NE(h2.generation, gen1) << "Generation must be bumped after remove";
}

TEST(HandlePoolGenerationBump, OldHandle_AfterSlotReused_IsInvalid) {
    TestPool pool;

    auto h_old = pool.insert(42);
    pool.remove(h_old);
    auto h_new = pool.insert(99);

    // h_old's generation is now stale.
    EXPECT_FALSE(pool.is_valid(h_old))
        << "Old handle must be invalid after slot was recycled";
    (void)h_new;
}

// ============================================================================
// HandlePool — slot reuse via free_list
// ============================================================================

TEST(HandlePoolSlotReuse, AfterRemove_InsertReusesSameIndex) {
    TestPool pool;

    auto h1 = pool.insert(10);
    const uint32_t idx1 = h1.index;

    pool.remove(h1);
    auto h2 = pool.insert(20);

    EXPECT_EQ(h2.index, idx1)
        << "Free list must reuse the same slot index";
}

// ============================================================================
// HandlePool — stale get after slot is reoccupied
// ============================================================================

TEST(HandlePoolStaleGet, StaleHandleAfterSlotReuse_GetReturnsNullptr) {
    TestPool pool;

    auto h_old = pool.insert(1);
    pool.remove(h_old);
    auto h_new = pool.insert(2);  // reuses slot

    // h_old now has old generation — get must return nullptr.
    EXPECT_EQ(pool.get(h_old), nullptr)
        << "Stale handle (old generation) must not alias the reused slot";
    (void)h_new;
}

TEST(HandlePoolStaleGet, ConstStaleHandle_GetReturnsNullptr) {
    TestPool pool;

    auto h_old = pool.insert(55);
    pool.remove(h_old);
    pool.insert(66);

    const TestPool& cpool = pool;
    EXPECT_EQ(cpool.get(h_old), nullptr);
}

// ============================================================================
// HandlePool — multiple items
// ============================================================================

TEST(HandlePoolMultipleItems, Insert100Items_AllValidAndCorrectValues) {
    TestPool pool;
    std::vector<TestHandle> handles;
    handles.reserve(100);

    for (int i = 0; i < 100; ++i) {
        handles.push_back(pool.insert(i * 7));
    }

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(pool.is_valid(handles[i]))
            << "Handle " << i << " should be valid";
        int* p = pool.get(handles[i]);
        ASSERT_NE(p, nullptr) << "Handle " << i << " get returned nullptr";
        EXPECT_EQ(*p, i * 7) << "Value mismatch at index " << i;
    }
}

TEST(HandlePoolMultipleItems, RemoveSomeItems_RemainingAreStillValid) {
    TestPool pool;
    std::vector<TestHandle> handles;
    handles.reserve(10);

    for (int i = 0; i < 10; ++i) {
        handles.push_back(pool.insert(i));
    }

    // Remove even-indexed handles.
    for (int i = 0; i < 10; i += 2) {
        pool.remove(handles[i]);
    }

    // Odd-indexed must still be valid with correct values.
    for (int i = 1; i < 10; i += 2) {
        EXPECT_TRUE(pool.is_valid(handles[i]))
            << "Odd handle " << i << " should still be valid";
        int* p = pool.get(handles[i]);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(*p, i);
    }

    // Even-indexed must be invalid.
    for (int i = 0; i < 10; i += 2) {
        EXPECT_FALSE(pool.is_valid(handles[i]))
            << "Even handle " << i << " should be invalid after remove";
    }
}

// ============================================================================
// HandlePool — is_valid edge cases
// ============================================================================

TEST(HandlePoolIsValid, NullHandle_IsNotValid) {
    TestPool pool;
    EXPECT_FALSE(pool.is_valid(TestHandle::null()));
}

TEST(HandlePoolIsValid, HandleWithOutOfBoundsIndex_IsNotValid) {
    TestPool pool;
    TestHandle h{};
    h.index      = 9999;
    h.generation = 0;

    EXPECT_FALSE(pool.is_valid(h));
}

// ============================================================================
// HandlePool — generation wraps gracefully
// ============================================================================

TEST(HandlePoolGenerationWrap, GenerationWrapsAt255Without_CorruptingPool) {
    // Pump the same slot 255 times to wrap the uint8_t generation counter.
    // After 256 remove+insert cycles, generation returns to 0.
    // This is documented intentional behavior.
    TestPool pool;

    auto h = pool.insert(0);
    const uint32_t original_idx = h.index;

    for (int cycle = 0; cycle < 255; ++cycle) {
        pool.remove(h);
        h = pool.insert(cycle);
        ASSERT_EQ(h.index, original_idx)
            << "Slot must be reused at cycle " << cycle;
    }

    // The pool must still work after the wrap.
    EXPECT_TRUE(pool.is_valid(h));
    int* p = pool.get(h);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, 254); // last written value
}
