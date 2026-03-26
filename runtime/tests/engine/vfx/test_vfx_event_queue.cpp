/// Unit tests for VFXEventQueue -- per-emitter burst event queue.
///
/// Covers: Emit+Drain round-trip, double Drain returns empty second time,
/// EmitFromDef copies all fields, HasPending() state transitions,
/// GetPendingCount() accuracy, Clear() empties queue, multiple events
/// drain in FIFO order, Emit with non-default velocity scale.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "events/vfx_event.h"

#include <string>
#include <vector>

using namespace uz;

// =============================================================================
// Emit then Drain — basic round-trip
// =============================================================================

/// A single emitted event is returned by the first Drain() call.
TEST(VFXEventQueue, Emit_ThenDrain_ReturnsEvent) {
    VFXEventQueue queue;
    queue.Emit("explode", 500, 2.0f);

    const auto events = queue.Drain();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "explode");
    EXPECT_EQ(events[0].emissionCount, 500u);
    EXPECT_FLOAT_EQ(events[0].velocityScale, 2.0f);
}

/// Drain on a freshly constructed queue returns an empty vector.
TEST(VFXEventQueue, Drain_EmptyQueue_ReturnsEmptyVector) {
    VFXEventQueue queue;
    const auto events = queue.Drain();
    EXPECT_TRUE(events.empty());
}

/// Default velocity scale (1.0) is used when no scale argument is provided.
TEST(VFXEventQueue, Emit_DefaultVelocityScale_IsOne) {
    VFXEventQueue queue;
    queue.Emit("spark", 10);

    const auto events = queue.Drain();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FLOAT_EQ(events[0].velocityScale, 1.0f)
        << "Default velocityScale must be 1.0";
}

// =============================================================================
// Double Drain — second call returns empty
// =============================================================================

/// Draining twice without an intermediate Emit returns empty on the second call.
TEST(VFXEventQueue, DrainTwice_SecondDrainReturnsEmpty) {
    VFXEventQueue queue;
    queue.Emit("burst", 100, 1.5f);

    const auto first = queue.Drain();
    ASSERT_EQ(first.size(), 1u);

    const auto second = queue.Drain();
    EXPECT_TRUE(second.empty())
        << "Second Drain must return empty — queue was cleared by first Drain";
}

// =============================================================================
// EmitFromDef — copies all fields
// =============================================================================

/// EmitFromDef enqueues an event with name, emissionCount, and velocityScale
/// copied directly from the provided VFXEventDef.
TEST(VFXEventQueue, EmitFromDef_CopiesAllFieldsFromDef) {
    VFXEventDef def;
    def.name = "impact";
    def.emissionCount = 250u;
    def.velocityScale = 3.5f;

    VFXEventQueue queue;
    queue.EmitFromDef(def);

    const auto events = queue.Drain();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "impact");
    EXPECT_EQ(events[0].emissionCount, 250u);
    EXPECT_FLOAT_EQ(events[0].velocityScale, 3.5f);
}

/// EmitFromDef with default-constructed VFXEventDef uses VFXEventDef defaults.
TEST(VFXEventQueue, EmitFromDef_DefaultDef_UsesDefaultValues) {
    VFXEventDef def; // name="", emissionCount=100, velocityScale=1.0

    VFXEventQueue queue;
    queue.EmitFromDef(def);

    const auto events = queue.Drain();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "");
    EXPECT_EQ(events[0].emissionCount, 100u);
    EXPECT_FLOAT_EQ(events[0].velocityScale, 1.0f);
}

// =============================================================================
// HasPending()
// =============================================================================

TEST(VFXEventQueue, HasPending_EmptyQueue_ReturnsFalse) {
    VFXEventQueue queue;
    EXPECT_FALSE(queue.HasPending());
}

TEST(VFXEventQueue, HasPending_AfterEmit_ReturnsTrue) {
    VFXEventQueue queue;
    queue.Emit("pop", 10);
    EXPECT_TRUE(queue.HasPending());
}

TEST(VFXEventQueue, HasPending_AfterDrain_ReturnsFalse) {
    VFXEventQueue queue;
    queue.Emit("pop", 10);
    queue.Drain();
    EXPECT_FALSE(queue.HasPending())
        << "HasPending must be false after Drain clears the queue";
}

TEST(VFXEventQueue, HasPending_AfterEmitFromDef_ReturnsTrue) {
    VFXEventQueue queue;
    VFXEventDef def;
    def.name = "trail";
    def.emissionCount = 5;
    queue.EmitFromDef(def);

    EXPECT_TRUE(queue.HasPending());
}

// =============================================================================
// GetPendingCount()
// =============================================================================

TEST(VFXEventQueue, GetPendingCount_EmptyQueue_ReturnsZero) {
    VFXEventQueue queue;
    EXPECT_EQ(queue.GetPendingCount(), 0u);
}

