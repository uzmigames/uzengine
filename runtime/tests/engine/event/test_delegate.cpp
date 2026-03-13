/// Tests for uz::MulticastDelegate and uz::DelegateHandle.
///
/// Covers: basic dispatch, argument forwarding, removal, clear, count,
/// invalid-handle no-op, self-removal during invoke, and move semantics.

#include "delegate.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DelegateHandle tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(DelegateHandle, DefaultIsInvalid) {
    DelegateHandle h;
    EXPECT_FALSE(h.IsValid());
    EXPECT_EQ(h.id, 0u);
}

TEST(DelegateHandle, NonZeroIdIsValid) {
    DelegateHandle h{42};
    EXPECT_TRUE(h.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// MulticastDelegate<void> — zero-argument delegate
// ─────────────────────────────────────────────────────────────────────────────

TEST(MulticastDelegateVoid, InvokeWithNoListenersIsNoOp) {
    MulticastDelegate<> d;
    EXPECT_NO_THROW(d.Invoke());
}

TEST(MulticastDelegateVoid, AddReturnsValidHandle) {
    MulticastDelegate<> d;
    auto h = d.Add([] {});
    EXPECT_TRUE(h.IsValid());
}

TEST(MulticastDelegateVoid, CountReflectsRegistrations) {
    MulticastDelegate<> d;
    EXPECT_EQ(d.Count(), 0u);

    auto h1 = d.Add([] {});
    EXPECT_EQ(d.Count(), 1u);

    auto h2 = d.Add([] {});
    EXPECT_EQ(d.Count(), 2u);

    d.Remove(h1);
    EXPECT_EQ(d.Count(), 1u);

    d.Remove(h2);
    EXPECT_EQ(d.Count(), 0u);
}

TEST(MulticastDelegateVoid, InvokeCallsAllListeners) {
    MulticastDelegate<> d;
    int callCount = 0;
    d.Add([&] { ++callCount; });
    d.Add([&] { ++callCount; });
    d.Add([&] { ++callCount; });

    d.Invoke();
    EXPECT_EQ(callCount, 3);
}

TEST(MulticastDelegateVoid, InvokeCallsListenersInInsertionOrder) {
    MulticastDelegate<> d;
    std::vector<int> order;
    d.Add([&] { order.push_back(1); });
    d.Add([&] { order.push_back(2); });
    d.Add([&] { order.push_back(3); });

    d.Invoke();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(MulticastDelegateVoid, RemoveStopsCallback) {
    MulticastDelegate<> d;
    int callCount = 0;
    auto h = d.Add([&] { ++callCount; });

    d.Invoke();
    EXPECT_EQ(callCount, 1);

    d.Remove(h);
    d.Invoke();
    EXPECT_EQ(callCount, 1); // must not have been called again
}

TEST(MulticastDelegateVoid, RemoveOnlyTargetedListener) {
    MulticastDelegate<> d;
    int aCount = 0, bCount = 0;
    auto ha = d.Add([&] { ++aCount; });
    /*auto hb =*/ d.Add([&] { ++bCount; });

    d.Remove(ha);
    d.Invoke();

    EXPECT_EQ(aCount, 0); // removed — must not fire
    EXPECT_EQ(bCount, 1); // must still fire
}

TEST(MulticastDelegateVoid, RemoveWithInvalidHandleIsNoOp) {
    MulticastDelegate<> d;
    int callCount = 0;
    d.Add([&] { ++callCount; });

    DelegateHandle invalid; // id == 0
    EXPECT_NO_THROW(d.Remove(invalid));

    d.Invoke();
    EXPECT_EQ(callCount, 1); // listener unaffected
}

TEST(MulticastDelegateVoid, RemoveAlreadyRemovedHandleIsNoOp) {
    MulticastDelegate<> d;
    int callCount = 0;
    auto h = d.Add([&] { ++callCount; });

    d.Remove(h);
    EXPECT_NO_THROW(d.Remove(h)); // second remove must not throw or crash
    EXPECT_EQ(d.Count(), 0u);
}

TEST(MulticastDelegateVoid, ClearRemovesAllListeners) {
    MulticastDelegate<> d;
    int callCount = 0;
    d.Add([&] { ++callCount; });
    d.Add([&] { ++callCount; });

    d.Clear();
    EXPECT_EQ(d.Count(), 0u);

    d.Invoke();
    EXPECT_EQ(callCount, 0);
}

TEST(MulticastDelegateVoid, ClearDoesNotResetIdCounter) {
    // Handles created after Clear() must not alias handles created before it.
    MulticastDelegate<> d;
    auto h1 = d.Add([] {});
    d.Clear();
    auto h2 = d.Add([] {});

    EXPECT_NE(h1.id, h2.id);
}

TEST(MulticastDelegateVoid, MultipleAddsProduceUniqueHandles) {
    MulticastDelegate<> d;
    auto h1 = d.Add([] {});
    auto h2 = d.Add([] {});
    auto h3 = d.Add([] {});

    EXPECT_NE(h1.id, h2.id);
    EXPECT_NE(h2.id, h3.id);
    EXPECT_NE(h1.id, h3.id);
}

// ─────────────────────────────────────────────────────────────────────────────
// MulticastDelegate<int> — single-argument delegate
// ─────────────────────────────────────────────────────────────────────────────

TEST(MulticastDelegateInt, InvokeForwardsIntArgument) {
    MulticastDelegate<int> d;
    std::vector<int> received;
    d.Add([&](int v) { received.push_back(v); });
    d.Add([&](int v) { received.push_back(v * 2); });

    d.Invoke(7);
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0], 7);
    EXPECT_EQ(received[1], 14);
}

TEST(MulticastDelegateInt, InvokeMultipleTimesWithDifferentValues) {
    MulticastDelegate<int> d;
    int last = -1;
    d.Add([&](int v) { last = v; });

    d.Invoke(1);
    EXPECT_EQ(last, 1);
    d.Invoke(42);
    EXPECT_EQ(last, 42);
    d.Invoke(-5);
    EXPECT_EQ(last, -5);
}

TEST(MulticastDelegateInt, RemoveByHandleWhileOthersRemain) {
    MulticastDelegate<int> d;
    int sumA = 0, sumB = 0;
    auto ha = d.Add([&](int v) { sumA += v; });
    /*auto hb =*/ d.Add([&](int v) { sumB += v; });

    d.Invoke(3);
    EXPECT_EQ(sumA, 3);
    EXPECT_EQ(sumB, 3);

    d.Remove(ha);
    d.Invoke(5);
    EXPECT_EQ(sumA, 3);  // removed — must not accumulate
    EXPECT_EQ(sumB, 8);  // still active
}

// ─────────────────────────────────────────────────────────────────────────────
// MulticastDelegate<float, const std::string&> — multi-argument delegate
// ─────────────────────────────────────────────────────────────────────────────

TEST(MulticastDelegateMultiArg, InvokeForwardsAllArguments) {
    MulticastDelegate<float, const std::string&> d;
    float receivedDamage = 0.f;
    std::string receivedName;

    d.Add([&](float dmg, const std::string& name) {
        receivedDamage = dmg;
        receivedName   = name;
    });

    d.Invoke(42.5f, "fireball");
    EXPECT_FLOAT_EQ(receivedDamage, 42.5f);
    EXPECT_EQ(receivedName, "fireball");
}

TEST(MulticastDelegateMultiArg, MultipleListenersReceiveSameArguments) {
    MulticastDelegate<float, const std::string&> d;
    std::vector<float>       damages;
    std::vector<std::string> names;

    d.Add([&](float dmg, const std::string& name) {
        damages.push_back(dmg);
        names.push_back(name);
    });
    d.Add([&](float dmg, const std::string& name) {
        damages.push_back(dmg);
        names.push_back(name);
    });

    d.Invoke(10.f, "ice");
    ASSERT_EQ(damages.size(), 2u);
    EXPECT_FLOAT_EQ(damages[0], 10.f);
    EXPECT_FLOAT_EQ(damages[1], 10.f);
    EXPECT_EQ(names[0], "ice");
    EXPECT_EQ(names[1], "ice");
}

// ─────────────────────────────────────────────────────────────────────────────
// Self-removal during Invoke() — snapshot safety
// ─────────────────────────────────────────────────────────────────────────────

TEST(MulticastDelegate, SelfRemoveDuringInvokeIsSafe) {
    MulticastDelegate<> d;
    int callCount = 0;

    // This callback removes itself on the first call.
    DelegateHandle selfHandle;
    selfHandle = d.Add([&] {
        ++callCount;
        d.Remove(selfHandle); // must not crash or corrupt iteration
    });

    // Another listener that must still fire this Invoke() even after self-remove.
    int otherCount = 0;
    d.Add([&] { ++otherCount; });

    d.Invoke(); // both must fire for this call
    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(otherCount, 1);

    d.Invoke(); // self-removed listener must not fire again
    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(otherCount, 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Move semantics
// ─────────────────────────────────────────────────────────────────────────────

TEST(MulticastDelegate, MoveConstructorTransfersListeners) {
    MulticastDelegate<int> src;
    int received = -1;
    src.Add([&](int v) { received = v; });

    MulticastDelegate<int> dst(std::move(src));
    EXPECT_EQ(dst.Count(), 1u);

    dst.Invoke(99);
    EXPECT_EQ(received, 99);
}

TEST(MulticastDelegate, MoveAssignmentTransfersListeners) {
    MulticastDelegate<int> src;
    int received = -1;
    src.Add([&](int v) { received = v; });

    MulticastDelegate<int> dst;
    dst = std::move(src);
    EXPECT_EQ(dst.Count(), 1u);

    dst.Invoke(55);
    EXPECT_EQ(received, 55);
}

// ─────────────────────────────────────────────────────────────────────────────
// Count edge cases
// ─────────────────────────────────────────────────────────────────────────────

TEST(MulticastDelegate, CountAfterClearIsZero) {
    MulticastDelegate<> d;
    d.Add([] {});
    d.Add([] {});
    d.Clear();
    EXPECT_EQ(d.Count(), 0u);
}

TEST(MulticastDelegate, CountAfterRemoveAllOneByOne) {
    MulticastDelegate<> d;
    auto h1 = d.Add([] {});
    auto h2 = d.Add([] {});
    auto h3 = d.Add([] {});

    d.Remove(h1);
    EXPECT_EQ(d.Count(), 2u);
    d.Remove(h3);
    EXPECT_EQ(d.Count(), 1u);
    d.Remove(h2);
    EXPECT_EQ(d.Count(), 0u);
}

} // namespace uz
