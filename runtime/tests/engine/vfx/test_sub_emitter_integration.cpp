/// Integration tests for SubEmitterModule — child emitter spawning on parent events.
///
/// Covers: OnDeath trigger delivers payloads, velocity inheritance scaling,
/// color inheritance toggle, OnCollision trigger, OnEvent named relay, chained
/// sub-emitters (Parent -> Child1 -> Child2), empty readback no-op, null queue
/// safety, multi-frame accumulation, and 256-particle large readback.
///
/// No GPU hardware required — all tests are CPU-only. ExecuteContext::parameterData
/// is set to a stack-allocated SubEmitterReadback whose flaggedParticles are built
/// directly in test code.

#include <gtest/gtest.h>

#include "modules/sub_emitter_module.h"
#include "core/sub_emitter_readback.h"
#include "events/vfx_event.h"

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Build an ExecuteContext whose parameterData points to the given readback.
///
/// The readback must outlive the returned context.
ExecuteContext MakeCtx(const SubEmitterReadback& readback,
                       uint32_t numParticles = 0,
                       float dt = 0.016f) {
    ExecuteContext ctx{};
    ctx.numParticles  = numParticles;
    ctx.dt            = dt;
    ctx.frameNumber   = 1;
    ctx.parameterData = &readback;
    return ctx;
}

/// Build an ExecuteContext with no parameterData (null readback pointer).
ExecuteContext MakeCtxNoData(uint32_t numParticles = 0, float dt = 0.016f) {
    ExecuteContext ctx{};
    ctx.numParticles  = numParticles;
    ctx.dt            = dt;
    ctx.frameNumber   = 1;
    ctx.parameterData = nullptr;
    return ctx;
}

/// Build an EventPayload with explicit values.
EventPayload MakePayload(float px, float py, float pz,
                         float vx, float vy, float vz,
                         float cr, float cg, float cb, float ca,
                         float age = 0.5f) {
    EventPayload p;
    p.position = Vec3{px, py, pz};
    p.velocity = Vec3{vx, vy, vz};
    p.color    = Vec4{cr, cg, cb, ca};
    p.age      = age;
    return p;
}

} // anonymous namespace

// =============================================================================
// ParentDeath_ChildReceivesPayloads
//
// Three dead particles in the readback. After Execute() the child queue must
// hold exactly one VFXPendingEvent whose payloads vector contains three entries
// with positions matching the input exactly.
// =============================================================================

TEST(UZ_SubEmitterIntegration, ParentDeath_ChildReceivesPayloads) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger        = SubEmitterTrigger::OnDeath;
    mod.inheritVelocity = 1.0f; // full pass-through for easy position check
    mod.inheritColor    = true;
    mod.SetChildEventQueue(&childQueue);

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(10.f, 20.f, 30.f,  1.f, 0.f, 0.f,  1.f, 0.f, 0.f, 1.f, 0.1f),
        MakePayload(40.f, 50.f, 60.f,  0.f, 1.f, 0.f,  0.f, 1.f, 0.f, 1.f, 0.2f),
        MakePayload(70.f, 80.f, 90.f,  0.f, 0.f, 1.f,  0.f, 0.f, 1.f, 1.f, 0.3f),
    };

    mod.Execute(MakeCtx(readback));

    ASSERT_EQ(childQueue.GetPendingCount(), 1u)
        << "OnDeath: Execute must emit exactly one VFXPendingEvent";

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 1u);

    const VFXPendingEvent& ev = events[0];
    EXPECT_EQ(ev.emissionCount, 3u)
        << "emissionCount must equal the number of flagged particles";
    ASSERT_EQ(ev.payloads.size(), 3u);

    EXPECT_FLOAT_EQ(ev.payloads[0].position.x, 10.f);
    EXPECT_FLOAT_EQ(ev.payloads[0].position.y, 20.f);
    EXPECT_FLOAT_EQ(ev.payloads[0].position.z, 30.f);

    EXPECT_FLOAT_EQ(ev.payloads[1].position.x, 40.f);
    EXPECT_FLOAT_EQ(ev.payloads[1].position.y, 50.f);
    EXPECT_FLOAT_EQ(ev.payloads[1].position.z, 60.f);

    EXPECT_FLOAT_EQ(ev.payloads[2].position.x, 70.f);
    EXPECT_FLOAT_EQ(ev.payloads[2].position.y, 80.f);
    EXPECT_FLOAT_EQ(ev.payloads[2].position.z, 90.f);
}

