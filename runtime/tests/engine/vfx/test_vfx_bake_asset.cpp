/// Unit tests for phase 7.18.0 - `.uzvfxbake` metadata asset.
///
/// Covers:
///   - Round-trip load/save preserves every field.
///   - IsWellFormed enforces rows*cols == frameCount + positive cell dims.
///   - Unknown format tag rejected.
///   - Missing duration derived from frameCount / fps.
///   - Missing atlas path or zero frame count fails well-formed check.
///
/// @ref phase7_7.18.0-vfx-baker-flipbook-tool

#include <gtest/gtest.h>

#include "engine/vfx/baker/vfx_bake_asset.h"

using namespace uz;

// ─── Happy-path load ───────────────────────────────────────────────────────

TEST(VfxBakeAsset, LoadFromStringPopulatesFields) {
    const char* json = R"({
        "format":      "uzvfxbake",
        "version":     1,
        "source":      "vfx/systems/fireball.uzvfxsys",
        "atlas_path":  "vfx/bakes/fireball_atlas.png",
        "frame_count": 36,
        "rows":        6,
        "cols":        6,
        "fps":         30.0,
        "duration_s":  1.2,
        "cell_width":  256,
        "cell_height": 256
    })";
    auto asset = VfxBakeAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->sourcePath, "vfx/systems/fireball.uzvfxsys");
    EXPECT_EQ(asset->atlasPath,  "vfx/bakes/fireball_atlas.png");
    EXPECT_EQ(asset->frameCount, 36u);
    EXPECT_EQ(asset->rows, 6u);
    EXPECT_EQ(asset->cols, 6u);
    EXPECT_FLOAT_EQ(asset->fps, 30.0f);
    EXPECT_FLOAT_EQ(asset->durationSeconds, 1.2f);
    EXPECT_EQ(asset->cellWidth, 256u);
    EXPECT_EQ(asset->cellHeight, 256u);
    EXPECT_TRUE(asset->IsWellFormed());
}

// ─── Validation ────────────────────────────────────────────────────────────

TEST(VfxBakeAsset, RejectsUnknownFormat) {
    EXPECT_EQ(VfxBakeAsset::LoadFromString(R"({"format":"something_else"})"), nullptr);
    EXPECT_EQ(VfxBakeAsset::LoadFromString("not json"), nullptr);
    EXPECT_EQ(VfxBakeAsset::LoadFromString("[]"), nullptr);  // wrong root type
}

TEST(VfxBakeAsset, WellFormedDetectsRowsColsMismatch) {
    VfxBakeAsset a;
    a.atlasPath  = "x.png";
    a.frameCount = 36u;
    a.rows = 6u;
    a.cols = 5u;   // 6*5 = 30, not 36
    a.cellWidth = 128u;
    a.cellHeight = 128u;
    EXPECT_FALSE(a.IsWellFormed());
}

TEST(VfxBakeAsset, WellFormedRequiresPositiveCellDims) {
    VfxBakeAsset a;
    a.atlasPath = "x.png";
    a.frameCount = 4;
    a.rows = 2;
    a.cols = 2;
    a.cellWidth = 0;        // invalid
    a.cellHeight = 128u;
    EXPECT_FALSE(a.IsWellFormed());
}

TEST(VfxBakeAsset, WellFormedRequiresAtlasPath) {
    VfxBakeAsset a;
    a.frameCount = 4;
    a.rows = 2; a.cols = 2;
    a.cellWidth = 16; a.cellHeight = 16;
    // atlasPath empty
    EXPECT_FALSE(a.IsWellFormed());
}

// ─── Duration derivation ──────────────────────────────────────────────────

TEST(VfxBakeAsset, DerivesDurationFromFrameCountOverFpsWhenMissing) {
    const char* json = R"({
        "format": "uzvfxbake",
        "version": 1,
        "atlas_path": "x.png",
        "frame_count": 60,
        "rows": 6,
        "cols": 10,
        "fps": 30.0,
        "cell_width": 128,
        "cell_height": 128
    })";
    auto asset = VfxBakeAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_NEAR(asset->durationSeconds, 2.0f, 1e-4f);  // 60/30
}

TEST(VfxBakeAsset, ExplicitDurationOverridesDerived) {
    const char* json = R"({
        "format": "uzvfxbake",
        "version": 1,
        "atlas_path": "x.png",
        "frame_count": 60,
        "rows": 6,
        "cols": 10,
        "fps": 30.0,
        "duration_s": 5.0,
        "cell_width": 128,
        "cell_height": 128
    })";
    auto asset = VfxBakeAsset::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_FLOAT_EQ(asset->durationSeconds, 5.0f);
}

// ─── Defaults ──────────────────────────────────────────────────────────────

TEST(VfxBakeAsset, DefaultsForMissingFields) {
    auto asset = VfxBakeAsset::LoadFromString(R"({"format":"uzvfxbake"})");
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->frameCount, 0u);
    EXPECT_EQ(asset->rows, 0u);
    EXPECT_FLOAT_EQ(asset->fps, 30.0f);
    EXPECT_TRUE(asset->atlasPath.empty());
    EXPECT_FALSE(asset->IsWellFormed());
}

// ─── Save round-trip ──────────────────────────────────────────────────────

TEST(VfxBakeAsset, SavedFileRoundTripsThroughLoad) {
    // SaveToFile requires a writable path; on CI this is the runtime's
    // working directory, which test executables inherit. Use a local
    // filename and delete it explicitly at the end.
    const std::string tmpPath = "test_vfx_bake_asset_roundtrip.uzvfxbake";

    VfxBakeAsset src;
    src.sourcePath      = "src.uzvfxsys";
    src.atlasPath       = "atlas.png";
    src.frameCount      = 16u;
    src.rows            = 4u;
    src.cols            = 4u;
    src.fps             = 24.0f;
    src.durationSeconds = 16.0f / 24.0f;
    src.cellWidth       = 64u;
    src.cellHeight      = 64u;
    ASSERT_TRUE(src.SaveToFile(tmpPath));

    auto reloaded = VfxBakeAsset::Load(tmpPath, nullptr);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->sourcePath, src.sourcePath);
    EXPECT_EQ(reloaded->atlasPath,  src.atlasPath);
    EXPECT_EQ(reloaded->frameCount, src.frameCount);
    EXPECT_EQ(reloaded->rows, src.rows);
    EXPECT_EQ(reloaded->cols, src.cols);
    EXPECT_NEAR(reloaded->fps, src.fps, 1e-4f);
    EXPECT_TRUE(reloaded->IsWellFormed());

    std::remove(tmpPath.c_str());
}
