/// Unit tests for AttractorModule and AttractorBuffer.
///
/// Covers: default field values, GetName, GetShaderCode HLSL content,
/// GetParameters count/order/types/defaults, AttractorFalloff enum values,
/// kMinRadius/kKillDistance constexpr invariants, AttractorData struct size
/// (GPU layout contract), AttractorBuffer IsValid before Create, Execute no-op,
/// custom config propagation into GetParameters, multi-attractor HLSL tokens,
/// kMaxAttractors value, AttractorBuffer AddAttractor count tracking, Clear reset,
/// AddAttractor capacity overflow behavior, AttractorData field defaults,
/// parameter slot ordering, and falloff default (InverseSquare).
///
/// No GPU hardware required -- all tests are CPU-only.

#include <gtest/gtest.h>

#include "modules/attractor_module.h"
#include "core/attractor_buffer.h"

#include <string_view>
#include <cstdint>

using namespace uz;

// =============================================================================
// AttractorFalloff enum -- numeric values must match the HLSL falloffType field
// =============================================================================

/// The HLSL shader branch-dispatches on falloffType as an integer. If these
/// values drift from the enum, the GPU will silently apply the wrong falloff.

TEST(UZ_AttractorFalloff, Constant_IsZero) {
    EXPECT_EQ(static_cast<uint8_t>(AttractorFalloff::Constant), 0u);
}

TEST(UZ_AttractorFalloff, Linear_IsOne) {
    EXPECT_EQ(static_cast<uint8_t>(AttractorFalloff::Linear), 1u);
}

TEST(UZ_AttractorFalloff, InverseSquare_IsTwo) {
    EXPECT_EQ(static_cast<uint8_t>(AttractorFalloff::InverseSquare), 2u);
}

// =============================================================================
// AttractorModule constexpr constants -- must be positive
// =============================================================================

/// kMinRadius prevents division by zero in InverseSquare falloff (CPU + GPU).
/// kKillDistance is the arrival threshold for kill-on-arrival.

TEST(UZ_AttractorModule, kMinRadius_IsPositive) {
    EXPECT_GT(AttractorModule::kMinRadius, 0.0f)
        << "kMinRadius must be > 0 to prevent division by zero";
}

TEST(UZ_AttractorModule, kMinRadius_ExactValue) {
    EXPECT_FLOAT_EQ(AttractorModule::kMinRadius, 0.01f);
}

TEST(UZ_AttractorModule, kKillDistance_IsPositive) {
    EXPECT_GT(AttractorModule::kKillDistance, 0.0f)
        << "kKillDistance must be > 0 to detect arrival";
}

TEST(UZ_AttractorModule, kKillDistance_ExactValue) {
    EXPECT_FLOAT_EQ(AttractorModule::kKillDistance, 0.05f);
}

// =============================================================================
// AttractorModule -- default field values
// =============================================================================

/// Defaults must match the table in the header doc-comment and the AttractorData
/// struct defaults so that the primary attractor starts in a known state.

TEST(UZ_AttractorModule, DefaultConfig_AttractorPosition_IsOrigin) {
    AttractorModule m;
    EXPECT_FLOAT_EQ(m.attractorPosition.x, 0.0f);
    EXPECT_FLOAT_EQ(m.attractorPosition.y, 0.0f);
    EXPECT_FLOAT_EQ(m.attractorPosition.z, 0.0f);
}

TEST(UZ_AttractorModule, DefaultConfig_Strength_IsTen) {
    AttractorModule m;
    EXPECT_FLOAT_EQ(m.strength, 10.0f);
}

TEST(UZ_AttractorModule, DefaultConfig_Radius_IsFive) {
    AttractorModule m;
    EXPECT_FLOAT_EQ(m.radius, 5.0f);
}

TEST(UZ_AttractorModule, DefaultConfig_Falloff_IsInverseSquare) {
    AttractorModule m;
    EXPECT_EQ(m.falloff, AttractorFalloff::InverseSquare);
}

TEST(UZ_AttractorModule, DefaultConfig_KillOnArrival_IsFalse) {
    AttractorModule m;
    EXPECT_FALSE(m.killOnArrival);
}