// =============================================================================
// ParentDeath_InheritVelocityHalf
//
// inheritVelocity = 0.5. Parent velocity {10, 20, 30} -> child velocity {5, 10, 15}.
// =============================================================================

TEST(UZ_SubEmitterIntegration, ParentDeath_InheritVelocityHalf) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnDeath;
    mod.inheritVelocity = 0.5f;
    mod.inheritColor    = true;
    mod.SetChildEventQueue(&childQueue);

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(0.f, 0.f, 0.f,  10.f, 20.f, 30.f,  1.f, 1.f, 1.f, 1.f),
    };

    mod.Execute(MakeCtx(readback));

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].payloads.size(), 1u);

    const EventPayload& p = events[0].payloads[0];
    EXPECT_NEAR(p.velocity.x, 5.f,  1e-5f)
        << "velocity.x must be scaled by inheritVelocity=0.5";
    EXPECT_NEAR(p.velocity.y, 10.f, 1e-5f)
        << "velocity.y must be scaled by inheritVelocity=0.5";
    EXPECT_NEAR(p.velocity.z, 15.f, 1e-5f)
        << "velocity.z must be scaled by inheritVelocity=0.5";
}

// =============================================================================
// ParentDeath_InheritVelocityZero
//
// inheritVelocity = 0.0. Child velocity must be zero regardless of parent.
// =============================================================================

TEST(UZ_SubEmitterIntegration, ParentDeath_InheritVelocityZero) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnDeath;
    mod.inheritVelocity = 0.0f;
    mod.inheritColor    = true;
    mod.SetChildEventQueue(&childQueue);

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(0.f, 0.f, 0.f,  100.f, 200.f, 300.f,  1.f, 1.f, 1.f, 1.f),
    };

    mod.Execute(MakeCtx(readback));

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].payloads.size(), 1u);

    const EventPayload& p = events[0].payloads[0];
    EXPECT_FLOAT_EQ(p.velocity.x, 0.f)
        << "inheritVelocity=0 must zero out child velocity.x";
    EXPECT_FLOAT_EQ(p.velocity.y, 0.f)
        << "inheritVelocity=0 must zero out child velocity.y";
    EXPECT_FLOAT_EQ(p.velocity.z, 0.f)
        << "inheritVelocity=0 must zero out child velocity.z";
}

// =============================================================================
// ParentDeath_InheritColorDisabled
//
// inheritColor=false. Parent red {1,0,0,1} -> child white {1,1,1,1}.
// =============================================================================

TEST(UZ_SubEmitterIntegration, ParentDeath_InheritColorDisabled) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnDeath;
    mod.inheritVelocity = 0.0f;
    mod.inheritColor    = false;
    mod.SetChildEventQueue(&childQueue);

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(0.f, 0.f, 0.f,  0.f, 0.f, 0.f,  1.f, 0.f, 0.f, 1.f), // red
    };

    mod.Execute(MakeCtx(readback));

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].payloads.size(), 1u);

    const EventPayload& p = events[0].payloads[0];
    EXPECT_FLOAT_EQ(p.color.x, 1.f)
        << "inheritColor=false must reset color.r to 1.0 (white)";
    EXPECT_FLOAT_EQ(p.color.y, 1.f)
        << "inheritColor=false must reset color.g to 1.0 (white)";
    EXPECT_FLOAT_EQ(p.color.z, 1.f)
        << "inheritColor=false must reset color.b to 1.0 (white)";
    EXPECT_FLOAT_EQ(p.color.w, 1.f)
        << "inheritColor=false must reset color.a to 1.0 (white)";
}

