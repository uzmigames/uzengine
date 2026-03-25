/// Unit tests for VectorFieldModule.
///
/// Covers: default config values (fieldTexture invalid, intensity, tightness,
/// bounds, fieldOffset, timeScale), GetName, GetShaderCode (non-empty, required
/// HLSL identifiers), GetParameters (count, ordering, types, defaults),
/// field_texture defaultInt when fieldTexture is Invalid, Execute no-op
/// safety, custom config reflected through GetParameters, animated field
/// HLSL tokens (cbVectorFieldTimeScale, frac), tightness blend HLSL tokens
/// (lerp, saturate), and bounds safety HLSL guard (max(halfExtents).
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "modules/vector_field_module.h"

#include <cstdint>
#include <string_view>

using namespace uz;

// =============================================================================
// Default field values
// =============================================================================

TEST(UZ_VectorFieldModule, DefaultField_FieldTexture_IsInvalid) {
    VectorFieldModule m;
    EXPECT_FALSE(m.fieldTexture.IsValid())
        << "fieldTexture must default to Invalid (generation == 0)";
}

TEST(UZ_VectorFieldModule, DefaultField_Intensity_IsOne) {
    VectorFieldModule m;
    EXPECT_FLOAT_EQ(m.intensity, 1.0f)
        << "intensity must default to 1.0f";
}

TEST(UZ_VectorFieldModule, DefaultField_Tightness_IsZero) {
    VectorFieldModule m;
    EXPECT_FLOAT_EQ(m.tightness, 0.0f)
        << "tightness must default to 0.0f (purely additive mode)";
}

TEST(UZ_VectorFieldModule, DefaultField_Bounds_Is10_10_10) {
    VectorFieldModule m;
    EXPECT_FLOAT_EQ(m.bounds.x, 10.0f);
    EXPECT_FLOAT_EQ(m.bounds.y, 10.0f);
    EXPECT_FLOAT_EQ(m.bounds.z, 10.0f);
}

TEST(UZ_VectorFieldModule, DefaultField_FieldOffset_IsZero) {
    VectorFieldModule m;
    EXPECT_FLOAT_EQ(m.fieldOffset.x, 0.0f);
    EXPECT_FLOAT_EQ(m.fieldOffset.y, 0.0f);
    EXPECT_FLOAT_EQ(m.fieldOffset.z, 0.0f);
}

TEST(UZ_VectorFieldModule, DefaultField_TimeScale_IsZero) {
    VectorFieldModule m;
    EXPECT_FLOAT_EQ(m.timeScale, 0.0f)
        << "timeScale must default to 0.0f (static field, no UVW scroll)";
}

// =============================================================================
// GetName
// =============================================================================

TEST(UZ_VectorFieldModule, GetName_ReturnsVectorField) {
    VectorFieldModule m;
    EXPECT_STREQ(m.GetName(), "vector_field");
}

TEST(UZ_VectorFieldModule, GetName_IsNotNull) {
    VectorFieldModule m;
    EXPECT_NE(m.GetName(), nullptr);
}

// =============================================================================
// GetShaderCode — non-empty HLSL with required identifiers
// =============================================================================

TEST(UZ_VectorFieldModule, GetShaderCode_IsNonEmpty) {
    VectorFieldModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_FALSE(code.empty())
        << "VectorFieldModule must provide a non-empty HLSL snippet";
}

TEST(UZ_VectorFieldModule, GetShaderCode_ContainsIntensityConstant) {
    VectorFieldModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbVectorFieldIntensity"), std::string_view::npos)
        << "HLSL snippet must reference cbVectorFieldIntensity";
}

TEST(UZ_VectorFieldModule, GetShaderCode_ContainsUvwIdentifier) {
    VectorFieldModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("uvw"), std::string_view::npos)
        << "HLSL snippet must contain UVW coordinate variable";
}

TEST(UZ_VectorFieldModule, GetShaderCode_ContainsTimeScaleConstant) {
    VectorFieldModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbVectorFieldTimeScale"), std::string_view::npos)
        << "HLSL snippet must reference cbVectorFieldTimeScale for animated fields";
}

TEST(UZ_VectorFieldModule, GetShaderCode_ContainsFracForAnimation) {
    VectorFieldModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("frac"), std::string_view::npos)
        << "HLSL snippet must use frac() for UVW wrapping in animated fields";
}

TEST(UZ_VectorFieldModule, GetShaderCode_ContainsLerpForTightnessBlend) {
    VectorFieldModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("lerp"), std::string_view::npos)
        << "HLSL snippet must use lerp() for tightness velocity blend";
}

TEST(UZ_VectorFieldModule, GetShaderCode_ContainsSaturateForBlendFactor) {
    VectorFieldModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("saturate"), std::string_view::npos)
        << "HLSL snippet must use saturate() to clamp the tightness blend factor";
}

