/// Unit tests for VfxAttributeHlslGen — deterministic HLSL CB emission.

#include <gtest/gtest.h>

#include "engine/vfx/parameters/vfx_attribute_hlsl_gen.h"
#include "engine/vfx/parameters/vfx_attribute_store.h"

using namespace uz;

TEST(VfxAttributeHlslGen, EmptyScopeYieldsEmptyOutput) {
    VfxAttributeStore store;
    VfxAttributeHlslGenConfig cfg;
    cfg.scope = VfxAttributeScope::System;
    EXPECT_TRUE(GenerateVfxAttributeCbuffer(store, cfg).empty());
}

TEST(VfxAttributeHlslGen, EmitsOneRowPerAttributeInInsertionOrder) {
    VfxAttributeStore store;
    store.SetFloat(VfxAttributeScope::System, "intensity", 1.0f);
    store.SetVec3 (VfxAttributeScope::System, "windDir",   {1, 0, 0});

    VfxAttributeHlslGenConfig cfg;
    cfg.scope = VfxAttributeScope::System;
    const auto hlsl = GenerateVfxAttributeCbuffer(store, cfg);

    // Must mention the scope name, the register slot (default b8), and both fields.
    EXPECT_NE(hlsl.find("VfxSystemAttributes"),   std::string::npos);
    EXPECT_NE(hlsl.find("register(b8)"),          std::string::npos);
    EXPECT_NE(hlsl.find("_System_intensity"),     std::string::npos);
    EXPECT_NE(hlsl.find("_System_windDir"),       std::string::npos);

    // Insertion order preserved: `intensity` appears before `windDir`.
    const auto posIntensity = hlsl.find("_System_intensity");
    const auto posWind      = hlsl.find("_System_windDir");
    ASSERT_NE(posIntensity, std::string::npos);
    ASSERT_NE(posWind,      std::string::npos);
    EXPECT_LT(posIntensity, posWind);

    // Typed accessors present.
    EXPECT_NE(hlsl.find("float4 System_intensity()"), std::string::npos);
    EXPECT_NE(hlsl.find("float4 System_windDir()"),   std::string::npos);
}

TEST(VfxAttributeHlslGen, RegisterSlotOverrideUsed) {
    VfxAttributeStore store;
    store.SetFloat(VfxAttributeScope::Emitter, "loopCount", 3.0f);

    VfxAttributeHlslGenConfig cfg;
    cfg.scope        = VfxAttributeScope::Emitter;
    cfg.registerSlot = 12u;
    const auto hlsl = GenerateVfxAttributeCbuffer(store, cfg);

    EXPECT_NE(hlsl.find("register(b12)"), std::string::npos);
}

TEST(VfxAttributeHlslGen, CustomStructNameUsed) {
    VfxAttributeStore store;
    store.SetFloat(VfxAttributeScope::System, "intensity", 1.0f);

    VfxAttributeHlslGenConfig cfg;
    cfg.scope      = VfxAttributeScope::System;
    cfg.structName = "MyCustomCB";
    const auto hlsl = GenerateVfxAttributeCbuffer(store, cfg);

    EXPECT_NE(hlsl.find("cbuffer MyCustomCB"), std::string::npos);
}

TEST(VfxAttributeHlslGen, DeterministicOutputAcrossCalls) {
    VfxAttributeStore a;
    VfxAttributeStore b;
    a.SetFloat(VfxAttributeScope::System, "intensity", 1.0f);
    a.SetVec3 (VfxAttributeScope::System, "windDir",   {1, 0, 0});
    b.SetFloat(VfxAttributeScope::System, "intensity", 1.0f);
    b.SetVec3 (VfxAttributeScope::System, "windDir",   {1, 0, 0});

    VfxAttributeHlslGenConfig cfg;
    cfg.scope = VfxAttributeScope::System;
    EXPECT_EQ(GenerateVfxAttributeCbuffer(a, cfg),
              GenerateVfxAttributeCbuffer(b, cfg));
}

TEST(VfxAttributeHlslGen, DefaultRegisterSlotsMatchConvention) {
    EXPECT_EQ(VfxAttributeDefaultRegisterSlot(VfxAttributeScope::System),    8u);
    EXPECT_EQ(VfxAttributeDefaultRegisterSlot(VfxAttributeScope::Particles), 9u);
    EXPECT_EQ(VfxAttributeDefaultRegisterSlot(VfxAttributeScope::Emitter),  10u);
}
