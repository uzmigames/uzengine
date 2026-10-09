/// Unit tests for phase 9.15.0 — VFX advection field DI registration.
///
/// @ref engine/vfx/data_interfaces/vfx_advection_field_di_impl.h

#include <gtest/gtest.h>

#include "engine/vfx/data_interfaces/vfx_advection_field_di_impl.h"
#include "engine/vfx/modules/vfx_di_registry.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Direct DI interface
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxAdvectionFieldDI, TypeName) {
    VfxAdvectionFieldDataInterface di;
    EXPECT_STREQ(di.TypeName(), "Advection");
}

TEST(VfxAdvectionFieldDI, CBFieldsContainRequiredDecls) {
    VfxAdvectionFieldDataInterface di;
    const auto cb = di.EmitCBFields();
    EXPECT_NE(cb.find("advectionFieldSrvIdx"), std::string::npos);
    EXPECT_NE(cb.find("advectionWorldMin"), std::string::npos);
    EXPECT_NE(cb.find("advectionWorldMax"), std::string::npos);
    EXPECT_NE(cb.find("advectionTightness"), std::string::npos);
}

TEST(VfxAdvectionFieldDI, HelperFunctionsNonEmpty) {
    VfxAdvectionFieldDataInterface di;
    EXPECT_FALSE(di.EmitHelperFunctions().empty());
}

TEST(VfxAdvectionFieldDI, CallSiteBlendsVelocity) {
    VfxAdvectionFieldDataInterface di;
    const auto cs = di.EmitCallSite();
    EXPECT_NE(cs.find("velocity"), std::string::npos);
    EXPECT_NE(cs.find("lerp"), std::string::npos);
    EXPECT_NE(cs.find("SampleAdvection"), std::string::npos);
    // Short-circuit guard present.
    EXPECT_NE(cs.find("0xFFFFFFFFu"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Registry integration
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxAdvectionFieldDI, RegistryComposeProducesAllFragments) {
    VfxDIRegistry reg;
    reg.Register(std::make_unique<VfxAdvectionFieldDataInterface>());

    const auto fragments = reg.ComposeForEmitter({"Advection"});
    EXPECT_NE(fragments.cbFields.find("advectionFieldSrvIdx"), std::string::npos);
    EXPECT_NE(fragments.helperFunctions.find("SampleAdvection"), std::string::npos);
    EXPECT_NE(fragments.callSite.find("lerp"), std::string::npos);
}

TEST(VfxAdvectionFieldDI, UnregisteredProducesNothing) {
    VfxDIRegistry reg;
    reg.Register(std::make_unique<VfxAdvectionFieldDataInterface>());

    // Different DI name → not found.
    const auto fragments = reg.ComposeForEmitter({"Unknown"});
    EXPECT_TRUE(fragments.cbFields.empty());
    EXPECT_TRUE(fragments.helperFunctions.empty());
    EXPECT_TRUE(fragments.callSite.empty());
}
