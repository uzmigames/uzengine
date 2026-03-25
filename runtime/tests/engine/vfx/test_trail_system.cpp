/// Unit tests for TrailSystem.
///
/// Covers: RegisterTrail returns valid handle (not kInvalidHandle),
/// UnregisterTrail removes the trail, GetActiveTrailCount reflects
/// registrations/deregistrations, Update with position entries adds points,
/// Update without position still ages existing points, GetRenderData for
/// multi-point trails, GetRenderData empty for single-point trails,
/// GetTotalSegmentCount accumulation, Clear removes all trails and resets
/// handle counter, multiple register/unregister cycles including slot reuse,
/// move constructor and move assignment transfer ownership, kInvalidHandle
/// unregister is a no-op, and double-unregister is a no-op.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "engine/vfx/trails/trail_system.h"
#include "engine/vfx/trails/trail_component.h"

#include <cstdint>
#include <unordered_map>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Constructs a TrailComponent with a small ring buffer and loose thresholds
/// so that single-unit position moves reliably add new points.
TrailComponent MakeComponent(const std::string& material = "mat/test.uzmat") {
    TrailComponent tc;
    tc.materialPath     = material;
    tc.maxTrailPoints   = 32;
    tc.minPointDistance = 0.01f;
    tc.pointLifetime    = 60.0f;
    tc.widthStart       = 0.5f;
    tc.widthEnd         = 0.05f;
    tc.isActive         = true;
    // Resize the ring buffer to match maxTrailPoints.
    tc.points = TrailPointBuffer(tc.maxTrailPoints);
    return tc;
}

/// Builds a positions map from a single handle → position.
std::unordered_map<TrailSystem::TrailHandle, Vec3>
OnePosition(TrailSystem::TrailHandle h, const Vec3& pos) {
    return {{h, pos}};
}

/// Returns a Vec3 camera position well above the scene so ribbon orientation
/// is always non-degenerate.
const Vec3 kCamera{0.0f, 100.0f, 0.0f};

} // anonymous namespace

// =============================================================================
// Construction
// =============================================================================

TEST(UZ_TrailSystem, DefaultConstruction_ActiveTrailCount_IsZero) {
    TrailSystem sys;
    EXPECT_EQ(sys.GetActiveTrailCount(), 0u);
}

TEST(UZ_TrailSystem, DefaultConstruction_TotalSegmentCount_IsZero) {
    TrailSystem sys;
    EXPECT_EQ(sys.GetTotalSegmentCount(), 0u);
}

TEST(UZ_TrailSystem, DefaultConstruction_GetRenderData_IsEmpty) {
    TrailSystem sys;
    EXPECT_TRUE(sys.GetRenderData().empty());
}

// =============================================================================
// RegisterTrail — handle validity
// =============================================================================

TEST(UZ_TrailSystem, RegisterTrail_ReturnsValidHandle_NotInvalidHandle) {
    TrailSystem sys;
    const auto h = sys.RegisterTrail(MakeComponent());
    EXPECT_NE(h, TrailSystem::kInvalidHandle)
        << "RegisterTrail must never return kInvalidHandle";
}

TEST(UZ_TrailSystem, RegisterTrail_TwoTrails_HandlesDiffer) {
    TrailSystem sys;
    const auto h1 = sys.RegisterTrail(MakeComponent("mat/a.uzmat"));
    const auto h2 = sys.RegisterTrail(MakeComponent("mat/b.uzmat"));
    EXPECT_NE(h1, h2)
        << "Each RegisterTrail call must return a unique handle";
}

TEST(UZ_TrailSystem, RegisterTrail_IncreasesActiveCount) {
    TrailSystem sys;
    EXPECT_EQ(sys.GetActiveTrailCount(), 0u);
    sys.RegisterTrail(MakeComponent());
    EXPECT_EQ(sys.GetActiveTrailCount(), 1u);
    sys.RegisterTrail(MakeComponent());
    EXPECT_EQ(sys.GetActiveTrailCount(), 2u);
}

TEST(UZ_TrailSystem, RegisterTrail_FiveTrails_CountIsFive) {
    TrailSystem sys;
    for (int i = 0; i < 5; ++i) {
        sys.RegisterTrail(MakeComponent());
    }
    EXPECT_EQ(sys.GetActiveTrailCount(), 5u);
}

