#include "event_queue.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Test fixtures and helpers
// ─────────────────────────────────────────────────────────────────────────────

struct IntEvent {
    int value = 0;
};

struct StringEvent {
    std::string message;
};

// ─────────────────────────────────────────────────────────────────────────────
// Order preservation
// ─────────────────────────────────────────────────────────────────────────────

TEST(EventQueueTest, FlushDeliversEventsInEnqueueOrder) {
    uz::EventQueue<IntEvent> queue;
    std::vector<int> received;

    queue.Subscribe([&](const IntEvent& e) {
        received.push_back(e.value);
    });

    queue.Enqueue({10});
    queue.Enqueue({20});
    queue.Enqueue({30});
    queue.Flush();

    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0], 10);
    EXPECT_EQ(received[1], 20);
    EXPECT_EQ(received[2], 30);
}

// ─────────────────────────────────────────────────────────────────────────────
// Queue clearing
// ─────────────────────────────────────────────────────────────────────────────

TEST(EventQueueTest, FlushClearsQueueSoSecondFlushIsNoOp) {
    uz::EventQueue<IntEvent> queue;
    int callCount = 0;

    queue.Subscribe([&](const IntEvent&) { ++callCount; });

    queue.Enqueue({1});
    queue.Enqueue({2});
    queue.Flush(); // dispatches 2 events
    queue.Flush(); // no pending events — must be a no-op

    EXPECT_EQ(callCount, 2);
    EXPECT_EQ(queue.PendingCount(), 0u);
}

