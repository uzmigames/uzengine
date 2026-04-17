/// Unit tests for the particle motion-vector computation.
///
/// The HLSL helper `ComputeMotionVector` in shaders/runtime/vfx/vfx_motion_vectors.hlsli
/// is tested here via a C++ mirror of the math so we get CPU-only coverage of:
///   - Perspective divide on current / previous clip-space positions.
///   - NDC → UV transform with Y flip (DirectX convention).
///   - Degenerate prev.w → zero output (no NaN / Inf leakage).
///   - Stationary particle → zero motion.
///   - Pure rightward motion → positive X, zero Y.
///   - Pure downward motion → zero X, positive Y (DirectX V increases downward).
///
/// All tests are CPU-only — no GPU hardware is required.
///
/// @ref Karis 2014 "High Quality Temporal Supersampling" — TAA motion vectors
/// @ref UE5 NiagaraSpriteVertexFactory motion-vector output

#include <gtest/gtest.h>

#include <cmath>

namespace {

/// Mirror of the HLSL `ComputeMotionVector` helper for CPU-side testing.
///
/// Must stay bit-for-bit equivalent to vfx_motion_vectors.hlsli. If the HLSL
/// version changes, this mirror must change too — the test catches the math,
/// not the per-backend codegen.
struct Float2 { float x = 0.0f, y = 0.0f; };
struct Float4 { float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f; };

Float2 ComputeMotionVectorCPU(Float4 currentCS, Float4 prevCS) {
    // Guard against degenerate previous-frame projection (w near zero).
    if (std::fabs(prevCS.w) < 1e-6f) {
        return {0.0f, 0.0f};
    }

    const Float2 currentNDC{ currentCS.x / currentCS.w, currentCS.y / currentCS.w };
    const Float2 prevNDC   { prevCS.x    / prevCS.w,    prevCS.y    / prevCS.w    };

    // NDC [-1,+1] → UV [0,1] with Y flipped for DirectX (V increases downward).
    const Float2 currentUV{ currentNDC.x * 0.5f + 0.5f, currentNDC.y * -0.5f + 0.5f };
    const Float2 prevUV   { prevNDC.x    * 0.5f + 0.5f, prevNDC.y    * -0.5f + 0.5f };

    return { currentUV.x - prevUV.x, currentUV.y - prevUV.y };
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Degenerate input
// ─────────────────────────────────────────────────────────────────────────────

/// When prevCS.w is near zero (first frame with identity prevViewProj and a
/// particle at the origin, or a particle behind the previous camera), the
/// helper must short-circuit to zero rather than propagate Inf / NaN.
TEST(VfxMotionVector, ZeroPrevW_ReturnsZero) {
    const Float4 currentCS{ 0.5f, 0.25f, 0.5f, 1.0f };
    const Float4 prevCS   { 1.0f, 1.0f,  1.0f, 0.0f }; // w=0 → degenerate

    const Float2 mv = ComputeMotionVectorCPU(currentCS, prevCS);

    EXPECT_FLOAT_EQ(mv.x, 0.0f);
    EXPECT_FLOAT_EQ(mv.y, 0.0f);
}

TEST(VfxMotionVector, NearZeroPrevW_ReturnsZero) {
    const Float4 currentCS{ 0.5f, 0.25f, 0.5f, 1.0f };
    const Float4 prevCS   { 1.0f, 1.0f,  1.0f, 1e-7f }; // below 1e-6 threshold

    const Float2 mv = ComputeMotionVectorCPU(currentCS, prevCS);

    EXPECT_FLOAT_EQ(mv.x, 0.0f);
    EXPECT_FLOAT_EQ(mv.y, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stationary particle
// ─────────────────────────────────────────────────────────────────────────────

/// A stationary particle (same clip-space position this frame and last)
/// produces a zero motion vector so TAA accumulates full history weight.
TEST(VfxMotionVector, IdenticalPositions_ReturnsZero) {
    const Float4 cs{ 0.25f, -0.5f, 0.8f, 2.0f };

    const Float2 mv = ComputeMotionVectorCPU(cs, cs);

    EXPECT_NEAR(mv.x, 0.0f, 1e-6f);
    EXPECT_NEAR(mv.y, 0.0f, 1e-6f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pure rightward motion
// ─────────────────────────────────────────────────────────────────────────────

/// Moving the particle in NDC from x=-0.5 → x=+0.5 with identical y/w should
/// produce a motion vector of (+0.5, 0) in UV space: current NDC 0.5 → UV 0.75,
/// previous NDC -0.5 → UV 0.25, delta 0.5.
TEST(VfxMotionVector, RightwardMotion_PositiveX_ZeroY) {
    const Float4 currentCS{  0.5f, 0.0f, 0.5f, 1.0f };
    const Float4 prevCS   { -0.5f, 0.0f, 0.5f, 1.0f };

    const Float2 mv = ComputeMotionVectorCPU(currentCS, prevCS);

    EXPECT_NEAR(mv.x, 0.5f, 1e-6f);
    EXPECT_NEAR(mv.y, 0.0f, 1e-6f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pure downward motion (DirectX V-increases-down convention)
// ─────────────────────────────────────────────────────────────────────────────

/// Moving the particle in NDC from y=+0.5 → y=-0.5 means downward on screen
/// (DirectX flips Y). Current UV.y = -0.5*-0.5+0.5 = 0.75, prev UV.y = 0.25,
/// delta = +0.5. X component stays zero.
TEST(VfxMotionVector, DownwardMotion_ZeroX_PositiveY) {
    const Float4 currentCS{ 0.0f, -0.5f, 0.5f, 1.0f };
    const Float4 prevCS   { 0.0f,  0.5f, 0.5f, 1.0f };

    const Float2 mv = ComputeMotionVectorCPU(currentCS, prevCS);

    EXPECT_NEAR(mv.x, 0.0f, 1e-6f);
    EXPECT_NEAR(mv.y, 0.5f, 1e-6f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Perspective divide correctness
// ─────────────────────────────────────────────────────────────────────────────

/// A particle with clip-space w=2 must have its x/y divided by 2 before the
/// NDC→UV transform. Verifies the perspective divide is applied to both
/// current and previous positions independently (different ws).
TEST(VfxMotionVector, DifferentWs_ApplyPerspectiveDivideIndependently) {
    // Current: x=1, w=2 → NDC.x=0.5 → UV.x=0.75
    const Float4 currentCS{ 1.0f, 0.0f, 0.5f, 2.0f };
    // Previous: x=-0.25, w=0.5 → NDC.x=-0.5 → UV.x=0.25
    const Float4 prevCS{ -0.25f, 0.0f, 0.5f, 0.5f };

    const Float2 mv = ComputeMotionVectorCPU(currentCS, prevCS);

    EXPECT_NEAR(mv.x, 0.5f, 1e-6f);
    EXPECT_NEAR(mv.y, 0.0f, 1e-6f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Output magnitude sanity
// ─────────────────────────────────────────────────────────────────────────────

/// A typical frame (60 fps, particle moving across 10% of the screen in one
/// frame) should produce a motion vector with magnitude roughly 0.1 — well
/// within the RG16Float range. Guards against accidental scale errors.
TEST(VfxMotionVector, TypicalMotionMagnitude_FitsRG16Float) {
    const Float4 currentCS{ 0.10f, 0.05f, 0.5f, 1.0f };
    const Float4 prevCS   { 0.00f, 0.00f, 0.5f, 1.0f };

    const Float2 mv = ComputeMotionVectorCPU(currentCS, prevCS);

    const float mag = std::sqrt(mv.x * mv.x + mv.y * mv.y);
    EXPECT_GT(mag, 0.0f);
    EXPECT_LT(mag, 1.0f) << "Motion magnitude must stay inside RG16Float dynamic range";
}
