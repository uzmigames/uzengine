/// Unit tests for TrailComponent and its ring-buffer simulation.
///
/// Covers: default construction values, Tick adding first point unconditionally,
/// Tick adding when distance >= minPointDistance, Tick NOT adding when below
/// threshold, Tick aging all points by dt, Tick removing expired tail points,
/// Tick recalculating distanceFromHead, Tick no-op when isActive=false, Clear,
/// IsEmpty, GetActivePointCount, GetTotalLength, GetColorAtNormalized (0, 0.5, 1,
/// clamping), GetWidthAtNormalized (0, 0.5, 1, clamping), and multi-Tick trails.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "engine/vfx/trails/trail_component.h"

#include <cmath>
#include <cstdint>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Fixture helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Constructs a TrailComponent with a small buffer and tight thresholds so that
/// exact distances can be asserted without floating-point noise from large scales.
TrailComponent MakeSmallTrail() {
    TrailComponent tc;
    tc.maxTrailPoints   = 16;
    tc.widthStart       = 1.0f;
    tc.widthEnd         = 0.0f;
    tc.colorStart       = Vec4{1.0f, 0.0f, 0.0f, 1.0f};
    tc.colorEnd         = Vec4{0.0f, 0.0f, 1.0f, 0.0f};
    tc.pointLifetime    = 5.0f;
    tc.minPointDistance = 1.0f;
    tc.isActive         = true;
    // Re-initialize the ring buffer to honour maxTrailPoints = 16.
    tc.points = TrailPointBuffer(tc.maxTrailPoints);
    return tc;
}

} // anonymous namespace

// =============================================================================
// Default construction
// =============================================================================

TEST(UZ_TrailComponent, DefaultConstruction_MaxTrailPoints_Is100) {
    TrailComponent tc;
    EXPECT_EQ(tc.maxTrailPoints, 100u);
}

TEST(UZ_TrailComponent, DefaultConstruction_WidthStart_Is0p5) {
    TrailComponent tc;
    EXPECT_FLOAT_EQ(tc.widthStart, 0.5f);
}

TEST(UZ_TrailComponent, DefaultConstruction_WidthEnd_Is0p05) {
    TrailComponent tc;
    EXPECT_FLOAT_EQ(tc.widthEnd, 0.05f);
}

TEST(UZ_TrailComponent, DefaultConstruction_UvScrollRate_IsZero) {
    TrailComponent tc;
    EXPECT_FLOAT_EQ(tc.uvScrollRate, 0.0f);
}

TEST(UZ_TrailComponent, DefaultConstruction_PointLifetime_Is2) {
    TrailComponent tc;
    EXPECT_FLOAT_EQ(tc.pointLifetime, 2.0f);
}

TEST(UZ_TrailComponent, DefaultConstruction_MinPointDistance_Is0p1) {
    TrailComponent tc;
    EXPECT_FLOAT_EQ(tc.minPointDistance, 0.1f);
}

TEST(UZ_TrailComponent, DefaultConstruction_ColorStart_IsOpaqueWhite) {
    TrailComponent tc;
    EXPECT_FLOAT_EQ(tc.colorStart.x, 1.0f);
    EXPECT_FLOAT_EQ(tc.colorStart.y, 1.0f);
    EXPECT_FLOAT_EQ(tc.colorStart.z, 1.0f);
    EXPECT_FLOAT_EQ(tc.colorStart.w, 1.0f);
}

TEST(UZ_TrailComponent, DefaultConstruction_ColorEnd_IsTransparentWhite) {
    TrailComponent tc;
    EXPECT_FLOAT_EQ(tc.colorEnd.x, 1.0f);
    EXPECT_FLOAT_EQ(tc.colorEnd.y, 1.0f);
    EXPECT_FLOAT_EQ(tc.colorEnd.z, 1.0f);
    EXPECT_FLOAT_EQ(tc.colorEnd.w, 0.0f);
}

TEST(UZ_TrailComponent, DefaultConstruction_SortOrder_IsZero) {
    TrailComponent tc;
    EXPECT_EQ(tc.sortOrder, 0);
}

TEST(UZ_TrailComponent, DefaultConstruction_IsActive_IsTrue) {
    TrailComponent tc;
    EXPECT_TRUE(tc.isActive);
}