// =============================================================================
// ParentDeath_InheritColorEnabled
//
// inheritColor=true (default). Parent's color must pass through unchanged.
// =============================================================================

TEST(UZ_SubEmitterIntegration, ParentDeath_InheritColorEnabled) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnDeath;
    mod.inheritVelocity = 0.0f;
    mod.inheritColor    = true;
    mod.SetChildEventQueue(&childQueue);

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(0.f, 0.f, 0.f,  0.f, 0.f, 0.f,  0.2f, 0.4f, 0.8f, 0.9f),
    };

    mod.Execute(MakeCtx(readback));

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].payloads.size(), 1u);

    const EventPayload& p = events[0].payloads[0];
    EXPECT_NEAR(p.color.x, 0.2f, 1e-5f) << "color.r must be preserved when inheritColor=true";
    EXPECT_NEAR(p.color.y, 0.4f, 1e-5f) << "color.g must be preserved when inheritColor=true";
    EXPECT_NEAR(p.color.z, 0.8f, 1e-5f) << "color.b must be preserved when inheritColor=true";
    EXPECT_NEAR(p.color.w, 0.9f, 1e-5f) << "color.a must be preserved when inheritColor=true";
}

// =============================================================================
// ParentCollision_GroundPlane
//
// trigger=OnCollision, 2 particles in readback. Child queue receives 2 payloads.
// Trigger name in the emitted event must be "OnCollision".
// =============================================================================

TEST(UZ_SubEmitterIntegration, ParentCollision_GroundPlane) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnCollision;
    mod.inheritVelocity = 1.0f;
    mod.inheritColor    = true;
    mod.SetChildEventQueue(&childQueue);

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(100.f, 200.f, -1.f,  5.f, 0.f, 0.f,  1.f, 1.f, 1.f, 1.f),
        MakePayload(150.f, 250.f, -2.f,  0.f, 5.f, 0.f,  1.f, 1.f, 1.f, 1.f),
    };

    mod.Execute(MakeCtx(readback));

    ASSERT_EQ(childQueue.GetPendingCount(), 1u);

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 1u);

    const VFXPendingEvent& ev = events[0];
    EXPECT_EQ(ev.name, "OnCollision")
        << "OnCollision trigger must set event name to \"OnCollision\"";
    EXPECT_EQ(ev.emissionCount, 2u)
        << "emissionCount must equal the number of collided particles";
    ASSERT_EQ(ev.payloads.size(), 2u);

    EXPECT_FLOAT_EQ(ev.payloads[0].position.x, 100.f);
    EXPECT_FLOAT_EQ(ev.payloads[1].position.x, 150.f);
}

// =============================================================================
// OnEvent_NamedEvent
//
// trigger=OnEvent. 1 payload in readback. Child queue receives event whose
// name matches mod.eventName exactly.
// =============================================================================

TEST(UZ_SubEmitterIntegration, OnEvent_NamedEvent) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnEvent;
    mod.eventName       = "explode";
    mod.inheritVelocity = 1.0f;
    mod.inheritColor    = true;
    mod.SetChildEventQueue(&childQueue);

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(0.f, 0.f, 0.f,  0.f, 0.f, 0.f,  1.f, 1.f, 1.f, 1.f),
    };

    mod.Execute(MakeCtx(readback));

    ASSERT_EQ(childQueue.GetPendingCount(), 1u);

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 1u);

    EXPECT_EQ(events[0].name, "explode")
        << "OnEvent trigger must use mod.eventName as the child event name";
    ASSERT_EQ(events[0].payloads.size(), 1u);
}

