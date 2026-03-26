/// Unit tests for SubEmitterModule.
///
/// Covers: default config values, GetName, GetShaderCode content, GetParameters
/// (count + each entry), SubEmitterTrigger enum values, Execute with null queue,
/// Execute with null parameterData, Execute OnDeath path (velocity scaling,
/// color preservation), Execute OnCollision path, Execute OnEvent path (relay
/// named events), inheritVelocity=0.0 zeroes velocity, inheritVelocity=1.0
/// passes velocity unchanged, inheritColor=false resets to white, inheritColor=
/// true preserves parent color, empty readback does nothing, SetChildEventQueue
/// accepts and clears pointer, SubEmitterGPUPayload sizeof/alignof,
/// SubEmitterReadbackBuffer constants and IsValid() before Create.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "modules/sub_emitter_module.h"
#include "core/sub_emitter_readback.h"
#include "events/vfx_event.h"

#include <cstdint>
#include <string_view>
#include <vector>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a SubEmitterReadback with N flagged particles.
// Each particle has a distinct position/velocity/color/age so that
// per-particle assertions are unambiguous.
// ─────────────────────────────────────────────────────────────────────────────

SubEmitterReadback MakeReadback(int count) {
    SubEmitterReadback rb;
    rb.flaggedParticles.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        EventPayload p;
        p.position = Vec3{static_cast<float>(i) * 10.0f,
                          static_cast<float>(i) * 20.0f,
                          static_cast<float>(i) * 30.0f};
        p.velocity = Vec3{1.0f + static_cast<float>(i),
                          2.0f + static_cast<float>(i),
                          3.0f + static_cast<float>(i)};
        p.color    = Vec4{0.25f, 0.5f, 0.75f, 1.0f};
        p.age      = static_cast<float>(i) * 0.1f;
        rb.flaggedParticles.push_back(p);
    }
    return rb;
}

/// Run Execute() and return all drained events from the child queue.
std::vector<VFXPendingEvent> RunExecute(SubEmitterModule& mod,
                                        VFXEventQueue& queue,
                                        const SubEmitterReadback* rb)
{
    ExecuteContext ctx{};
    ctx.numParticles  = 10;
    ctx.dt            = 0.016f;
    ctx.parameterData = rb;
    mod.SetChildEventQueue(&queue);
    mod.Execute(ctx);
    return queue.Drain();
}

} // anonymous namespace

// =============================================================================
// Default configuration
// =============================================================================

TEST(UZ_SubEmitterModule, DefaultConfig_ChildGraphId_IsZero) {
    SubEmitterModule m;
    EXPECT_EQ(m.childGraphId, 0);
}

TEST(UZ_SubEmitterModule, DefaultConfig_Trigger_IsOnDeath) {
    SubEmitterModule m;
    EXPECT_EQ(m.trigger, SubEmitterTrigger::OnDeath);
}

TEST(UZ_SubEmitterModule, DefaultConfig_InheritVelocity_IsPointFive) {
    SubEmitterModule m;
    EXPECT_FLOAT_EQ(m.inheritVelocity, 0.5f);
}

TEST(UZ_SubEmitterModule, DefaultConfig_InheritColor_IsTrue) {
    SubEmitterModule m;
    EXPECT_TRUE(m.inheritColor);
}

TEST(UZ_SubEmitterModule, DefaultConfig_EventName_IsEmpty) {
    SubEmitterModule m;
    EXPECT_TRUE(m.eventName.empty());
}

// =============================================================================
// GetName
// =============================================================================

TEST(UZ_SubEmitterModule, GetName_ReturnsSubEmitter) {
    SubEmitterModule m;
    EXPECT_STREQ(m.GetName(), "sub_emitter");
}

// =============================================================================
// GetShaderCode — non-empty and contains required identifiers
// =============================================================================

TEST(UZ_SubEmitterModule, GetShaderCode_IsNonEmpty) {
    SubEmitterModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_FALSE(code.empty())
        << "SubEmitterModule must provide a non-empty HLSL snippet";
}