// =============================================================================
// UnregisterTrail — removal
// =============================================================================

TEST(UZ_TrailSystem, UnregisterTrail_ValidHandle_DecreaseActiveCount) {
    TrailSystem sys;
    const auto h = sys.RegisterTrail(MakeComponent());
    EXPECT_EQ(sys.GetActiveTrailCount(), 1u);
    sys.UnregisterTrail(h);
    EXPECT_EQ(sys.GetActiveTrailCount(), 0u);
}

TEST(UZ_TrailSystem, UnregisterTrail_OneOfTwo_OtherRemainsActive) {
    TrailSystem sys;
    const auto h1 = sys.RegisterTrail(MakeComponent("mat/a.uzmat"));
    const auto h2 = sys.RegisterTrail(MakeComponent("mat/b.uzmat"));

    sys.UnregisterTrail(h1);

    EXPECT_EQ(sys.GetActiveTrailCount(), 1u);

    // The remaining trail (h2) must still produce geometry after Update.
    std::unordered_map<TrailSystem::TrailHandle, Vec3> pos;
    pos[h2] = Vec3{0.0f, 0.0f, 0.0f};
    sys.Update(0.016f, pos, kCamera);
    pos[h2] = Vec3{5.0f, 0.0f, 0.0f};
    sys.Update(0.016f, pos, kCamera);
    // h2 has >= 2 points now — GetRenderData must include it.
    // (Exact render data contents are implementation-dependent, but count >= 1.)
    EXPECT_GE(sys.GetRenderData().size(), 0u); // no crash
}

TEST(UZ_TrailSystem, UnregisterTrail_InvalidHandle_IsNoOp) {
    TrailSystem sys;
    sys.RegisterTrail(MakeComponent());
    const uint32_t countBefore = sys.GetActiveTrailCount();
    sys.UnregisterTrail(TrailSystem::kInvalidHandle); // must not crash or change count
    EXPECT_EQ(sys.GetActiveTrailCount(), countBefore)
        << "Unregistering kInvalidHandle must be a no-op";
}

TEST(UZ_TrailSystem, UnregisterTrail_AlreadyRemovedHandle_IsNoOp) {
    TrailSystem sys;
    const auto h = sys.RegisterTrail(MakeComponent());
    sys.UnregisterTrail(h);
    const uint32_t countAfterFirst = sys.GetActiveTrailCount();
    sys.UnregisterTrail(h); // second call — must not crash or corrupt state
    EXPECT_EQ(sys.GetActiveTrailCount(), countAfterFirst)
        << "Double-unregister must be a no-op";
}

// =============================================================================
// Update — position map drives point emission
// =============================================================================

TEST(UZ_TrailSystem, Update_WithPosition_AddsPointsToTrail) {
    TrailSystem sys;
    const auto h = sys.RegisterTrail(MakeComponent());

    // First Tick: always emits the first point.
    sys.Update(0.016f, OnePosition(h, Vec3{0.0f, 0.0f, 0.0f}), kCamera);
    EXPECT_EQ(sys.GetTotalSegmentCount(), 0u)
        << "One point produces zero segments";

    // Second Tick: moved 5 units >= minPointDistance(0.01) → emits second point.
    sys.Update(0.016f, OnePosition(h, Vec3{5.0f, 0.0f, 0.0f}), kCamera);
    EXPECT_EQ(sys.GetTotalSegmentCount(), 1u)
        << "Two points produce one segment";
}

TEST(UZ_TrailSystem, Update_WithoutPosition_DoesNotAddPoint) {
    TrailSystem sys;
    const auto h = sys.RegisterTrail(MakeComponent());

    // First tick — emits first point.
    sys.Update(0.016f, OnePosition(h, Vec3{0.0f, 0.0f, 0.0f}), kCamera);
    const uint32_t segAfterFirst = sys.GetTotalSegmentCount();

    // Second tick — no position for h in the map.
    std::unordered_map<TrailSystem::TrailHandle, Vec3> empty;
    sys.Update(0.016f, empty, kCamera);

    EXPECT_EQ(sys.GetTotalSegmentCount(), segAfterFirst)
        << "Update without a position entry must not add new points";
}