// =============================================================================
// OnEvent_EmptyEventName
//
// OnEvent with eventName="" and 1 payload. Event name in child queue is "".
// =============================================================================

TEST(UZ_SubEmitterIntegration, OnEvent_EmptyEventName) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnEvent;
    mod.eventName       = "";
    mod.inheritVelocity = 1.0f;
    mod.inheritColor    = true;
    mod.SetChildEventQueue(&childQueue);

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(1.f, 2.f, 3.f,  0.f, 0.f, 0.f,  1.f, 1.f, 1.f, 1.f),
    };

    mod.Execute(MakeCtx(readback));

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "")
        << "OnEvent with empty eventName must relay empty string as event name";
}

// =============================================================================
// ChainedSubEmitters
//
// Parent (OnDeath) -> Child1 -> Child2.
// Parent fires 2 payloads to Child1. Child1's module re-fires to Child2.
// Both Child1 and Child2 queues must hold the correct events.
// =============================================================================

TEST(UZ_SubEmitterIntegration, ChainedSubEmitters) {
    VFXEventQueue child1Queue;
    VFXEventQueue child2Queue;

    // Layer 1: Parent -> Child1.
    SubEmitterModule parentMod;
    parentMod.trigger         = SubEmitterTrigger::OnDeath;
    parentMod.inheritVelocity = 1.0f;
    parentMod.inheritColor    = true;
    parentMod.SetChildEventQueue(&child1Queue);

    SubEmitterReadback parentReadback;
    parentReadback.flaggedParticles = {
        MakePayload(10.f, 0.f, 0.f,  1.f, 0.f, 0.f,  1.f, 0.f, 0.f, 1.f, 0.1f),
        MakePayload(20.f, 0.f, 0.f,  2.f, 0.f, 0.f,  0.f, 1.f, 0.f, 1.f, 0.2f),
    };

    parentMod.Execute(MakeCtx(parentReadback));

    // Child1 now has 1 event with 2 payloads.
    ASSERT_EQ(child1Queue.GetPendingCount(), 1u)
        << "Child1 queue must have received 1 event from parent";
    const auto child1Events = child1Queue.Drain();
    ASSERT_EQ(child1Events.size(), 1u);
    ASSERT_EQ(child1Events[0].payloads.size(), 2u);

    // Layer 2: Child1 -> Child2.  Build a new readback from Child1's drained payloads.
    SubEmitterModule child1Mod;
    child1Mod.trigger         = SubEmitterTrigger::OnDeath;
    child1Mod.inheritVelocity = 0.5f;
    child1Mod.inheritColor    = true;
    child1Mod.SetChildEventQueue(&child2Queue);

    SubEmitterReadback child1Readback;
    child1Readback.flaggedParticles = child1Events[0].payloads;

    child1Mod.Execute(MakeCtx(child1Readback));

    ASSERT_EQ(child2Queue.GetPendingCount(), 1u)
        << "Child2 queue must have received 1 event from Child1";
    const auto child2Events = child2Queue.Drain();
    ASSERT_EQ(child2Events.size(), 1u);
    ASSERT_EQ(child2Events[0].payloads.size(), 2u)
        << "Child2 must hold 2 payloads matching Child1's 2 flagged particles";

    // Verify inheritance scaling applied by child1Mod (0.5 factor).
    EXPECT_NEAR(child2Events[0].payloads[0].velocity.x, 0.5f, 1e-5f)
        << "Child2 payload[0] velocity.x must be 1.0 * 0.5 = 0.5";
    EXPECT_NEAR(child2Events[0].payloads[1].velocity.x, 1.0f, 1e-5f)
        << "Child2 payload[1] velocity.x must be 2.0 * 0.5 = 1.0";
}

// =============================================================================
// NoTrigger_EmptyReadback
//
// Readback with no flaggedParticles. Child queue must remain empty.
// =============================================================================