TEST(UZ_TrailComponent, DefaultConstruction_IsEmpty_IsTrue) {
    TrailComponent tc;
    EXPECT_TRUE(tc.IsEmpty());
}

TEST(UZ_TrailComponent, DefaultConstruction_ActivePointCount_IsZero) {
    TrailComponent tc;
    EXPECT_EQ(tc.GetActivePointCount(), 0u);
}

TEST(UZ_TrailComponent, DefaultConstruction_TotalLength_IsZero) {
    TrailComponent tc;
    EXPECT_FLOAT_EQ(tc.GetTotalLength(), 0.0f);
}

TEST(UZ_TrailComponent, DefaultConstruction_RingBufferCapacity_MatchesMaxPoints) {
    TrailComponent tc;
    EXPECT_EQ(tc.points.Capacity(), tc.maxTrailPoints);
}

// =============================================================================
// Tick — first point emission (unconditional)
// =============================================================================

TEST(UZ_TrailComponent, Tick_FirstCall_AddsPointRegardlessOfDistance) {
    TrailComponent tc = MakeSmallTrail();
    // minPointDistance = 1.0, but the buffer is empty so Tick must emit.
    tc.Tick(0.016f, Vec3{0.0f, 0.0f, 0.0f});
    EXPECT_EQ(tc.GetActivePointCount(), 1u)
        << "First Tick must always emit a point, regardless of minPointDistance";
}

TEST(UZ_TrailComponent, Tick_FirstPoint_IsNotEmpty) {
    TrailComponent tc = MakeSmallTrail();
    tc.Tick(0.016f, Vec3{0.0f, 0.0f, 0.0f});
    EXPECT_FALSE(tc.IsEmpty());
}

TEST(UZ_TrailComponent, Tick_FirstPoint_PositionMatchesCurrentPosition) {
    TrailComponent tc = MakeSmallTrail();
    const Vec3 pos{3.0f, 4.0f, 5.0f};
    tc.Tick(0.016f, pos);
    EXPECT_FLOAT_EQ(tc.points.GetPoint(0).position.x, 3.0f);
    EXPECT_FLOAT_EQ(tc.points.GetPoint(0).position.y, 4.0f);
    EXPECT_FLOAT_EQ(tc.points.GetPoint(0).position.z, 5.0f);
}

TEST(UZ_TrailComponent, Tick_FirstPoint_WidthEqualsWidthStart) {
    TrailComponent tc = MakeSmallTrail();
    tc.widthStart = 0.75f;
    tc.Tick(0.016f, Vec3{0.0f, 0.0f, 0.0f});
    EXPECT_FLOAT_EQ(tc.points.GetPoint(0).width, 0.75f);
}

TEST(UZ_TrailComponent, Tick_FirstPoint_ColorEqualsColorStart) {
    TrailComponent tc = MakeSmallTrail();
    tc.colorStart = Vec4{0.1f, 0.2f, 0.3f, 0.4f};
    tc.Tick(0.016f, Vec3{0.0f, 0.0f, 0.0f});
    const Vec4& c = tc.points.GetPoint(0).color;
    EXPECT_NEAR(c.x, 0.1f, 1e-5f);
    EXPECT_NEAR(c.y, 0.2f, 1e-5f);
    EXPECT_NEAR(c.z, 0.3f, 1e-5f);
    EXPECT_NEAR(c.w, 0.4f, 1e-5f);
}

// =============================================================================
// Tick — point emission threshold
// =============================================================================

TEST(UZ_TrailComponent, Tick_MovedBeyondThreshold_AddsSecondPoint) {
    TrailComponent tc = MakeSmallTrail();
    tc.Tick(0.016f, Vec3{0.0f, 0.0f, 0.0f});  // first point at origin
    tc.Tick(0.016f, Vec3{2.0f, 0.0f, 0.0f});  // moved 2.0 >= minPointDistance(1.0)
    EXPECT_EQ(tc.GetActivePointCount(), 2u)
        << "Second point must be emitted when distance >= minPointDistance";
}

TEST(UZ_TrailComponent, Tick_MovedExactlyAtThreshold_AddsPoint) {
    TrailComponent tc = MakeSmallTrail();
    tc.minPointDistance = 1.0f;
    tc.Tick(0.016f, Vec3{0.0f, 0.0f, 0.0f});
    tc.Tick(0.016f, Vec3{1.0f, 0.0f, 0.0f}); // exactly 1.0 == minPointDistance
    EXPECT_EQ(tc.GetActivePointCount(), 2u)
        << "Point must be emitted when distance == minPointDistance";
}

