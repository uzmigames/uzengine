/// UzEngine VertexLayout Tests
///
/// Covers Standard(), Simple(), FromFlags(), GetVertexStride(), and
/// VertexFormatFlags bitwise operations. All assertions verify exact
/// element counts, strides, semantics, formats, and byte offsets.

#include <gtest/gtest.h>

#include "vertex_layout.h"
#include "vertex.h"
#include "rhi_types.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// VertexLayout::Standard()
// ─────────────────────────────────────────────────────────────────────────────

TEST(VertexLayout_Standard, Count_IsFour) {
    auto layout = VertexLayout::Standard();
    EXPECT_EQ(layout.count, 4u);
}

TEST(VertexLayout_Standard, Stride_Is48) {
    auto layout = VertexLayout::Standard();
    EXPECT_EQ(layout.stride, 48u);
    EXPECT_EQ(layout.stride, static_cast<uint32_t>(sizeof(Vertex)));
}

TEST(VertexLayout_Standard, Element0_Position_Offset0_RGB32Float) {
    auto layout = VertexLayout::Standard();
    EXPECT_EQ(layout.elements[0].semantic,      RHIInputSemantic::Position);
    EXPECT_EQ(layout.elements[0].semanticIndex, 0u);
    EXPECT_EQ(layout.elements[0].format,        RHIFormat::RGB32Float);
    EXPECT_EQ(layout.elements[0].byteOffset,        0u);
}

TEST(VertexLayout_Standard, Element1_Normal_Offset12_RGB32Float) {
    auto layout = VertexLayout::Standard();
    EXPECT_EQ(layout.elements[1].semantic,      RHIInputSemantic::Normal);
    EXPECT_EQ(layout.elements[1].semanticIndex, 0u);
    EXPECT_EQ(layout.elements[1].format,        RHIFormat::RGB32Float);
    EXPECT_EQ(layout.elements[1].byteOffset,        12u);
}

TEST(VertexLayout_Standard, Element2_Texcoord_Offset24_RG32Float) {
    auto layout = VertexLayout::Standard();
    EXPECT_EQ(layout.elements[2].semantic,      RHIInputSemantic::Texcoord);
    EXPECT_EQ(layout.elements[2].semanticIndex, 0u);
    EXPECT_EQ(layout.elements[2].format,        RHIFormat::RG32Float);
    EXPECT_EQ(layout.elements[2].byteOffset,        24u);
}