TEST(UZ_SubEmitterIntegration, NoTrigger_EmptyReadback) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnDeath;
    mod.inheritVelocity = 1.0f;
    mod.inheritColor    = true;
    mod.SetChildEventQueue(&childQueue);

    SubEmitterReadback emptyReadback;
    // flaggedParticles is empty by default.

    mod.Execute(MakeCtx(emptyReadback));

    EXPECT_FALSE(childQueue.HasPending())
        << "Empty readback must produce no events in the child queue";
    EXPECT_EQ(childQueue.GetPendingCount(), 0u);
}

// =============================================================================
// NullQueue_NoMethods
//
// No child queue wired (SetChildEventQueue never called or called with nullptr).
// Execute with valid readback must not crash or throw.
// =============================================================================

TEST(UZ_SubEmitterIntegration, NullQueue_NoMethods) {
    // Do NOT call SetChildEventQueue — m_childEventQueue stays null.
    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnDeath;
    mod.inheritVelocity = 1.0f;
    mod.inheritColor    = true;

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(1.f, 2.f, 3.f,  0.f, 0.f, 0.f,  1.f, 1.f, 1.f, 1.f),
    };

    // Must complete without crashing — no queue pointer means no-op.
    EXPECT_NO_THROW(mod.Execute(MakeCtx(readback)));
}

// =============================================================================
// NullQueue_ExplicitlySetToNull
//
// SetChildEventQueue(nullptr) after a valid queue is set. Execute must be a no-op.
// =============================================================================

TEST(UZ_SubEmitterIntegration, NullQueue_ExplicitlySetToNull) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger = SubEmitterTrigger::OnDeath;
    mod.SetChildEventQueue(&childQueue);
    mod.SetChildEventQueue(nullptr); // detach

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(5.f, 5.f, 5.f,  1.f, 1.f, 1.f,  1.f, 1.f, 1.f, 1.f),
    };

    EXPECT_NO_THROW(mod.Execute(MakeCtx(readback)));
    EXPECT_FALSE(childQueue.HasPending())
        << "After SetChildEventQueue(nullptr), no events must arrive in the old queue";
}

// =============================================================================
// NullParameterData_NoMethods
//
// ctx.parameterData is null (OnDeath path). Execute must be a no-op, not crash.
// =============================================================================

TEST(UZ_SubEmitterIntegration, NullParameterData_NoMethods) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger = SubEmitterTrigger::OnDeath;
    mod.SetChildEventQueue(&childQueue);

    const ExecuteContext ctx = MakeCtxNoData();
    EXPECT_NO_THROW(mod.Execute(ctx));
    EXPECT_FALSE(childQueue.HasPending())
        << "Null parameterData must result in no events emitted";
}

// =============================================================================
// OnEvent_NullParameterData_NoMethods
//
// OnEvent path with null parameterData. Execute must be a no-op, not crash.
// =============================================================================

TEST(UZ_SubEmitterIntegration, OnEvent_NullParameterData_NoMethods) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger    = SubEmitterTrigger::OnEvent;
    mod.eventName  = "burst";
    mod.SetChildEventQueue(&childQueue);

    const ExecuteContext ctx = MakeCtxNoData();
    EXPECT_NO_THROW(mod.Execute(ctx));
    EXPECT_FALSE(childQueue.HasPending())
        << "OnEvent with null parameterData must produce no events";
}

// =============================================================================
// MultipleFrames_AccumulateEvents
//
// Execute 3 times with different readbacks (1, 2, 3 particles).
// Child queue must accumulate all 3 VFXPendingEvents (drained at the end).
// =============================================================================