TEST(UZ_TrailComponent, Tick_MovedBelowThreshold_DoesNotAddPoint) {
    TrailComponent tc = MakeSmallTrail();
    tc.minPointDistance = 1.0f;
    tc.Tick(0.016f, Vec3{0.0f, 0.0f, 0.0f});  // first point
    tc.Tick(0.016f, Vec3{0.5f, 0.0f, 0.0f});  // 0.5 < 1.0: no new point
    EXPECT_EQ(tc.GetActivePointCount(), 1u)
        << "No point must be emitted when distance < minPointDistance";
}

TEST(UZ_TrailComponent, Tick_StationaryMultipleTicks_OnlyFirstPointAdded) {
    TrailComponent tc = MakeSmallTrail();
    tc.minPointDistance = 0.5f;
    const Vec3 pos{0.0f, 0.0f, 0.0f};
    tc.Tick(0.016f, pos);
    tc.Tick(0.016f, pos);
    tc.Tick(0.016f, pos);
    EXPECT_EQ(tc.GetActivePointCount(), 1u)
        << "Stationary emitter must not add more than one point";
}

// =============================================================================
// Tick — age advancement
// =============================================================================

TEST(UZ_TrailComponent, Tick_AgesExistingPoints_ByDt) {
    TrailComponent tc = MakeSmallTrail();
    tc.Tick(0.0f, Vec3{0.0f, 0.0f, 0.0f}); // emit, age += 0
    tc.Tick(0.1f, Vec3{0.0f, 0.0f, 0.0f}); // age all existing by 0.1

    // The original point was aged during the second Tick — it should now be 0.1.
    // (The new point added in the second call starts at age=0 then gets +0.1
    // this same tick, so age = 0.1. Both points have age 0.1.)
    EXPECT_NEAR(tc.points.GetPoint(0).age, 0.1f, 1e-5f);
}

TEST(UZ_TrailComponent, Tick_AgesAllPoints_NotJustHead) {
    TrailComponent tc = MakeSmallTrail();
    // Emit two points far apart so both are stored.
    tc.Tick(0.0f, Vec3{0.0f, 0.0f, 0.0f});
    tc.Tick(0.0f, Vec3{5.0f, 0.0f, 0.0f});

    // Now tick with dt=0.5 without moving (below threshold, no new point added).
    // Both points should gain +0.5 age.
    tc.Tick(0.5f, Vec3{5.0f, 0.0f, 0.0f});

    // Head (newest): gained 0.5 from this tick only.
    // Tail (oldest): gained 0.5 from this tick (it already had some age from earlier ticks too).
    const float headAge = tc.points.GetPoint(0).age;
    const float tailAge = tc.points.GetPoint(tc.GetActivePointCount() - 1).age;
    EXPECT_GT(headAge, 0.0f) << "Head must have non-zero age after Tick(0.5)";
    EXPECT_GE(tailAge, headAge)
        << "Older tail point must have age >= head's age";
}

// =============================================================================
// Tick — lifetime expiry culling
// =============================================================================

TEST(UZ_TrailComponent, Tick_ExpiredTailPoint_IsRemoved) {
    TrailComponent tc = MakeSmallTrail();
    tc.pointLifetime    = 0.1f;
    tc.minPointDistance = 0.5f;

    tc.Tick(0.0f, Vec3{0.0f, 0.0f, 0.0f}); // P0, age=0 after tick
    // Move enough to emit a second point, age P0 to 0.05
    tc.Tick(0.05f, Vec3{1.0f, 0.0f, 0.0f}); // P1 head, P0 tail (age=0.05)
    EXPECT_EQ(tc.GetActivePointCount(), 2u);

    // Tick enough for the tail (P0) to exceed lifetime=0.1
    // P0 age after this tick: 0.05 + 0.1 = 0.15 > 0.1 — must be removed.
    tc.Tick(0.1f, Vec3{1.0f, 0.0f, 0.0f}); // no movement — no new point

    EXPECT_EQ(tc.GetActivePointCount(), 1u)
        << "Tail point with age > pointLifetime must be culled";
}

