/// Unit tests for TrailPointBuffer.
///
/// Covers: construction with capacity, AddPoint (stores correctly, Count increments),
/// GetPoint(0) returns newest (head), GetPoint(count-1) returns oldest (tail),
/// ring buffer wrap-around when exceeding capacity, RemoveOldest removes tail,
/// IsFull/IsEmpty at capacity boundaries, Clear resets to empty, AddPoint overwrites
/// oldest when full, multiple add/remove cycles maintain correct order,
/// Capacity() returns configured size, CopyOrdered provides contiguous snapshot,
/// and zero-capacity edge case.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "engine/vfx/trails/trail_point.h"

#include <cstdint>
#include <vector>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Constructs a minimal TrailPoint at the given world-space x-coordinate.
/// All other fields are zeroed except position.x, which makes it easy to
/// identify which point is which in ordering assertions.
TrailPoint MakePoint(float x) {
    TrailPoint p{};
    p.position        = Vec3{x, 0.0f, 0.0f};
    p.direction       = Vec3{0.0f, 1.0f, 0.0f};
    p.width           = 1.0f;
    p.color           = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    p.age             = 0.0f;
    p.distanceFromHead = 0.0f;
    return p;
}

} // anonymous namespace

// =============================================================================
// Construction
// =============================================================================

TEST(UZ_TrailPointBuffer, Constructor_Capacity4_CorrectInitialState) {
    TrailPointBuffer buf(4);
    EXPECT_EQ(buf.Capacity(), 4u);
    EXPECT_EQ(buf.Count(), 0u);
    EXPECT_TRUE(buf.IsEmpty());
    EXPECT_FALSE(buf.IsFull());
}

TEST(UZ_TrailPointBuffer, Constructor_Capacity1_CorrectInitialState) {
    TrailPointBuffer buf(1);
    EXPECT_EQ(buf.Capacity(), 1u);
    EXPECT_EQ(buf.Count(), 0u);
    EXPECT_TRUE(buf.IsEmpty());
}

TEST(UZ_TrailPointBuffer, Constructor_Capacity0_IsEmptyAndNotFull) {
    TrailPointBuffer buf(0);
    EXPECT_EQ(buf.Capacity(), 0u);
    EXPECT_EQ(buf.Count(), 0u);
    EXPECT_TRUE(buf.IsEmpty());
    EXPECT_FALSE(buf.IsFull());
}

TEST(UZ_TrailPointBuffer, Constructor_LargeCapacity_CountIsZero) {
    TrailPointBuffer buf(512);
    EXPECT_EQ(buf.Capacity(), 512u);
    EXPECT_EQ(buf.Count(), 0u);
}

// =============================================================================
// AddPoint — count and retrieval
// =============================================================================

TEST(UZ_TrailPointBuffer, AddPoint_SinglePoint_CountBecomesOne) {
    TrailPointBuffer buf(8);
    buf.AddPoint(MakePoint(1.0f));
    EXPECT_EQ(buf.Count(), 1u);
    EXPECT_FALSE(buf.IsEmpty());
}

TEST(UZ_TrailPointBuffer, AddPoint_ThreePoints_CountIsThree) {
    TrailPointBuffer buf(8);
    buf.AddPoint(MakePoint(1.0f));
    buf.AddPoint(MakePoint(2.0f));
    buf.AddPoint(MakePoint(3.0f));
    EXPECT_EQ(buf.Count(), 3u);
}

TEST(UZ_TrailPointBuffer, AddPoint_StoresValues_CorrectlyRetrieved) {
    TrailPointBuffer buf(4);
    TrailPoint p = MakePoint(42.0f);
    p.age   = 0.25f;
    p.width = 0.8f;
    p.color = Vec4{0.1f, 0.2f, 0.3f, 0.4f};
    buf.AddPoint(p);

    const TrailPoint& got = buf.GetPoint(0);
    EXPECT_FLOAT_EQ(got.position.x, 42.0f);
    EXPECT_FLOAT_EQ(got.age, 0.25f);
    EXPECT_FLOAT_EQ(got.width, 0.8f);
    EXPECT_FLOAT_EQ(got.color.x, 0.1f);
    EXPECT_FLOAT_EQ(got.color.y, 0.2f);
    EXPECT_FLOAT_EQ(got.color.z, 0.3f);
    EXPECT_FLOAT_EQ(got.color.w, 0.4f);
}

