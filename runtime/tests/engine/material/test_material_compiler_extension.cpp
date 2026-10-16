/// Unit tests for phase 10.8.0 — material compiler extension API.
///
/// @ref engine/material/compiler/material_compiler_extension.h

#include <gtest/gtest.h>

#include "engine/material/compiler/material_compiler_extension.h"
#include "engine/material/compiler/hlsl_material_translator.h"

using namespace uz;

namespace {

/// Test extension that emits at VS_PreViewProj and FunctionScope.
class TestWPOExtension : public IMaterialCompilerExtension {
public:
    const char* Name() const noexcept override { return "TestWPO"; }

    bool EmitHLSL(MaterialInjectionPoint point, std::string& out) const override {
        if (point == MaterialInjectionPoint::VS_PreViewProj) {
            out = "worldPos.xyz += float3(1, 0, 0);";
            return true;
        }
        if (point == MaterialInjectionPoint::FunctionScope) {
            out = "float3 WPOOffset() { return float3(1, 0, 0); }";
            return true;
        }
        return false;
    }

    std::string PSOPermutationTag() const override { return "WPO_ENABLED"; }
};

/// Test extension that emits at PS_PostLighting only.
class TestEmissiveExtension : public IMaterialCompilerExtension {
public:
    const char* Name() const noexcept override { return "TestEmissive"; }

