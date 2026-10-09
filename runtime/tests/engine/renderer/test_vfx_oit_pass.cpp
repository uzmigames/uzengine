/// Unit tests for phase 9.11.0 — VFX OIT render passes.
///
/// Tests cover descriptor defaults, emitter data layout, and the
/// empty-emitter skip path.
///
/// @ref engine/renderer/passes/vfx/vfx_oit_pass.h

#include <gtest/gtest.h>

#include "engine/renderer/passes/vfx/vfx_oit_pass.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxOITPass, AccumPassDescDefaults) {
    VfxOITAccumPassDesc desc{};
    EXPECT_FALSE(desc.oitAccumPipeline.IsValid());
    EXPECT_FALSE(desc.accumTarget.IsValid());
    EXPECT_FALSE(desc.revealageTarget.IsValid());
    EXPECT_FALSE(desc.depthTarget.IsValid());
    EXPECT_EQ(desc.renderWidth, 0u);
    EXPECT_EQ(desc.renderHeight, 0u);
    EXPECT_TRUE(desc.emitters.empty());
}

TEST(VfxOITPass, ResolvePassDescDefaults) {
    VfxOITResolvePassDesc desc{};
    EXPECT_FALSE(desc.resolvePipeline.IsValid());
    EXPECT_FALSE(desc.accumTarget.IsValid());
    EXPECT_FALSE(desc.revealageTarget.IsValid());
    EXPECT_EQ(desc.accumSrvIndex, UINT32_MAX);
    EXPECT_EQ(desc.revealageSrvIndex, UINT32_MAX);
    EXPECT_FALSE(desc.colorTarget.IsValid());
    EXPECT_EQ(desc.renderWidth, 0u);
    EXPECT_EQ(desc.renderHeight, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// OIT emitter render data defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxOITPass, EmitterDataDefaults) {
    OITEmitterRenderData data{};
    EXPECT_EQ(data.positionSrvIdx, UINT32_MAX);
    EXPECT_EQ(data.sizeSrvIdx, UINT32_MAX);
    EXPECT_EQ(data.colorSrvIdx, UINT32_MAX);
    EXPECT_EQ(data.rotationSrvIdx, UINT32_MAX);
    EXPECT_FALSE(data.indirectDrawArgs.IsValid());
    EXPECT_FLOAT_EQ(data.distanceToCamera, 0.0f);
}