// =============================================================================
// GetPoint — head is newest (index 0), tail is oldest (index Count-1)
// =============================================================================

TEST(UZ_TrailPointBuffer, GetPoint_Index0_IsNewest) {
    // Add points A, B, C in that order. C is the newest.
    TrailPointBuffer buf(8);
    buf.AddPoint(MakePoint(10.0f)); // A — oldest
    buf.AddPoint(MakePoint(20.0f)); // B
    buf.AddPoint(MakePoint(30.0f)); // C — newest (head)

    EXPECT_FLOAT_EQ(buf.GetPoint(0).position.x, 30.0f)
        << "Index 0 must be the most recently added point (head)";
}

TEST(UZ_TrailPointBuffer, GetPoint_LastIndex_IsOldest) {
    TrailPointBuffer buf(8);
    buf.AddPoint(MakePoint(10.0f)); // A — oldest
    buf.AddPoint(MakePoint(20.0f)); // B
    buf.AddPoint(MakePoint(30.0f)); // C — newest

    const uint32_t lastIdx = buf.Count() - 1u;
    EXPECT_FLOAT_EQ(buf.GetPoint(lastIdx).position.x, 10.0f)
        << "Last index must be the oldest point (tail)";
}

TEST(UZ_TrailPointBuffer, GetPoint_MiddleIndex_IsIntermediatePoint) {
    TrailPointBuffer buf(8);
    buf.AddPoint(MakePoint(1.0f));
    buf.AddPoint(MakePoint(2.0f));
    buf.AddPoint(MakePoint(3.0f));
    buf.AddPoint(MakePoint(4.0f));

    // Order: 4(newest) 3 2 1(oldest)
    EXPECT_FLOAT_EQ(buf.GetPoint(0).position.x, 4.0f);
    EXPECT_FLOAT_EQ(buf.GetPoint(1).position.x, 3.0f);
    EXPECT_FLOAT_EQ(buf.GetPoint(2).position.x, 2.0f);
    EXPECT_FLOAT_EQ(buf.GetPoint(3).position.x, 1.0f);
}

// =============================================================================
// IsFull / IsEmpty — boundary conditions
// =============================================================================

TEST(UZ_TrailPointBuffer, IsFull_ReturnsFalse_WhenOneSlotRemains) {
    TrailPointBuffer buf(4);
    buf.AddPoint(MakePoint(1.0f));
    buf.AddPoint(MakePoint(2.0f));
    buf.AddPoint(MakePoint(3.0f));
    EXPECT_FALSE(buf.IsFull());
}

TEST(UZ_TrailPointBuffer, IsFull_ReturnsTrue_WhenAtCapacity) {
    TrailPointBuffer buf(4);
    buf.AddPoint(MakePoint(1.0f));
    buf.AddPoint(MakePoint(2.0f));
    buf.AddPoint(MakePoint(3.0f));
    buf.AddPoint(MakePoint(4.0f));
    EXPECT_TRUE(buf.IsFull());
}

TEST(UZ_TrailPointBuffer, IsEmpty_ReturnsTrue_AfterConstruction) {
    TrailPointBuffer buf(8);
    EXPECT_TRUE(buf.IsEmpty());
}

TEST(UZ_TrailPointBuffer, IsEmpty_ReturnsFalse_AfterOneAdd) {
    TrailPointBuffer buf(8);
    buf.AddPoint(MakePoint(1.0f));
    EXPECT_FALSE(buf.IsEmpty());
}

// =============================================================================
// Ring buffer wrap-around — AddPoint overwrites oldest when full
// =============================================================================