TEST(UZ_SubEmitterIntegration, MultipleFrames_AccumulateEvents) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnDeath;
    mod.inheritVelocity = 1.0f;
    mod.inheritColor    = true;
    mod.SetChildEventQueue(&childQueue);

    // Frame 1: 1 particle.
    SubEmitterReadback rb1;
    rb1.flaggedParticles = {
        MakePayload(1.f, 0.f, 0.f,  0.f, 0.f, 0.f,  1.f, 1.f, 1.f, 1.f),
    };
    mod.Execute(MakeCtx(rb1, 10u));

    // Frame 2: 2 particles.
    SubEmitterReadback rb2;
    rb2.flaggedParticles = {
        MakePayload(2.f, 0.f, 0.f,  0.f, 0.f, 0.f,  1.f, 1.f, 1.f, 1.f),
        MakePayload(3.f, 0.f, 0.f,  0.f, 0.f, 0.f,  1.f, 1.f, 1.f, 1.f),
    };
    mod.Execute(MakeCtx(rb2, 10u));

    // Frame 3: 3 particles.
    SubEmitterReadback rb3;
    rb3.flaggedParticles = {
        MakePayload(4.f, 0.f, 0.f,  0.f, 0.f, 0.f,  1.f, 1.f, 1.f, 1.f),
        MakePayload(5.f, 0.f, 0.f,  0.f, 0.f, 0.f,  1.f, 1.f, 1.f, 1.f),
        MakePayload(6.f, 0.f, 0.f,  0.f, 0.f, 0.f,  1.f, 1.f, 1.f, 1.f),
    };
    mod.Execute(MakeCtx(rb3, 10u));

    EXPECT_EQ(childQueue.GetPendingCount(), 3u)
        << "Child queue must accumulate 3 VFXPendingEvents across 3 Execute calls";

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 3u);

    EXPECT_EQ(events[0].emissionCount, 1u) << "Frame 1 event must carry 1 payload";
    EXPECT_EQ(events[1].emissionCount, 2u) << "Frame 2 event must carry 2 payloads";
    EXPECT_EQ(events[2].emissionCount, 3u) << "Frame 3 event must carry 3 payloads";

    // Verify identity of first payload in each event using x position.
    EXPECT_FLOAT_EQ(events[0].payloads[0].position.x, 1.f);
    EXPECT_FLOAT_EQ(events[1].payloads[0].position.x, 2.f);
    EXPECT_FLOAT_EQ(events[2].payloads[0].position.x, 4.f);
}

// =============================================================================
// LargeReadback_256Particles
//
// 256 flagged particles (SubEmitterReadbackBuffer::kMaxReadbackParticles).
// All 256 payloads must arrive in the child queue untruncated.
// =============================================================================

TEST(UZ_SubEmitterIntegration, LargeReadback_256Particles) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnDeath;
    mod.inheritVelocity = 1.0f;
    mod.inheritColor    = true;
    mod.SetChildEventQueue(&childQueue);

    constexpr uint32_t kCount = SubEmitterReadbackBuffer::kMaxReadbackParticles; // 256
    SubEmitterReadback readback;
    readback.flaggedParticles.reserve(kCount);
    for (uint32_t i = 0; i < kCount; ++i) {
        const float fi = static_cast<float>(i);
        readback.flaggedParticles.push_back(
            MakePayload(fi, fi * 2.f, fi * 3.f,
                        fi, 0.f, 0.f,
                        1.f, 1.f, 1.f, 1.f,
                        fi * 0.01f));
    }

    mod.Execute(MakeCtx(readback, kCount));

    ASSERT_EQ(childQueue.GetPendingCount(), 1u)
        << "256-particle readback must produce exactly 1 VFXPendingEvent";

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].emissionCount, kCount)
        << "emissionCount must be 256";
    ASSERT_EQ(events[0].payloads.size(), kCount)
        << "payloads vector must contain all 256 entries";

    // Spot-check first and last entries.
    EXPECT_FLOAT_EQ(events[0].payloads[0].position.x, 0.f)
        << "First payload position.x must be 0";
    EXPECT_FLOAT_EQ(events[0].payloads[kCount - 1].position.x,
                    static_cast<float>(kCount - 1))
        << "Last payload position.x must be " << (kCount - 1);
    EXPECT_NEAR(events[0].payloads[kCount - 1].age,
                static_cast<float>(kCount - 1) * 0.01f, 1e-4f)
        << "Last payload age must match the constructed value";
}