TEST(EventQueueTest, PendingCountIsZeroAfterFlush) {
    uz::EventQueue<IntEvent> queue;
    queue.Subscribe([](const IntEvent&) {});

    queue.Enqueue({1});
    queue.Enqueue({2});
    EXPECT_EQ(queue.PendingCount(), 2u);

    queue.Flush();
    EXPECT_EQ(queue.PendingCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple subscribers
// ─────────────────────────────────────────────────────────────────────────────

TEST(EventQueueTest, AllSubscribersReceiveEachEvent) {
    uz::EventQueue<IntEvent> queue;

    std::vector<int> a_received;
    std::vector<int> b_received;
    std::vector<int> c_received;

    queue.Subscribe([&](const IntEvent& e) { a_received.push_back(e.value); });
    queue.Subscribe([&](const IntEvent& e) { b_received.push_back(e.value); });
    queue.Subscribe([&](const IntEvent& e) { c_received.push_back(e.value); });

    queue.Enqueue({7});
    queue.Enqueue({13});
    queue.Flush();

    ASSERT_EQ(a_received.size(), 2u);
    ASSERT_EQ(b_received.size(), 2u);
    ASSERT_EQ(c_received.size(), 2u);

    EXPECT_EQ(a_received[0], 7);
    EXPECT_EQ(b_received[0], 7);
    EXPECT_EQ(c_received[0], 7);

    EXPECT_EQ(a_received[1], 13);
    EXPECT_EQ(b_received[1], 13);
    EXPECT_EQ(c_received[1], 13);
}

// ─────────────────────────────────────────────────────────────────────────────
// Unsubscribe
// ─────────────────────────────────────────────────────────────────────────────

TEST(EventQueueTest, UnsubscribeStopsDeliveryToRemovedListener) {
    uz::EventQueue<IntEvent> queue;

    std::vector<int> a_received;
    std::vector<int> b_received;

    auto handleA = queue.Subscribe([&](const IntEvent& e) { a_received.push_back(e.value); });
    queue.Subscribe([&](const IntEvent& e) { b_received.push_back(e.value); });

    queue.Enqueue({1});
    queue.Flush();

    // Remove subscriber A before the second flush
    queue.Unsubscribe(handleA);

    queue.Enqueue({2});
    queue.Flush();

    // A received only the first event, B received both
    ASSERT_EQ(a_received.size(), 1u);
    EXPECT_EQ(a_received[0], 1);

    ASSERT_EQ(b_received.size(), 2u);
    EXPECT_EQ(b_received[0], 1);
    EXPECT_EQ(b_received[1], 2);
}

TEST(EventQueueTest, UnsubscribeWithInvalidHandleIsNoOp) {
    uz::EventQueue<IntEvent> queue;
    int callCount = 0;

    queue.Subscribe([&](const IntEvent&) { ++callCount; });

    uz::DelegateHandle invalid{}; // id == 0 — always invalid
    queue.Unsubscribe(invalid);   // must not crash or corrupt state

    queue.Enqueue({42});
    queue.Flush();

    EXPECT_EQ(callCount, 1);
}

TEST(EventQueueTest, UnsubscribeAlreadyRemovedHandleIsNoOp) {
    uz::EventQueue<IntEvent> queue;
    int callCount = 0;

    auto h = queue.Subscribe([&](const IntEvent&) { ++callCount; });
    queue.Unsubscribe(h);
    queue.Unsubscribe(h); // double-remove must not crash

    queue.Enqueue({1});
    queue.Flush();

    EXPECT_EQ(callCount, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// PendingCount
// ─────────────────────────────────────────────────────────────────────────────

TEST(EventQueueTest, PendingCountTracksEnqueuedEvents) {
    uz::EventQueue<IntEvent> queue;

    EXPECT_EQ(queue.PendingCount(), 0u);

    queue.Enqueue({1});
    EXPECT_EQ(queue.PendingCount(), 1u);

    queue.Enqueue({2});
    queue.Enqueue({3});
    EXPECT_EQ(queue.PendingCount(), 3u);

    queue.Flush();
    EXPECT_EQ(queue.PendingCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// No subscribers
// ─────────────────────────────────────────────────────────────────────────────

TEST(EventQueueTest, FlushWithNoSubscribersDoesNotCrash) {
    uz::EventQueue<IntEvent> queue;

    queue.Enqueue({1});
    queue.Enqueue({2});

    // No subscribers registered — Flush must be a safe no-op dispatch
    EXPECT_NO_THROW(queue.Flush());
    EXPECT_EQ(queue.PendingCount(), 0u);
}

TEST(EventQueueTest, FlushWithNoEventsAndNoSubscribersDoesNotCrash) {
    uz::EventQueue<IntEvent> queue;
    EXPECT_NO_THROW(queue.Flush());
}

// ─────────────────────────────────────────────────────────────────────────────
// Re-enqueue after flush
// ─────────────────────────────────────────────────────────────────────────────

TEST(EventQueueTest, EnqueueAfterFlushWorksForNextCycle) {
    uz::EventQueue<IntEvent> queue;
    std::vector<int> received;

    queue.Subscribe([&](const IntEvent& e) { received.push_back(e.value); });

    // First cycle
    queue.Enqueue({100});
    queue.Flush();

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0], 100);

    // Second cycle — queue must accept new events after flush
    queue.Enqueue({200});
    queue.Enqueue({300});
    queue.Flush();

    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[1], 200);
    EXPECT_EQ(received[2], 300);
}

// ─────────────────────────────────────────────────────────────────────────────
// Move-only event types
// ─────────────────────────────────────────────────────────────────────────────

struct MoveOnlyEvent {
    explicit MoveOnlyEvent(int v) : value(v) {}
    MoveOnlyEvent(const MoveOnlyEvent&)            = delete;
    MoveOnlyEvent& operator=(const MoveOnlyEvent&) = delete;
    MoveOnlyEvent(MoveOnlyEvent&&) noexcept            = default;
    MoveOnlyEvent& operator=(MoveOnlyEvent&&) noexcept = default;

    int value = 0;
};

TEST(EventQueueTest, MoveOnlyEventTypeIsSupported) {
    uz::EventQueue<MoveOnlyEvent> queue;
    std::vector<int> received;

    queue.Subscribe([&](const MoveOnlyEvent& e) { received.push_back(e.value); });

    queue.Enqueue(MoveOnlyEvent{42});
    queue.Enqueue(MoveOnlyEvent{99});
    queue.Flush();

    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0], 42);
    EXPECT_EQ(received[1], 99);
}

// ─────────────────────────────────────────────────────────────────────────────
// SubscriberCount
// ─────────────────────────────────────────────────────────────────────────────

TEST(EventQueueTest, SubscriberCountReflectsActiveListeners) {
    uz::EventQueue<IntEvent> queue;

    EXPECT_EQ(queue.SubscriberCount(), 0u);

    auto h1 = queue.Subscribe([](const IntEvent&) {});
    EXPECT_EQ(queue.SubscriberCount(), 1u);

    auto h2 = queue.Subscribe([](const IntEvent&) {});
    EXPECT_EQ(queue.SubscriberCount(), 2u);

    queue.Unsubscribe(h1);
    EXPECT_EQ(queue.SubscriberCount(), 1u);

    queue.Unsubscribe(h2);
    EXPECT_EQ(queue.SubscriberCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// String event payload (non-trivial type)
// ─────────────────────────────────────────────────────────────────────────────

TEST(EventQueueTest, StringEventPayloadIsDeliveredCorrectly) {
    uz::EventQueue<StringEvent> queue;
    std::vector<std::string> received;

    queue.Subscribe([&](const StringEvent& e) { received.push_back(e.message); });

    queue.Enqueue({"hello"});
    queue.Enqueue({"world"});
    queue.Flush();

    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0], "hello");
    EXPECT_EQ(received[1], "world");
}