TEST(UZ_SubEmitterModule, GetShaderCode_ContainsSubEmitterFlags) {
    SubEmitterModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("SubEmitterFlags"), std::string_view::npos)
        << "HLSL snippet must reference SubEmitterFlags UAV";
}

TEST(UZ_SubEmitterModule, GetShaderCode_ContainsShouldTrigger) {
    SubEmitterModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("shouldTrigger"), std::string_view::npos)
        << "HLSL snippet must use 'shouldTrigger' boolean variable";
}

TEST(UZ_SubEmitterModule, GetShaderCode_ContainsCbSubEmitterTrigger) {
    SubEmitterModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbSubEmitterTrigger"), std::string_view::npos)
        << "HLSL snippet must reference cbSubEmitterTrigger constant";
}

// =============================================================================
// GetParameters — count and each entry
// =============================================================================

TEST(UZ_SubEmitterModule, GetParameters_ReturnsFourEntries) {
    SubEmitterModule m;
    const auto params = m.GetParameters();
    EXPECT_EQ(params.size(), 4u)
        << "SubEmitterModule must expose exactly 4 parameters";
}

TEST(UZ_SubEmitterModule, GetParameters_FirstEntry_IsChildGraph_Int) {
    SubEmitterModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_STREQ(params[0].name, "child_graph");
    EXPECT_EQ(params[0].type, ParameterType::Int);
}

TEST(UZ_SubEmitterModule, GetParameters_FirstEntry_DefaultInt_MatchesChildGraphId) {
    SubEmitterModule m;
    m.childGraphId = 42;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_EQ(params[0].defaultInt, 42);
}

TEST(UZ_SubEmitterModule, GetParameters_SecondEntry_IsTrigger_Int) {
    SubEmitterModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_STREQ(params[1].name, "trigger");
    EXPECT_EQ(params[1].type, ParameterType::Int);
}

TEST(UZ_SubEmitterModule, GetParameters_SecondEntry_DefaultInt_IsOnDeath) {
    SubEmitterModule m;
    // Default trigger is OnDeath == 0.
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_EQ(params[1].defaultInt, static_cast<int32_t>(SubEmitterTrigger::OnDeath));
}

TEST(UZ_SubEmitterModule, GetParameters_ThirdEntry_IsInheritVelocity_Float) {
    SubEmitterModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 3u);
    EXPECT_STREQ(params[2].name, "inherit_velocity");
    EXPECT_EQ(params[2].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[2].defaultFloat, 0.5f);
}

TEST(UZ_SubEmitterModule, GetParameters_FourthEntry_IsInheritColor_Bool) {
    SubEmitterModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_STREQ(params[3].name, "inherit_color");
    EXPECT_EQ(params[3].type, ParameterType::Bool);
    EXPECT_TRUE(params[3].defaultBool);
}

// =============================================================================
// SubEmitterTrigger enum values
// =============================================================================

TEST(UZ_SubEmitterTrigger, OnDeath_IsZero) {
    EXPECT_EQ(static_cast<uint8_t>(SubEmitterTrigger::OnDeath), 0u);
}

TEST(UZ_SubEmitterTrigger, OnCollision_IsOne) {
    EXPECT_EQ(static_cast<uint8_t>(SubEmitterTrigger::OnCollision), 1u);
}

TEST(UZ_SubEmitterTrigger, OnEvent_IsTwo) {
    EXPECT_EQ(static_cast<uint8_t>(SubEmitterTrigger::OnEvent), 2u);
}

// =============================================================================
// Execute — null child queue: no crash, early return
// =============================================================================

TEST(UZ_SubEmitterModule, Execute_NullQueue_NoChildQueueSet_DoesNotCrash) {
    SubEmitterModule m;
    // m_childEventQueue is nullptr by default — never call SetChildEventQueue.
    SubEmitterReadback rb = MakeReadback(3);
    ExecuteContext ctx{};
    ctx.numParticles  = 3;
    ctx.parameterData = &rb;
    // Must not crash.
    m.Execute(ctx);
    // Still alive: module name remains valid.
    EXPECT_STREQ(m.GetName(), "sub_emitter");
}