// =============================================================================
// OnDeath_EventNameIsOnDeath
//
// OnDeath trigger: the emitted VFXPendingEvent name must be "OnDeath" so
// child emitters can filter by source trigger type.
// =============================================================================

TEST(UZ_SubEmitterIntegration, OnDeath_EventNameIsOnDeath) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnDeath;
    mod.inheritVelocity = 1.0f;
    mod.inheritColor    = true;
    mod.SetChildEventQueue(&childQueue);

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(0.f, 0.f, 0.f,  0.f, 0.f, 0.f,  1.f, 1.f, 1.f, 1.f),
    };

    mod.Execute(MakeCtx(readback));

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "OnDeath")
        << "OnDeath trigger must set event name to \"OnDeath\"";
}

// =============================================================================
// OnEvent_VelocityInheritanceApplied
//
// OnEvent path must apply inheritVelocity the same way as OnDeath/OnCollision.
// =============================================================================

TEST(UZ_SubEmitterIntegration, OnEvent_VelocityInheritanceApplied) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnEvent;
    mod.eventName       = "shockwave";
    mod.inheritVelocity = 0.25f;
    mod.inheritColor    = true;
    mod.SetChildEventQueue(&childQueue);

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(0.f, 0.f, 0.f,  8.f, 4.f, 2.f,  1.f, 1.f, 1.f, 1.f),
    };

    mod.Execute(MakeCtx(readback));

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].payloads.size(), 1u);

    const EventPayload& p = events[0].payloads[0];
    EXPECT_NEAR(p.velocity.x, 2.f,  1e-5f)
        << "OnEvent: velocity.x must be 8.0 * 0.25 = 2.0";
    EXPECT_NEAR(p.velocity.y, 1.f,  1e-5f)
        << "OnEvent: velocity.y must be 4.0 * 0.25 = 1.0";
    EXPECT_NEAR(p.velocity.z, 0.5f, 1e-5f)
        << "OnEvent: velocity.z must be 2.0 * 0.25 = 0.5";
}

// =============================================================================
// OnEvent_ColorInheritanceDisabled
//
// OnEvent path with inheritColor=false. Parent blue -> child white.
// =============================================================================

TEST(UZ_SubEmitterIntegration, OnEvent_ColorInheritanceDisabled) {
    VFXEventQueue childQueue;

    SubEmitterModule mod;
    mod.trigger         = SubEmitterTrigger::OnEvent;
    mod.eventName       = "flash";
    mod.inheritVelocity = 1.0f;
    mod.inheritColor    = false;
    mod.SetChildEventQueue(&childQueue);

    SubEmitterReadback readback;
    readback.flaggedParticles = {
        MakePayload(0.f, 0.f, 0.f,  0.f, 0.f, 0.f,  0.f, 0.f, 1.f, 0.5f), // blue
    };

    mod.Execute(MakeCtx(readback));

    const auto events = childQueue.Drain();
    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].payloads.size(), 1u);

    const EventPayload& p = events[0].payloads[0];
    EXPECT_FLOAT_EQ(p.color.x, 1.f) << "OnEvent inheritColor=false: color.r must be 1.0";
    EXPECT_FLOAT_EQ(p.color.y, 1.f) << "OnEvent inheritColor=false: color.g must be 1.0";
    EXPECT_FLOAT_EQ(p.color.z, 1.f) << "OnEvent inheritColor=false: color.b must be 1.0";
    EXPECT_FLOAT_EQ(p.color.w, 1.f) << "OnEvent inheritColor=false: color.a must be 1.0";
}
