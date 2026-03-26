/// Unit tests for OrbitModule, PinModule, and DistanceConstraintModule.
///
/// Covers per-module: default field values, GetName(), GetShaderCode() content
/// invariants, GetParameters() count and per-entry name/type/default, Execute()
/// no-op safety, and custom config round-trip.
///
/// No GPU hardware required -- all tests are CPU-only.

#include <gtest/gtest.h>

#include "modules/orbit_module.h"
#include "modules/pin_module.h"
#include "modules/distance_constraint_module.h"

#include <string_view>

using namespace uz;

// =============================================================================
// OrbitModule
// =============================================================================

// -- Default field values -----------------------------------------------------

TEST(UZ_OrbitModule, DefaultCenter_IsOrigin) {
    OrbitModule m;
    EXPECT_FLOAT_EQ(m.center.x, 0.0f);
    EXPECT_FLOAT_EQ(m.center.y, 0.0f);
    EXPECT_FLOAT_EQ(m.center.z, 0.0f);
}

TEST(UZ_OrbitModule, DefaultSpeed_IsOne) {
    OrbitModule m;
    EXPECT_FLOAT_EQ(m.speed, 1.0f);
}

TEST(UZ_OrbitModule, DefaultOrbitRadius_IsFive) {
    OrbitModule m;
    EXPECT_FLOAT_EQ(m.orbitRadius, 5.0f);
}

TEST(UZ_OrbitModule, DefaultAxis_IsZUp) {
    OrbitModule m;
    EXPECT_FLOAT_EQ(m.axis.x, 0.0f);
    EXPECT_FLOAT_EQ(m.axis.y, 0.0f);
    EXPECT_FLOAT_EQ(m.axis.z, 1.0f);
}

// -- GetName ------------------------------------------------------------------

TEST(UZ_OrbitModule, GetName_ReturnsOrbit) {
    OrbitModule m;
    EXPECT_STREQ(m.GetName(), "orbit");
}

// -- GetShaderCode ------------------------------------------------------------

TEST(UZ_OrbitModule, GetShaderCode_IsNonEmpty) {
    OrbitModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_FALSE(code.empty())
        << "OrbitModule must provide a non-empty HLSL snippet";
}

TEST(UZ_OrbitModule, GetShaderCode_ContainsCrossForTangent) {
    OrbitModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cross"), std::string_view::npos)
        << "HLSL snippet must use cross() for tangential direction computation";
}

TEST(UZ_OrbitModule, GetShaderCode_ContainsDivByZeroGuard) {
    OrbitModule m;
    const std::string_view code = m.GetShaderCode();
    // The guard is `if (planeLen > 1e-6f)` -- verify the threshold literal is present.
    EXPECT_NE(code.find("1e-6"), std::string_view::npos)
        << "HLSL snippet must guard against near-zero lengths using the 1e-6 threshold";
}

TEST(UZ_OrbitModule, GetShaderCode_ContainsOrbitCenterBinding) {
    OrbitModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbOrbitCenter"), std::string_view::npos)
        << "HLSL snippet must reference cbOrbitCenter constant buffer binding";
}

TEST(UZ_OrbitModule, GetShaderCode_ContainsOrbitAxisBinding) {
    OrbitModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbOrbitAxis"), std::string_view::npos)
        << "HLSL snippet must reference cbOrbitAxis constant buffer binding";
}

// -- GetParameters ------------------------------------------------------------

TEST(UZ_OrbitModule, GetParameters_ReturnsFourEntries) {
    OrbitModule m;
    const auto params = m.GetParameters();
    EXPECT_EQ(params.size(), 4u)
        << "OrbitModule must expose exactly 4 parameters: center, speed, orbit_radius, axis";
}

TEST(UZ_OrbitModule, GetParameters_FirstEntry_IsCenterVec3) {
    OrbitModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_STREQ(params[0].name, "center");
    EXPECT_EQ(params[0].type, ParameterType::Vec3);
}

TEST(UZ_OrbitModule, GetParameters_FirstEntry_CenterDefault_IsOrigin) {
    OrbitModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.x, 0.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.y, 0.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.z, 0.0f);
}