    bool EmitHLSL(MaterialInjectionPoint point, std::string& out) const override {
        if (point == MaterialInjectionPoint::PS_PostLighting) {
            out = "emissive += float3(10, 0, 0);";
            return true;
        }
        return false;
    }
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// InjectionPoint names
// ─────────────────────────────────────────────────────────────────────────────

TEST(MaterialCompilerExtension, InjectionPointNames) {
    EXPECT_STREQ(MaterialInjectionPointName(MaterialInjectionPoint::VS_PreViewProj),
                 "VS_PreViewProj");
    EXPECT_STREQ(MaterialInjectionPointName(MaterialInjectionPoint::VS_PrevFrame),
                 "VS_PrevFrame");
    EXPECT_STREQ(MaterialInjectionPointName(MaterialInjectionPoint::PS_PostLighting),
                 "PS_PostLighting");
    EXPECT_STREQ(MaterialInjectionPointName(MaterialInjectionPoint::FunctionScope),
                 "FunctionScope");
}

// ─────────────────────────────────────────────────────────────────────────────
// Registry basics
// ─────────────────────────────────────────────────────────────────────────────

TEST(MaterialCompilerExtension, RegistryStartsEmpty) {
    MaterialCompilerExtensionRegistry reg;
    EXPECT_EQ(reg.Count(), 0u);
}

TEST(MaterialCompilerExtension, RegisterAndCount) {
    MaterialCompilerExtensionRegistry reg;
    reg.Register(std::make_unique<TestWPOExtension>());
    EXPECT_EQ(reg.Count(), 1u);
    reg.Register(std::make_unique<TestEmissiveExtension>());
    EXPECT_EQ(reg.Count(), 2u);
}

TEST(MaterialCompilerExtension, NullIgnored) {
    MaterialCompilerExtensionRegistry reg;
    reg.Register(nullptr);
    EXPECT_EQ(reg.Count(), 0u);
}

TEST(MaterialCompilerExtension, Clear) {
    MaterialCompilerExtensionRegistry reg;
    reg.Register(std::make_unique<TestWPOExtension>());
    reg.Clear();
    EXPECT_EQ(reg.Count(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// HLSL collection
// ─────────────────────────────────────────────────────────────────────────────

TEST(MaterialCompilerExtension, CollectHLSL_SingleExtension) {
    MaterialCompilerExtensionRegistry reg;
    reg.Register(std::make_unique<TestWPOExtension>());

    auto vs = reg.CollectHLSL(MaterialInjectionPoint::VS_PreViewProj);
    EXPECT_NE(vs.find("worldPos.xyz"), std::string::npos);

    auto fs = reg.CollectHLSL(MaterialInjectionPoint::FunctionScope);
    EXPECT_NE(fs.find("WPOOffset"), std::string::npos);

    // PS_PostLighting — WPO doesn't emit here.
    auto ps = reg.CollectHLSL(MaterialInjectionPoint::PS_PostLighting);
    EXPECT_TRUE(ps.empty());
}

TEST(MaterialCompilerExtension, CollectHLSL_MultipleExtensions) {
    MaterialCompilerExtensionRegistry reg;
    reg.Register(std::make_unique<TestWPOExtension>());
    reg.Register(std::make_unique<TestEmissiveExtension>());

    auto ps = reg.CollectHLSL(MaterialInjectionPoint::PS_PostLighting);
    EXPECT_NE(ps.find("emissive += float3(10, 0, 0)"), std::string::npos);
}

TEST(MaterialCompilerExtension, CollectHLSL_EmptyRegistry) {
    MaterialCompilerExtensionRegistry reg;
    EXPECT_TRUE(reg.CollectHLSL(MaterialInjectionPoint::VS_PreViewProj).empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// PSO permutation tags
// ─────────────────────────────────────────────────────────────────────────────

TEST(MaterialCompilerExtension, PermutationTags) {
    MaterialCompilerExtensionRegistry reg;
    reg.Register(std::make_unique<TestWPOExtension>());
    reg.Register(std::make_unique<TestEmissiveExtension>()); // no tag

    auto tags = reg.CollectPermutationTags();
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(tags[0], "WPO_ENABLED");
}

TEST(MaterialCompilerExtension, PermutationTags_Empty) {
    MaterialCompilerExtensionRegistry reg;
    EXPECT_TRUE(reg.CollectPermutationTags().empty());
}

TEST(MaterialTranslator, ContextMask_ReflectsReferencedInputs) {
    struct ContextCase {
        MatExprType type;
        uint32_t expectedMask;
    };
    const ContextCase cases[] = {
        {MatExprType::VertexColor, MaterialCompileResult::VertexColor},
        {MatExprType::CameraVector, MaterialCompileResult::Camera},
        {MatExprType::ObjectPosition, MaterialCompileResult::ObjectPosition},
        {MatExprType::ScreenPosition, MaterialCompileResult::ScreenPosition},
        {MatExprType::Time, MaterialCompileResult::Time},
        {MatExprType::Fresnel, MaterialCompileResult::Camera},
        {MatExprType::Panner, MaterialCompileResult::Time},
    };

    HlslMaterialTranslator translator;
    for (const auto& testCase : cases) {
        SCOPED_TRACE(static_cast<uint32_t>(testCase.type));
        MaterialGraph graph;
        const uint32_t id = graph.AddExpression(testCase.type).id;
        ASSERT_TRUE(graph.Connect(id, 0, graph.GetPBROutput().id, 0));

        const auto result = translator.Compile(graph);
        ASSERT_TRUE(result.success) << result.errorMessage;
        EXPECT_EQ(result.contextMask, testCase.expectedMask);
    }
}

TEST(MaterialTranslator, ContextMask_ReflectsReferencedInputs_TextureOnlyAndUnusedNodes) {
    MaterialGraph graph;
    const uint32_t uv = graph.AddExpression(MatExprType::TexCoord).id;
    const uint32_t texture = graph.AddExpression(MatExprType::TextureSample).id;
    ASSERT_TRUE(graph.Connect(uv, 0, texture, 0));
    ASSERT_TRUE(graph.Connect(texture, 0, graph.GetPBROutput().id, 0));
    // These inputs exist in the graph, but must not affect emitted code metadata.
    graph.AddExpression(MatExprType::VertexColor);
    graph.AddExpression(MatExprType::CameraVector);
    graph.AddExpression(MatExprType::ObjectPosition);
    graph.AddExpression(MatExprType::ScreenPosition);
    graph.AddExpression(MatExprType::Time);
    graph.AddExpression(MatExprType::Fresnel);
    graph.AddExpression(MatExprType::Panner);

    HlslMaterialTranslator translator;
    const auto result = translator.Compile(graph);
    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(result.textureSlotCount, 1u);
    EXPECT_EQ(result.contextMask, 0u);
}

TEST(MaterialTranslator, ContextMask_ReflectsReferencedInputs_CombinedAndCompileReset) {
    MaterialGraph graph;
    const MatExprType types[] = {
        MatExprType::VertexColor, MatExprType::CameraVector, MatExprType::ObjectPosition,
        MatExprType::ScreenPosition, MatExprType::Time,
    };
    for (uint32_t pin = 0; pin < 5; ++pin) {
        const uint32_t id = graph.AddExpression(types[pin]).id;
        ASSERT_TRUE(graph.Connect(id, 0, graph.GetPBROutput().id, pin));
    }

    HlslMaterialTranslator translator;
    const auto combined = translator.Compile(graph);
    ASSERT_TRUE(combined.success) << combined.errorMessage;
    EXPECT_EQ(combined.contextMask,
              MaterialCompileResult::VertexColor | MaterialCompileResult::Camera |
              MaterialCompileResult::ObjectPosition | MaterialCompileResult::ScreenPosition |
              MaterialCompileResult::Time);

    MaterialGraph emptyGraph;
    const auto empty = translator.Compile(emptyGraph);
    ASSERT_TRUE(empty.success) << empty.errorMessage;
    EXPECT_EQ(empty.contextMask, 0u);

    MaterialGraph timeGraph;
    const uint32_t time = timeGraph.AddExpression(MatExprType::Time).id;
    ASSERT_TRUE(timeGraph.Connect(time, 0, timeGraph.GetPBROutput().id, 0));
    const auto timeOnly = translator.Compile(timeGraph);
    ASSERT_TRUE(timeOnly.success) << timeOnly.errorMessage;
    EXPECT_EQ(timeOnly.contextMask, MaterialCompileResult::Time);
    EXPECT_EQ(combined.contextMask, 31u);
}