TEST(UZ_VectorFieldModule, GetShaderCode_ContainsBoundsSafetyGuard) {
    VectorFieldModule m;
    const std::string_view code = m.GetShaderCode();
    // Bounds safety: half-extents divided by max(..., epsilon) to prevent divide-by-zero.
    EXPECT_NE(code.find("max(halfExtents"), std::string_view::npos)
        << "HLSL snippet must guard against zero half-extents via max(halfExtents, ...)";
}

TEST(UZ_VectorFieldModule, GetShaderCode_ContainsTightnessConstant) {
    VectorFieldModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbVectorFieldTightness"), std::string_view::npos)
        << "HLSL snippet must reference cbVectorFieldTightness";
}

TEST(UZ_VectorFieldModule, GetShaderCode_ContainsSrvIndexConstant) {
    VectorFieldModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbVectorFieldSrvIndex"), std::string_view::npos)
        << "HLSL snippet must reference cbVectorFieldSrvIndex for bindless texture";
}

// =============================================================================
// GetParameters — count, ordering, types, defaults
// =============================================================================

TEST(UZ_VectorFieldModule, GetParameters_ReturnsSixEntries) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    EXPECT_EQ(params.size(), 6u)
        << "VectorFieldModule must expose exactly 6 parameters";
}

TEST(UZ_VectorFieldModule, GetParameters_Slot0_IsFieldTexture) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_STREQ(params[0].name, "field_texture");
}

TEST(UZ_VectorFieldModule, GetParameters_Slot0_TypeIsInt) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_EQ(params[0].type, ParameterType::Int)
        << "field_texture must be ParameterType::Int (bindless SRV index)";
}

TEST(UZ_VectorFieldModule, GetParameters_Slot1_IsIntensity) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_STREQ(params[1].name, "intensity");
}

TEST(UZ_VectorFieldModule, GetParameters_Slot1_TypeIsFloat) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_EQ(params[1].type, ParameterType::Float);
}

TEST(UZ_VectorFieldModule, GetParameters_Slot1_DefaultIntensityIsOne) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_FLOAT_EQ(params[1].defaultFloat, 1.0f)
        << "intensity default must be 1.0f";
}

TEST(UZ_VectorFieldModule, GetParameters_Slot2_IsTightness) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 3u);
    EXPECT_STREQ(params[2].name, "tightness");
}

TEST(UZ_VectorFieldModule, GetParameters_Slot2_TypeIsFloat) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 3u);
    EXPECT_EQ(params[2].type, ParameterType::Float);
}

TEST(UZ_VectorFieldModule, GetParameters_Slot2_DefaultTightnessIsZero) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 3u);
    EXPECT_FLOAT_EQ(params[2].defaultFloat, 0.0f)
        << "tightness default must be 0.0f (additive mode)";
}

TEST(UZ_VectorFieldModule, GetParameters_Slot3_IsBounds) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_STREQ(params[3].name, "bounds");
}

TEST(UZ_VectorFieldModule, GetParameters_Slot3_TypeIsVec3) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_EQ(params[3].type, ParameterType::Vec3);
}

TEST(UZ_VectorFieldModule, GetParameters_Slot3_DefaultBoundsIs10_10_10) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_FLOAT_EQ(params[3].defaultVec3.x, 10.0f);
    EXPECT_FLOAT_EQ(params[3].defaultVec3.y, 10.0f);
    EXPECT_FLOAT_EQ(params[3].defaultVec3.z, 10.0f);
}

TEST(UZ_VectorFieldModule, GetParameters_Slot4_IsFieldOffset) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 5u);
    EXPECT_STREQ(params[4].name, "field_offset");
}

TEST(UZ_VectorFieldModule, GetParameters_Slot4_TypeIsVec3) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 5u);
    EXPECT_EQ(params[4].type, ParameterType::Vec3);
}

TEST(UZ_VectorFieldModule, GetParameters_Slot4_DefaultFieldOffsetIsZero) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 5u);
    EXPECT_FLOAT_EQ(params[4].defaultVec3.x, 0.0f);
    EXPECT_FLOAT_EQ(params[4].defaultVec3.y, 0.0f);
    EXPECT_FLOAT_EQ(params[4].defaultVec3.z, 0.0f);
}

TEST(UZ_VectorFieldModule, GetParameters_Slot5_IsTimeScale) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 6u);
    EXPECT_STREQ(params[5].name, "time_scale");
}

TEST(UZ_VectorFieldModule, GetParameters_Slot5_TypeIsFloat) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 6u);
    EXPECT_EQ(params[5].type, ParameterType::Float);
}

TEST(UZ_VectorFieldModule, GetParameters_Slot5_DefaultTimeScaleIsZero) {
    VectorFieldModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 6u);
    EXPECT_FLOAT_EQ(params[5].defaultFloat, 0.0f)
        << "time_scale default must be 0.0f (static field)";
}

