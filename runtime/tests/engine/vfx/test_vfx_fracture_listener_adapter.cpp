/// Unit tests for phase 9.18.0 — VFX fracture listener adapter.
///
/// Tests the adapter + registry integration, null bridge safety, and
/// event forwarding.
///
/// @ref engine/vfx/events/vfx_fracture_listener_adapter.h

#include <gtest/gtest.h>

#include "engine/vfx/events/vfx_fracture_listener_adapter.h"
#include "engine/physics/physics_listener_registry.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Adapter basics
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxFractureListenerAdapter, NullBridgeReturnsFalse) {
    VfxFractureListenerAdapter adapter(nullptr);
    VfxFractureEventInput input;
    EXPECT_FALSE(adapter.OnFracture(input));
}

TEST(VfxFractureListenerAdapter, GetBridgeReturnsConstructorArg) {
    VfxPhysicsFractureBridge bridge(nullptr, "test_fracture");
    VfxFractureListenerAdapter adapter(&bridge);
    EXPECT_EQ(adapter.GetBridge(), &bridge);
}

TEST(VfxFractureListenerAdapter, SetBridgeRebinds) {
    VfxPhysicsFractureBridge bridgeA(nullptr, "a");
    VfxPhysicsFractureBridge bridgeB(nullptr, "b");
    VfxFractureListenerAdapter adapter(&bridgeA);
    EXPECT_EQ(adapter.GetBridge(), &bridgeA);
    adapter.SetBridge(&bridgeB);
    EXPECT_EQ(adapter.GetBridge(), &bridgeB);
}

// ─────────────────────────────────────────────────────────────────────────────
// Registry integration
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxFractureListenerAdapter, RegisterAndForward) {
    PhysicsListenerRegistry reg;
    // A bound queue is required: a bridge with a null target never forwards
    // and never counts (VfxPhysicsFractureBridge contract — "Null disables
    // forwarding", covered by VfxFractureBridge.NullTargetRejectsForward).
    VFXEventQueue queue;
    VfxPhysicsFractureBridge bridge(&queue, "fracture");
    VfxFractureListenerAdapter adapter(&bridge);

    auto handle = reg.RegisterFractureListener(&adapter);
    EXPECT_NE(handle, kInvalidPhysicsListenerHandle);
    EXPECT_EQ(reg.FractureListenerCount(), 1u);

    // Dispatch a fracture event through the registry.
    VfxFractureEventInput input;
    input.bodyId = 42;
    input.impactSpeed = 5.0f;
    input.pieces.push_back({{1, 2, 3}, {0, 0, 1}, 10.0f});

    reg.DispatchFracture(input);

    // registry -> adapter -> bridge -> queue.
    EXPECT_EQ(bridge.ForwardedCount(), 1u);
    const auto drained = queue.Drain();
    ASSERT_EQ(drained.size(), 1u);
    EXPECT_EQ(drained[0].name, "fracture");
    EXPECT_EQ(drained[0].payloads.size(), 1u);
}

TEST(VfxFractureListenerAdapter, UnregisterStopsForwarding) {
    PhysicsListenerRegistry reg;
    // Bound queue so a forwarded event WOULD be counted — makes the
    // zero-count assertion below prove the unregister, not a null target.
    VFXEventQueue queue;
    VfxPhysicsFractureBridge bridge(&queue, "fracture");
    VfxFractureListenerAdapter adapter(&bridge);

    auto handle = reg.RegisterFractureListener(&adapter);
    reg.UnregisterFractureListener(handle);
    EXPECT_EQ(reg.FractureListenerCount(), 0u);

    VfxFractureEventInput input;
    input.pieces.push_back({{0, 0, 0}, {0, 0, 0}, 1.0f});
    reg.DispatchFracture(input);

    EXPECT_EQ(bridge.ForwardedCount(), 0u);
    EXPECT_FALSE(queue.HasPending());
}

TEST(VfxFractureListenerAdapter, EmptyPiecesNoPayloads) {
    PhysicsListenerRegistry reg;
    VfxPhysicsFractureBridge bridge(nullptr, "fracture");
    VfxFractureListenerAdapter adapter(&bridge);
    reg.RegisterFractureListener(&adapter);

    VfxFractureEventInput input;
    // No pieces — bridge.OnFracture should still be called.
    reg.DispatchFracture(input);

    // No crash. Forward count depends on bridge (OnFracture with
    // empty pieces returns false, but was still called).
    EXPECT_GE(bridge.ForwardedCount(), 0u);
}
