/// Unit tests for flipbook_atlas.hlsli — shared atlas math used by both the
/// particle billboard VS and the flipbook debug overlay.
///
/// The HLSL helpers are mirrored below in C++ so they can be exercised on CPU
/// without a shader compiler. The mirrors MUST match the HLSL byte-for-byte:
///
///   ComputeFlipbookFrames(subImageIndex, totalFrames, frameA, frameB, lerpT)
///   AtlasUV(frameIndex, rows, cols, cornerUV)
///
/// @ref UE5 Niagara NiagaraSpriteVertexFactory.ush — SubUV VS pre-computation
/// @ref Nguyen 2007 "GPU Gems 3" ch.23 — SubUV sprite sheet animation

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace {

// ── CPU mirrors of flipbook_atlas.hlsli ──────────────────────────────────────

struct FramePair {
    uint32_t frameA;
    uint32_t frameB;
    float    lerpT;
};

FramePair ComputeFlipbookFrames(float subImageIndex, uint32_t totalFrames) {
    const uint32_t clamped = totalFrames < 1u ? 1u : totalFrames;
    const uint32_t floored = static_cast<uint32_t>(std::floor(subImageIndex));
    FramePair r{};
    r.frameA = floored % clamped;
    r.frameB = (r.frameA + 1u) % clamped;
    r.lerpT  = subImageIndex - std::floor(subImageIndex);
    return r;
}

std::array<float, 2> AtlasUV(
    uint32_t frameIndex, uint32_t rows, uint32_t cols,
    float cornerU, float cornerV) {
    const uint32_t colsC = cols < 1u ? 1u : cols;
    const uint32_t rowsC = rows < 1u ? 1u : rows;
    const float    invCols = 1.0f / static_cast<float>(colsC);
    const float    invRows = 1.0f / static_cast<float>(rowsC);
    const float    col = static_cast<float>(frameIndex % colsC);
    const float    row = static_cast<float>(frameIndex / colsC);
    return { (col + cornerU) * invCols, (row + cornerV) * invRows };
}

} // namespace

// ─── ComputeFlipbookFrames ──────────────────────────────────────────────────

TEST(FlipbookAtlas, ComputeFrames_FractionalIndexInterpolatesAdjacent) {
    const auto r = ComputeFlipbookFrames(5.3f, 36u);
    EXPECT_EQ(r.frameA, 5u);
    EXPECT_EQ(r.frameB, 6u);
    EXPECT_NEAR(r.lerpT, 0.3f, 1e-5f);
}

TEST(FlipbookAtlas, ComputeFrames_IntegerIndexYieldsZeroBlend) {
    const auto r = ComputeFlipbookFrames(12.0f, 36u);
    EXPECT_EQ(r.frameA, 12u);
    EXPECT_EQ(r.frameB, 13u);
    EXPECT_NEAR(r.lerpT, 0.0f, 1e-6f);
}

TEST(FlipbookAtlas, ComputeFrames_WrapsAtTotalFramesBoundary) {
    // subImageIndex = 35 = last frame of a 36-frame atlas. frameB should wrap
    // back to 0 so the PS lerp cross-fades from frame 35 into frame 0.
    const auto r = ComputeFlipbookFrames(35.0f, 36u);
    EXPECT_EQ(r.frameA, 35u);
    EXPECT_EQ(r.frameB, 0u);
}

TEST(FlipbookAtlas, ComputeFrames_WrapsSubImageIndexBeyondTotal) {
    // subImageIndex = 37 in a 36-frame atlas wraps to frame 1.
    const auto r = ComputeFlipbookFrames(37.25f, 36u);
    EXPECT_EQ(r.frameA, 1u);
    EXPECT_EQ(r.frameB, 2u);
    EXPECT_NEAR(r.lerpT, 0.25f, 1e-5f);
}

TEST(FlipbookAtlas, ComputeFrames_TotalFramesZeroClampsToOne) {
    // Guard: totalFrames==0 must clamp to 1 to avoid divide-by-zero.
    const auto r = ComputeFlipbookFrames(0.75f, 0u);
    EXPECT_EQ(r.frameA, 0u);
    EXPECT_EQ(r.frameB, 0u);
    EXPECT_NEAR(r.lerpT, 0.75f, 1e-5f);
}

// ─── AtlasUV ────────────────────────────────────────────────────────────────

TEST(FlipbookAtlas, AtlasUV_Frame0TopLeftOfFirstCell) {
    const auto uv = AtlasUV(0, 6, 6, 0.0f, 0.0f);
    EXPECT_NEAR(uv[0], 0.0f, 1e-6f);
    EXPECT_NEAR(uv[1], 0.0f, 1e-6f);
}

TEST(FlipbookAtlas, AtlasUV_Frame15TopLeftInSixBySixAtlas) {
    // Frame 15 in a 6x6 atlas: col = 15 % 6 = 3, row = 15 / 6 = 2.
    // Top-left corner (0,0) of that tile → (3/6, 2/6) = (0.5, 0.333…).
    const auto uv = AtlasUV(15, 6, 6, 0.0f, 0.0f);
    EXPECT_NEAR(uv[0], 3.0f / 6.0f, 1e-6f);
    EXPECT_NEAR(uv[1], 2.0f / 6.0f, 1e-6f);
}

TEST(FlipbookAtlas, AtlasUV_Frame15BottomRightInSixBySixAtlas) {
    // Bottom-right of the same tile (cornerUV=(1,1)) → (4/6, 3/6).
    const auto uv = AtlasUV(15, 6, 6, 1.0f, 1.0f);
    EXPECT_NEAR(uv[0], 4.0f / 6.0f, 1e-6f);
    EXPECT_NEAR(uv[1], 3.0f / 6.0f, 1e-6f);
}

TEST(FlipbookAtlas, AtlasUV_LastFrameCoversBottomRightTile) {
    // Frame 35 in a 6x6 atlas is col 5, row 5.
    // Bottom-right corner should land exactly at (1, 1).
    const auto uv = AtlasUV(35, 6, 6, 1.0f, 1.0f);
    EXPECT_NEAR(uv[0], 1.0f, 1e-6f);
    EXPECT_NEAR(uv[1], 1.0f, 1e-6f);
}

TEST(FlipbookAtlas, AtlasUV_NonSquareAtlas) {
    // 4 rows × 8 cols, frame 10 → col = 10 % 8 = 2, row = 10 / 8 = 1.
    const auto uv = AtlasUV(10, 4, 8, 0.0f, 0.0f);
    EXPECT_NEAR(uv[0], 2.0f / 8.0f, 1e-6f);
    EXPECT_NEAR(uv[1], 1.0f / 4.0f, 1e-6f);
}

TEST(FlipbookAtlas, AtlasUV_ZeroRowsOrColsClampsToOne) {
    // Misconfigured atlas must not divide by zero.
    const auto uv = AtlasUV(0, 0, 0, 0.5f, 0.5f);
    EXPECT_NEAR(uv[0], 0.5f, 1e-6f);
    EXPECT_NEAR(uv[1], 0.5f, 1e-6f);
}