TEST(VFXEventQueue, GetPendingCount_AfterOneEmit_ReturnsOne) {
    VFXEventQueue queue;
    queue.Emit("a", 10);
    EXPECT_EQ(queue.GetPendingCount(), 1u);
}

TEST(VFXEventQueue, GetPendingCount_AfterThreeEmits_ReturnsThree) {
    VFXEventQueue queue;
    queue.Emit("a", 10);
    queue.Emit("b", 20);
    queue.Emit("c", 30);
    EXPECT_EQ(queue.GetPendingCount(), 3u);
}

TEST(VFXEventQueue, GetPendingCount_AfterDrain_ReturnsZero) {
    VFXEventQueue queue;
    queue.Emit("a", 10);
    queue.Emit("b", 20);
    queue.Drain();
    EXPECT_EQ(queue.GetPendingCount(), 0u);
}

// =============================================================================
// Clear()
// =============================================================================

TEST(VFXEventQueue, Clear_EmptiesQueue) {
    VFXEventQueue queue;
    queue.Emit("x", 100);
    queue.Emit("y", 200);
    ASSERT_EQ(queue.GetPendingCount(), 2u);

    queue.Clear();

    EXPECT_EQ(queue.GetPendingCount(), 0u);
    EXPECT_FALSE(queue.HasPending());
}

TEST(VFXEventQueue, Clear_OnEmptyQueue_DoesNotCrash) {
    VFXEventQueue queue;
    queue.Clear(); // Must not crash
    EXPECT_EQ(queue.GetPendingCount(), 0u);
}

/// After Clear(), Drain() returns empty (events were discarded, not drained).
TEST(VFXEventQueue, Clear_ThenDrain_ReturnsEmpty) {
    VFXEventQueue queue;
    queue.Emit("fire", 50);
    queue.Clear();

    const auto events = queue.Drain();
    EXPECT_TRUE(events.empty())
        << "Drain after Clear must return empty — events were discarded";
}

// =============================================================================
// Multiple events drain in FIFO order
// =============================================================================

/// Five events emitted in sequence are returned by Drain in the same order.
TEST(VFXEventQueue, MultipleEmits_DrainReturnsInFIFOOrder) {
    VFXEventQueue queue;
    queue.Emit("first",  10, 1.0f);
    queue.Emit("second", 20, 2.0f);
    queue.Emit("third",  30, 3.0f);
    queue.Emit("fourth", 40, 4.0f);
    queue.Emit("fifth",  50, 5.0f);

    const auto events = queue.Drain();
    ASSERT_EQ(events.size(), 5u);

    EXPECT_EQ(events[0].name, "first");
    EXPECT_EQ(events[0].emissionCount, 10u);
    EXPECT_FLOAT_EQ(events[0].velocityScale, 1.0f);

    EXPECT_EQ(events[1].name, "second");
    EXPECT_EQ(events[1].emissionCount, 20u);
    EXPECT_FLOAT_EQ(events[1].velocityScale, 2.0f);

    EXPECT_EQ(events[2].name, "third");
    EXPECT_EQ(events[2].emissionCount, 30u);
    EXPECT_FLOAT_EQ(events[2].velocityScale, 3.0f);

    EXPECT_EQ(events[3].name, "fourth");
    EXPECT_EQ(events[3].emissionCount, 40u);
    EXPECT_FLOAT_EQ(events[3].velocityScale, 4.0f);

    EXPECT_EQ(events[4].name, "fifth");
    EXPECT_EQ(events[4].emissionCount, 50u);
    EXPECT_FLOAT_EQ(events[4].velocityScale, 5.0f);
}

/// Mixed Emit and EmitFromDef events are drained in the order they were queued.
TEST(VFXEventQueue, MixedEmitAndEmitFromDef_FIFOOrderPreserved) {
    VFXEventDef def;
    def.name = "def_event";
    def.emissionCount = 77u;
    def.velocityScale = 7.7f;

    VFXEventQueue queue;
    queue.Emit("before_def", 1, 0.1f);
    queue.EmitFromDef(def);
    queue.Emit("after_def", 2, 0.2f);

    const auto events = queue.Drain();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].name, "before_def");
    EXPECT_EQ(events[1].name, "def_event");
    EXPECT_EQ(events[2].name, "after_def");
}

// =============================================================================
// Edge cases — event with zero emissionCount and extreme values
// =============================================================================

/// Emitting with emissionCount==0 is allowed (no assertion); the event is queued.
TEST(VFXEventQueue, Emit_ZeroEmissionCount_IsQueued) {
    VFXEventQueue queue;
    queue.Emit("empty", 0u, 1.0f);

    const auto events = queue.Drain();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].emissionCount, 0u);
}

/// Very large emissionCount (UINT32_MAX) is stored without truncation.
TEST(VFXEventQueue, Emit_MaxUInt32EmissionCount_StoredCorrectly) {
    VFXEventQueue queue;
    queue.Emit("huge", UINT32_MAX, 1.0f);

    const auto events = queue.Drain();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].emissionCount, UINT32_MAX);
}