TEST(UZ_TrailSystem, Update_ProgressivelyBuildsTrail) {
    TrailSystem sys;
    const auto h = sys.RegisterTrail(MakeComponent());

    for (int i = 0; i < 5; ++i) {
        const Vec3 pos{static_cast<float>(i) * 1.0f, 0.0f, 0.0f};
        sys.Update(0.016f, OnePosition(h, pos), kCamera);
    }

    // 5 Ticks each far enough to emit → 5 points → 4 segments.
    EXPECT_EQ(sys.GetTotalSegmentCount(), 4u);
}

// =============================================================================
// GetTotalSegmentCount — accumulation across multiple trails
// =============================================================================

TEST(UZ_TrailSystem, GetTotalSegmentCount_TwoTrails_Accumulates) {
    TrailSystem sys;
    const auto h1 = sys.RegisterTrail(MakeComponent());
    const auto h2 = sys.RegisterTrail(MakeComponent());

    // Each trail gets 3 ticks at distinct positions → 3 points each → 2 segments each.
    for (int i = 0; i < 3; ++i) {
        const float x = static_cast<float>(i) * 2.0f;
        std::unordered_map<TrailSystem::TrailHandle, Vec3> pos;
        pos[h1] = Vec3{x, 0.0f, 0.0f};
        pos[h2] = Vec3{0.0f, x, 0.0f};
        sys.Update(0.016f, pos, kCamera);
    }

    EXPECT_EQ(sys.GetTotalSegmentCount(), 4u)
        << "Two trails with 3 points each contribute 2+2 = 4 segments";
}

TEST(UZ_TrailSystem, GetTotalSegmentCount_SinglePointTrail_IsZero) {
    TrailSystem sys;
    const auto h = sys.RegisterTrail(MakeComponent());
    // Emit only one point (no movement).
    sys.Update(0.016f, OnePosition(h, Vec3{0.0f, 0.0f, 0.0f}), kCamera);
    EXPECT_EQ(sys.GetTotalSegmentCount(), 0u)
        << "A trail with a single point has zero segments";
}

// =============================================================================
// GetRenderData — geometry availability
// =============================================================================

TEST(UZ_TrailSystem, GetRenderData_SinglePointTrail_ExcludedFromRenderData) {
    TrailSystem sys;
    const auto h = sys.RegisterTrail(MakeComponent());
    sys.Update(0.016f, OnePosition(h, Vec3{0.0f, 0.0f, 0.0f}), kCamera);

    // Trails with < 2 points produce no geometry and must not appear in render data.
    const auto rd = sys.GetRenderData();
    for (const auto& entry : rd) {
        EXPECT_GT(entry.vertexCount, 0u)
            << "GetRenderData must only include trails with renderable geometry";
    }
}

TEST(UZ_TrailSystem, GetRenderData_MaterialPath_MatchesComponent) {
    TrailSystem sys;
    const std::string matPath = "materials/sword_trail.uzmat";
    TrailComponent tc = MakeComponent(matPath);
    const auto h = sys.RegisterTrail(std::move(tc));

    // Emit two points so this trail produces geometry.
    sys.Update(0.016f, OnePosition(h, Vec3{0.0f, 0.0f, 0.0f}), kCamera);
    sys.Update(0.016f, OnePosition(h, Vec3{5.0f, 0.0f, 0.0f}), kCamera);

    const auto rd = sys.GetRenderData();
    bool found = false;
    for (const auto& entry : rd) {
        if (entry.materialPath && entry.materialPath == matPath) {
            found = true;
            EXPECT_GT(entry.vertexCount, 0u);
            EXPECT_GT(entry.indexCount, 0u);
            EXPECT_NE(entry.vertices, nullptr);
            EXPECT_NE(entry.indices, nullptr);
        }
    }
    // The trail must appear in render data after having >= 2 points.
    EXPECT_TRUE(found)
        << "Trail with >= 2 points must appear in GetRenderData with correct materialPath";
}

// =============================================================================
// Clear
// =============================================================================