// =============================================================================
// Execute — null parameterData with OnDeath: no crash, early return
// =============================================================================

TEST(UZ_SubEmitterModule, Execute_NullParameterData_OnDeath_DoesNotCrash) {
    SubEmitterModule m;
    m.trigger = SubEmitterTrigger::OnDeath;

    VFXEventQueue queue;
    m.SetChildEventQueue(&queue);

    ExecuteContext ctx{};
    ctx.numParticles  = 5;
    ctx.parameterData = nullptr; // No readback data.
    m.Execute(ctx);

    // Queue must remain empty — nothing to fire.
    EXPECT_FALSE(queue.HasPending());
}

// =============================================================================
// Execute — null parameterData with OnCollision: no crash, early return
// =============================================================================

TEST(UZ_SubEmitterModule, Execute_NullParameterData_OnCollision_DoesNotCrash) {
    SubEmitterModule m;
    m.trigger = SubEmitterTrigger::OnCollision;

    VFXEventQueue queue;
    m.SetChildEventQueue(&queue);

    ExecuteContext ctx{};
    ctx.parameterData = nullptr;
    m.Execute(ctx);

    EXPECT_FALSE(queue.HasPending());
}

// =============================================================================
// Execute — null parameterData with OnEvent: early return, no crash
// =============================================================================

TEST(UZ_SubEmitterModule, Execute_NullParameterData_OnEvent_DoesNotCrash) {
    SubEmitterModule m;
    m.trigger    = SubEmitterTrigger::OnEvent;
    m.eventName  = "burst";

    VFXEventQueue queue;
    m.SetChildEventQueue(&queue);

    ExecuteContext ctx{};
    ctx.parameterData = nullptr;
    m.Execute(ctx);

    EXPECT_FALSE(queue.HasPending());
}

// =============================================================================
// Execute OnDeath — fires payloads into child queue
// =============================================================================

TEST(UZ_SubEmitterModule, Execute_OnDeath_FiresOneEventWithCorrectCount) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnDeath;
    m.inheritVelocity = 1.0f;
    m.inheritColor    = true;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(3);

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u)
        << "Execute(OnDeath) must fire exactly one EmitWithPayloads call";
    EXPECT_EQ(events[0].emissionCount, 3u)
        << "emissionCount must match flaggedParticles.size()";
}

TEST(UZ_SubEmitterModule, Execute_OnDeath_EventName_IsOnDeath) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnDeath;
    m.inheritVelocity = 1.0f;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(2);

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "OnDeath")
        << "OnDeath trigger must emit event named 'OnDeath'";
}

TEST(UZ_SubEmitterModule, Execute_OnDeath_PayloadsPreservePosition) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnDeath;
    m.inheritVelocity = 0.5f;
    m.inheritColor    = true;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(1);
    // Particle 0: position = {0, 0, 0}.

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].payloads.size(), 1u);
    EXPECT_FLOAT_EQ(events[0].payloads[0].position.x, 0.0f);
    EXPECT_FLOAT_EQ(events[0].payloads[0].position.y, 0.0f);
    EXPECT_FLOAT_EQ(events[0].payloads[0].position.z, 0.0f);
}

TEST(UZ_SubEmitterModule, Execute_OnDeath_PayloadsPreserveAge) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnDeath;
    m.inheritVelocity = 1.0f;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(2);
    // Particle 1 has age = 0.1f (i*0.1).

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].payloads.size(), 2u);
    EXPECT_NEAR(events[0].payloads[1].age, 0.1f, 1e-5f);
}

// =============================================================================
// Execute OnCollision — same structural behavior as OnDeath
// =============================================================================

TEST(UZ_SubEmitterModule, Execute_OnCollision_FiresOneEventWithCorrectCount) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnCollision;
    m.inheritVelocity = 1.0f;
    m.inheritColor    = true;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(4);

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].emissionCount, 4u);
}