TEST(UZ_TrailComponent, Tick_AllPointsExpire_BufferBecomesEmpty) {
    TrailComponent tc = MakeSmallTrail();
    tc.pointLifetime    = 0.01f;
    tc.minPointDistance = 100.0f;

    tc.Tick(0.0f, Vec3{0.0f, 0.0f, 0.0f}); // emit one point
    tc.Tick(1.0f, Vec3{0.0f, 0.0f, 0.0f}); // age += 1.0 >> lifetime=0.01

    EXPECT_TRUE(tc.IsEmpty())
        << "All points must expire when age greatly exceeds pointLifetime";
    EXPECT_FLOAT_EQ(tc.GetTotalLength(), 0.0f);
}

// =============================================================================
// Tick — distanceFromHead recalculation
// =============================================================================

TEST(UZ_TrailComponent, Tick_TwoPoints_DistanceFromHeadIsZeroForHead) {
    TrailComponent tc = MakeSmallTrail();
    tc.Tick(0.0f, Vec3{0.0f, 0.0f, 0.0f});
    tc.Tick(0.0f, Vec3{3.0f, 0.0f, 0.0f}); // 3 units apart

    EXPECT_FLOAT_EQ(tc.points.GetPoint(0).distanceFromHead, 0.0f)
        << "Head always has distanceFromHead == 0";
}

TEST(UZ_TrailComponent, Tick_TwoPoints_TailDistanceEqualsSegmentLength) {
    TrailComponent tc = MakeSmallTrail();
    tc.Tick(0.0f, Vec3{0.0f, 0.0f, 0.0f});
    tc.Tick(0.0f, Vec3{3.0f, 0.0f, 0.0f});

    // Tail (index 1) is 3 units from head.
    EXPECT_NEAR(tc.points.GetPoint(1).distanceFromHead, 3.0f, 1e-4f)
        << "Tail distanceFromHead must equal the Euclidean segment length";
}

TEST(UZ_TrailComponent, Tick_ThreePoints_TotalLengthEqualsSum) {
    TrailComponent tc = MakeSmallTrail();
    tc.Tick(0.0f, Vec3{0.0f, 0.0f, 0.0f});
    tc.Tick(0.0f, Vec3{1.0f, 0.0f, 0.0f}); // segment 1: length 1
    tc.Tick(0.0f, Vec3{4.0f, 0.0f, 0.0f}); // segment 2: length 3, total 4

    EXPECT_NEAR(tc.GetTotalLength(), 4.0f, 1e-4f);
}

// =============================================================================
// Tick — isActive = false is a no-op
// =============================================================================

TEST(UZ_TrailComponent, Tick_WhenInactive_DoesNotAddPoint) {
    TrailComponent tc = MakeSmallTrail();
    tc.isActive = false;
    tc.Tick(0.016f, Vec3{100.0f, 0.0f, 0.0f});
    EXPECT_TRUE(tc.IsEmpty())
        << "Tick must be a no-op when isActive is false";
}

TEST(UZ_TrailComponent, Tick_WhenInactive_DoesNotAgePoints) {
    TrailComponent tc = MakeSmallTrail();
    // First emit a point while active.
    tc.Tick(0.0f, Vec3{0.0f, 0.0f, 0.0f});
    const float ageBeforeDeactivate = tc.points.GetPoint(0).age;

    tc.isActive = false;
    tc.Tick(1.0f, Vec3{0.0f, 0.0f, 0.0f}); // should do nothing

    EXPECT_FLOAT_EQ(tc.points.GetPoint(0).age, ageBeforeDeactivate)
        << "Existing points must not age when isActive is false";
}

TEST(UZ_TrailComponent, Tick_ReactivatedAfterInactive_EmitsNewPoint) {
    TrailComponent tc = MakeSmallTrail();
    tc.Tick(0.0f, Vec3{0.0f, 0.0f, 0.0f}); // first point

    tc.isActive = false;
    tc.Tick(0.0f, Vec3{100.0f, 0.0f, 0.0f}); // no-op
    EXPECT_EQ(tc.GetActivePointCount(), 1u);

    tc.isActive = true;
    tc.Tick(0.0f, Vec3{100.0f, 0.0f, 0.0f}); // now active again, 100 >= minDist(1)
    EXPECT_EQ(tc.GetActivePointCount(), 2u)
        << "Re-activating must allow new points to be emitted";
}

// =============================================================================
// Clear
// =============================================================================