TEST(UZ_OrbitModule, GetParameters_SecondEntry_IsSpeedFloat) {
    OrbitModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_STREQ(params[1].name, "speed");
    EXPECT_EQ(params[1].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[1].defaultFloat, 1.0f);
}

TEST(UZ_OrbitModule, GetParameters_ThirdEntry_IsOrbitRadiusFloat) {
    OrbitModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 3u);
    EXPECT_STREQ(params[2].name, "orbit_radius");
    EXPECT_EQ(params[2].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[2].defaultFloat, 5.0f);
}

TEST(UZ_OrbitModule, GetParameters_FourthEntry_IsAxisVec3) {
    OrbitModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_STREQ(params[3].name, "axis");
    EXPECT_EQ(params[3].type, ParameterType::Vec3);
}

TEST(UZ_OrbitModule, GetParameters_FourthEntry_AxisDefault_IsZUp) {
    OrbitModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_FLOAT_EQ(params[3].defaultVec3.x, 0.0f);
    EXPECT_FLOAT_EQ(params[3].defaultVec3.y, 0.0f);
    EXPECT_FLOAT_EQ(params[3].defaultVec3.z, 1.0f);
}

// -- Execute ------------------------------------------------------------------

TEST(UZ_OrbitModule, Execute_IsNoOp_DoesNotCrash) {
    OrbitModule m;
    ExecuteContext ctx{};
    ctx.numParticles = 512;
    ctx.dt = 0.016f;
    // Must complete without crashing -- all work is GPU-side.
    m.Execute(ctx);
    // Field values must be unchanged after execute.
    EXPECT_FLOAT_EQ(m.speed, 1.0f);
    EXPECT_FLOAT_EQ(m.orbitRadius, 5.0f);
}

// -- Custom config round-trip -------------------------------------------------

TEST(UZ_OrbitModule, CustomConfig_FieldsStoredCorrectly) {
    OrbitModule m;
    m.center      = {10.0f, 20.0f, 30.0f};
    m.speed       = 3.5f;
    m.orbitRadius = 12.0f;
    m.axis        = {0.0f, 1.0f, 0.0f};

    EXPECT_FLOAT_EQ(m.center.x,      10.0f);
    EXPECT_FLOAT_EQ(m.center.y,      20.0f);
    EXPECT_FLOAT_EQ(m.center.z,      30.0f);
    EXPECT_FLOAT_EQ(m.speed,          3.5f);
    EXPECT_FLOAT_EQ(m.orbitRadius,   12.0f);
    EXPECT_FLOAT_EQ(m.axis.y,         1.0f);
}

TEST(UZ_OrbitModule, CustomConfig_ReflectedInGetParameters) {
    OrbitModule m;
    m.center      = {1.0f, 2.0f, 3.0f};
    m.speed       = 2.0f;
    m.orbitRadius = 8.0f;
    m.axis        = {1.0f, 0.0f, 0.0f};

    const auto params = m.GetParameters();
    ASSERT_EQ(params.size(), 4u);

    // center
    EXPECT_FLOAT_EQ(params[0].defaultVec3.x, 1.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.y, 2.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.z, 3.0f);
    // speed
    EXPECT_FLOAT_EQ(params[1].defaultFloat, 2.0f);
    // orbit_radius
    EXPECT_FLOAT_EQ(params[2].defaultFloat, 8.0f);
    // axis
    EXPECT_FLOAT_EQ(params[3].defaultVec3.x, 1.0f);
    EXPECT_FLOAT_EQ(params[3].defaultVec3.y, 0.0f);
    EXPECT_FLOAT_EQ(params[3].defaultVec3.z, 0.0f);
}

// =============================================================================
// PinModule
// =============================================================================

// -- Default field values -----------------------------------------------------

TEST(UZ_PinModule, DefaultPinPosition_IsOrigin) {
    PinModule m;
    EXPECT_FLOAT_EQ(m.pinPosition.x, 0.0f);
    EXPECT_FLOAT_EQ(m.pinPosition.y, 0.0f);
    EXPECT_FLOAT_EQ(m.pinPosition.z, 0.0f);
}

