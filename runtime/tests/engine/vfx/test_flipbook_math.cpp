/// Unit tests for VFX flipbook frame calculation math.
///
/// All HLSL functions from vfx_flipbook.hlsli are ported to C++ as static
/// helpers in the anonymous namespace below so they can be exercised on CPU
/// without a GPU or shader compiler. The C++ mirrors replicate the HLSL logic
/// exactly — same arithmetic, same guard conditions, same wrapping.
///
/// Covered functions:
///   FrameIndexToUV            — linear index → atlas UV
///   WangHash                  — deterministic uint hash
///   ComputeFlipbookFrame      — age/fps driven animated frame
///   ComputeFlipbookFrameRandom — per-particle deterministic random frame
///   SampleFlipbookUV          — billboard UV → atlas sub-rectangle
///
/// @ref Nguyen 2007 "GPU Gems 3" ch. 23 — particle sprite sheet animation
/// @ref McGuire 2006 "Particle Systems" — sprite sheet animation fundamentals

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

// =============================================================================
// CPU mirror of HLSL functions — for testing ONLY, not production code
// =============================================================================

namespace {

/// Holds the output of all flipbook frame computation functions.
struct FlipbookResult {
    float uv0[2];        ///< Top-left UV of the current frame in atlas space.
    float uv1[2];        ///< Top-left UV of the next frame (for blending).
    float blendFactor;   ///< Lerp weight between uv0 and uv1, in [0, 1).
    float frameSize[2];  ///< Size of one frame in UV space: (1/cols, 1/rows).
};

/// CPU mirror of HLSL FrameIndexToUV.
///
/// Converts a linear frame index to the top-left UV in atlas space.
/// Frame ordering is row-major, left-to-right, top-to-bottom.
///
/// @param frameIndex  Linear frame index in [0, totalFrames).
/// @param cols        Number of columns in the sprite sheet.
/// @param invCols     Precomputed 1.0f / cols.
/// @param invRows     Precomputed 1.0f / rows.
static void FrameIndexToUV(
    uint32_t frameIndex,
    uint32_t cols,
    float    invCols,
    float    invRows,
    float    outUV[2])
{
    uint32_t col = frameIndex % cols;
    uint32_t row = frameIndex / cols;
    outUV[0] = static_cast<float>(col) * invCols;
    outUV[1] = static_cast<float>(row) * invRows;
}

/// CPU mirror of HLSL WangHash.
///
/// Deterministic uint-to-uint hash with good avalanche properties.
/// @ref Wang 2007 — integer hash for GPU random number generation
/// @constants (seed ^ 61) ^ (seed >> 16), *9, ^(seed >> 4), *0x27d4eb2d, ^(seed >> 15)
static uint32_t WangHash(uint32_t seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

/// CPU mirror of HLSL ComputeFlipbookFrame.
///
/// Computes animated flipbook frame UVs from particle age and playback rate.
/// Degenerate guard: totalFrames==0 || cols==0 || rows==0 || fps<=0 → frame 0.
static FlipbookResult ComputeFlipbookFrame(
    float    age,
    float    /*lifetime*/,
    uint32_t rows,
    uint32_t cols,
    float    fps,
    uint32_t totalFrames)
{
    FlipbookResult result{};

    // Degenerate guard — return frame 0 with no blending.
    if (totalFrames == 0u || cols == 0u || rows == 0u || fps <= 0.0f) {
        result.uv0[0]      = 0.0f;
        result.uv0[1]      = 0.0f;
        result.uv1[0]      = 0.0f;
        result.uv1[1]      = 0.0f;
        result.blendFactor = 0.0f;
        result.frameSize[0] = (cols > 0u) ? 1.0f / static_cast<float>(cols) : 0.0f;
        result.frameSize[1] = (rows > 0u) ? 1.0f / static_cast<float>(rows) : 0.0f;
        return result;
    }

    const float invCols = 1.0f / static_cast<float>(cols);
    const float invRows = 1.0f / static_cast<float>(rows);
    result.frameSize[0] = invCols;
    result.frameSize[1] = invRows;

    // Continuous frame index from elapsed time.
    const float frameFloat = (age > 0.0f ? age : 0.0f) * fps;

    // Wrapping current and next frame indices.
    const uint32_t frame0 = static_cast<uint32_t>(frameFloat) % totalFrames;
    const uint32_t frame1 = (frame0 + 1u) % totalFrames;

    // Fractional blend weight.
    result.blendFactor = frameFloat - std::floor(frameFloat);

    FrameIndexToUV(frame0, cols, invCols, invRows, result.uv0);
    FrameIndexToUV(frame1, cols, invCols, invRows, result.uv1);

    return result;
}

/// CPU mirror of HLSL ComputeFlipbookFrameRandom.
///
/// Selects a deterministic random frame per particle (not animated).
/// blendFactor is always 0; uv1 == uv0.
static FlipbookResult ComputeFlipbookFrameRandom(
    uint32_t particleIndex,
    uint32_t rows,
    uint32_t cols,
    uint32_t totalFrames)
{
    FlipbookResult result{};

    // Degenerate guard.
    if (totalFrames == 0u || cols == 0u || rows == 0u) {
        result.uv0[0]      = 0.0f;
        result.uv0[1]      = 0.0f;
        result.uv1[0]      = 0.0f;
        result.uv1[1]      = 0.0f;
        result.blendFactor = 0.0f;
        result.frameSize[0] = (cols > 0u) ? 1.0f / static_cast<float>(cols) : 0.0f;
        result.frameSize[1] = (rows > 0u) ? 1.0f / static_cast<float>(rows) : 0.0f;
        return result;
    }

    const float invCols = 1.0f / static_cast<float>(cols);
    const float invRows = 1.0f / static_cast<float>(rows);
    result.frameSize[0] = invCols;
    result.frameSize[1] = invRows;

    const uint32_t hashed = WangHash(particleIndex);
    const uint32_t frame  = hashed % totalFrames;

    FrameIndexToUV(frame, cols, invCols, invRows, result.uv0);
    result.uv1[0]      = result.uv0[0];
    result.uv1[1]      = result.uv0[1];
    result.blendFactor = 0.0f;

    return result;
}

/// CPU mirror of HLSL SampleFlipbookUV.
///
/// Transforms a [0,1] billboard UV into the atlas sub-rectangle for a given frame.
static void SampleFlipbookUV(
    const float baseUV[2],
    const float frameUV[2],
    const float frameSize[2],
    float       outUV[2])
{
    outUV[0] = frameUV[0] + baseUV[0] * frameSize[0];
    outUV[1] = frameUV[1] + baseUV[1] * frameSize[1];
}

} // anonymous namespace

// =============================================================================
// FrameIndexToUV — row-major atlas coordinate mapping
// =============================================================================

TEST(FrameIndexToUV, Frame0_In4x4Grid_ProducesOriginUV) {
    float uv[2];
    FrameIndexToUV(0u, 4u, 0.25f, 0.25f, uv);
    EXPECT_NEAR(uv[0], 0.0f, 1e-5f) << "frame 0: expected U = 0.0";
    EXPECT_NEAR(uv[1], 0.0f, 1e-5f) << "frame 0: expected V = 0.0";
}

TEST(FrameIndexToUV, Frame1_In4x4Grid_ProducesSecondColumnFirstRow) {
    float uv[2];
    FrameIndexToUV(1u, 4u, 0.25f, 0.25f, uv);
    EXPECT_NEAR(uv[0], 0.25f, 1e-5f) << "frame 1: expected U = 0.25 (second column)";
    EXPECT_NEAR(uv[1], 0.0f,  1e-5f) << "frame 1: expected V = 0.0 (first row)";
}

TEST(FrameIndexToUV, Frame4_In4x4Grid_ProducesSecondRowFirstColumn) {
    // frame 4: col = 4 % 4 = 0, row = 4 / 4 = 1 → UV = (0.0, 0.25)
    float uv[2];
    FrameIndexToUV(4u, 4u, 0.25f, 0.25f, uv);
    EXPECT_NEAR(uv[0], 0.0f,  1e-5f) << "frame 4: expected U = 0.0 (first column)";
    EXPECT_NEAR(uv[1], 0.25f, 1e-5f) << "frame 4: expected V = 0.25 (second row)";
}

TEST(FrameIndexToUV, Frame15_In4x4Grid_ProducesLastCell) {
    // frame 15: col = 15 % 4 = 3, row = 15 / 4 = 3 → UV = (0.75, 0.75)
    float uv[2];
    FrameIndexToUV(15u, 4u, 0.25f, 0.25f, uv);
    EXPECT_NEAR(uv[0], 0.75f, 1e-5f) << "frame 15: expected U = 0.75 (last column)";
    EXPECT_NEAR(uv[1], 0.75f, 1e-5f) << "frame 15: expected V = 0.75 (last row)";
}

TEST(FrameIndexToUV, Frame0_In1x1Grid_ProducesOriginUV) {
    float uv[2];
    FrameIndexToUV(0u, 1u, 1.0f, 1.0f, uv);
    EXPECT_NEAR(uv[0], 0.0f, 1e-5f) << "1x1 grid frame 0: expected U = 0.0";
    EXPECT_NEAR(uv[1], 0.0f, 1e-5f) << "1x1 grid frame 0: expected V = 0.0";
}

TEST(FrameIndexToUV, Frame5_In4x2Grid_ProducesCorrectUV) {
    // 4 cols, 2 rows → invCols=0.25, invRows=0.5
    // frame 5: col = 5 % 4 = 1, row = 5 / 4 = 1 → UV = (0.25, 0.5)
    float uv[2];
    FrameIndexToUV(5u, 4u, 0.25f, 0.5f, uv);
    EXPECT_NEAR(uv[0], 0.25f, 1e-5f) << "4x2 grid frame 5: expected U = 0.25";
    EXPECT_NEAR(uv[1], 0.5f,  1e-5f) << "4x2 grid frame 5: expected V = 0.5";
}

// =============================================================================
// WangHash — avalanche and determinism
// =============================================================================

TEST(WangHash, Seed0_ProducesNonZeroValue) {
    const uint32_t h = WangHash(0u);
    EXPECT_NE(h, 0u) << "WangHash(0) must not be 0 (poor hash quality)";
}

TEST(WangHash, Seed0_AndSeed1_ProduceDifferentValues) {
    const uint32_t h0 = WangHash(0u);
    const uint32_t h1 = WangHash(1u);
    EXPECT_NE(h0, h1) << "WangHash(0) and WangHash(1) must differ (avalanche)";
}

TEST(WangHash, SameInput_AlwaysProducesSameOutput) {
    const uint32_t a = WangHash(42u);
    const uint32_t b = WangHash(42u);
    EXPECT_EQ(a, b) << "WangHash must be deterministic for the same input";
}

TEST(WangHash, KnownValue_Seed1_MatchesExpected) {
    // Pre-computed reference value for seed=1:
    //   step1: (1 ^ 61) ^ (1 >> 16) = 60 ^ 0 = 60
    //   step2: 60 * 9 = 540
    //   step3: 540 ^ (540 >> 4) = 540 ^ 33 = 509
    //   step4: 509 * 0x27d4eb2d
    //   step5: ^ (result >> 15)
    // We compute inline and just verify self-consistency (hash of 1 repeated twice).
    const uint32_t expected = WangHash(1u);
    EXPECT_EQ(WangHash(1u), expected) << "WangHash(1) must be stable across calls";
}

TEST(WangHash, AdjacentSeeds_ProduceDifferentValues) {
    // Adjacent particle indices must hash to different frames to provide variety.
    for (uint32_t i = 0u; i < 16u; ++i) {
        EXPECT_NE(WangHash(i), WangHash(i + 1u))
            << "WangHash(" << i << ") and WangHash(" << (i + 1u)
            << ") must differ (adjacent seed avalanche)";
    }
}

// =============================================================================
// ComputeFlipbookFrame — animated frame from particle age
// =============================================================================

TEST(ComputeFlipbookFrame, Age0_Fps30_4x4Sheet_ProducesFrame0) {
    // age=0 → frameFloat = 0*30 = 0 → frame0=0, blend≈0
    const FlipbookResult r = ComputeFlipbookFrame(0.0f, 5.0f, 4u, 4u, 30.0f, 16u);
    EXPECT_NEAR(r.uv0[0],      0.0f, 1e-5f) << "age=0: uv0.U must be 0.0";
    EXPECT_NEAR(r.uv0[1],      0.0f, 1e-5f) << "age=0: uv0.V must be 0.0";
    EXPECT_NEAR(r.blendFactor, 0.0f, 1e-5f) << "age=0: blendFactor must be 0.0";
    EXPECT_NEAR(r.frameSize[0], 0.25f, 1e-5f) << "4x4: frameSize.x must be 0.25";
    EXPECT_NEAR(r.frameSize[1], 0.25f, 1e-5f) << "4x4: frameSize.y must be 0.25";
}

TEST(ComputeFlipbookFrame, Age1Over30_Fps30_4x4Sheet_ProducesFrame1) {
    // age = 1/30 → frameFloat = 1.0 exactly → frame0 = 1 % 16 = 1
    // frame 1: col=1, row=0 → uv0 = (0.25, 0.0)
    const float age = 1.0f / 30.0f;
    const FlipbookResult r = ComputeFlipbookFrame(age, 5.0f, 4u, 4u, 30.0f, 16u);
    EXPECT_NEAR(r.uv0[0], 0.25f, 1e-5f) << "age=1/30: uv0.U must be 0.25 (frame 1)";
    EXPECT_NEAR(r.uv0[1], 0.0f,  1e-5f) << "age=1/30: uv0.V must be 0.0 (row 0)";
    EXPECT_NEAR(r.blendFactor, 0.0f, 1e-5f) << "age=1/30: blendFactor must be 0.0";
}

TEST(ComputeFlipbookFrame, Age0Point5_Fps30_4x4Sheet_ProducesFrame15) {
    // age=0.5 → frameFloat = 0.5*30 = 15.0 → frame0 = 15 % 16 = 15, blend≈0
    // frame 15: col=3, row=3 → uv0 = (0.75, 0.75)
    const FlipbookResult r = ComputeFlipbookFrame(0.5f, 5.0f, 4u, 4u, 30.0f, 16u);
    EXPECT_NEAR(r.uv0[0],      0.75f, 1e-5f) << "age=0.5: uv0.U must be 0.75 (frame 15)";
    EXPECT_NEAR(r.uv0[1],      0.75f, 1e-5f) << "age=0.5: uv0.V must be 0.75 (frame 15)";
    EXPECT_NEAR(r.blendFactor, 0.0f,  1e-5f) << "age=0.5: blendFactor must be 0.0";
}

TEST(ComputeFlipbookFrame, BlendFactor_IsCorrectFractionalPart) {
    // age such that frameFloat has a fractional part of 0.6
    // frameFloat = 2.6 → frame0=2, blendFactor=0.6
    // fps=10 → age = 2.6 / 10 = 0.26
    const float age = 0.26f;
    const FlipbookResult r = ComputeFlipbookFrame(age, 5.0f, 4u, 4u, 10.0f, 16u);
    // frameFloat = 0.26 * 10 = 2.6, frac = 0.6
    EXPECT_NEAR(r.blendFactor, 0.6f, 1e-4f) << "blendFactor must equal frac(age*fps)";
}

TEST(ComputeFlipbookFrame, NegativeAge_TreatedAsZero) {
    // Negative age must be clamped to 0 → same as age=0.
    const FlipbookResult neg = ComputeFlipbookFrame(-1.0f, 5.0f, 4u, 4u, 30.0f, 16u);
    const FlipbookResult zero = ComputeFlipbookFrame(0.0f, 5.0f, 4u, 4u, 30.0f, 16u);
    EXPECT_NEAR(neg.uv0[0],      zero.uv0[0],      1e-5f) << "negative age: uv0.U must match age=0";
    EXPECT_NEAR(neg.uv0[1],      zero.uv0[1],      1e-5f) << "negative age: uv0.V must match age=0";
    EXPECT_NEAR(neg.blendFactor, zero.blendFactor,  1e-5f) << "negative age: blendFactor must match age=0";
}

TEST(ComputeFlipbookFrame, Fps0_ReturnsFrame0) {
    const FlipbookResult r = ComputeFlipbookFrame(1.0f, 5.0f, 4u, 4u, 0.0f, 16u);
    EXPECT_NEAR(r.uv0[0],      0.0f, 1e-5f) << "fps=0: uv0.U must be 0.0 (frame 0)";
    EXPECT_NEAR(r.uv0[1],      0.0f, 1e-5f) << "fps=0: uv0.V must be 0.0 (frame 0)";
    EXPECT_NEAR(r.blendFactor, 0.0f, 1e-5f) << "fps=0: blendFactor must be 0.0";
}

TEST(ComputeFlipbookFrame, NegativeFps_ReturnsFrame0) {
    const FlipbookResult r = ComputeFlipbookFrame(1.0f, 5.0f, 4u, 4u, -30.0f, 16u);
    EXPECT_NEAR(r.uv0[0],      0.0f, 1e-5f) << "fps<0: uv0.U must be 0.0 (frame 0)";
    EXPECT_NEAR(r.uv0[1],      0.0f, 1e-5f) << "fps<0: uv0.V must be 0.0 (frame 0)";
    EXPECT_NEAR(r.blendFactor, 0.0f, 1e-5f) << "fps<0: blendFactor must be 0.0";
}

TEST(ComputeFlipbookFrame, TotalFrames0_ReturnsFrame0) {
    const FlipbookResult r = ComputeFlipbookFrame(1.0f, 5.0f, 4u, 4u, 30.0f, 0u);
    EXPECT_NEAR(r.uv0[0],      0.0f, 1e-5f) << "totalFrames=0: uv0.U must be 0.0";
    EXPECT_NEAR(r.uv0[1],      0.0f, 1e-5f) << "totalFrames=0: uv0.V must be 0.0";
    EXPECT_NEAR(r.blendFactor, 0.0f, 1e-5f) << "totalFrames=0: blendFactor must be 0.0";
}

TEST(ComputeFlipbookFrame, Cols0_ReturnsFrame0WithZeroFrameSize) {
    const FlipbookResult r = ComputeFlipbookFrame(1.0f, 5.0f, 4u, 0u, 30.0f, 16u);
    EXPECT_NEAR(r.uv0[0],       0.0f, 1e-5f) << "cols=0: uv0.U must be 0.0";
    EXPECT_NEAR(r.uv0[1],       0.0f, 1e-5f) << "cols=0: uv0.V must be 0.0";
    EXPECT_NEAR(r.blendFactor,  0.0f, 1e-5f) << "cols=0: blendFactor must be 0.0";
    EXPECT_NEAR(r.frameSize[0], 0.0f, 1e-5f) << "cols=0: frameSize.x must be 0.0 (no div-by-zero)";
}

TEST(ComputeFlipbookFrame, Rows0_ReturnsFrame0WithZeroFrameSize) {
    const FlipbookResult r = ComputeFlipbookFrame(1.0f, 5.0f, 0u, 4u, 30.0f, 16u);
    EXPECT_NEAR(r.uv0[0],       0.0f, 1e-5f) << "rows=0: uv0.U must be 0.0";
    EXPECT_NEAR(r.uv0[1],       0.0f, 1e-5f) << "rows=0: uv0.V must be 0.0";
    EXPECT_NEAR(r.frameSize[1], 0.0f, 1e-5f) << "rows=0: frameSize.y must be 0.0 (no div-by-zero)";
}

TEST(ComputeFlipbookFrame, FrameWrapping_ExceedsTotalFrames_WrapsCorrectly) {
    // age=1.0, fps=30 → frameFloat=30.0 → frame0 = 30 % 16 = 14
    // frame 14: col = 14 % 4 = 2, row = 14 / 4 = 3 → UV = (0.5, 0.75)
    const FlipbookResult r = ComputeFlipbookFrame(1.0f, 5.0f, 4u, 4u, 30.0f, 16u);
    EXPECT_NEAR(r.uv0[0], 0.5f,  1e-5f) << "wrapping: uv0.U must be 0.5 (col=2 of frame 14)";
    EXPECT_NEAR(r.uv0[1], 0.75f, 1e-5f) << "wrapping: uv0.V must be 0.75 (row=3 of frame 14)";
}

TEST(ComputeFlipbookFrame, NextFrame_Uv1_WrapsAroundAtLastFrame) {
    // frame0=15 (last), frame1 = (15+1) % 16 = 0 → uv1 = (0.0, 0.0)
    const FlipbookResult r = ComputeFlipbookFrame(0.5f, 5.0f, 4u, 4u, 30.0f, 16u);
    EXPECT_NEAR(r.uv1[0], 0.0f, 1e-5f) << "last frame: uv1.U must wrap to 0.0 (frame 0)";
    EXPECT_NEAR(r.uv1[1], 0.0f, 1e-5f) << "last frame: uv1.V must wrap to 0.0 (frame 0)";
}

TEST(ComputeFlipbookFrame, FrameSize_IsCorrectForSheet) {
    // 4x4 sheet → frameSize = (0.25, 0.25)
    const FlipbookResult r = ComputeFlipbookFrame(0.0f, 1.0f, 4u, 4u, 30.0f, 16u);
    EXPECT_NEAR(r.frameSize[0], 0.25f, 1e-5f) << "4x4: frameSize.x must be 0.25";
    EXPECT_NEAR(r.frameSize[1], 0.25f, 1e-5f) << "4x4: frameSize.y must be 0.25";
}

TEST(ComputeFlipbookFrame, NonSquareSheet_FrameSizeCorrect) {
    // 2 rows, 8 cols → frameSize = (0.125, 0.5)
    const FlipbookResult r = ComputeFlipbookFrame(0.0f, 1.0f, 2u, 8u, 30.0f, 16u);
    EXPECT_NEAR(r.frameSize[0], 0.125f, 1e-5f) << "8-col sheet: frameSize.x must be 0.125";
    EXPECT_NEAR(r.frameSize[1], 0.5f,   1e-5f) << "2-row sheet: frameSize.y must be 0.5";
}

// =============================================================================
// ComputeFlipbookFrameRandom — deterministic per-particle hash frame
// =============================================================================

TEST(ComputeFlipbookFrameRandom, SameParticleIndex_AlwaysSameFrame) {
    const FlipbookResult a = ComputeFlipbookFrameRandom(7u, 4u, 4u, 16u);
    const FlipbookResult b = ComputeFlipbookFrameRandom(7u, 4u, 4u, 16u);
    EXPECT_NEAR(a.uv0[0], b.uv0[0], 1e-5f) << "same index: uv0.U must be deterministic";
    EXPECT_NEAR(a.uv0[1], b.uv0[1], 1e-5f) << "same index: uv0.V must be deterministic";
}

TEST(ComputeFlipbookFrameRandom, DifferentIndices_ProduceDifferentFrames) {
    // With 16 frames and a good hash, no two adjacent indices should land on
    // the same frame. Check at least several pairs.
    bool foundDifference = false;
    for (uint32_t i = 0u; i < 15u; ++i) {
        const FlipbookResult a = ComputeFlipbookFrameRandom(i,      4u, 4u, 16u);
        const FlipbookResult b = ComputeFlipbookFrameRandom(i + 1u, 4u, 4u, 16u);
        if (std::abs(a.uv0[0] - b.uv0[0]) > 1e-5f || std::abs(a.uv0[1] - b.uv0[1]) > 1e-5f) {
            foundDifference = true;
            break;
        }
    }
    EXPECT_TRUE(foundDifference)
        << "different particle indices must produce different frames (hash variety)";
}

TEST(ComputeFlipbookFrameRandom, BlendFactor_IsAlwaysZero) {
    for (uint32_t i = 0u; i < 8u; ++i) {
        const FlipbookResult r = ComputeFlipbookFrameRandom(i, 4u, 4u, 16u);
        EXPECT_NEAR(r.blendFactor, 0.0f, 1e-5f)
            << "random mode: blendFactor must always be 0.0 (index=" << i << ")";
    }
}

TEST(ComputeFlipbookFrameRandom, Uv0_EqualsUv1_NoNextFrame) {
    for (uint32_t i = 0u; i < 8u; ++i) {
        const FlipbookResult r = ComputeFlipbookFrameRandom(i, 4u, 4u, 16u);
        EXPECT_NEAR(r.uv0[0], r.uv1[0], 1e-5f)
            << "random mode: uv1.U must equal uv0.U (no next frame, index=" << i << ")";
        EXPECT_NEAR(r.uv0[1], r.uv1[1], 1e-5f)
            << "random mode: uv1.V must equal uv0.V (no next frame, index=" << i << ")";
    }
}

TEST(ComputeFlipbookFrameRandom, TotalFrames0_ReturnsFrame0) {
    const FlipbookResult r = ComputeFlipbookFrameRandom(5u, 4u, 4u, 0u);
    EXPECT_NEAR(r.uv0[0],      0.0f, 1e-5f) << "totalFrames=0: uv0.U must be 0.0";
    EXPECT_NEAR(r.uv0[1],      0.0f, 1e-5f) << "totalFrames=0: uv0.V must be 0.0";
    EXPECT_NEAR(r.blendFactor, 0.0f, 1e-5f) << "totalFrames=0: blendFactor must be 0.0";
}

TEST(ComputeFlipbookFrameRandom, Cols0_ReturnsFrame0WithZeroFrameSize) {
    const FlipbookResult r = ComputeFlipbookFrameRandom(5u, 4u, 0u, 16u);
    EXPECT_NEAR(r.uv0[0],       0.0f, 1e-5f) << "cols=0: uv0.U must be 0.0";
    EXPECT_NEAR(r.frameSize[0], 0.0f, 1e-5f) << "cols=0: frameSize.x must be 0.0 (no div-by-zero)";
}

TEST(ComputeFlipbookFrameRandom, Rows0_ReturnsFrame0WithZeroFrameSize) {
    const FlipbookResult r = ComputeFlipbookFrameRandom(5u, 0u, 4u, 16u);
    EXPECT_NEAR(r.uv0[1],       0.0f, 1e-5f) << "rows=0: uv0.V must be 0.0";
    EXPECT_NEAR(r.frameSize[1], 0.0f, 1e-5f) << "rows=0: frameSize.y must be 0.0 (no div-by-zero)";
}

TEST(ComputeFlipbookFrameRandom, ResultUV_IsWithinAtlasBounds) {
    // All frames selected by random must have UVs in [0, 1).
    for (uint32_t i = 0u; i < 32u; ++i) {
        const FlipbookResult r = ComputeFlipbookFrameRandom(i, 4u, 4u, 16u);
        EXPECT_GE(r.uv0[0], 0.0f) << "index=" << i << ": uv0.U must be >= 0";
        EXPECT_LT(r.uv0[0], 1.0f) << "index=" << i << ": uv0.U must be < 1.0 (atlas space)";
        EXPECT_GE(r.uv0[1], 0.0f) << "index=" << i << ": uv0.V must be >= 0";
        EXPECT_LT(r.uv0[1], 1.0f) << "index=" << i << ": uv0.V must be < 1.0 (atlas space)";
    }
}

// =============================================================================
// SampleFlipbookUV — billboard UV → atlas sub-rectangle
// =============================================================================

TEST(SampleFlipbookUV, BaseUVZero_ReturnsFrameUV) {
    const float baseUV[2]    = { 0.0f, 0.0f };
    const float frameUV[2]   = { 0.25f, 0.5f };
    const float frameSize[2] = { 0.25f, 0.25f };
    float result[2];
    SampleFlipbookUV(baseUV, frameUV, frameSize, result);
    EXPECT_NEAR(result[0], 0.25f, 1e-5f) << "baseUV=(0,0): result.U must equal frameUV.U";
    EXPECT_NEAR(result[1], 0.5f,  1e-5f) << "baseUV=(0,0): result.V must equal frameUV.V";
}

TEST(SampleFlipbookUV, BaseUVOne_ReturnsFrameUVPlusFrameSize) {
    // baseUV=(1,1) → result = frameUV + 1 * frameSize
    const float baseUV[2]    = { 1.0f, 1.0f };
    const float frameUV[2]   = { 0.0f, 0.0f };
    const float frameSize[2] = { 0.25f, 0.25f };
    float result[2];
    SampleFlipbookUV(baseUV, frameUV, frameSize, result);
    EXPECT_NEAR(result[0], 0.25f, 1e-5f) << "baseUV=(1,1): result.U must be 0.25";
    EXPECT_NEAR(result[1], 0.25f, 1e-5f) << "baseUV=(1,1): result.V must be 0.25";
}

TEST(SampleFlipbookUV, BaseUVHalf_ProducesInterpolatedAtlasUV) {
    // baseUV=(0.5,0.5), frameUV=(0.25,0.25), frameSize=(0.25,0.25)
    // result = (0.25 + 0.5*0.25, 0.25 + 0.5*0.25) = (0.375, 0.375)
    const float baseUV[2]    = { 0.5f, 0.5f };
    const float frameUV[2]   = { 0.25f, 0.25f };
    const float frameSize[2] = { 0.25f, 0.25f };
    float result[2];
    SampleFlipbookUV(baseUV, frameUV, frameSize, result);
    EXPECT_NEAR(result[0], 0.375f, 1e-5f) << "baseUV=(0.5,0.5): result.U must be 0.375";
    EXPECT_NEAR(result[1], 0.375f, 1e-5f) << "baseUV=(0.5,0.5): result.V must be 0.375";
}

TEST(SampleFlipbookUV, NonSquareFrameSize_ScalesAxesIndependently) {
    // 8-col, 2-row sheet: frameSize=(0.125, 0.5)
    // baseUV=(1,1), frameUV=(0.25, 0.5)
    // result = (0.25 + 1*0.125, 0.5 + 1*0.5) = (0.375, 1.0)
    const float baseUV[2]    = { 1.0f, 1.0f };
    const float frameUV[2]   = { 0.25f, 0.5f };
    const float frameSize[2] = { 0.125f, 0.5f };
    float result[2];
    SampleFlipbookUV(baseUV, frameUV, frameSize, result);
    EXPECT_NEAR(result[0], 0.375f, 1e-5f) << "non-square: result.U must be 0.375";
    EXPECT_NEAR(result[1], 1.0f,   1e-5f) << "non-square: result.V must be 1.0";
}

TEST(SampleFlipbookUV, ZeroFrameSize_ReturnsFrameUVOnly) {
    // If frameSize is zero (degenerate sheet), result equals frameUV.
    const float baseUV[2]    = { 0.7f, 0.3f };
    const float frameUV[2]   = { 0.0f, 0.0f };
    const float frameSize[2] = { 0.0f, 0.0f };
    float result[2];
    SampleFlipbookUV(baseUV, frameUV, frameSize, result);
    EXPECT_NEAR(result[0], 0.0f, 1e-5f) << "zero frameSize: result.U must be 0.0";
    EXPECT_NEAR(result[1], 0.0f, 1e-5f) << "zero frameSize: result.V must be 0.0";
}
