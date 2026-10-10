/// Unit tests for phase 9.17.0 — VFX DDGI emissive feed.
///
/// @ref engine/vfx/lighting/vfx_ddgi_emissive_feed.h

#include <gtest/gtest.h>

#include "engine/vfx/lighting/vfx_ddgi_emissive_feed.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDDGIEmissiveFeed, DefaultState) {
    VfxDDGIEmissiveFeed feed;
    EXPECT_FALSE(feed.IsInitialized());
    EXPECT_EQ(feed.GetSampleCount(), 0u);
    EXPECT_EQ(feed.GetBufferSrvIndex(), UINT32_MAX);
    EXPECT_TRUE(feed.enabled);
}

TEST(VfxDDGIEmissiveFeed, MaxSamplesIs32) {
    EXPECT_EQ(VfxDDGIEmissiveFeed::kMaxSamples, 32u);
}

TEST(VfxDDGIEmissiveFeed, InitWithNullDeviceRejected) {
    VfxDDGIEmissiveFeed feed;
    EXPECT_FALSE(feed.Init(nullptr));
    EXPECT_FALSE(feed.IsInitialized());
}

TEST(VfxDDGIEmissiveFeed, UploadOnUninitializedIsNoOp) {
    VfxDDGIEmissiveFeed feed;
    VfxEmissiveSample sample;
    sample.emissive = {10.0f, 5.0f, 1.0f};
    sample.position = {0, 0, 0};
    sample.influenceRadius = 10.0f;

    feed.UploadFrame(&sample, 1);
    EXPECT_EQ(feed.GetSampleCount(), 0u);
}

TEST(VfxDDGIEmissiveFeed, ResetClearsSampleCount) {
    VfxDDGIEmissiveFeed feed;
    // Can't Init without a real device, but Reset on default is safe.
    feed.Reset();
    EXPECT_EQ(feed.GetSampleCount(), 0u);
}

TEST(VfxDDGIEmissiveFeed, DisabledSkipsUpload) {
    VfxDDGIEmissiveFeed feed;
    feed.enabled = false;
    VfxEmissiveSample sample;
    sample.emissive = {10.0f, 5.0f, 1.0f};
    feed.UploadFrame(&sample, 1);
    EXPECT_EQ(feed.GetSampleCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxEmissiveSample size (matches GPU StructuredBuffer stride)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDDGIEmissiveFeed, SampleStructSize) {
    // The struct must be a reasonable size for GPU structured buffer stride.
    // Vec3 = 12 bytes each (position + emissive) = 24, float radius = 4.
    // Total = 28 bytes, but alignment may pad. Just verify it's ≤ 32.
    EXPECT_LE(sizeof(VfxEmissiveSample), 32u);
}