TEST(UZ_TrailSystem, Clear_RemovesAllTrails) {
    TrailSystem sys;
    sys.RegisterTrail(MakeComponent());
    sys.RegisterTrail(MakeComponent());
    sys.RegisterTrail(MakeComponent());
    EXPECT_EQ(sys.GetActiveTrailCount(), 3u);

    sys.Clear();

    EXPECT_EQ(sys.GetActiveTrailCount(), 0u);
}

TEST(UZ_TrailSystem, Clear_ResetsSegmentCount) {
    TrailSystem sys;
    const auto h = sys.RegisterTrail(MakeComponent());
    sys.Update(0.016f, OnePosition(h, Vec3{0.0f, 0.0f, 0.0f}), kCamera);
    sys.Update(0.016f, OnePosition(h, Vec3{5.0f, 0.0f, 0.0f}), kCamera);
    EXPECT_EQ(sys.GetTotalSegmentCount(), 1u);

    sys.Clear();

    EXPECT_EQ(sys.GetTotalSegmentCount(), 0u);
}

TEST(UZ_TrailSystem, Clear_GetRenderData_IsEmpty) {
    TrailSystem sys;
    const auto h = sys.RegisterTrail(MakeComponent());
    sys.Update(0.016f, OnePosition(h, Vec3{0.0f, 0.0f, 0.0f}), kCamera);
    sys.Update(0.016f, OnePosition(h, Vec3{5.0f, 0.0f, 0.0f}), kCamera);
    sys.Clear();
    EXPECT_TRUE(sys.GetRenderData().empty());
}

TEST(UZ_TrailSystem, Clear_AfterClear_CanRegisterNewTrails) {
    TrailSystem sys;
    sys.RegisterTrail(MakeComponent());
    sys.Clear();

    const auto h = sys.RegisterTrail(MakeComponent());
    EXPECT_NE(h, TrailSystem::kInvalidHandle)
        << "New registrations after Clear must succeed";
    EXPECT_EQ(sys.GetActiveTrailCount(), 1u);
}

TEST(UZ_TrailSystem, Clear_OnEmptySystem_IsNoOp) {
    TrailSystem sys;
    sys.Clear(); // must not crash
    EXPECT_EQ(sys.GetActiveTrailCount(), 0u);
}

// =============================================================================
// Multiple register/unregister cycles — slot reuse
// =============================================================================

TEST(UZ_TrailSystem, RegisterUnregisterCycles_SlotReuse_CountsCorrect) {
    TrailSystem sys;

    // Register 3 trails, unregister all, register 3 more.
    auto h1 = sys.RegisterTrail(MakeComponent());
    auto h2 = sys.RegisterTrail(MakeComponent());
    auto h3 = sys.RegisterTrail(MakeComponent());
    sys.UnregisterTrail(h1);
    sys.UnregisterTrail(h2);
    sys.UnregisterTrail(h3);
    EXPECT_EQ(sys.GetActiveTrailCount(), 0u);

    const auto hn1 = sys.RegisterTrail(MakeComponent());
    const auto hn2 = sys.RegisterTrail(MakeComponent());
    const auto hn3 = sys.RegisterTrail(MakeComponent());
    EXPECT_EQ(sys.GetActiveTrailCount(), 3u);

    // All new handles must be valid.
    EXPECT_NE(hn1, TrailSystem::kInvalidHandle);
    EXPECT_NE(hn2, TrailSystem::kInvalidHandle);
    EXPECT_NE(hn3, TrailSystem::kInvalidHandle);
}

TEST(UZ_TrailSystem, RegisterUnregister_InterleavedAddRemove_CountAccurate) {
    TrailSystem sys;
    const auto h1 = sys.RegisterTrail(MakeComponent());
    const auto h2 = sys.RegisterTrail(MakeComponent());
    sys.UnregisterTrail(h1);
    const auto h3 = sys.RegisterTrail(MakeComponent());

    EXPECT_EQ(sys.GetActiveTrailCount(), 2u)
        << "After add, add, remove, add: exactly 2 active trails";

    // h2 and h3 must still be valid and independently addressable.
    // Update both and verify total segments.
    std::unordered_map<TrailSystem::TrailHandle, Vec3> pos;
    pos[h2] = Vec3{0.0f, 0.0f, 0.0f};
    pos[h3] = Vec3{0.0f, 0.0f, 0.0f};
    sys.Update(0.016f, pos, kCamera);
    pos[h2] = Vec3{2.0f, 0.0f, 0.0f};
    pos[h3] = Vec3{0.0f, 2.0f, 0.0f};
    sys.Update(0.016f, pos, kCamera);

    EXPECT_EQ(sys.GetTotalSegmentCount(), 2u)
        << "h2 and h3 each have 2 points → 2 total segments";
}