TEST(UZ_TrailComponent, Clear_EmptiesBuffer) {
    TrailComponent tc = MakeSmallTrail();
    tc.Tick(0.0f, Vec3{0.0f, 0.0f, 0.0f});
    tc.Tick(0.0f, Vec3{5.0f, 0.0f, 0.0f});
    tc.Clear();
    EXPECT_TRUE(tc.IsEmpty());
}

TEST(UZ_TrailComponent, Clear_ResetsTotalLengthToZero) {
    TrailComponent tc = MakeSmallTrail();
    tc.Tick(0.0f, Vec3{0.0f, 0.0f, 0.0f});
    tc.Tick(0.0f, Vec3{5.0f, 0.0f, 0.0f});
    tc.Clear();
    EXPECT_FLOAT_EQ(tc.GetTotalLength(), 0.0f);
}

TEST(UZ_TrailComponent, Clear_NextTickEmitsFirstPointAgain) {
    TrailComponent tc = MakeSmallTrail();
    tc.Tick(0.0f, Vec3{0.0f, 0.0f, 0.0f});
    tc.Clear();

    // After clear, even a position within minPointDistance must emit a fresh first point.
    tc.Tick(0.0f, Vec3{0.01f, 0.0f, 0.0f}); // 0.01 < minPointDistance(1.0) — still emits
    EXPECT_EQ(tc.GetActivePointCount(), 1u)
        << "Post-Clear Tick must emit the first point unconditionally";
}

// =============================================================================
// GetColorAtNormalized
// =============================================================================

TEST(UZ_TrailComponent, GetColorAtNormalized_0_ReturnsColorStart) {
    TrailComponent tc;
    tc.colorStart = Vec4{1.0f, 0.0f, 0.0f, 1.0f};
    tc.colorEnd   = Vec4{0.0f, 0.0f, 1.0f, 0.0f};
    const Vec4 c = tc.GetColorAtNormalized(0.0f);
    EXPECT_NEAR(c.x, 1.0f, 1e-5f);
    EXPECT_NEAR(c.y, 0.0f, 1e-5f);
    EXPECT_NEAR(c.z, 0.0f, 1e-5f);
    EXPECT_NEAR(c.w, 1.0f, 1e-5f);
}

TEST(UZ_TrailComponent, GetColorAtNormalized_1_ReturnsColorEnd) {
    TrailComponent tc;
    tc.colorStart = Vec4{1.0f, 0.0f, 0.0f, 1.0f};
    tc.colorEnd   = Vec4{0.0f, 0.0f, 1.0f, 0.0f};
    const Vec4 c = tc.GetColorAtNormalized(1.0f);
    EXPECT_NEAR(c.x, 0.0f, 1e-5f);
    EXPECT_NEAR(c.z, 1.0f, 1e-5f);
    EXPECT_NEAR(c.w, 0.0f, 1e-5f);
}

TEST(UZ_TrailComponent, GetColorAtNormalized_0p5_ReturnsMidpoint) {
    TrailComponent tc;
    tc.colorStart = Vec4{0.0f, 0.0f, 0.0f, 0.0f};
    tc.colorEnd   = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    const Vec4 c = tc.GetColorAtNormalized(0.5f);
    EXPECT_NEAR(c.x, 0.5f, 1e-5f);
    EXPECT_NEAR(c.y, 0.5f, 1e-5f);
    EXPECT_NEAR(c.z, 0.5f, 1e-5f);
    EXPECT_NEAR(c.w, 0.5f, 1e-5f);
}

TEST(UZ_TrailComponent, GetColorAtNormalized_BelowZero_ClampsToStart) {
    TrailComponent tc;
    tc.colorStart = Vec4{1.0f, 0.0f, 0.0f, 1.0f};
    tc.colorEnd   = Vec4{0.0f, 0.0f, 1.0f, 0.0f};
    const Vec4 c = tc.GetColorAtNormalized(-0.5f);
    EXPECT_NEAR(c.x, 1.0f, 1e-5f)
        << "t < 0 must clamp to colorStart";
}

TEST(UZ_TrailComponent, GetColorAtNormalized_AboveOne_ClampsToEnd) {
    TrailComponent tc;
    tc.colorStart = Vec4{1.0f, 0.0f, 0.0f, 1.0f};
    tc.colorEnd   = Vec4{0.0f, 0.0f, 1.0f, 0.0f};
    const Vec4 c = tc.GetColorAtNormalized(2.0f);
    EXPECT_NEAR(c.x, 0.0f, 1e-5f)
        << "t > 1 must clamp to colorEnd";
    EXPECT_NEAR(c.z, 1.0f, 1e-5f);
}

