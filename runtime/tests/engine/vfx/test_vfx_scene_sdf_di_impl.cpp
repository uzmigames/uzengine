/// Unit tests for phase 9.16.0 — VFX scene SDF DI registration.
///
/// @ref engine/vfx/data_interfaces/vfx_scene_sdf_di_impl.h

#include <gtest/gtest.h>

#include "engine/vfx/data_interfaces/vfx_scene_sdf_di_impl.h"
#include "engine/vfx/modules/vfx_di_registry.h"

using namespace uz;

TEST(VfxSceneSDFDI, TypeName) {
    VfxSceneSDFDataInterface di;
    EXPECT_STREQ(di.TypeName(), "SceneSDF");
}

TEST(VfxSceneSDFDI, CBFieldsContainRequiredDecls) {
    VfxSceneSDFDataInterface di;
    const auto cb = di.EmitCBFields();
    EXPECT_NE(cb.find("sdfAtlasSrvIdx"), std::string::npos);
    EXPECT_NE(cb.find("sdfWorldMin"),   std::string::npos);
    EXPECT_NE(cb.find("sdfWorldMax"),   std::string::npos);
    EXPECT_NE(cb.find("sdfRes"),        std::string::npos);
}

TEST(VfxSceneSDFDI, HelperFunctionsNonEmpty) {
    VfxSceneSDFDataInterface di;
    EXPECT_FALSE(di.EmitHelperFunctions().empty());
}

TEST(VfxSceneSDFDI, CallSiteResolvesCollisionOnNegativeDistance) {
    VfxSceneSDFDataInterface di;
    const auto cs = di.EmitCallSite();
    EXPECT_NE(cs.find("SampleSDF"), std::string::npos);
    EXPECT_NE(cs.find("VfxSceneSDFResolveCollision"), std::string::npos);
    EXPECT_NE(cs.find("< 0.0f"), std::string::npos);
    EXPECT_NE(cs.find("0xFFFFFFFFu"), std::string::npos);
}

TEST(VfxSceneSDFDI, RegistryComposeProducesAllFragments) {
    VfxDIRegistry reg;
    reg.Register(std::make_unique<VfxSceneSDFDataInterface>());

    const auto fragments = reg.ComposeForEmitter({"SceneSDF"});
    EXPECT_NE(fragments.cbFields.find("sdfAtlasSrvIdx"),        std::string::npos);
    EXPECT_NE(fragments.helperFunctions.find("SampleSDF"),      std::string::npos);
    EXPECT_NE(fragments.callSite.find("ResolveCollision"),      std::string::npos);
}

TEST(VfxSceneSDFDI, HelperFunctionsDefineEveryCallSiteSymbol) {
    // The call site invokes SampleSDF + VfxSceneSDFResolveCollision; both must
    // be *defined* by the helper fragment (not only referenced), otherwise the
    // composed update CS fails to compile with undeclared identifiers.
    VfxSceneSDFDataInterface di;
    const auto helpers = di.EmitHelperFunctions();
    EXPECT_NE(helpers.find("float SampleSceneSDF(float3 worldPos)"), std::string::npos);
    EXPECT_NE(helpers.find("float3 GradientSceneSDF(float3 worldPos, float eps)"),
              std::string::npos);
    EXPECT_NE(helpers.find("float SampleSDF(float3 worldPos)"), std::string::npos);
    EXPECT_NE(helpers.find(
                  "void VfxSceneSDFResolveCollision(inout float3 position, inout float3 velocity)"),
              std::string::npos);
    // The HLSL resolver mirrors the CPU reference: push-out along the
    // normalised gradient, (1 + e) reflection, world-Z degenerate fallback.
    EXPECT_NE(helpers.find("position += n * (radius - distance);"), std::string::npos);
    EXPECT_NE(helpers.find("(1.0f + saturate(restitution)) * vDotN * n"), std::string::npos);
    EXPECT_NE(helpers.find("float3(0.0f, 0.0f, 1.0f)"), std::string::npos);
}

TEST(VfxSceneSDFDI, CombinedWithAdvectionInOneRegistry) {
    // Both DIs can coexist in a single registry.
    VfxDIRegistry reg;
    reg.Register(std::make_unique<VfxSceneSDFDataInterface>());
    EXPECT_EQ(reg.Count(), 1u);
    EXPECT_NE(reg.Find("SceneSDF"), nullptr);
    EXPECT_EQ(reg.Find("Advection"), nullptr);
}
