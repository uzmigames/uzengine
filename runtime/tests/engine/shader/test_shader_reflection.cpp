/// UzEngine Shader Reflection Tests

#include "shader_reflection.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>

namespace uz {

class ShaderReflectionTest : public ::testing::Test {
protected:
    /// Helper to build a ShaderReflectionResult with the given bindings and stage.
    static ShaderReflectionResult MakeResult(
        std::vector<ShaderResourceBinding> bindings,
        ShaderStageFlags stage,
        const std::string& entryPoint = "main")
    {
        ShaderReflectionResult r;
        r.bindings   = std::move(bindings);
        r.stage      = stage;
        r.entryPoint = entryPoint;
        return r;
    }

    /// Helper to build a single binding.
    static ShaderResourceBinding MakeBinding(
        const std::string& name, ShaderResourceType type,
        uint32_t set, uint32_t binding, uint32_t count,
        ShaderStageFlags stageFlags)
    {
        ShaderResourceBinding b;
        b.name       = name;
        b.type       = type;
        b.set        = set;
        b.binding    = binding;
        b.count      = count;
        b.stageFlags = stageFlags;
        return b;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// ReflectionSucceeded
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderReflectionTest, ReflectionSucceeded_EmptyError_ReturnsTrue) {
    ShaderReflectionResult result;
    result.error = "";
    EXPECT_TRUE(ReflectionSucceeded(result));
}

TEST_F(ShaderReflectionTest, ReflectionSucceeded_WithError_ReturnsFalse) {
    ShaderReflectionResult result;
    result.error = "something went wrong";
    EXPECT_FALSE(ReflectionSucceeded(result));
}

// ─────────────────────────────────────────────────────────────────────────────
// ReflectSPIRV — invalid input
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderReflectionTest, ReflectSPIRV_NullBytecode_ReturnsError) {
    auto result = ReflectSPIRV(nullptr, 100);
    EXPECT_FALSE(ReflectionSucceeded(result));
    EXPECT_TRUE(result.bindings.empty());
    EXPECT_NE(result.error.find("Null"), std::string::npos);
}

TEST_F(ShaderReflectionTest, ReflectSPIRV_ZeroSize_ReturnsError) {
    uint32_t dummy = 0;
    auto result = ReflectSPIRV(&dummy, 0);
    EXPECT_FALSE(ReflectionSucceeded(result));
    EXPECT_TRUE(result.bindings.empty());
}

TEST_F(ShaderReflectionTest, ReflectSPIRV_SizeNotMultipleOf4_ReturnsError) {
    uint8_t data[5] = {1, 2, 3, 4, 5};
    auto result = ReflectSPIRV(data, 5);
    EXPECT_FALSE(ReflectionSucceeded(result));
    EXPECT_NE(result.error.find("multiple of 4"), std::string::npos);
}

TEST_F(ShaderReflectionTest, ReflectSPIRV_InvalidBytecode_ReturnsError) {
    // Random garbage that is 4-byte aligned but not valid SPIRV
    uint32_t garbage[8] = {0xDEADBEEF, 0, 0, 0, 0, 0, 0, 0};
    auto result = ReflectSPIRV(garbage, sizeof(garbage));
    EXPECT_FALSE(ReflectionSucceeded(result));
    EXPECT_FALSE(result.error.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// MapSpvDescriptorType
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderReflectionTest, MapSpvDescriptorType_UniformBuffer_IsConstantBuffer) {
    EXPECT_EQ(MapSpvDescriptorType(6),  ShaderResourceType::ConstantBuffer);  // UNIFORM_BUFFER
    EXPECT_EQ(MapSpvDescriptorType(8),  ShaderResourceType::ConstantBuffer);  // UNIFORM_BUFFER_DYNAMIC
}

TEST_F(ShaderReflectionTest, MapSpvDescriptorType_SampledImage_IsShaderResource) {
    EXPECT_EQ(MapSpvDescriptorType(2),  ShaderResourceType::ShaderResource);  // SAMPLED_IMAGE
    EXPECT_EQ(MapSpvDescriptorType(4),  ShaderResourceType::ShaderResource);  // UNIFORM_TEXEL_BUFFER
}

TEST_F(ShaderReflectionTest, MapSpvDescriptorType_StorageTypes_AreUnorderedAccess) {
    EXPECT_EQ(MapSpvDescriptorType(3),  ShaderResourceType::UnorderedAccess);  // STORAGE_IMAGE
    EXPECT_EQ(MapSpvDescriptorType(7),  ShaderResourceType::UnorderedAccess);  // STORAGE_BUFFER
    EXPECT_EQ(MapSpvDescriptorType(9),  ShaderResourceType::UnorderedAccess);  // STORAGE_BUFFER_DYNAMIC
    EXPECT_EQ(MapSpvDescriptorType(5),  ShaderResourceType::UnorderedAccess);  // STORAGE_TEXEL_BUFFER
}

TEST_F(ShaderReflectionTest, MapSpvDescriptorType_Samplers_AreSampler) {
    EXPECT_EQ(MapSpvDescriptorType(0),  ShaderResourceType::Sampler);  // SAMPLER
    EXPECT_EQ(MapSpvDescriptorType(1),  ShaderResourceType::Sampler);  // COMBINED_IMAGE_SAMPLER
}

TEST_F(ShaderReflectionTest, MapSpvDescriptorType_InputAttachment_IsShaderResource) {
    EXPECT_EQ(MapSpvDescriptorType(10), ShaderResourceType::ShaderResource);  // INPUT_ATTACHMENT
}

TEST_F(ShaderReflectionTest, MapSpvDescriptorType_Unknown_DefaultsToShaderResource) {
    EXPECT_EQ(MapSpvDescriptorType(9999), ShaderResourceType::ShaderResource);
}

// ─────────────────────────────────────────────────────────────────────────────
// MapSpvStageFlags
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderReflectionTest, MapSpvStageFlags_SingleBits) {
    EXPECT_EQ(MapSpvStageFlags(0x00000001), kStageVertex);
    EXPECT_EQ(MapSpvStageFlags(0x00000010), kStagePixel);
    EXPECT_EQ(MapSpvStageFlags(0x00000020), kStageCompute);
    EXPECT_EQ(MapSpvStageFlags(0x00000008), kStageGeometry);
    EXPECT_EQ(MapSpvStageFlags(0x00000002), kStageHull);
    EXPECT_EQ(MapSpvStageFlags(0x00000004), kStageDomain);
    EXPECT_EQ(MapSpvStageFlags(0x00000040), kStageAmplification);
    EXPECT_EQ(MapSpvStageFlags(0x00000080), kStageMesh);
}

TEST_F(ShaderReflectionTest, MapSpvStageFlags_CombinedBits) {
    // Vertex + Fragment
    uint32_t combined = 0x00000001 | 0x00000010;
    ShaderStageFlags mapped = MapSpvStageFlags(combined);
    EXPECT_EQ(mapped, kStageVertex | kStagePixel);
}

TEST_F(ShaderReflectionTest, MapSpvStageFlags_Zero_ReturnsZero) {
    EXPECT_EQ(MapSpvStageFlags(0), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShaderResourceTypeName
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderReflectionTest, ShaderResourceTypeName_AllValues) {
    EXPECT_STREQ(ShaderResourceTypeName(ShaderResourceType::ConstantBuffer),  "ConstantBuffer");
    EXPECT_STREQ(ShaderResourceTypeName(ShaderResourceType::ShaderResource),   "ShaderResource");
    EXPECT_STREQ(ShaderResourceTypeName(ShaderResourceType::UnorderedAccess),  "UnorderedAccess");
    EXPECT_STREQ(ShaderResourceTypeName(ShaderResourceType::Sampler),          "Sampler");
    EXPECT_STREQ(ShaderResourceTypeName(ShaderResourceType::PushConstant),     "PushConstant");
}

// ─────────────────────────────────────────────────────────────────────────────
// SortBindings
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderReflectionTest, SortBindings_EmptyVector_NoCrash) {
    std::vector<ShaderResourceBinding> empty;
    SortBindings(empty);
    EXPECT_TRUE(empty.empty());
}

TEST_F(ShaderReflectionTest, SortBindings_OrdersBySetThenBinding) {
    std::vector<ShaderResourceBinding> bindings = {
        MakeBinding("c", ShaderResourceType::Sampler,          1, 2, 1, kStagePixel),
        MakeBinding("a", ShaderResourceType::ConstantBuffer,   0, 0, 1, kStageVertex),
        MakeBinding("d", ShaderResourceType::UnorderedAccess,  2, 0, 1, kStageCompute),
        MakeBinding("b", ShaderResourceType::ShaderResource,   0, 3, 1, kStageVertex),
    };

    SortBindings(bindings);

    ASSERT_EQ(bindings.size(), 4u);
    EXPECT_EQ(bindings[0].set, 0u); EXPECT_EQ(bindings[0].binding, 0u); EXPECT_EQ(bindings[0].name, "a");
    EXPECT_EQ(bindings[1].set, 0u); EXPECT_EQ(bindings[1].binding, 3u); EXPECT_EQ(bindings[1].name, "b");
    EXPECT_EQ(bindings[2].set, 1u); EXPECT_EQ(bindings[2].binding, 2u); EXPECT_EQ(bindings[2].name, "c");
    EXPECT_EQ(bindings[3].set, 2u); EXPECT_EQ(bindings[3].binding, 0u); EXPECT_EQ(bindings[3].name, "d");
}

// ─────────────────────────────────────────────────────────────────────────────
// MergeBindings
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderReflectionTest, MergeBindings_EmptyInput_ReturnsEmpty) {
    std::vector<ShaderReflectionResult> empty;
    auto merged = MergeBindings(empty);
    EXPECT_TRUE(merged.empty());
}