// =============================================================================
// GetWidthAtNormalized
// =============================================================================

TEST(UZ_TrailComponent, GetWidthAtNormalized_0_ReturnsWidthStart) {
    TrailComponent tc;
    tc.widthStart = 2.0f;
    tc.widthEnd   = 0.2f;
    EXPECT_FLOAT_EQ(tc.GetWidthAtNormalized(0.0f), 2.0f);
}

TEST(UZ_TrailComponent, GetWidthAtNormalized_1_ReturnsWidthEnd) {
    TrailComponent tc;
    tc.widthStart = 2.0f;
    tc.widthEnd   = 0.2f;
    EXPECT_NEAR(tc.GetWidthAtNormalized(1.0f), 0.2f, 1e-5f);
}

TEST(UZ_TrailComponent, GetWidthAtNormalized_0p5_ReturnsMidpoint) {
    TrailComponent tc;
    tc.widthStart = 2.0f;
    tc.widthEnd   = 0.0f;
    EXPECT_NEAR(tc.GetWidthAtNormalized(0.5f), 1.0f, 1e-5f);
}

TEST(UZ_TrailComponent, GetWidthAtNormalized_BelowZero_ClampsToStart) {
    TrailComponent tc;
    tc.widthStart = 3.0f;
    tc.widthEnd   = 0.5f;
    EXPECT_FLOAT_EQ(tc.GetWidthAtNormalized(-1.0f), 3.0f)
        << "t < 0 must clamp to widthStart";
}

TEST(UZ_TrailComponent, GetWidthAtNormalized_AboveOne_ClampsToEnd) {
    TrailComponent tc;
    tc.widthStart = 3.0f;
    tc.widthEnd   = 0.5f;
    EXPECT_NEAR(tc.GetWidthAtNormalized(5.0f), 0.5f, 1e-5f)
        << "t > 1 must clamp to widthEnd";
}

// =============================================================================
// GetPoints() — const accessor
// =============================================================================

TEST(UZ_TrailComponent, GetPoints_ReturnsConstReference_SameAsPoints) {
    TrailComponent tc = MakeSmallTrail();
    tc.Tick(0.0f, Vec3{1.0f, 2.0f, 3.0f});

    const TrailPointBuffer& ref = tc.GetPoints();
    EXPECT_EQ(ref.Count(), 1u);
    EXPECT_FLOAT_EQ(ref.GetPoint(0).position.x, 1.0f);
}

// =============================================================================
// Multi-Tick trail build — progressive accumulation
// =============================================================================

TEST(UZ_TrailComponent, MultiTick_BuildsTrailProgressively) {
    TrailComponent tc = MakeSmallTrail();
    tc.minPointDistance = 1.0f;
    tc.pointLifetime    = 100.0f;

    // Move in steps of 2.0 (> minPointDistance=1) for 5 ticks.
    for (int i = 0; i < 5; ++i) {
        tc.Tick(0.016f, Vec3{static_cast<float>(i) * 2.0f, 0.0f, 0.0f});
    }

    EXPECT_EQ(tc.GetActivePointCount(), 5u)
        << "Each Tick that moves >= minPointDistance must add a point";
    EXPECT_GT(tc.GetTotalLength(), 0.0f)
        << "Total length must be positive after building the trail";
}

TEST(UZ_TrailComponent, MultiTick_DistanceFromHead_MonotonicallyIncreasing) {
    TrailComponent tc = MakeSmallTrail();
    tc.minPointDistance = 1.0f;
    tc.pointLifetime    = 100.0f;

    for (int i = 0; i < 4; ++i) {
        tc.Tick(0.016f, Vec3{static_cast<float>(i) * 2.0f, 0.0f, 0.0f});
    }

    const uint32_t n = tc.GetActivePointCount();
    ASSERT_GE(n, 2u);
    float prevDist = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        const float d = tc.points.GetPoint(i).distanceFromHead;
        EXPECT_GE(d, prevDist)
            << "distanceFromHead must be monotonically non-decreasing from head to tail";
        prevDist = d;
    }
}