// =============================================================================
// field_texture defaultInt — Invalid handle maps to static_cast<int32_t>(~uint32_t{0})
// =============================================================================

TEST(UZ_VectorFieldModule, GetParameters_FieldTextureInvalid_DefaultIntIsUINT32_MAX_Cast) {
    VectorFieldModule m;
    // fieldTexture is Invalid() by default (generation == 0, IsValid() == false).
    ASSERT_FALSE(m.fieldTexture.IsValid());

    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);

    // Implementation sets: defaultInt = static_cast<int32_t>(~uint32_t{0})
    // which equals -1 in two's-complement, i.e. the bit pattern 0xFFFFFFFF.
    const int32_t expectedSentinel = static_cast<int32_t>(~uint32_t{0});
    EXPECT_EQ(params[0].defaultInt, expectedSentinel)
        << "When fieldTexture is Invalid, defaultInt must be "
           "static_cast<int32_t>(~uint32_t{0}) == "
        << expectedSentinel;
}

// =============================================================================
// Execute — no-op: must not crash and must not mutate observable CPU state
// =============================================================================

TEST(UZ_VectorFieldModule, Execute_IsNoOp_DoesNotCrash) {
    VectorFieldModule m;
    ExecuteContext ctx{};
    ctx.numParticles = 256;
    ctx.dt           = 0.016f;
    ctx.frameNumber  = 42u;
    // Must complete without throwing or crashing.
    m.Execute(ctx);
    // Observable CPU state (public fields) must be unchanged.
    EXPECT_FLOAT_EQ(m.intensity, 1.0f);
    EXPECT_FLOAT_EQ(m.tightness, 0.0f);
}

TEST(UZ_VectorFieldModule, Execute_ZeroParticles_DoesNotCrash) {
    VectorFieldModule m;
    ExecuteContext ctx{};
    ctx.numParticles = 0;
    ctx.dt           = 0.0f;
    m.Execute(ctx);
    EXPECT_FLOAT_EQ(m.timeScale, 0.0f);
}

// =============================================================================
// Custom config — mutating fields is reflected in GetParameters defaults
// =============================================================================

TEST(UZ_VectorFieldModule, CustomIntensity_ReflectedInGetParameters) {
    VectorFieldModule m;
    m.intensity = 3.5f;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_FLOAT_EQ(params[1].defaultFloat, 3.5f)
        << "GetParameters must reflect the current intensity value";
}

TEST(UZ_VectorFieldModule, CustomTightness_ReflectedInGetParameters) {
    VectorFieldModule m;
    m.tightness = 0.75f;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 3u);
    EXPECT_FLOAT_EQ(params[2].defaultFloat, 0.75f)
        << "GetParameters must reflect the current tightness value";
}

TEST(UZ_VectorFieldModule, CustomBounds_ReflectedInGetParameters) {
    VectorFieldModule m;
    m.bounds = Vec3{50.0f, 100.0f, 25.0f};
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_FLOAT_EQ(params[3].defaultVec3.x, 50.0f);
    EXPECT_FLOAT_EQ(params[3].defaultVec3.y, 100.0f);
    EXPECT_FLOAT_EQ(params[3].defaultVec3.z, 25.0f);
}

TEST(UZ_VectorFieldModule, CustomFieldOffset_ReflectedInGetParameters) {
    VectorFieldModule m;
    m.fieldOffset = Vec3{-5.0f, 20.0f, 3.0f};
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 5u);
    EXPECT_FLOAT_EQ(params[4].defaultVec3.x, -5.0f);
    EXPECT_FLOAT_EQ(params[4].defaultVec3.y, 20.0f);
    EXPECT_FLOAT_EQ(params[4].defaultVec3.z,  3.0f);
}

TEST(UZ_VectorFieldModule, CustomTimeScale_ReflectedInGetParameters) {
    VectorFieldModule m;
    m.timeScale = 0.5f;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 6u);
    EXPECT_FLOAT_EQ(params[5].defaultFloat, 0.5f)
        << "GetParameters must reflect the current timeScale value";
}

TEST(UZ_VectorFieldModule, ValidFieldTextureHandle_ReflectedAsPositiveDefaultInt) {
    VectorFieldModule m;
    // Simulate a valid handle (generation != 0, arbitrary index).
    m.fieldTexture.index      = 7u;
    m.fieldTexture.generation = 1u;
    ASSERT_TRUE(m.fieldTexture.IsValid());

    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    // When valid, defaultInt == static_cast<int32_t>(fieldTexture.index) == 7.
    EXPECT_EQ(params[0].defaultInt, static_cast<int32_t>(7u))
        << "When fieldTexture is valid, defaultInt must equal its SRV index";
}