TEST_F(ShaderReflectionTest, MergeBindings_SingleStage_PassThrough) {
    auto r = MakeResult({
        MakeBinding("cbuf", ShaderResourceType::ConstantBuffer, 0, 0, 1, kStageVertex),
        MakeBinding("tex",  ShaderResourceType::ShaderResource,  0, 1, 1, kStageVertex),
    }, kStageVertex);

    auto merged = MergeBindings({r});

    ASSERT_EQ(merged.size(), 2u);
    EXPECT_EQ(merged[0].name, "cbuf");
    EXPECT_EQ(merged[0].stageFlags, kStageVertex);
    EXPECT_EQ(merged[1].name, "tex");
}

TEST_F(ShaderReflectionTest, MergeBindings_TwoStages_DeduplicatesAndORsFlags) {
    auto vs = MakeResult({
        MakeBinding("cbuf", ShaderResourceType::ConstantBuffer, 0, 0, 1, kStageVertex),
        MakeBinding("tex",  ShaderResourceType::ShaderResource,  0, 1, 1, kStageVertex),
    }, kStageVertex);

    auto ps = MakeResult({
        MakeBinding("cbuf", ShaderResourceType::ConstantBuffer, 0, 0, 1, kStagePixel),
        MakeBinding("samp", ShaderResourceType::Sampler,         0, 2, 1, kStagePixel),
    }, kStagePixel);

    auto merged = MergeBindings({vs, ps});

    ASSERT_EQ(merged.size(), 3u);

    // Find cbuf — should have both vertex and pixel flags
    auto cbufIt = std::find_if(merged.begin(), merged.end(),
        [](const ShaderResourceBinding& b) { return b.name == "cbuf"; });
    ASSERT_NE(cbufIt, merged.end());
    EXPECT_EQ(cbufIt->stageFlags, kStageVertex | kStagePixel);
    EXPECT_EQ(cbufIt->set, 0u);
    EXPECT_EQ(cbufIt->binding, 0u);

    // tex — vertex only
    auto texIt = std::find_if(merged.begin(), merged.end(),
        [](const ShaderResourceBinding& b) { return b.name == "tex"; });
    ASSERT_NE(texIt, merged.end());
    EXPECT_EQ(texIt->stageFlags, kStageVertex);

    // samp — pixel only
    auto sampIt = std::find_if(merged.begin(), merged.end(),
        [](const ShaderResourceBinding& b) { return b.name == "samp"; });
    ASSERT_NE(sampIt, merged.end());
    EXPECT_EQ(sampIt->stageFlags, kStagePixel);
}

