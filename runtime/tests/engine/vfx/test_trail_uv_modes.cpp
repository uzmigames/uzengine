/// Unit tests for phase 8.12.0 — VFX trail UV authoring modes.
///
/// Validates `VfxComputeTrailV` at known arc lengths for each mode plus
/// the string round-trip helpers and JSON loader integration.
///
/// @ref engine/vfx/trails/trail_mesh_builder.h
/// @ref docs/engine/25-vfx/29-trail-uv-modes.md

#include <gtest/gtest.h>

#include "engine/vfx/trails/trail_asset.h"
#include "engine/vfx/trails/trail_mesh_builder.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// String round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailUVModeString, StretchRoundTrip) {
    VfxTrailUVMode m;
    EXPECT_TRUE(VfxTrailUVModeFromString("stretch", m));
    EXPECT_EQ(m, VfxTrailUVMode::Stretch);
    EXPECT_STREQ(VfxTrailUVModeToString(VfxTrailUVMode::Stretch), "stretch");
}

TEST(VfxTrailUVModeString, TiledRoundTrip) {
    VfxTrailUVMode m;
    EXPECT_TRUE(VfxTrailUVModeFromString("tiled", m));
    EXPECT_EQ(m, VfxTrailUVMode::Tiled);
    EXPECT_STREQ(VfxTrailUVModeToString(VfxTrailUVMode::Tiled), "tiled");
}

TEST(VfxTrailUVModeString, PerRibbonRoundTrip) {
    VfxTrailUVMode m;
    EXPECT_TRUE(VfxTrailUVModeFromString("per_ribbon", m));
    EXPECT_EQ(m, VfxTrailUVMode::PerRibbon);
    EXPECT_STREQ(VfxTrailUVModeToString(VfxTrailUVMode::PerRibbon), "per_ribbon");
}

TEST(VfxTrailUVModeString, UnknownStringRejected) {
    VfxTrailUVMode m = VfxTrailUVMode::Stretch;
    EXPECT_FALSE(VfxTrailUVModeFromString("unknown", m));
    EXPECT_EQ(m, VfxTrailUVMode::Stretch); // unchanged
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode: Stretch
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxComputeTrailV_Stretch, VEqualsTAtEndpoints) {
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::Stretch, 0.0f, 0, 1, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::Stretch, 1.0f, 0, 1, 1.0f), 1.0f);
}

TEST(VfxComputeTrailV_Stretch, VEqualsTAtMidpoint) {
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::Stretch, 0.5f, 0, 1, 1.0f), 0.5f);
}

TEST(VfxComputeTrailV_Stretch, IgnoresStrandIndex) {
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::Stretch, 0.25f, 3, 5, 1.0f), 0.25f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode: Tiled
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxComputeTrailV_Tiled, TilesPerRibbonMultipliesT) {
    // 4 tiles, t=0.5 → V=2 (halfway through 4 tiles)
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::Tiled, 0.5f, 0, 1, 4.0f), 2.0f);
    // 4 tiles, t=1.0 → V=4
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::Tiled, 1.0f, 0, 1, 4.0f), 4.0f);
}

TEST(VfxComputeTrailV_Tiled, ZeroTilesClampsToPositive) {
    // tilesPerRibbon == 0 would collapse V to 0; the clamp prevents that.
    const float v = VfxComputeTrailV(VfxTrailUVMode::Tiled, 1.0f, 0, 1, 0.0f);
    EXPECT_GT(v, 0.0f);
    EXPECT_LT(v, 0.001f); // near-zero but non-zero
}

TEST(VfxComputeTrailV_Tiled, FractionalTilesWork) {
    // 0.5 tiles means texture stretches over 2 ribbons worth
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::Tiled, 1.0f, 0, 1, 0.5f), 0.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode: PerRibbon
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxComputeTrailV_PerRibbon, SingleStrandDegeneratesToStretch) {
    // strandCount = 1 → V = (0 + t) / 1 = t
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::PerRibbon, 0.5f, 0, 1, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::PerRibbon, 1.0f, 0, 1, 1.0f), 1.0f);
}

TEST(VfxComputeTrailV_PerRibbon, TwoStrandsSplitVSpaceInHalves) {
    // strand 0: V in [0, 0.5]
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::PerRibbon, 0.0f, 0, 2, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::PerRibbon, 1.0f, 0, 2, 1.0f), 0.5f);
    // strand 1: V in [0.5, 1.0]
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::PerRibbon, 0.0f, 1, 2, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(VfxComputeTrailV(VfxTrailUVMode::PerRibbon, 1.0f, 1, 2, 1.0f), 1.0f);
}

TEST(VfxComputeTrailV_PerRibbon, FourStrandsProduceQuarterBands) {
    for (uint32_t s = 0; s < 4; ++s) {
        const float v0 = VfxComputeTrailV(VfxTrailUVMode::PerRibbon, 0.0f, s, 4, 1.0f);
        const float v1 = VfxComputeTrailV(VfxTrailUVMode::PerRibbon, 1.0f, s, 4, 1.0f);
        EXPECT_FLOAT_EQ(v0, static_cast<float>(s) / 4.0f);
        EXPECT_FLOAT_EQ(v1, static_cast<float>(s + 1) / 4.0f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON round-trip on TrailAsset
// ─────────────────────────────────────────────────────────────────────────────

TEST(TrailAssetUV, DefaultIsStretch) {
    const char* kJson = R"({
        "format": "uztrail",
        "version": 1,
        "material": "test.uzmat"
    })";
    auto asset = TrailAsset::LoadFromString(kJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->uvMode, VfxTrailUVMode::Stretch);
    EXPECT_FLOAT_EQ(asset->tilesPerRibbon, 1.0f);
}

TEST(TrailAssetUV, LoadsTiledModeWithTileCount) {
    const char* kJson = R"({
        "format": "uztrail",
        "version": 1,
        "material": "test.uzmat",
        "uv_mode": "tiled",
        "tiles_per_ribbon": 8.0
    })";
    auto asset = TrailAsset::LoadFromString(kJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->uvMode, VfxTrailUVMode::Tiled);
    EXPECT_FLOAT_EQ(asset->tilesPerRibbon, 8.0f);
}

TEST(TrailAssetUV, LoadsPerRibbonMode) {
    const char* kJson = R"({
        "format": "uztrail",
        "version": 1,
        "material": "test.uzmat",
        "uv_mode": "per_ribbon"
    })";
    auto asset = TrailAsset::LoadFromString(kJson);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->uvMode, VfxTrailUVMode::PerRibbon);
}

TEST(TrailAssetUV, UnknownUvModeRejected) {
    const char* kJson = R"({
        "format": "uztrail",
        "version": 1,
        "material": "test.uzmat",
        "uv_mode": "magic"
    })";
    auto asset = TrailAsset::LoadFromString(kJson);
    EXPECT_EQ(asset, nullptr);
}

TEST(TrailAssetUV, NegativeTilesRejected) {
    const char* kJson = R"({
        "format": "uztrail",
        "version": 1,
        "material": "test.uzmat",
        "uv_mode": "tiled",
        "tiles_per_ribbon": -1.0
    })";
    auto asset = TrailAsset::LoadFromString(kJson);
    EXPECT_EQ(asset, nullptr);
}