// =============================================================================
// GetName
// =============================================================================

TEST(UZ_AttractorModule, GetName_ReturnsAttractor) {
    AttractorModule m;
    EXPECT_STREQ(m.GetName(), "attractor");
}

// =============================================================================
// GetShaderCode -- non-empty, contains required HLSL symbols
// =============================================================================

/// The compositor stitches this snippet verbatim into a compute shader.
/// Each expected token is a symbol that the broader shader framework provides;
/// their absence means the snippet is broken or outdated.

TEST(UZ_AttractorModule, GetShaderCode_IsNonEmpty) {
    AttractorModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_FALSE(code.empty())
        << "AttractorModule must provide a non-empty HLSL snippet";
}

TEST(UZ_AttractorModule, GetShaderCode_ContainsAttractorsBuf) {
    AttractorModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("AttractorsBuf"), std::string_view::npos)
        << "HLSL snippet must reference AttractorsBuf structured buffer";
}

TEST(UZ_AttractorModule, GetShaderCode_ContainsCbAttractorCount) {
    AttractorModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbAttractorCount"), std::string_view::npos)
        << "HLSL snippet must reference cbAttractorCount for loop bound";
}

TEST(UZ_AttractorModule, GetShaderCode_ContainsForLoop) {
    AttractorModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("for"), std::string_view::npos)
        << "HLSL snippet must iterate over attractors with a for loop";
}

TEST(UZ_AttractorModule, GetShaderCode_ContainsVelocityAccumulation) {
    AttractorModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("Velocities"), std::string_view::npos)
        << "HLSL snippet must write to Velocities particle UAV";
}

TEST(UZ_AttractorModule, GetShaderCode_ContainsFalloffTypeBranch) {
    AttractorModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("falloffType"), std::string_view::npos)
        << "HLSL snippet must branch on falloffType for mode dispatch";
}

TEST(UZ_AttractorModule, GetShaderCode_ContainsKillOnArrivalCheck) {
    AttractorModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("killOnArrival"), std::string_view::npos)
        << "HLSL snippet must check killOnArrival flag";
}

// =============================================================================
// GetParameters -- count, ordering, names, types, default values
// =============================================================================

/// Parameter slot indices are documented in the header and correspond to
/// constant buffer field positions consumed by the compositor.

TEST(UZ_AttractorModule, GetParameters_ReturnsFiveEntries) {
    AttractorModule m;
    const auto params = m.GetParameters();
    EXPECT_EQ(params.size(), 5u)
        << "AttractorModule must expose exactly 5 parameters";
}

TEST(UZ_AttractorModule, GetParameters_Slot0_IsAttractorPosition) {
    AttractorModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_STREQ(params[0].name, "attractor_position");
    EXPECT_EQ(params[0].type, ParameterType::Vec3);
}

TEST(UZ_AttractorModule, GetParameters_Slot0_DefaultIsOrigin) {
    AttractorModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.x, 0.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.y, 0.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.z, 0.0f);
}

TEST(UZ_AttractorModule, GetParameters_Slot1_IsStrength) {
    AttractorModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_STREQ(params[1].name, "strength");
    EXPECT_EQ(params[1].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[1].defaultFloat, 10.0f);
}

TEST(UZ_AttractorModule, GetParameters_Slot2_IsRadius) {
    AttractorModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 3u);
    EXPECT_STREQ(params[2].name, "radius");
    EXPECT_EQ(params[2].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[2].defaultFloat, 5.0f);
}

TEST(UZ_AttractorModule, GetParameters_Slot3_IsFalloff) {
    AttractorModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_STREQ(params[3].name, "falloff");
    EXPECT_EQ(params[3].type, ParameterType::Int);
}

TEST(UZ_AttractorModule, GetParameters_Slot3_FalloffDefault_IsInverseSquare) {
    AttractorModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    // Default falloff is InverseSquare == 2.
    EXPECT_EQ(params[3].defaultInt, static_cast<int32_t>(AttractorFalloff::InverseSquare));
}