TEST(UZ_SubEmitterModule, Execute_OnCollision_EventName_IsOnCollision) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnCollision;
    m.inheritVelocity = 1.0f;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(2);

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "OnCollision")
        << "OnCollision trigger must emit event named 'OnCollision'";
}

// =============================================================================
// Execute OnEvent — relays named events into child queue
// =============================================================================

TEST(UZ_SubEmitterModule, Execute_OnEvent_FiresEventWithMatchingName) {
    SubEmitterModule m;
    m.trigger    = SubEmitterTrigger::OnEvent;
    m.eventName  = "explode";
    m.inheritVelocity = 1.0f;
    m.inheritColor    = true;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(2);

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "explode")
        << "OnEvent must emit under the configured eventName";
}

TEST(UZ_SubEmitterModule, Execute_OnEvent_EmissionCount_MatchesFlaggedParticles) {
    SubEmitterModule m;
    m.trigger    = SubEmitterTrigger::OnEvent;
    m.eventName  = "burst";
    m.inheritVelocity = 1.0f;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(5);

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].emissionCount, 5u);
    EXPECT_EQ(events[0].payloads.size(), 5u);
}

TEST(UZ_SubEmitterModule, Execute_OnEvent_EmptyFlaggedParticles_DoesNotFire) {
    SubEmitterModule m;
    m.trigger    = SubEmitterTrigger::OnEvent;
    m.eventName  = "burst";

    VFXEventQueue queue;
    SubEmitterReadback rb;
    // rb.flaggedParticles is empty.

    const auto events = RunExecute(m, queue, &rb);

    EXPECT_TRUE(events.empty())
        << "OnEvent with empty flaggedParticles must not fire any event";
}

// =============================================================================
// inheritVelocity = 0.0 — velocity zeroed in payloads
// =============================================================================

TEST(UZ_SubEmitterModule, InheritVelocityZero_ZeroesVelocityInPayloads) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnDeath;
    m.inheritVelocity = 0.0f;
    m.inheritColor    = true;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(2);
    // Both particles have non-zero velocity (set by MakeReadback).

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u);
    ASSERT_GE(events[0].payloads.size(), 2u);

    for (const auto& payload : events[0].payloads) {
        EXPECT_FLOAT_EQ(payload.velocity.x, 0.0f)
            << "inheritVelocity=0 must zero X velocity";
        EXPECT_FLOAT_EQ(payload.velocity.y, 0.0f)
            << "inheritVelocity=0 must zero Y velocity";
        EXPECT_FLOAT_EQ(payload.velocity.z, 0.0f)
            << "inheritVelocity=0 must zero Z velocity";
    }
}

// =============================================================================
// inheritVelocity = 1.0 — velocity passed through unchanged
// =============================================================================

TEST(UZ_SubEmitterModule, InheritVelocityOne_PassesThroughVelocityUnchanged) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnDeath;
    m.inheritVelocity = 1.0f;
    m.inheritColor    = true;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(1);
    // Particle 0: velocity = {1.0, 2.0, 3.0} (i=0 → 1+0, 2+0, 3+0).

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].payloads.size(), 1u);
    EXPECT_FLOAT_EQ(events[0].payloads[0].velocity.x, 1.0f);
    EXPECT_FLOAT_EQ(events[0].payloads[0].velocity.y, 2.0f);
    EXPECT_FLOAT_EQ(events[0].payloads[0].velocity.z, 3.0f);
}

// =============================================================================
// inheritVelocity = 0.5 — velocity scaled by half
// =============================================================================

TEST(UZ_SubEmitterModule, InheritVelocityHalf_ScalesVelocityByHalf) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnDeath;
    m.inheritVelocity = 0.5f;
    m.inheritColor    = true;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(1);
    // Particle 0: velocity = {1.0, 2.0, 3.0}.

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].payloads.size(), 1u);
    EXPECT_NEAR(events[0].payloads[0].velocity.x, 0.5f, 1e-5f);
    EXPECT_NEAR(events[0].payloads[0].velocity.y, 1.0f, 1e-5f);
    EXPECT_NEAR(events[0].payloads[0].velocity.z, 1.5f, 1e-5f);
}