TEST(UZ_PinModule, DefaultSpringStrength_IsFive) {
    // Implementation default is 5.0 (Hooke's law spring constant k).
    PinModule m;
    EXPECT_FLOAT_EQ(m.springStrength, 5.0f);
}

// -- GetName ------------------------------------------------------------------

TEST(UZ_PinModule, GetName_ReturnsPin) {
    PinModule m;
    EXPECT_STREQ(m.GetName(), "pin");
}

// -- GetShaderCode ------------------------------------------------------------

TEST(UZ_PinModule, GetShaderCode_IsNonEmpty) {
    PinModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_FALSE(code.empty())
        << "PinModule must provide a non-empty HLSL snippet";
}

TEST(UZ_PinModule, GetShaderCode_ContainsHookesLawKeyword) {
    PinModule m;
    const std::string_view code = m.GetShaderCode();
    // The snippet header documents Hooke's law as "F = k * x".
    EXPECT_NE(code.find("Hooke"), std::string_view::npos)
        << "HLSL snippet must reference Hooke's law for spring force";
}

TEST(UZ_PinModule, GetShaderCode_ContainsPinPositionBinding) {
    PinModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbPinPosition"), std::string_view::npos)
        << "HLSL snippet must reference cbPinPosition constant buffer binding";
}

TEST(UZ_PinModule, GetShaderCode_ContainsSpringStrengthBinding) {
    PinModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbPinSpringStrength"), std::string_view::npos)
        << "HLSL snippet must reference cbPinSpringStrength constant buffer binding";
}

TEST(UZ_PinModule, GetShaderCode_ContainsNearZeroDistanceGuard) {
    PinModule m;
    const std::string_view code = m.GetShaderCode();
    // Particles within 0.001 world units are skipped to avoid divide-by-zero.
    EXPECT_NE(code.find("0.001"), std::string_view::npos)
        << "HLSL snippet must guard against near-zero particle-to-pin distance";
}

// -- GetParameters ------------------------------------------------------------

TEST(UZ_PinModule, GetParameters_ReturnsTwoEntries) {
    PinModule m;
    const auto params = m.GetParameters();
    EXPECT_EQ(params.size(), 2u)
        << "PinModule must expose exactly 2 parameters: pin_position, spring_strength";
}

TEST(UZ_PinModule, GetParameters_FirstEntry_IsPinPositionVec3) {
    PinModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_STREQ(params[0].name, "pin_position");
    EXPECT_EQ(params[0].type, ParameterType::Vec3);
}

TEST(UZ_PinModule, GetParameters_FirstEntry_PinPositionDefault_IsOrigin) {
    PinModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.x, 0.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.y, 0.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.z, 0.0f);
}

TEST(UZ_PinModule, GetParameters_SecondEntry_IsSpringStrengthFloat) {
    PinModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_STREQ(params[1].name, "spring_strength");
    EXPECT_EQ(params[1].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[1].defaultFloat, 5.0f);
}

// -- Execute ------------------------------------------------------------------

TEST(UZ_PinModule, Execute_IsNoOp_DoesNotCrash) {
    PinModule m;
    ExecuteContext ctx{};
    ctx.numParticles = 256;
    ctx.dt = 0.016f;
    // Must complete without crashing.
    m.Execute(ctx);
    // Field values must be unchanged after execute.
    EXPECT_FLOAT_EQ(m.springStrength, 5.0f);
}

// -- Custom config round-trip -------------------------------------------------

TEST(UZ_PinModule, CustomConfig_FieldsStoredCorrectly) {
    PinModule m;
    m.pinPosition    = {5.0f, 10.0f, -3.0f};
    m.springStrength = 20.0f;

    EXPECT_FLOAT_EQ(m.pinPosition.x,   5.0f);
    EXPECT_FLOAT_EQ(m.pinPosition.y,  10.0f);
    EXPECT_FLOAT_EQ(m.pinPosition.z,  -3.0f);
    EXPECT_FLOAT_EQ(m.springStrength, 20.0f);
}