TEST(UZ_TrailPointBuffer, AddPoint_WhenFull_OverwritesOldest_CountStaysAtCapacity) {
    // Capacity = 3. Fill: A B C. Add D — A is overwritten.
    TrailPointBuffer buf(3);
    buf.AddPoint(MakePoint(1.0f)); // A
    buf.AddPoint(MakePoint(2.0f)); // B
    buf.AddPoint(MakePoint(3.0f)); // C — full

    buf.AddPoint(MakePoint(4.0f)); // D — A must be dropped

    EXPECT_EQ(buf.Count(), 3u)
        << "Count must stay at capacity after wrap-around";
    EXPECT_TRUE(buf.IsFull());
}

TEST(UZ_TrailPointBuffer, AddPoint_WhenFull_NewPointIsHead_OldestIsExpelled) {
    TrailPointBuffer buf(3);
    buf.AddPoint(MakePoint(1.0f)); // A
    buf.AddPoint(MakePoint(2.0f)); // B
    buf.AddPoint(MakePoint(3.0f)); // C

    buf.AddPoint(MakePoint(4.0f)); // D replaces A

    // Order after wrap: D(newest) C B  (A is gone)
    EXPECT_FLOAT_EQ(buf.GetPoint(0).position.x, 4.0f)
        << "Newest point (D) must be at index 0 after wrap";
    EXPECT_FLOAT_EQ(buf.GetPoint(1).position.x, 3.0f);
    EXPECT_FLOAT_EQ(buf.GetPoint(2).position.x, 2.0f)
        << "Oldest point (B) is at tail; A has been overwritten";
}

TEST(UZ_TrailPointBuffer, AddPoint_MultipleWraps_MaintainsCorrectOrder) {
    // Capacity = 2. Add E1..E5. After wrap, only the 2 newest survive.
    TrailPointBuffer buf(2);
    for (float x = 1.0f; x <= 5.0f; x += 1.0f) {
        buf.AddPoint(MakePoint(x));
    }

    EXPECT_EQ(buf.Count(), 2u);
    EXPECT_FLOAT_EQ(buf.GetPoint(0).position.x, 5.0f)
        << "Head must be the very last added point";
    EXPECT_FLOAT_EQ(buf.GetPoint(1).position.x, 4.0f)
        << "Tail must be second-to-last added point";
}

TEST(UZ_TrailPointBuffer, AddPoint_ZeroCapacity_IsNoOp) {
    TrailPointBuffer buf(0);
    buf.AddPoint(MakePoint(1.0f)); // must not crash
    EXPECT_EQ(buf.Count(), 0u);
    EXPECT_TRUE(buf.IsEmpty());
}

// =============================================================================
// RemoveOldest
// =============================================================================

TEST(UZ_TrailPointBuffer, RemoveOldest_SinglePoint_BufferBecomesEmpty) {
    TrailPointBuffer buf(4);
    buf.AddPoint(MakePoint(1.0f));
    buf.RemoveOldest();
    EXPECT_EQ(buf.Count(), 0u);
    EXPECT_TRUE(buf.IsEmpty());
}

TEST(UZ_TrailPointBuffer, RemoveOldest_ThreePoints_TailIsRemovedHeadPreserved) {
    TrailPointBuffer buf(8);
    buf.AddPoint(MakePoint(10.0f)); // tail
    buf.AddPoint(MakePoint(20.0f)); // middle
    buf.AddPoint(MakePoint(30.0f)); // head

    buf.RemoveOldest(); // removes 10.0f

    EXPECT_EQ(buf.Count(), 2u);
    EXPECT_FLOAT_EQ(buf.GetPoint(0).position.x, 30.0f)
        << "Head must be unchanged after removing oldest";
    EXPECT_FLOAT_EQ(buf.GetPoint(1).position.x, 20.0f)
        << "New tail must be the previously middle point";
}

TEST(UZ_TrailPointBuffer, RemoveOldest_DecreasesCount) {
    TrailPointBuffer buf(8);
    buf.AddPoint(MakePoint(1.0f));
    buf.AddPoint(MakePoint(2.0f));
    buf.AddPoint(MakePoint(3.0f));

    EXPECT_EQ(buf.Count(), 3u);
    buf.RemoveOldest();
    EXPECT_EQ(buf.Count(), 2u);
    buf.RemoveOldest();
    EXPECT_EQ(buf.Count(), 1u);
}

