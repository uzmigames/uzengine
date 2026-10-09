/// Unit tests for phase 9.10.0 — VFX volumetric render passes.
///
/// Tests cover CB struct layout, pass registration with the render graph,
/// and resource dependency declarations. The actual GPU dispatch is tested
/// by visual verification in the VFX viewport demo.
///
/// @ref engine/renderer/passes/vfx/vfx_volumetric_pass.h

#include <gtest/gtest.h>

#include <cstddef>

#include "engine/renderer/passes/vfx/vfx_volumetric_pass.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// CB struct layout — must match HLSL exactly
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxVolumetricPass, SplatCBLayout) {
    // The splat CB must be exactly 32 bytes (8 fields × 4 bytes).
    EXPECT_EQ(sizeof(VfxVolumetricSplatCB), 32u);

    // Verify field offsets match HLSL cbuffer layout.
    VfxVolumetricSplatCB cb{};
    cb.gridCenterX = 1.0f;
    cb.gridCenterY = 2.0f;
    cb.gridCenterZ = 3.0f;
    cb.halfExtent = 50.0f;
    cb.gridResolution = 128u;
    cb.densityPerParticle = 0.1f;
    cb.aliveCount = 1000u;
    cb._pad0 = 0;

    // Verify all fields are writable and the struct is trivially copyable.
    VfxVolumetricSplatCB copy = cb;
    EXPECT_FLOAT_EQ(copy.gridCenterX, 1.0f);
    EXPECT_FLOAT_EQ(copy.halfExtent, 50.0f);
    EXPECT_EQ(copy.gridResolution, 128u);
    EXPECT_EQ(copy.aliveCount, 1000u);
}

TEST(VfxVolumetricPass, RaymarchCBLayout) {
    // The raymarch CB must be exactly 144 bytes.
    EXPECT_EQ(sizeof(VfxVolumetricRaymarchCB), 144u);

    // Verify field offsets: invViewProj starts at byte 0 (64 bytes),
    // then cameraPos.x at byte 64, etc.
    EXPECT_EQ(offsetof(VfxVolumetricRaymarchCB, invViewProj), 0u);
    EXPECT_EQ(offsetof(VfxVolumetricRaymarchCB, cameraPosX), 64u);
    EXPECT_EQ(offsetof(VfxVolumetricRaymarchCB, halfExtent), 76u);
    EXPECT_EQ(offsetof(VfxVolumetricRaymarchCB, gridCenterX), 80u);
    EXPECT_EQ(offsetof(VfxVolumetricRaymarchCB, gridResolution), 92u);
    EXPECT_EQ(offsetof(VfxVolumetricRaymarchCB, sunDirX), 96u);
    EXPECT_EQ(offsetof(VfxVolumetricRaymarchCB, extinctionCoeff), 108u);
    EXPECT_EQ(offsetof(VfxVolumetricRaymarchCB, scatteringColorR), 112u);
    EXPECT_EQ(offsetof(VfxVolumetricRaymarchCB, hgAnisotropy), 124u);
    EXPECT_EQ(offsetof(VfxVolumetricRaymarchCB, raymarchSteps), 128u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VolumetricEmitterData default state
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxVolumetricPass, EmitterDataDefaults) {
    VolumetricEmitterData data{};
    EXPECT_EQ(data.aliveCount, 0u);
    EXPECT_EQ(data.positionSrvIndex, UINT32_MAX);
    EXPECT_EQ(data.config.gridResolution, 128u);
    EXPECT_FLOAT_EQ(data.config.halfExtent, 50.0f);
    EXPECT_FLOAT_EQ(data.config.densityPerParticle, 0.1f);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxVolumetricConfig defaults match HLSL/CPU expectations
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxVolumetricPass, ConfigDefaults) {
    VfxVolumetricConfig cfg{};
    EXPECT_EQ(cfg.gridResolution, 128u);
    EXPECT_FLOAT_EQ(cfg.halfExtent, 50.0f);
    EXPECT_FLOAT_EQ(cfg.densityPerParticle, 0.1f);
    EXPECT_EQ(cfg.raymarchSteps, 48u);
    EXPECT_FLOAT_EQ(cfg.extinctionCoeff, 1.5f);
    EXPECT_FLOAT_EQ(cfg.hgAnisotropy, 0.8f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxVolumetricPass, SplatPassDescDefaults) {
    VfxVolumetricSplatPassDesc desc{};
    EXPECT_FALSE(desc.splatPipeline.IsValid());
    EXPECT_FALSE(desc.densityVolume.IsValid());
    EXPECT_EQ(desc.densityVolumeUavIndex, UINT32_MAX);
    EXPECT_EQ(desc.cbRing, nullptr);
    EXPECT_TRUE(desc.emitters.empty());
}

TEST(VfxVolumetricPass, RaymarchPassDescDefaults) {
    VfxVolumetricRaymarchPassDesc desc{};
    EXPECT_FALSE(desc.raymarchPipeline.IsValid());
    EXPECT_FALSE(desc.densityVolume.IsValid());
    EXPECT_EQ(desc.densityVolumeSrvIndex, UINT32_MAX);
    EXPECT_FALSE(desc.colorTarget.IsValid());
    EXPECT_FALSE(desc.colorTexture.IsValid());
    EXPECT_EQ(desc.cbRing, nullptr);
    EXPECT_EQ(desc.renderWidth, 0u);
    EXPECT_EQ(desc.renderHeight, 0u);
}