TEST(UZ_AttractorModule, GetParameters_Slot4_IsKillOnArrival) {
    AttractorModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 5u);
    EXPECT_STREQ(params[4].name, "kill_on_arrival");
    EXPECT_EQ(params[4].type, ParameterType::Bool);
    EXPECT_FALSE(params[4].defaultBool);
}

// =============================================================================
// Custom config propagates into GetParameters defaults
// =============================================================================

/// GetParameters reads live field values, not cached copies. Changing a field
/// must be reflected in the returned parameter defaults so that the compositor
/// and editor see the same values.

TEST(UZ_AttractorModule, CustomConfig_StrengthReflectedInParameters) {
    AttractorModule m;
    m.strength = 42.0f;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_FLOAT_EQ(params[1].defaultFloat, 42.0f)
        << "strength field must be reflected in slot 1 defaultFloat";
}

TEST(UZ_AttractorModule, CustomConfig_RadiusReflectedInParameters) {
    AttractorModule m;
    m.radius = 250.0f;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 3u);
    EXPECT_FLOAT_EQ(params[2].defaultFloat, 250.0f)
        << "radius field must be reflected in slot 2 defaultFloat";
}

TEST(UZ_AttractorModule, CustomConfig_FalloffLinearReflectedInParameters) {
    AttractorModule m;
    m.falloff = AttractorFalloff::Linear;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_EQ(params[3].defaultInt, static_cast<int32_t>(AttractorFalloff::Linear))
        << "falloff field Linear must map to integer 1 in slot 3";
}

TEST(UZ_AttractorModule, CustomConfig_FalloffConstantReflectedInParameters) {
    AttractorModule m;
    m.falloff = AttractorFalloff::Constant;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_EQ(params[3].defaultInt, static_cast<int32_t>(AttractorFalloff::Constant))
        << "falloff field Constant must map to integer 0 in slot 3";
}

TEST(UZ_AttractorModule, CustomConfig_KillOnArrivalReflectedInParameters) {
    AttractorModule m;
    m.killOnArrival = true;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 5u);
    EXPECT_TRUE(params[4].defaultBool)
        << "killOnArrival=true must be reflected in slot 4 defaultBool";
}

TEST(UZ_AttractorModule, CustomConfig_PositionReflectedInParameters) {
    AttractorModule m;
    m.attractorPosition = Vec3{100.0f, 200.0f, 300.0f};
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.x, 100.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.y, 200.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.z, 300.0f);
}

// =============================================================================
// Execute -- no-op, must not crash
// =============================================================================

/// Execute is a GPU-only module. The CPU path does nothing observable, but it
/// must complete without throwing, crashing, or modifying the module's fields.

TEST(UZ_AttractorModule, Execute_IsNoOp_DoesNotCrash) {
    AttractorModule m;
    ExecuteContext ctx{};
    ctx.numParticles = 1024;
    ctx.dt = 0.016f;
    ctx.frameNumber = 42;
    // Must not throw or crash.
    m.Execute(ctx);
    // Verify the module's own fields are untouched.
    EXPECT_FLOAT_EQ(m.strength, 10.0f);
    EXPECT_FLOAT_EQ(m.radius, 5.0f);
    EXPECT_EQ(m.falloff, AttractorFalloff::InverseSquare);
}

TEST(UZ_AttractorModule, Execute_WithZeroParticles_DoesNotCrash) {
    AttractorModule m;
    ExecuteContext ctx{};
    ctx.numParticles = 0;
    ctx.dt = 0.0f;
    m.Execute(ctx);
    EXPECT_FALSE(m.killOnArrival); // state unchanged
}

// =============================================================================
// AttractorData -- struct layout (GPU contract)
// =============================================================================

/// The static_assert in the header is the authoritative check, but the test
/// here makes the constraint visible in test output and catches any regression
/// if someone silences the static_assert.

TEST(UZ_AttractorData, StructSize_Is32Bytes) {
    EXPECT_EQ(sizeof(AttractorData), 32u)
        << "AttractorData must be 32 bytes to match HLSL AttractorParams";
}

TEST(UZ_AttractorData, DefaultPositionX_IsZero) {
    AttractorData d;
    EXPECT_FLOAT_EQ(d.positionX, 0.0f);
}

