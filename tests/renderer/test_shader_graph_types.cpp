// test_shader_graph_types.cpp -- Google Test coverage for ValueType bitmask system.
//
// Covers:
//   - types_compatible(): float compatibility, composite masks, texture types, mismatches
//   - is_concrete(): single-bit vs composite types
//   - promote_type(): scalar broadcast, same-type pass-through, ambiguous errors
//   - component_count(): all float types + non-float returns 0
//   - hlsl_type_name(): all concrete types + composite returns nullptr

#include <gtest/gtest.h>
#include "engine/renderer/shader_graph/shader_graph_types.h"

using namespace uz::sg;

// ============================================================================
// types_compatible
// ============================================================================

TEST(ValueTypeTest, CompatibleSameType) {
    EXPECT_TRUE(types_compatible(ValueType::Float1, ValueType::Float1));
    EXPECT_TRUE(types_compatible(ValueType::Float3, ValueType::Float3));
    EXPECT_TRUE(types_compatible(ValueType::Texture2D, ValueType::Texture2D));
}

TEST(ValueTypeTest, CompatibleScalarAcceptsAnyFloat) {
    EXPECT_TRUE(types_compatible(ValueType::Scalar, ValueType::Float1));
    EXPECT_TRUE(types_compatible(ValueType::Scalar, ValueType::Float2));
    EXPECT_TRUE(types_compatible(ValueType::Scalar, ValueType::Float3));
    EXPECT_TRUE(types_compatible(ValueType::Scalar, ValueType::Float4));
}

TEST(ValueTypeTest, CompatibleFloatWithScalar) {
    // Symmetric: concrete float matches Scalar composite.
    EXPECT_TRUE(types_compatible(ValueType::Float1, ValueType::Scalar));
    EXPECT_TRUE(types_compatible(ValueType::Float4, ValueType::Scalar));
}

TEST(ValueTypeTest, CompatibleAnyTexture) {
    EXPECT_TRUE(types_compatible(ValueType::AnyTexture, ValueType::Texture2D));
    EXPECT_TRUE(types_compatible(ValueType::AnyTexture, ValueType::TextureCube));
    EXPECT_TRUE(types_compatible(ValueType::Texture2D, ValueType::AnyTexture));
}

TEST(ValueTypeTest, IncompatibleTextureAndFloat) {
    EXPECT_FALSE(types_compatible(ValueType::Texture2D, ValueType::Float3));
    EXPECT_FALSE(types_compatible(ValueType::Float1, ValueType::Texture2D));
}

TEST(ValueTypeTest, IncompatibleDifferentNonFloat) {
    EXPECT_FALSE(types_compatible(ValueType::Bool, ValueType::Int));
    EXPECT_FALSE(types_compatible(ValueType::Sampler, ValueType::Texture2D));
}

TEST(ValueTypeTest, IncompatibleNoneWithAnything) {
    EXPECT_FALSE(types_compatible(ValueType::None, ValueType::Float1));
    EXPECT_FALSE(types_compatible(ValueType::Float3, ValueType::None));
    EXPECT_FALSE(types_compatible(ValueType::None, ValueType::None));
}

TEST(ValueTypeTest, CompatibleFloatCrossWidths) {
    // Float2 and Float3 are incompatible as concrete types (no shared bits).
    EXPECT_FALSE(types_compatible(ValueType::Float2, ValueType::Float3));
    EXPECT_FALSE(types_compatible(ValueType::Float1, ValueType::Float4));
}

// ============================================================================
// is_concrete
// ============================================================================

TEST(ValueTypeTest, IsConcreteForSingleBit) {
    EXPECT_TRUE(is_concrete(ValueType::Float1));
    EXPECT_TRUE(is_concrete(ValueType::Float2));
    EXPECT_TRUE(is_concrete(ValueType::Float3));
    EXPECT_TRUE(is_concrete(ValueType::Float4));
    EXPECT_TRUE(is_concrete(ValueType::Texture2D));
    EXPECT_TRUE(is_concrete(ValueType::Bool));
    EXPECT_TRUE(is_concrete(ValueType::Int));
    EXPECT_TRUE(is_concrete(ValueType::Sampler));
}

TEST(ValueTypeTest, IsConcreteForComposite) {
    EXPECT_FALSE(is_concrete(ValueType::Scalar));
    EXPECT_FALSE(is_concrete(ValueType::AnyTexture));
    EXPECT_FALSE(is_concrete(ValueType::None));
}

// ============================================================================
// promote_type
// ============================================================================

TEST(ValueTypeTest, PromoteSameType) {
    EXPECT_EQ(promote_type(ValueType::Float1, ValueType::Float1), ValueType::Float1);
    EXPECT_EQ(promote_type(ValueType::Float3, ValueType::Float3), ValueType::Float3);
    EXPECT_EQ(promote_type(ValueType::Float4, ValueType::Float4), ValueType::Float4);
}

