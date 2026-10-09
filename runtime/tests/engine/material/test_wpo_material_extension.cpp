/// Unit tests for phase 9.13.0 — WPO material compiler extension.
///
/// @ref engine/material/compiler/wpo_material_extension.h

#include <gtest/gtest.h>

#include "engine/material/compiler/wpo_material_extension.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Basic identity
// ─────────────────────────────────────────────────────────────────────────────

TEST(WPOMaterialExtension, NameIsStable) {
    VfxWPOMaterialAttributes attrs;
    WPOMaterialExtension ext(attrs);
    EXPECT_STREQ(ext.Name(), "WPO");
}

// ─────────────────────────────────────────────────────────────────────────────
// Disabled attribute → no emission, no permutation
// ─────────────────────────────────────────────────────────────────────────────

TEST(WPOMaterialExtension, DisabledEmitsNothing) {
    VfxWPOMaterialAttributes attrs; // empty → disabled
    WPOMaterialExtension ext(attrs);

    std::string out;
    EXPECT_FALSE(ext.EmitHLSL(MaterialInjectionPoint::FunctionScope, out));
    EXPECT_FALSE(ext.EmitHLSL(MaterialInjectionPoint::VS_PreViewProj, out));
    EXPECT_FALSE(ext.EmitHLSL(MaterialInjectionPoint::VS_PrevFrame, out));
    EXPECT_FALSE(ext.EmitHLSL(MaterialInjectionPoint::PS_PostLighting, out));
}

TEST(WPOMaterialExtension, DisabledNoPermutationTag) {
    VfxWPOMaterialAttributes attrs;
    WPOMaterialExtension ext(attrs);
    EXPECT_TRUE(ext.PSOPermutationTag().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Enabled attribute → emits at VS + FunctionScope, gets permutation tag
// ─────────────────────────────────────────────────────────────────────────────

TEST(WPOMaterialExtension, EnabledEmitsAtFunctionScope) {
    VfxWPOMaterialAttributes attrs;
    attrs.hlslExpression = "float3(sin(time), 0, 0)";
    WPOMaterialExtension ext(attrs);

    std::string out;
    EXPECT_TRUE(ext.EmitHLSL(MaterialInjectionPoint::FunctionScope, out));
    // The function signature must contain EvaluateWPO.
    EXPECT_NE(out.find("EvaluateWPO"), std::string::npos);
    EXPECT_NE(out.find("objectPos"), std::string::npos);
    EXPECT_NE(out.find("worldPos"), std::string::npos);
    EXPECT_NE(out.find("normalizedAge"), std::string::npos);
}

TEST(WPOMaterialExtension, EnabledEmitsAtVSPreViewProj) {
    VfxWPOMaterialAttributes attrs;
    attrs.hlslExpression = "float3(0, 0, 0.5)";
    WPOMaterialExtension ext(attrs);

    std::string out;
    EXPECT_TRUE(ext.EmitHLSL(MaterialInjectionPoint::VS_PreViewProj, out));
    EXPECT_FALSE(out.empty());
}

TEST(WPOMaterialExtension, EnabledEmitsAtVSPrevFrame) {
    VfxWPOMaterialAttributes attrs;
    attrs.hlslExpression = "float3(0, 0, 0.5)";
    WPOMaterialExtension ext(attrs);

    std::string out;
    EXPECT_TRUE(ext.EmitHLSL(MaterialInjectionPoint::VS_PrevFrame, out));
    EXPECT_FALSE(out.empty());
}

TEST(WPOMaterialExtension, EnabledNoEmissionAtPSStage) {
    VfxWPOMaterialAttributes attrs;
    attrs.hlslExpression = "float3(0, 0, 0.5)";
    WPOMaterialExtension ext(attrs);

    std::string out;
    EXPECT_FALSE(ext.EmitHLSL(MaterialInjectionPoint::PS_PostLighting, out));
}

TEST(WPOMaterialExtension, EnabledHasPermutationTag) {
    VfxWPOMaterialAttributes attrs;
    attrs.hlslExpression = "float3(1, 0, 0)";
    WPOMaterialExtension ext(attrs);
    EXPECT_EQ(ext.PSOPermutationTag(), "WPO_ENABLED");
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration with the registry
// ─────────────────────────────────────────────────────────────────────────────

TEST(WPOMaterialExtension, RegistryCollectsAllPoints) {
    VfxWPOMaterialAttributes attrs;
    attrs.hlslExpression = "float3(sin(time) * 0.1, 0, 0)";

    MaterialCompilerExtensionRegistry reg;
    reg.Register(std::make_unique<WPOMaterialExtension>(attrs));

    auto fn = reg.CollectHLSL(MaterialInjectionPoint::FunctionScope);
    EXPECT_FALSE(fn.empty());
    EXPECT_NE(fn.find("EvaluateWPO"), std::string::npos);

    auto vs = reg.CollectHLSL(MaterialInjectionPoint::VS_PreViewProj);
    EXPECT_FALSE(vs.empty());

    auto prev = reg.CollectHLSL(MaterialInjectionPoint::VS_PrevFrame);
    EXPECT_FALSE(prev.empty());

    auto tags = reg.CollectPermutationTags();
    ASSERT_EQ(tags.size(), 1u);
    EXPECT_EQ(tags[0], "WPO_ENABLED");
}

TEST(WPOMaterialExtension, DisabledRegistryProducesNoPermutation) {
    VfxWPOMaterialAttributes attrs; // disabled

    MaterialCompilerExtensionRegistry reg;
    reg.Register(std::make_unique<WPOMaterialExtension>(attrs));

    EXPECT_TRUE(reg.CollectHLSL(MaterialInjectionPoint::FunctionScope).empty());
    EXPECT_TRUE(reg.CollectPermutationTags().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessor
// ─────────────────────────────────────────────────────────────────────────────

TEST(WPOMaterialExtension, AttributesAccessor) {
    VfxWPOMaterialAttributes attrs;
    attrs.hlslExpression = "float3(42, 0, 0)";
    WPOMaterialExtension ext(attrs);
    EXPECT_EQ(ext.Attributes().hlslExpression, "float3(42, 0, 0)");
    EXPECT_TRUE(ext.Attributes().IsEnabled());
}