// =============================================================================
// inheritColor = false — color reset to white {1,1,1,1}
// =============================================================================

TEST(UZ_SubEmitterModule, InheritColorFalse_ResetsColorToWhite) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnDeath;
    m.inheritVelocity = 1.0f;
    m.inheritColor    = false;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(2);
    // Both particles have color = {0.25, 0.5, 0.75, 1.0} (set by MakeReadback).

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u);
    ASSERT_GE(events[0].payloads.size(), 2u);

    for (const auto& payload : events[0].payloads) {
        EXPECT_FLOAT_EQ(payload.color.x, 1.0f) << "R must be reset to 1.0";
        EXPECT_FLOAT_EQ(payload.color.y, 1.0f) << "G must be reset to 1.0";
        EXPECT_FLOAT_EQ(payload.color.z, 1.0f) << "B must be reset to 1.0";
        EXPECT_FLOAT_EQ(payload.color.w, 1.0f) << "A must be reset to 1.0";
    }
}

// =============================================================================
// inheritColor = true — parent color preserved in payload
// =============================================================================

TEST(UZ_SubEmitterModule, InheritColorTrue_PreservesParentColor) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnDeath;
    m.inheritVelocity = 1.0f;
    m.inheritColor    = true;

    VFXEventQueue queue;
    SubEmitterReadback rb = MakeReadback(1);
    // Particle 0: color = {0.25, 0.5, 0.75, 1.0}.

    const auto events = RunExecute(m, queue, &rb);

    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events[0].payloads.size(), 1u);
    EXPECT_NEAR(events[0].payloads[0].color.x, 0.25f, 1e-5f);
    EXPECT_NEAR(events[0].payloads[0].color.y, 0.50f, 1e-5f);
    EXPECT_NEAR(events[0].payloads[0].color.z, 0.75f, 1e-5f);
    EXPECT_NEAR(events[0].payloads[0].color.w, 1.0f,  1e-5f);
}

// =============================================================================
// Execute — empty readback: fires no events
// =============================================================================

TEST(UZ_SubEmitterModule, Execute_EmptyReadback_OnDeath_FiresNoEvents) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnDeath;
    m.inheritVelocity = 1.0f;

    VFXEventQueue queue;
    SubEmitterReadback rb;
    // rb.flaggedParticles is empty.

    const auto events = RunExecute(m, queue, &rb);

    EXPECT_TRUE(events.empty())
        << "OnDeath with empty readback must fire no events";
}

TEST(UZ_SubEmitterModule, Execute_EmptyReadback_OnCollision_FiresNoEvents) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnCollision;
    m.inheritVelocity = 1.0f;

    VFXEventQueue queue;
    SubEmitterReadback rb;

    const auto events = RunExecute(m, queue, &rb);

    EXPECT_TRUE(events.empty())
        << "OnCollision with empty readback must fire no events";
}

// =============================================================================
// SetChildEventQueue — set and unset
// =============================================================================

TEST(UZ_SubEmitterModule, SetChildEventQueue_Set_EnablesEventDelivery) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnDeath;
    m.inheritVelocity = 1.0f;

    VFXEventQueue queue;
    m.SetChildEventQueue(&queue);

    SubEmitterReadback rb = MakeReadback(1);
    ExecuteContext ctx{};
    ctx.parameterData = &rb;
    m.Execute(ctx);

    EXPECT_TRUE(queue.HasPending())
        << "With queue set, Execute must deliver events";
}

