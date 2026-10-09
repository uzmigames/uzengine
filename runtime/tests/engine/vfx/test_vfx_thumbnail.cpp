/// Unit tests for phase 8.7.0 — VFX thumbnail capture pipeline.
///
/// Validates the path resolver, config validation, capture state machine,
/// and deterministic frame selection without requiring a GPU.
///
/// @ref engine/vfx/baker/vfx_thumbnail.h
/// @ref docs/engine/25-vfx/24-thumbnail-capture.md

#include <gtest/gtest.h>

#include "engine/vfx/baker/vfx_thumbnail.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Path resolver
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailPath, ReplacesExtensionWithThumbPng) {
    EXPECT_EQ(VfxThumbnailPathForAsset("vfx/fireball.uzvfxsys"),
              "vfx/fireball.thumb.png");
}

TEST(VfxThumbnailPath, HandlesUzfxExtension) {
    EXPECT_EQ(VfxThumbnailPathForAsset("effects/smoke.uzfx"),
              "effects/smoke.thumb.png");
}

TEST(VfxThumbnailPath, NoExtension_AppendsSuffix) {
    EXPECT_EQ(VfxThumbnailPathForAsset("fire"), "fire.thumb.png");
}

TEST(VfxThumbnailPath, DotsInDirectory_HandledCorrectly) {
    EXPECT_EQ(VfxThumbnailPathForAsset("v2.0/effects/spark.uzfx"),
              "v2.0/effects/spark.thumb.png");
}

// ─────────────────────────────────────────────────────────────────────────────
// Config validation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailCapturer, PrepareCapture_ValidConfigSucceeds) {
    VfxThumbnailCapturer cap;
    VfxThumbnailConfig cfg;
    cfg.assetPath = "test.uzvfxsys";
    EXPECT_TRUE(cap.PrepareCapture(cfg));
    EXPECT_FLOAT_EQ(cap.GetStepDt(), 1.0f / 60.0f);
    EXPECT_EQ(cap.GetWidth(), 128u);
    EXPECT_EQ(cap.GetHeight(), 128u);
}

TEST(VfxThumbnailCapturer, PrepareCapture_ZeroFpsRejected) {
    VfxThumbnailCapturer cap;
    VfxThumbnailConfig cfg;
    cfg.assetPath = "test.uzvfxsys";
    cfg.fps = 0.0f;
    EXPECT_FALSE(cap.PrepareCapture(cfg));
}

TEST(VfxThumbnailCapturer, PrepareCapture_NegativeFpsRejected) {
    VfxThumbnailCapturer cap;
    VfxThumbnailConfig cfg;
    cfg.assetPath = "test.uzvfxsys";
    cfg.fps = -30.0f;
    EXPECT_FALSE(cap.PrepareCapture(cfg));
}

TEST(VfxThumbnailCapturer, PrepareCapture_ZeroResolutionRejected) {
    VfxThumbnailCapturer cap;
    VfxThumbnailConfig cfg;
    cfg.assetPath = "test.uzvfxsys";
    cfg.width = 0;
    EXPECT_FALSE(cap.PrepareCapture(cfg));
}

// ─────────────────────────────────────────────────────────────────────────────
// State machine — frame advance
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailCapturer, CaptureFrame0_ImmediatelyReady) {
    VfxThumbnailCapturer cap;
    VfxThumbnailConfig cfg;
    cfg.assetPath = "test.uzvfxsys";
    cfg.captureFrame = 0;
    EXPECT_TRUE(cap.PrepareCapture(cfg));
    EXPECT_TRUE(cap.IsReadyToCapture());
}

TEST(VfxThumbnailCapturer, AdvancesToTargetFrame) {
    VfxThumbnailCapturer cap;
    VfxThumbnailConfig cfg;
    cfg.assetPath = "test.uzvfxsys";
    cfg.captureFrame = 5;
    cap.PrepareCapture(cfg);

    EXPECT_FALSE(cap.IsReadyToCapture());
    for (uint32_t i = 0; i < 4; ++i) {
        cap.AdvanceFrame();
        EXPECT_FALSE(cap.IsReadyToCapture());
    }
    cap.AdvanceFrame();
    EXPECT_TRUE(cap.IsReadyToCapture());
    EXPECT_EQ(cap.GetCurrentTick(), 5u);
}

TEST(VfxThumbnailCapturer, MaxTicksCapsInfiniteLoop) {
    VfxThumbnailCapturer cap;
    VfxThumbnailConfig cfg;
    cfg.assetPath = "test.uzvfxsys";
    cfg.captureFrame = 999;
    cfg.maxTicks = 10;
    cap.PrepareCapture(cfg);

    for (uint32_t i = 0; i < 9; ++i) cap.AdvanceFrame();
    EXPECT_FALSE(cap.IsReadyToCapture());
    cap.AdvanceFrame();
    EXPECT_TRUE(cap.IsReadyToCapture());
    EXPECT_EQ(cap.GetCurrentTick(), 10u);
}

TEST(VfxThumbnailCapturer, AdvanceAfterReady_IsNoOp) {
    VfxThumbnailCapturer cap;
    VfxThumbnailConfig cfg;
    cfg.assetPath = "test.uzvfxsys";
    cfg.captureFrame = 1;
    cap.PrepareCapture(cfg);
    cap.AdvanceFrame();
    EXPECT_TRUE(cap.IsReadyToCapture());
    EXPECT_EQ(cap.GetCurrentTick(), 1u);
    // Extra advance does nothing.
    cap.AdvanceFrame();
    EXPECT_EQ(cap.GetCurrentTick(), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// FinishCapture — empty data
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailCapturer, FinishCapture_EmptyPngReturnsFailure) {
    VfxThumbnailCapturer cap;
    VfxThumbnailConfig cfg;
    cfg.assetPath = "test.uzvfxsys";
    cfg.captureFrame = 0;
    cap.PrepareCapture(cfg);

    auto result = cap.FinishCapture({});
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.outputPath, "test.thumb.png");
}

// ─────────────────────────────────────────────────────────────────────────────
// Deterministic seed
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailCapturer, DefaultSeedIs42) {
    VfxThumbnailCapturer cap;
    VfxThumbnailConfig cfg;
    cfg.assetPath = "test.uzvfxsys";
    cap.PrepareCapture(cfg);
    EXPECT_EQ(cap.GetRandomSeed(), 42u);
}

TEST(VfxThumbnailCapturer, CustomSeedRoundTrips) {
    VfxThumbnailCapturer cap;
    VfxThumbnailConfig cfg;
    cfg.assetPath = "test.uzvfxsys";
    cfg.randomSeed = 123456u;
    cap.PrepareCapture(cfg);
    EXPECT_EQ(cap.GetRandomSeed(), 123456u);
}