TEST(ValueTypeTest, PromoteScalarBroadcast) {
    EXPECT_EQ(promote_type(ValueType::Float1, ValueType::Float3), ValueType::Float3);
    EXPECT_EQ(promote_type(ValueType::Float3, ValueType::Float1), ValueType::Float3);
    EXPECT_EQ(promote_type(ValueType::Float1, ValueType::Float2), ValueType::Float2);
    EXPECT_EQ(promote_type(ValueType::Float1, ValueType::Float4), ValueType::Float4);
}

TEST(ValueTypeTest, PromoteAmbiguousFails) {
    // Float2 + Float3 — cannot promote, different widths.
    EXPECT_EQ(promote_type(ValueType::Float2, ValueType::Float3), ValueType::None);
    EXPECT_EQ(promote_type(ValueType::Float2, ValueType::Float4), ValueType::None);
    EXPECT_EQ(promote_type(ValueType::Float3, ValueType::Float4), ValueType::None);
}

TEST(ValueTypeTest, PromoteWithNone) {
    EXPECT_EQ(promote_type(ValueType::None, ValueType::Float3), ValueType::None);
    EXPECT_EQ(promote_type(ValueType::Float1, ValueType::None), ValueType::None);
    EXPECT_EQ(promote_type(ValueType::None, ValueType::None), ValueType::None);
}

TEST(ValueTypeTest, PromoteNonFloat) {
    // Texture types cannot be promoted.
    EXPECT_EQ(promote_type(ValueType::Texture2D, ValueType::Float3), ValueType::None);
    EXPECT_EQ(promote_type(ValueType::Bool, ValueType::Float1), ValueType::None);
    EXPECT_EQ(promote_type(ValueType::Int, ValueType::Float2), ValueType::None);
}

TEST(ValueTypeTest, PromoteScalarComposite) {
    // Float1 + Scalar composite → None (Scalar is not concrete).
    EXPECT_EQ(promote_type(ValueType::Float1, ValueType::Scalar), ValueType::None);
    EXPECT_EQ(promote_type(ValueType::Scalar, ValueType::Float1), ValueType::None);
}

// ============================================================================
// component_count
// ============================================================================

TEST(ValueTypeTest, ComponentCountFloat) {
    EXPECT_EQ(component_count(ValueType::Float1), 1);
    EXPECT_EQ(component_count(ValueType::Float2), 2);
    EXPECT_EQ(component_count(ValueType::Float3), 3);
    EXPECT_EQ(component_count(ValueType::Float4), 4);
}

TEST(ValueTypeTest, ComponentCountNonFloat) {
    EXPECT_EQ(component_count(ValueType::None), 0);
    EXPECT_EQ(component_count(ValueType::Texture2D), 0);
    EXPECT_EQ(component_count(ValueType::Bool), 0);
    EXPECT_EQ(component_count(ValueType::Scalar), 0);  // Composite, not concrete.
}

// ============================================================================
// hlsl_type_name
// ============================================================================

TEST(ValueTypeTest, HlslNameFloat) {
    EXPECT_STREQ(hlsl_type_name(ValueType::Float1), "float");
    EXPECT_STREQ(hlsl_type_name(ValueType::Float2), "float2");
    EXPECT_STREQ(hlsl_type_name(ValueType::Float3), "float3");
    EXPECT_STREQ(hlsl_type_name(ValueType::Float4), "float4");
}

TEST(ValueTypeTest, HlslNameNonFloat) {
    EXPECT_STREQ(hlsl_type_name(ValueType::Int), "int");
    EXPECT_STREQ(hlsl_type_name(ValueType::Bool), "bool");
    EXPECT_STREQ(hlsl_type_name(ValueType::Texture2D), "Texture2D");
    EXPECT_STREQ(hlsl_type_name(ValueType::TextureCube), "TextureCube");
    EXPECT_STREQ(hlsl_type_name(ValueType::Sampler), "SamplerState");
}

TEST(ValueTypeTest, HlslNameCompositeReturnsNull) {
    EXPECT_EQ(hlsl_type_name(ValueType::None), nullptr);
    EXPECT_EQ(hlsl_type_name(ValueType::Scalar), nullptr);
    EXPECT_EQ(hlsl_type_name(ValueType::AnyTexture), nullptr);
}

// ============================================================================
// Bitwise operators
// ============================================================================

TEST(ValueTypeTest, BitwiseOr) {
    auto combined = ValueType::Float1 | ValueType::Float2;
    EXPECT_EQ(static_cast<uint32_t>(combined),
              static_cast<uint32_t>(ValueType::Float1) | static_cast<uint32_t>(ValueType::Float2));
}

TEST(ValueTypeTest, BitwiseAnd) {
    auto result = ValueType::Scalar & ValueType::Float3;
    EXPECT_EQ(result, ValueType::Float3);
}

TEST(ValueTypeTest, BoolNot) {
    EXPECT_TRUE(!ValueType::None);
    EXPECT_FALSE(!ValueType::Float1);
}