TEST(VertexLayout_Standard, Element3_Tangent_Offset32_RGBA32Float) {
    auto layout = VertexLayout::Standard();
    EXPECT_EQ(layout.elements[3].semantic,      RHIInputSemantic::Tangent);
    EXPECT_EQ(layout.elements[3].semanticIndex, 0u);
    EXPECT_EQ(layout.elements[3].format,        RHIFormat::RGBA32Float);
    EXPECT_EQ(layout.elements[3].byteOffset,        32u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VertexLayout::Simple()
// ─────────────────────────────────────────────────────────────────────────────

TEST(VertexLayout_Simple, Count_IsThree) {
    auto layout = VertexLayout::Simple();
    EXPECT_EQ(layout.count, 3u);
}

TEST(VertexLayout_Simple, Stride_Is36) {
    auto layout = VertexLayout::Simple();
    EXPECT_EQ(layout.stride, 36u);
    EXPECT_EQ(layout.stride, static_cast<uint32_t>(sizeof(SimpleVertex)));
}

TEST(VertexLayout_Simple, Element0_Position_Offset0_RGB32Float) {
    auto layout = VertexLayout::Simple();
    EXPECT_EQ(layout.elements[0].semantic, RHIInputSemantic::Position);
    EXPECT_EQ(layout.elements[0].format,   RHIFormat::RGB32Float);
    EXPECT_EQ(layout.elements[0].byteOffset,   0u);
}

TEST(VertexLayout_Simple, Element1_Color_Offset12_RGBA32Float) {
    auto layout = VertexLayout::Simple();
    EXPECT_EQ(layout.elements[1].semantic, RHIInputSemantic::Color);
    EXPECT_EQ(layout.elements[1].format,   RHIFormat::RGBA32Float);
    EXPECT_EQ(layout.elements[1].byteOffset,   12u);
}

TEST(VertexLayout_Simple, Element2_Texcoord_Offset28_RG32Float) {
    auto layout = VertexLayout::Simple();
    EXPECT_EQ(layout.elements[2].semantic, RHIInputSemantic::Texcoord);
    EXPECT_EQ(layout.elements[2].format,   RHIFormat::RG32Float);
    EXPECT_EQ(layout.elements[2].byteOffset,   28u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VertexLayout::FromFlags() — position only (None flags)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VertexLayout_FromFlags, None_Count_IsOne) {
    auto layout = VertexLayout::FromFlags(VertexFormatFlags::None);
    EXPECT_EQ(layout.count, 1u);
}

TEST(VertexLayout_FromFlags, None_Stride_Is12) {
    auto layout = VertexLayout::FromFlags(VertexFormatFlags::None);
    EXPECT_EQ(layout.stride, 12u);
}

TEST(VertexLayout_FromFlags, None_Element0_Position_Offset0) {
    auto layout = VertexLayout::FromFlags(VertexFormatFlags::None);
    EXPECT_EQ(layout.elements[0].semantic, RHIInputSemantic::Position);
    EXPECT_EQ(layout.elements[0].byteOffset,   0u);
    EXPECT_EQ(layout.elements[0].format,   RHIFormat::RGB32Float);
}

// ─────────────────────────────────────────────────────────────────────────────
// VertexLayout::FromFlags() — Normal + UV0
// ─────────────────────────────────────────────────────────────────────────────

TEST(VertexLayout_FromFlags, NormalAndUV0_Count_IsThree) {
    auto flags = VertexFormatFlags::HasNormal | VertexFormatFlags::HasUV0;
    auto layout = VertexLayout::FromFlags(flags);
    EXPECT_EQ(layout.count, 3u);
}

TEST(VertexLayout_FromFlags, NormalAndUV0_Stride_Is32) {
    // Position(12) + Normal(12) + UV0(8) = 32
    auto flags = VertexFormatFlags::HasNormal | VertexFormatFlags::HasUV0;
    auto layout = VertexLayout::FromFlags(flags);
    EXPECT_EQ(layout.stride, 32u);
}

TEST(VertexLayout_FromFlags, NormalAndUV0_Element0_Position_Offset0) {
    auto flags = VertexFormatFlags::HasNormal | VertexFormatFlags::HasUV0;
    auto layout = VertexLayout::FromFlags(flags);
    EXPECT_EQ(layout.elements[0].semantic, RHIInputSemantic::Position);
    EXPECT_EQ(layout.elements[0].byteOffset,   0u);
}

TEST(VertexLayout_FromFlags, NormalAndUV0_Element1_Normal_Offset12) {
    auto flags = VertexFormatFlags::HasNormal | VertexFormatFlags::HasUV0;
    auto layout = VertexLayout::FromFlags(flags);
    EXPECT_EQ(layout.elements[1].semantic, RHIInputSemantic::Normal);
    EXPECT_EQ(layout.elements[1].byteOffset,   12u);
    EXPECT_EQ(layout.elements[1].format,   RHIFormat::RGB32Float);
}

TEST(VertexLayout_FromFlags, NormalAndUV0_Element2_Texcoord_Offset24) {
    auto flags = VertexFormatFlags::HasNormal | VertexFormatFlags::HasUV0;
    auto layout = VertexLayout::FromFlags(flags);
    EXPECT_EQ(layout.elements[2].semantic, RHIInputSemantic::Texcoord);
    EXPECT_EQ(layout.elements[2].byteOffset,   24u);
    EXPECT_EQ(layout.elements[2].format,   RHIFormat::RG32Float);
    EXPECT_EQ(layout.elements[2].semanticIndex, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VertexLayout::FromFlags() — StandardFlags() matches Standard()
// ─────────────────────────────────────────────────────────────────────────────

TEST(VertexLayout_FromFlags, StandardFlags_Count_MatchesStandard) {
    auto fromFlags = VertexLayout::FromFlags(VertexLayout::StandardFlags());
    auto standard  = VertexLayout::Standard();
    EXPECT_EQ(fromFlags.count, standard.count);
}

TEST(VertexLayout_FromFlags, StandardFlags_Stride_MatchesStandard) {
    auto fromFlags = VertexLayout::FromFlags(VertexLayout::StandardFlags());
    auto standard  = VertexLayout::Standard();
    EXPECT_EQ(fromFlags.stride, standard.stride);
}

TEST(VertexLayout_FromFlags, StandardFlags_Stride_Is48) {
    auto layout = VertexLayout::FromFlags(VertexLayout::StandardFlags());
    EXPECT_EQ(layout.stride, 48u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VertexLayout::FromFlags() — all flags
// ─────────────────────────────────────────────────────────────────────────────

TEST(VertexLayout_FromFlags, AllFlags_Stride_Is80) {
    // Position(12) + Normal(12) + UV0(8) + Tangent(16) + UV1(8) + UV2(8) + Color(16) = 80
    auto flags = VertexFormatFlags::HasNormal
               | VertexFormatFlags::HasTangent
               | VertexFormatFlags::HasUV0
               | VertexFormatFlags::HasUV1
               | VertexFormatFlags::HasUV2
               | VertexFormatFlags::HasColor;
    auto layout = VertexLayout::FromFlags(flags);
    EXPECT_EQ(layout.stride, 80u);
}

TEST(VertexLayout_FromFlags, AllFlags_Count_IsSeven) {
    auto flags = VertexFormatFlags::HasNormal
               | VertexFormatFlags::HasTangent
               | VertexFormatFlags::HasUV0
               | VertexFormatFlags::HasUV1
               | VertexFormatFlags::HasUV2
               | VertexFormatFlags::HasColor;
    auto layout = VertexLayout::FromFlags(flags);
    EXPECT_EQ(layout.count, 7u);
}

TEST(VertexLayout_FromFlags, UV1_SemanticIndex_IsOne) {
    // UV1 must have semanticIndex 1, not 0 (avoids HLSL collision with UV0)
    auto flags = VertexFormatFlags::HasUV0 | VertexFormatFlags::HasUV1;
    auto layout = VertexLayout::FromFlags(flags);
    // elements: [0]=Position, [1]=Texcoord0, [2]=Texcoord1
    EXPECT_EQ(layout.count, 3u);
    EXPECT_EQ(layout.elements[2].semantic,      RHIInputSemantic::Texcoord);
    EXPECT_EQ(layout.elements[2].semanticIndex, 1u);
}

TEST(VertexLayout_FromFlags, UV2_SemanticIndex_IsTwo) {
    auto flags = VertexFormatFlags::HasUV0 | VertexFormatFlags::HasUV1 | VertexFormatFlags::HasUV2;
    auto layout = VertexLayout::FromFlags(flags);
    // elements: [0]=Position, [1]=TC0, [2]=TC1, [3]=TC2
    EXPECT_EQ(layout.count, 4u);
    EXPECT_EQ(layout.elements[3].semantic,      RHIInputSemantic::Texcoord);
    EXPECT_EQ(layout.elements[3].semanticIndex, 2u);
}

TEST(VertexLayout_FromFlags, Color_Element_Format_IsRGBA32Float) {
    auto flags = VertexFormatFlags::HasColor;
    auto layout = VertexLayout::FromFlags(flags);
    // elements: [0]=Position, [1]=Color
    EXPECT_EQ(layout.count, 2u);
    EXPECT_EQ(layout.elements[1].semantic, RHIInputSemantic::Color);
    EXPECT_EQ(layout.elements[1].format,   RHIFormat::RGBA32Float);
    EXPECT_EQ(layout.elements[1].byteOffset,   12u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetVertexStride()
// ─────────────────────────────────────────────────────────────────────────────

TEST(GetVertexStride, None_Is12) {
    EXPECT_EQ(VertexLayout::GetVertexStride(VertexFormatFlags::None), 12u);
}

TEST(GetVertexStride, HasNormal_Is24) {
    EXPECT_EQ(VertexLayout::GetVertexStride(VertexFormatFlags::HasNormal), 24u);
}

TEST(GetVertexStride, HasNormal_HasUV0_Is32) {
    auto flags = VertexFormatFlags::HasNormal | VertexFormatFlags::HasUV0;
    EXPECT_EQ(VertexLayout::GetVertexStride(flags), 32u);
}

TEST(GetVertexStride, HasNormal_HasUV0_HasTangent_Is48) {
    auto flags = VertexFormatFlags::HasNormal
               | VertexFormatFlags::HasUV0
               | VertexFormatFlags::HasTangent;
    EXPECT_EQ(VertexLayout::GetVertexStride(flags), 48u);
}

TEST(GetVertexStride, AllFlags_Is80) {
    auto flags = VertexFormatFlags::HasNormal
               | VertexFormatFlags::HasTangent
               | VertexFormatFlags::HasUV0
               | VertexFormatFlags::HasUV1
               | VertexFormatFlags::HasUV2
               | VertexFormatFlags::HasColor;
    EXPECT_EQ(VertexLayout::GetVertexStride(flags), 80u);
}

TEST(GetVertexStride, MatchesFromFlagsStride_ForArbitraryFlags) {
    auto flags = VertexFormatFlags::HasNormal | VertexFormatFlags::HasUV1;
    auto fromLayout = VertexLayout::FromFlags(flags);
    EXPECT_EQ(VertexLayout::GetVertexStride(flags), fromLayout.stride);
}

// ─────────────────────────────────────────────────────────────────────────────
// VertexFormatFlags bitwise operations
// ─────────────────────────────────────────────────────────────────────────────

TEST(VertexFormatFlags, OR_TwoFlags_BothSet) {
    auto combined = VertexFormatFlags::HasNormal | VertexFormatFlags::HasUV0;
    EXPECT_TRUE(HasFlag(combined, VertexFormatFlags::HasNormal));
    EXPECT_TRUE(HasFlag(combined, VertexFormatFlags::HasUV0));
}

TEST(VertexFormatFlags, OR_TwoFlags_ThirdNotSet) {
    auto combined = VertexFormatFlags::HasNormal | VertexFormatFlags::HasUV0;
    EXPECT_FALSE(HasFlag(combined, VertexFormatFlags::HasTangent));
    EXPECT_FALSE(HasFlag(combined, VertexFormatFlags::HasColor));
}

TEST(VertexFormatFlags, AND_CommonBit_ReturnsNonZero) {
    auto a = VertexFormatFlags::HasNormal | VertexFormatFlags::HasUV0;
    auto b = VertexFormatFlags::HasNormal | VertexFormatFlags::HasTangent;
    auto result = a & b;
    EXPECT_TRUE(HasFlag(result, VertexFormatFlags::HasNormal));
    EXPECT_FALSE(HasFlag(result, VertexFormatFlags::HasUV0));
    EXPECT_FALSE(HasFlag(result, VertexFormatFlags::HasTangent));
}

TEST(VertexFormatFlags, None_HasFlag_IsFalseForAllChannels) {
    auto none = VertexFormatFlags::None;
    EXPECT_FALSE(HasFlag(none, VertexFormatFlags::HasNormal));
    EXPECT_FALSE(HasFlag(none, VertexFormatFlags::HasTangent));
    EXPECT_FALSE(HasFlag(none, VertexFormatFlags::HasUV0));
    EXPECT_FALSE(HasFlag(none, VertexFormatFlags::HasUV1));
    EXPECT_FALSE(HasFlag(none, VertexFormatFlags::HasUV2));
    EXPECT_FALSE(HasFlag(none, VertexFormatFlags::HasColor));
}

TEST(VertexFormatFlags, AllFlags_HasFlag_IsTrueForAllChannels) {
    auto flags = VertexFormatFlags::HasNormal
               | VertexFormatFlags::HasTangent
               | VertexFormatFlags::HasUV0
               | VertexFormatFlags::HasUV1
               | VertexFormatFlags::HasUV2
               | VertexFormatFlags::HasColor;
    EXPECT_TRUE(HasFlag(flags, VertexFormatFlags::HasNormal));
    EXPECT_TRUE(HasFlag(flags, VertexFormatFlags::HasTangent));
    EXPECT_TRUE(HasFlag(flags, VertexFormatFlags::HasUV0));
    EXPECT_TRUE(HasFlag(flags, VertexFormatFlags::HasUV1));
    EXPECT_TRUE(HasFlag(flags, VertexFormatFlags::HasUV2));
    EXPECT_TRUE(HasFlag(flags, VertexFormatFlags::HasColor));
}

TEST(VertexFormatFlags, StandardFlags_ContainsNormalUV0Tangent_Only) {
    auto sf = VertexLayout::StandardFlags();
    EXPECT_TRUE(HasFlag(sf,  VertexFormatFlags::HasNormal));
    EXPECT_TRUE(HasFlag(sf,  VertexFormatFlags::HasUV0));
    EXPECT_TRUE(HasFlag(sf,  VertexFormatFlags::HasTangent));
    EXPECT_FALSE(HasFlag(sf, VertexFormatFlags::HasUV1));
    EXPECT_FALSE(HasFlag(sf, VertexFormatFlags::HasUV2));
    EXPECT_FALSE(HasFlag(sf, VertexFormatFlags::HasColor));
}