TEST(UZ_TrailPointBuffer, RemoveOldest_OnEmptyBuffer_IsNoOp) {
    TrailPointBuffer buf(4);
    buf.RemoveOldest(); // must not crash
    EXPECT_EQ(buf.Count(), 0u);
}

TEST(UZ_TrailPointBuffer, RemoveOldest_RepeatedUntilEmpty_NoCrash) {
    TrailPointBuffer buf(4);
    buf.AddPoint(MakePoint(1.0f));
    buf.AddPoint(MakePoint(2.0f));
    buf.RemoveOldest();
    buf.RemoveOldest();
    buf.RemoveOldest(); // extra call on empty — must not crash
    EXPECT_EQ(buf.Count(), 0u);
}

// =============================================================================
// Clear
// =============================================================================

TEST(UZ_TrailPointBuffer, Clear_ResetsCountToZero) {
    TrailPointBuffer buf(8);
    buf.AddPoint(MakePoint(1.0f));
    buf.AddPoint(MakePoint(2.0f));
    buf.AddPoint(MakePoint(3.0f));
    buf.Clear();
    EXPECT_EQ(buf.Count(), 0u);
}

TEST(UZ_TrailPointBuffer, Clear_SetsIsEmptyToTrue) {
    TrailPointBuffer buf(8);
    buf.AddPoint(MakePoint(1.0f));
    buf.Clear();
    EXPECT_TRUE(buf.IsEmpty());
}

TEST(UZ_TrailPointBuffer, Clear_PreservesCapacity) {
    TrailPointBuffer buf(16);
    buf.AddPoint(MakePoint(1.0f));
    buf.AddPoint(MakePoint(2.0f));
    buf.Clear();
    EXPECT_EQ(buf.Capacity(), 16u);
}

TEST(UZ_TrailPointBuffer, Clear_AfterClear_NewPointsAreStoredCorrectly) {
    TrailPointBuffer buf(4);
    buf.AddPoint(MakePoint(10.0f));
    buf.AddPoint(MakePoint(20.0f));
    buf.Clear();

    buf.AddPoint(MakePoint(99.0f));
    EXPECT_EQ(buf.Count(), 1u);
    EXPECT_FLOAT_EQ(buf.GetPoint(0).position.x, 99.0f)
        << "Post-clear AddPoint must store at head (index 0)";
}

TEST(UZ_TrailPointBuffer, Clear_OnAlreadyEmptyBuffer_IsNoOp) {
    TrailPointBuffer buf(4);
    buf.Clear(); // must not crash
    EXPECT_EQ(buf.Count(), 0u);
    EXPECT_EQ(buf.Capacity(), 4u);
}

// =============================================================================
// Multiple add/remove cycles — order invariant
// =============================================================================

TEST(UZ_TrailPointBuffer, AddRemoveCycles_OrderIsConsistentlyNewestFirst) {
    TrailPointBuffer buf(4);

    // Cycle 1
    buf.AddPoint(MakePoint(1.0f));
    buf.AddPoint(MakePoint(2.0f));
    buf.AddPoint(MakePoint(3.0f));
    buf.RemoveOldest(); // removes 1.0
    EXPECT_FLOAT_EQ(buf.GetPoint(0).position.x, 3.0f);
    EXPECT_FLOAT_EQ(buf.GetPoint(1).position.x, 2.0f);

    // Cycle 2 — add two more to fill capacity (4 - 2 existing = 2 free slots)
    buf.AddPoint(MakePoint(4.0f));
    buf.AddPoint(MakePoint(5.0f));
    // Buffer: 5(head) 4 3 2(tail)
    EXPECT_EQ(buf.Count(), 4u);
    EXPECT_FLOAT_EQ(buf.GetPoint(0).position.x, 5.0f);
    EXPECT_FLOAT_EQ(buf.GetPoint(3).position.x, 2.0f);
}

