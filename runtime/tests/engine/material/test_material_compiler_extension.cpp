/// Unit tests for phase 10.8.0 — material compiler extension API.
///
/// @ref engine/material/compiler/material_compiler_extension.h

#include <gtest/gtest.h>

#include "engine/material/compiler/material_compiler_extension.h"

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