TEST(UZ_AttractorData, DefaultPositionY_IsZero) {
    AttractorData d;
    EXPECT_FLOAT_EQ(d.positionY, 0.0f);
}

TEST(UZ_AttractorData, DefaultPositionZ_IsZero) {
    AttractorData d;
    EXPECT_FLOAT_EQ(d.positionZ, 0.0f);
}

TEST(UZ_AttractorData, DefaultStrength_IsTen) {
    AttractorData d;
    EXPECT_FLOAT_EQ(d.strength, 10.0f);
}

TEST(UZ_AttractorData, DefaultRadius_IsFive) {
    AttractorData d;
    EXPECT_FLOAT_EQ(d.radius, 5.0f);
}

TEST(UZ_AttractorData, DefaultFalloffType_IsOne_Linear) {
    // AttractorData defaults to falloffType=1 (Linear), which differs from
    // AttractorModule::falloff that defaults to InverseSquare. The struct
    // represents the raw GPU layout, not the module's semantic default.
    AttractorData d;
    EXPECT_EQ(d.falloffType, 1u);
}

TEST(UZ_AttractorData, DefaultKillOnArrival_IsZero) {
    AttractorData d;
    EXPECT_EQ(d.killOnArrival, 0u);
}

TEST(UZ_AttractorData, DefaultKillDistance_IsPointZeroFive) {
    AttractorData d;
    EXPECT_FLOAT_EQ(d.killDistance, 0.05f);
}

// =============================================================================
// kMaxAttractors constant
// =============================================================================

TEST(UZ_AttractorBuffer, kMaxAttractors_IsEight) {
    EXPECT_EQ(kMaxAttractors, 8u);
}

// =============================================================================
// AttractorBuffer -- IsValid before Create
// =============================================================================

/// The buffer must report invalid before any GPU allocation is made so that
/// callers can guard on IsValid() before calling UploadToGPU().

TEST(UZ_AttractorBuffer, DefaultConstruct_IsNotValid) {
    AttractorBuffer buf;
    EXPECT_FALSE(buf.IsValid())
        << "AttractorBuffer must not report IsValid() before Create()";
}

TEST(UZ_AttractorBuffer, DefaultConstruct_ActiveCount_IsZero) {
    AttractorBuffer buf;
    EXPECT_EQ(buf.GetActiveCount(), 0u);
}

// =============================================================================
// AttractorBuffer::AddAttractor -- count tracking
// =============================================================================

/// AddAttractor must accept up to kMaxAttractors entries and reject the next.

TEST(UZ_AttractorBuffer, AddAttractor_SingleEntry_CountBecomesOne) {
    AttractorBuffer buf;
    AttractorData d;
    d.strength = 5.0f;
    const bool added = buf.AddAttractor(d);
    EXPECT_TRUE(added) << "First AddAttractor must succeed";
    EXPECT_EQ(buf.GetActiveCount(), 1u);
}

TEST(UZ_AttractorBuffer, AddAttractor_MultipleEntries_CountAccumulates) {
    AttractorBuffer buf;
    AttractorData d;
    for (uint32_t i = 0; i < 4; ++i) {
        d.strength = static_cast<float>(i);
        ASSERT_TRUE(buf.AddAttractor(d)) << "AddAttractor must succeed for slot " << i;
    }
    EXPECT_EQ(buf.GetActiveCount(), 4u);
}

TEST(UZ_AttractorBuffer, AddAttractor_AtCapacity_ReturnsFalse) {
    AttractorBuffer buf;
    AttractorData d;
    // Fill to capacity.
    for (uint32_t i = 0; i < kMaxAttractors; ++i) {
        ASSERT_TRUE(buf.AddAttractor(d)) << "AddAttractor must succeed for slot " << i;
    }
    EXPECT_EQ(buf.GetActiveCount(), kMaxAttractors);
    // One more must be rejected.
    const bool overflow = buf.AddAttractor(d);
    EXPECT_FALSE(overflow) << "AddAttractor beyond capacity must return false";
    // Count must remain at kMaxAttractors.
    EXPECT_EQ(buf.GetActiveCount(), kMaxAttractors);
}