TEST(UZ_PinModule, CustomConfig_ReflectedInGetParameters) {
    PinModule m;
    m.pinPosition    = {4.0f, 5.0f, 6.0f};
    m.springStrength = 15.0f;

    const auto params = m.GetParameters();
    ASSERT_EQ(params.size(), 2u);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.x,  4.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.y,  5.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.z,  6.0f);
    EXPECT_FLOAT_EQ(params[1].defaultFloat,   15.0f);
}

// =============================================================================
// DistanceConstraintModule
// =============================================================================

// -- Default field values -----------------------------------------------------

TEST(UZ_DistanceConstraintModule, DefaultConstraintOrigin_IsOrigin) {
    DistanceConstraintModule m;
    EXPECT_FLOAT_EQ(m.constraintOrigin.x, 0.0f);
    EXPECT_FLOAT_EQ(m.constraintOrigin.y, 0.0f);
    EXPECT_FLOAT_EQ(m.constraintOrigin.z, 0.0f);
}

TEST(UZ_DistanceConstraintModule, DefaultMinDistance_IsZero) {
    DistanceConstraintModule m;
    EXPECT_FLOAT_EQ(m.minDistance, 0.0f);
}

TEST(UZ_DistanceConstraintModule, DefaultMaxDistance_IsFifty) {
    // Header declares float maxDistance = 50.0f.
    DistanceConstraintModule m;
    EXPECT_FLOAT_EQ(m.maxDistance, 50.0f);
}

TEST(UZ_DistanceConstraintModule, DefaultConstraintStrength_IsTen) {
    DistanceConstraintModule m;
    EXPECT_FLOAT_EQ(m.constraintStrength, 10.0f);
}

// -- GetName ------------------------------------------------------------------

TEST(UZ_DistanceConstraintModule, GetName_ReturnsDistanceConstraint) {
    DistanceConstraintModule m;
    EXPECT_STREQ(m.GetName(), "distance_constraint");
}

// -- GetShaderCode ------------------------------------------------------------

TEST(UZ_DistanceConstraintModule, GetShaderCode_IsNonEmpty) {
    DistanceConstraintModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_FALSE(code.empty())
        << "DistanceConstraintModule must provide a non-empty HLSL snippet";
}

TEST(UZ_DistanceConstraintModule, GetShaderCode_ContainsMinDistanceBinding) {
    DistanceConstraintModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbMinDistance"), std::string_view::npos)
        << "HLSL snippet must reference cbMinDistance constant buffer binding";
}

TEST(UZ_DistanceConstraintModule, GetShaderCode_ContainsMaxDistanceBinding) {
    DistanceConstraintModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbMaxDistance"), std::string_view::npos)
        << "HLSL snippet must reference cbMaxDistance constant buffer binding";
}

TEST(UZ_DistanceConstraintModule, GetShaderCode_ContainsPushOutwardLogic) {
    DistanceConstraintModule m;
    const std::string_view code = m.GetShaderCode();
    // The push branch uses variable `pushForce`.
    EXPECT_NE(code.find("pushForce"), std::string_view::npos)
        << "HLSL snippet must contain pushForce variable for inner boundary correction";
}

TEST(UZ_DistanceConstraintModule, GetShaderCode_ContainsPullInwardLogic) {
    DistanceConstraintModule m;
    const std::string_view code = m.GetShaderCode();
    // The pull branch uses variable `pullForce`.
    EXPECT_NE(code.find("pullForce"), std::string_view::npos)
        << "HLSL snippet must contain pullForce variable for outer boundary correction";
}

TEST(UZ_DistanceConstraintModule, GetShaderCode_ContainsConstraintOriginBinding) {
    DistanceConstraintModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbConstraintOrigin"), std::string_view::npos)
        << "HLSL snippet must reference cbConstraintOrigin constant buffer binding";
}

TEST(UZ_DistanceConstraintModule, GetShaderCode_ContainsNearZeroDistanceGuard) {
    DistanceConstraintModule m;
    const std::string_view code = m.GetShaderCode();
    // The guard skips particles within 0.001 world units of the origin.
    EXPECT_NE(code.find("0.001"), std::string_view::npos)
        << "HLSL snippet must guard against near-zero distance to avoid normalizing zero vector";
}

// -- GetParameters ------------------------------------------------------------