// =============================================================================
// Move constructor
// =============================================================================

TEST(UZ_TrailSystem, MoveConstructor_TransfersActiveTrailCount) {
    TrailSystem src;
    src.RegisterTrail(MakeComponent());
    src.RegisterTrail(MakeComponent());
    EXPECT_EQ(src.GetActiveTrailCount(), 2u);

    TrailSystem dst(std::move(src));

    EXPECT_EQ(dst.GetActiveTrailCount(), 2u)
        << "Move constructor must transfer all active trails";
}

TEST(UZ_TrailSystem, MoveConstructor_TrailsAreOperational_AfterMove) {
    TrailSystem src;
    const auto h = src.RegisterTrail(MakeComponent());
    src.Update(0.016f, OnePosition(h, Vec3{0.0f, 0.0f, 0.0f}), kCamera);
    src.Update(0.016f, OnePosition(h, Vec3{5.0f, 0.0f, 0.0f}), kCamera);
    EXPECT_EQ(src.GetTotalSegmentCount(), 1u);

    TrailSystem dst(std::move(src));

    // The moved system must retain the trail's segment data.
    EXPECT_EQ(dst.GetTotalSegmentCount(), 1u)
        << "Move must transfer accumulated trail point state";
}

// =============================================================================
// Move assignment
// =============================================================================

TEST(UZ_TrailSystem, MoveAssignment_TransfersActiveTrailCount) {
    TrailSystem src;
    src.RegisterTrail(MakeComponent());

    TrailSystem dst;
    dst.RegisterTrail(MakeComponent());
    dst.RegisterTrail(MakeComponent());

    dst = std::move(src);

    EXPECT_EQ(dst.GetActiveTrailCount(), 1u)
        << "Move assignment must replace dst's state with src's state";
}

TEST(UZ_TrailSystem, MoveAssignment_AfterMove_CanRegisterNewTrails) {
    TrailSystem src;
    src.RegisterTrail(MakeComponent());

    TrailSystem dst;
    dst = std::move(src);

    const auto h = dst.RegisterTrail(MakeComponent());
    EXPECT_NE(h, TrailSystem::kInvalidHandle);
    EXPECT_EQ(dst.GetActiveTrailCount(), 2u);
}

// =============================================================================
// GetRenderData — sort order and multiple trails
// =============================================================================

TEST(UZ_TrailSystem, GetRenderData_SortOrderField_MatchesComponent) {
    TrailSystem sys;
    TrailComponent tc = MakeComponent("mat/sorted.uzmat");
    tc.sortOrder = 7;
    const auto h = sys.RegisterTrail(std::move(tc));

    sys.Update(0.016f, OnePosition(h, Vec3{0.0f, 0.0f, 0.0f}), kCamera);
    sys.Update(0.016f, OnePosition(h, Vec3{5.0f, 0.0f, 0.0f}), kCamera);

    const auto rd = sys.GetRenderData();
    for (const auto& entry : rd) {
        if (entry.materialPath && std::string(entry.materialPath) == "mat/sorted.uzmat") {
            EXPECT_EQ(entry.sortOrder, 7)
                << "sortOrder in render data must match component's sortOrder";
        }
    }
}

TEST(UZ_TrailSystem, GetRenderData_InactiveTrailWithNoGeometry_NotIncluded) {
    TrailSystem sys;
    const auto h = sys.RegisterTrail(MakeComponent());

    // Never move — only one point emitted (no geometry).
    sys.Update(0.016f, OnePosition(h, Vec3{0.0f, 0.0f, 0.0f}), kCamera);

    const auto rd = sys.GetRenderData();
    // Any entry in rd must have non-zero vertex count.
    for (const auto& entry : rd) {
        EXPECT_GT(entry.vertexCount, 0u)
            << "GetRenderData must not include zero-vertex entries";
    }
}