TEST(UZ_SubEmitterModule, SetChildEventQueue_SetThenNull_DisablesEventDelivery) {
    SubEmitterModule m;
    m.trigger         = SubEmitterTrigger::OnDeath;
    m.inheritVelocity = 1.0f;

    VFXEventQueue queue;
    m.SetChildEventQueue(&queue);
    // Detach the queue.
    m.SetChildEventQueue(nullptr);

    SubEmitterReadback rb = MakeReadback(2);
    ExecuteContext ctx{};
    ctx.parameterData = &rb;
    m.Execute(ctx);

    EXPECT_FALSE(queue.HasPending())
        << "After SetChildEventQueue(nullptr), Execute must not deliver events";
}

// =============================================================================
// SubEmitterGPUPayload — sizeof and alignof (GPU layout contract)
// =============================================================================

TEST(UZ_SubEmitterGPUPayload, SizeOf_Is48Bytes) {
    EXPECT_EQ(sizeof(SubEmitterGPUPayload), 48u)
        << "SubEmitterGPUPayload must be 48 bytes to match HLSL struct layout";
}

TEST(UZ_SubEmitterGPUPayload, AlignOf_Is16Bytes) {
    EXPECT_EQ(alignof(SubEmitterGPUPayload), 16u)
        << "SubEmitterGPUPayload must be 16-byte aligned for GPU structured buffer";
}

TEST(UZ_SubEmitterGPUPayload, DefaultConstruction_AllFieldsZeroExceptColor) {
    SubEmitterGPUPayload p{};
    EXPECT_FLOAT_EQ(p.posX, 0.0f);
    EXPECT_FLOAT_EQ(p.posY, 0.0f);
    EXPECT_FLOAT_EQ(p.posZ, 0.0f);
    EXPECT_FLOAT_EQ(p._pad0, 0.0f);
    EXPECT_FLOAT_EQ(p.velX, 0.0f);
    EXPECT_FLOAT_EQ(p.velY, 0.0f);
    EXPECT_FLOAT_EQ(p.velZ, 0.0f);
    EXPECT_FLOAT_EQ(p.age, 0.0f);
    // Color defaults to white (1,1,1,1).
    EXPECT_FLOAT_EQ(p.colorR, 1.0f);
    EXPECT_FLOAT_EQ(p.colorG, 1.0f);
    EXPECT_FLOAT_EQ(p.colorB, 1.0f);
    EXPECT_FLOAT_EQ(p.colorA, 1.0f);
}

// =============================================================================
// SubEmitterReadbackBuffer — compile-time constants
// =============================================================================

TEST(UZ_SubEmitterReadbackBuffer, kBufferCount_IsThree) {
    EXPECT_EQ(SubEmitterReadbackBuffer::kBufferCount, 3u)
        << "Triple-buffering requires exactly 3 readback slots";
}

TEST(UZ_SubEmitterReadbackBuffer, kMaxReadbackParticles_Is256) {
    EXPECT_EQ(SubEmitterReadbackBuffer::kMaxReadbackParticles, 256u)
        << "Max readback cap must be 256 particles per frame";
}

// =============================================================================
// SubEmitterReadbackBuffer — IsValid() before Create
// =============================================================================

TEST(UZ_SubEmitterReadbackBuffer, IsValid_BeforeCreate_ReturnsFalse) {
    SubEmitterReadbackBuffer buf{};
    EXPECT_FALSE(buf.IsValid())
        << "Newly default-constructed buffer must not report IsValid() true";
}

TEST(UZ_SubEmitterReadbackBuffer, CurrentWriteIndex_Default_IsZero) {
    SubEmitterReadbackBuffer buf{};
    EXPECT_EQ(buf.currentWriteIndex, 0u);
}

TEST(UZ_SubEmitterReadbackBuffer, ParticleCapacity_Default_IsZero) {
    SubEmitterReadbackBuffer buf{};
    EXPECT_EQ(buf.particleCapacity, 0u);
}

TEST(UZ_SubEmitterReadbackBuffer, FramesSinceCreate_Default_IsZero) {
    SubEmitterReadbackBuffer buf{};
    EXPECT_EQ(buf.framesSinceCreate, 0u);
}