TEST_F(ShaderReflectionTest, MergeBindings_ThreeStages_AllFlagsMerged) {
    // Same binding at (0,0) used by vertex, pixel, and compute
    auto vs = MakeResult({MakeBinding("globals", ShaderResourceType::ConstantBuffer, 0, 0, 1, kStageVertex)}, kStageVertex);
    auto ps = MakeResult({MakeBinding("globals", ShaderResourceType::ConstantBuffer, 0, 0, 1, kStagePixel)}, kStagePixel);
    auto cs = MakeResult({MakeBinding("globals", ShaderResourceType::ConstantBuffer, 0, 0, 1, kStageCompute)}, kStageCompute);

    auto merged = MergeBindings({vs, ps, cs});

    ASSERT_EQ(merged.size(), 1u);
    EXPECT_EQ(merged[0].stageFlags, kStageVertex | kStagePixel | kStageCompute);
}

TEST_F(ShaderReflectionTest, MergeBindings_DifferentSets_NotDeduplicated) {
    // Same binding number but different sets — should NOT be merged
    auto vs = MakeResult({
        MakeBinding("a", ShaderResourceType::ConstantBuffer, 0, 0, 1, kStageVertex),
        MakeBinding("b", ShaderResourceType::ConstantBuffer, 1, 0, 1, kStageVertex),
    }, kStageVertex);

    auto merged = MergeBindings({vs});

    ASSERT_EQ(merged.size(), 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShaderStageFlags bitmask operations
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderReflectionTest, StageFlags_BitwiseOperations) {
    ShaderStageFlags flags = kStageVertex | kStagePixel;
    EXPECT_TRUE(flags & kStageVertex);
    EXPECT_TRUE(flags & kStagePixel);
    EXPECT_FALSE(flags & kStageCompute);

    flags |= kStageCompute;
    EXPECT_TRUE(flags & kStageCompute);

    // All flags are distinct powers of 2
    EXPECT_EQ(kStageVertex & kStagePixel, 0u);
    EXPECT_EQ(kStageCompute & kStageGeometry, 0u);
    EXPECT_EQ(kStageHull & kStageDomain, 0u);
    EXPECT_EQ(kStageAmplification & kStageMesh, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShaderResourceBinding default values
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderReflectionTest, ShaderResourceBinding_DefaultValues) {
    ShaderResourceBinding b;
    EXPECT_TRUE(b.name.empty());
    EXPECT_EQ(b.set, 0u);
    EXPECT_EQ(b.binding, 0u);
    EXPECT_EQ(b.count, 1u);
    EXPECT_EQ(b.stageFlags, 0u);
}

TEST_F(ShaderReflectionTest, ShaderReflectionResult_DefaultValues) {
    ShaderReflectionResult r;
    EXPECT_TRUE(r.bindings.empty());
    EXPECT_TRUE(r.entryPoint.empty());
    EXPECT_EQ(r.stage, 0u);
    EXPECT_TRUE(r.error.empty());
    EXPECT_TRUE(ReflectionSucceeded(r));
}

} // namespace uz