TEST(UZ_DistanceConstraintModule, GetParameters_ReturnsFourEntries) {
    DistanceConstraintModule m;
    const auto params = m.GetParameters();
    EXPECT_EQ(params.size(), 4u)
        << "DistanceConstraintModule must expose exactly 4 parameters: "
           "constraint_origin, min_distance, max_distance, constraint_strength";
}

TEST(UZ_DistanceConstraintModule, GetParameters_FirstEntry_IsConstraintOriginVec3) {
    DistanceConstraintModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_STREQ(params[0].name, "constraint_origin");
    EXPECT_EQ(params[0].type, ParameterType::Vec3);
}

TEST(UZ_DistanceConstraintModule, GetParameters_FirstEntry_ConstraintOriginDefault_IsOrigin) {
    DistanceConstraintModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.x, 0.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.y, 0.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.z, 0.0f);
}

TEST(UZ_DistanceConstraintModule, GetParameters_SecondEntry_IsMinDistanceFloat) {
    DistanceConstraintModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_STREQ(params[1].name, "min_distance");
    EXPECT_EQ(params[1].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[1].defaultFloat, 0.0f);
}

TEST(UZ_DistanceConstraintModule, GetParameters_ThirdEntry_IsMaxDistanceFloat) {
    DistanceConstraintModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 3u);
    EXPECT_STREQ(params[2].name, "max_distance");
    EXPECT_EQ(params[2].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[2].defaultFloat, 50.0f);
}

TEST(UZ_DistanceConstraintModule, GetParameters_FourthEntry_IsConstraintStrengthFloat) {
    DistanceConstraintModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_STREQ(params[3].name, "constraint_strength");
    EXPECT_EQ(params[3].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[3].defaultFloat, 10.0f);
}

// -- Execute ------------------------------------------------------------------

TEST(UZ_DistanceConstraintModule, Execute_IsNoOp_DoesNotCrash) {
    DistanceConstraintModule m;
    ExecuteContext ctx{};
    ctx.numParticles = 1024;
    ctx.dt = 0.016f;
    // Must complete without crashing -- all work is GPU-side.
    m.Execute(ctx);
    // Field values must be unchanged after execute.
    EXPECT_FLOAT_EQ(m.minDistance,        0.0f);
    EXPECT_FLOAT_EQ(m.maxDistance,       50.0f);
    EXPECT_FLOAT_EQ(m.constraintStrength, 10.0f);
}

// -- Custom config round-trip -------------------------------------------------

TEST(UZ_DistanceConstraintModule, CustomConfig_FieldsStoredCorrectly) {
    DistanceConstraintModule m;
    m.constraintOrigin   = {1.0f, 2.0f, 3.0f};
    m.minDistance        = 2.5f;
    m.maxDistance        = 100.0f;
    m.constraintStrength = 25.0f;

    EXPECT_FLOAT_EQ(m.constraintOrigin.x,   1.0f);
    EXPECT_FLOAT_EQ(m.constraintOrigin.y,   2.0f);
    EXPECT_FLOAT_EQ(m.constraintOrigin.z,   3.0f);
    EXPECT_FLOAT_EQ(m.minDistance,           2.5f);
    EXPECT_FLOAT_EQ(m.maxDistance,         100.0f);
    EXPECT_FLOAT_EQ(m.constraintStrength,   25.0f);
}

TEST(UZ_DistanceConstraintModule, CustomConfig_ReflectedInGetParameters) {
    DistanceConstraintModule m;
    m.constraintOrigin   = {7.0f, 8.0f, 9.0f};
    m.minDistance        = 5.0f;
    m.maxDistance        = 75.0f;
    m.constraintStrength = 3.0f;

    const auto params = m.GetParameters();
    ASSERT_EQ(params.size(), 4u);

    // constraint_origin
    EXPECT_FLOAT_EQ(params[0].defaultVec3.x,  7.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.y,  8.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.z,  9.0f);
    // min_distance
    EXPECT_FLOAT_EQ(params[1].defaultFloat,    5.0f);
    // max_distance
    EXPECT_FLOAT_EQ(params[2].defaultFloat,   75.0f);
    // constraint_strength
    EXPECT_FLOAT_EQ(params[3].defaultFloat,    3.0f);
}