TEST(UZ_AttractorBuffer, AddAttractor_StoresDataCorrectly) {
    AttractorBuffer buf;
    AttractorData d;
    d.positionX    = 10.0f;
    d.positionY    = 20.0f;
    d.positionZ    = 30.0f;
    d.strength     = 99.0f;
    d.radius       = 55.0f;
    d.falloffType  = 2u;
    d.killOnArrival = 1u;
    d.killDistance  = 0.1f;

    ASSERT_TRUE(buf.AddAttractor(d));
    EXPECT_EQ(buf.GetActiveCount(), 1u);
    // Verify stored data via direct array access.
    EXPECT_FLOAT_EQ(buf.attractors[0].positionX, 10.0f);
    EXPECT_FLOAT_EQ(buf.attractors[0].positionY, 20.0f);
    EXPECT_FLOAT_EQ(buf.attractors[0].positionZ, 30.0f);
    EXPECT_FLOAT_EQ(buf.attractors[0].strength, 99.0f);
    EXPECT_FLOAT_EQ(buf.attractors[0].radius, 55.0f);
    EXPECT_EQ(buf.attractors[0].falloffType, 2u);
    EXPECT_EQ(buf.attractors[0].killOnArrival, 1u);
    EXPECT_FLOAT_EQ(buf.attractors[0].killDistance, 0.1f);
}

// =============================================================================
// AttractorBuffer::Clear -- resets count, leaves GPU buffer intact
// =============================================================================

TEST(UZ_AttractorBuffer, Clear_AfterAddAttractors_CountBecomesZero) {
    AttractorBuffer buf;
    AttractorData d;
    buf.AddAttractor(d);
    buf.AddAttractor(d);
    ASSERT_EQ(buf.GetActiveCount(), 2u);

    buf.Clear();

    EXPECT_EQ(buf.GetActiveCount(), 0u)
        << "Clear() must reset activeCount to zero";
}

TEST(UZ_AttractorBuffer, Clear_AfterClear_CanAddAttractorsAgain) {
    AttractorBuffer buf;
    AttractorData d;
    // Fill to capacity.
    for (uint32_t i = 0; i < kMaxAttractors; ++i) {
        buf.AddAttractor(d);
    }
    ASSERT_EQ(buf.GetActiveCount(), kMaxAttractors);

    // Clear and re-fill -- must succeed without error.
    buf.Clear();
    EXPECT_EQ(buf.GetActiveCount(), 0u);

    const bool added = buf.AddAttractor(d);
    EXPECT_TRUE(added) << "AddAttractor after Clear must succeed";
    EXPECT_EQ(buf.GetActiveCount(), 1u);
}

TEST(UZ_AttractorBuffer, Clear_IsNoOp_OnEmptyBuffer) {
    AttractorBuffer buf;
    // Must not crash on an empty (never-added) buffer.
    buf.Clear();
    EXPECT_EQ(buf.GetActiveCount(), 0u);
}

// =============================================================================
// AttractorBuffer::Destroy with null device -- must be a no-op
// =============================================================================

/// Destroy(nullptr) is documented as a no-op. It must not crash even if the
/// buffer was never allocated.

TEST(UZ_AttractorBuffer, Destroy_NullDevice_IsNoOp) {
    AttractorBuffer buf;
    // Must not crash: buffer is invalid, device is null.
    buf.Destroy(nullptr);
    EXPECT_FALSE(buf.IsValid());
    EXPECT_EQ(buf.GetActiveCount(), 0u);
}

TEST(UZ_AttractorBuffer, Destroy_NullDevice_AfterAddAttractors_IsNoOp_CountUnchanged) {
    AttractorBuffer buf;
    AttractorData d;
    buf.AddAttractor(d);
    buf.AddAttractor(d);
    ASSERT_EQ(buf.GetActiveCount(), 2u);

    // Destroy(nullptr) returns immediately without resetting state (see impl line 58).
    // The count is NOT cleared -- callers must pass a valid device for a real teardown.
    buf.Destroy(nullptr);
    EXPECT_EQ(buf.GetActiveCount(), 2u)
        << "Destroy(nullptr) must be a complete no-op; activeCount must remain 2";
}
