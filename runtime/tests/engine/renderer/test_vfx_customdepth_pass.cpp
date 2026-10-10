/// Unit tests for phase 9.12.0 — VFX CustomDepth + stencil pass.
///
/// @ref engine/renderer/passes/vfx/vfx_customdepth_pass.h

#include <gtest/gtest.h>

#include "engine/renderer/passes/vfx/vfx_customdepth_pass.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// RHI stencil types (phase 9.12.0 extension)
// ─────────────────────────────────────────────────────────────────────────────

TEST(RHIStencilTypes, FaceDescDefaults) {
    RHIStencilFaceDesc face{};
    EXPECT_EQ(face.failOp, RHIStencilOp::Keep);
    EXPECT_EQ(face.depthFailOp, RHIStencilOp::Keep);
    EXPECT_EQ(face.passOp, RHIStencilOp::Keep);
    EXPECT_EQ(face.stencilFunc, RHICompareFunc::Always);
}

TEST(RHIStencilTypes, DepthStencilDescDefaults) {
    RHIDepthStencilDesc ds{};
    EXPECT_TRUE(ds.depthEnable);
    EXPECT_TRUE(ds.depthWrite);
    EXPECT_EQ(ds.depthFunc, RHICompareFunc::Greater);
    EXPECT_FALSE(ds.stencilEnable);
    EXPECT_EQ(ds.stencilReadMask, 0xFF);
    EXPECT_EQ(ds.stencilWriteMask, 0xFF);
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCustomDepthPass, PassDescDefaults) {
    VfxCustomDepthPassDesc desc{};
    for (int i = 0; i < 3; ++i) {
        EXPECT_FALSE(desc.pipelines[i].IsValid());
    }
    EXPECT_FALSE(desc.customDepthTarget.IsValid());
    EXPECT_FALSE(desc.customDepthTexture.IsValid());
    EXPECT_EQ(desc.renderWidth, 0u);
    EXPECT_EQ(desc.renderHeight, 0u);
    EXPECT_TRUE(desc.emitters.empty());
}

TEST(VfxCustomDepthPass, EmitterDataDefaults) {
    CustomDepthEmitterData data{};
    EXPECT_EQ(data.stencilConfig.writeMode, VfxStencilWriteMode::Disabled);
    EXPECT_EQ(data.stencilConfig.stencilRef, 0);
    EXPECT_FALSE(data.stencilConfig.customDepthEnabled);
    EXPECT_FALSE(data.indirectDrawArgs.IsValid());
    EXPECT_EQ(data.positionSrvIdx, UINT32_MAX);
    EXPECT_EQ(data.sizeSrvIdx, UINT32_MAX);
    EXPECT_EQ(data.rotationSrvIdx, UINT32_MAX);
    EXPECT_EQ(data.spriteSrvIdx, UINT32_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
// PSO variant selection
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCustomDepthPass, StencilWriteModeIndexing) {
    // VfxStencilWriteMode values match the pipeline array indices.
    EXPECT_EQ(static_cast<uint8_t>(VfxStencilWriteMode::Disabled),  0);
    EXPECT_EQ(static_cast<uint8_t>(VfxStencilWriteMode::Replace),   1);
    EXPECT_EQ(static_cast<uint8_t>(VfxStencilWriteMode::Increment), 2);
}