TEST(UZ_TrailPointBuffer, WrapAroundThenRemoveOldest_HeadIsCorrect) {
    // Fill a capacity-2 buffer, wrap it, then remove the oldest.
    TrailPointBuffer buf(2);
    buf.AddPoint(MakePoint(1.0f)); // [1]
    buf.AddPoint(MakePoint(2.0f)); // [2, 1] full
    buf.AddPoint(MakePoint(3.0f)); // [3, 2] — 1 expelled

    buf.RemoveOldest(); // removes 2
    // Buffer now: [3] — only head remains
    EXPECT_EQ(buf.Count(), 1u);
    EXPECT_FLOAT_EQ(buf.GetPoint(0).position.x, 3.0f);
}

// =============================================================================
// CopyOrdered — contiguous ordered snapshot
// =============================================================================

TEST(UZ_TrailPointBuffer, CopyOrdered_EmptyBuffer_ReturnsZeroCount) {
    TrailPointBuffer buf(8);
    std::vector<TrailPoint> out;
    const uint32_t written = buf.CopyOrdered(out);
    EXPECT_EQ(written, 0u);
    EXPECT_TRUE(out.empty());
}

TEST(UZ_TrailPointBuffer, CopyOrdered_ThreePoints_ProducesNewestFirst) {
    TrailPointBuffer buf(8);
    buf.AddPoint(MakePoint(10.0f));
    buf.AddPoint(MakePoint(20.0f));
    buf.AddPoint(MakePoint(30.0f)); // newest

    std::vector<TrailPoint> out;
    const uint32_t written = buf.CopyOrdered(out);

    EXPECT_EQ(written, 3u);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_FLOAT_EQ(out[0].position.x, 30.0f) << "out[0] must be newest";
    EXPECT_FLOAT_EQ(out[1].position.x, 20.0f);
    EXPECT_FLOAT_EQ(out[2].position.x, 10.0f) << "out[2] must be oldest";
}

TEST(UZ_TrailPointBuffer, CopyOrdered_AfterWrap_StillNewestFirst) {
    TrailPointBuffer buf(3);
    buf.AddPoint(MakePoint(1.0f));
    buf.AddPoint(MakePoint(2.0f));
    buf.AddPoint(MakePoint(3.0f));
    buf.AddPoint(MakePoint(4.0f)); // wraps: expels 1

    std::vector<TrailPoint> out;
    buf.CopyOrdered(out);

    ASSERT_EQ(out.size(), 3u);
    EXPECT_FLOAT_EQ(out[0].position.x, 4.0f);
    EXPECT_FLOAT_EQ(out[1].position.x, 3.0f);
    EXPECT_FLOAT_EQ(out[2].position.x, 2.0f);
}

TEST(UZ_TrailPointBuffer, CopyOrdered_ResizesOutVector) {
    TrailPointBuffer buf(4);
    buf.AddPoint(MakePoint(1.0f));
    buf.AddPoint(MakePoint(2.0f));

    // Pre-populate out with stale data to verify it is overwritten.
    std::vector<TrailPoint> out(10, MakePoint(999.0f));
    const uint32_t written = buf.CopyOrdered(out);

    EXPECT_EQ(written, 2u);
    EXPECT_EQ(out.size(), 2u) << "CopyOrdered must resize out to Count()";
}

// =============================================================================
// Mutable GetPoint — in-place update
// =============================================================================

TEST(UZ_TrailPointBuffer, GetPointMutable_AllowsAgeUpdate_InPlace) {
    TrailPointBuffer buf(4);
    buf.AddPoint(MakePoint(1.0f));
    buf.GetPoint(0).age = 0.5f;
    EXPECT_FLOAT_EQ(buf.GetPoint(0).age, 0.5f)
        << "Mutable GetPoint must allow in-place field updates";
}

TEST(UZ_TrailPointBuffer, GetPointMutable_UpdateDistanceFromHead_InPlace) {
    TrailPointBuffer buf(4);
    buf.AddPoint(MakePoint(1.0f));
    buf.AddPoint(MakePoint(2.0f));
    buf.GetPoint(1).distanceFromHead = 3.14f;
    EXPECT_FLOAT_EQ(buf.GetPoint(1).distanceFromHead, 3.14f);
}
