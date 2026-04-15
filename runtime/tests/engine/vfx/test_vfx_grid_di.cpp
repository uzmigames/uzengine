/// Unit tests for phase 7.11.0 - Grid2D/Grid3D DI CPU reference.
///
/// Locks the HLSL sampler port (deferred follow-up) so the GPU branch
/// produces identical values as the CPU reference. Covers:
///   - Resize zero-initialises and GetCellCount.
///   - Write+Read round-trip at integer coords.
///   - Clamp vs Wrap addressing at out-of-range coords.
///   - Bilinear sample at exact cell centres equals the stored value.
///   - Bilinear midpoint equals the linear average of 4 cells.
///   - Trilinear sample at a cube centre equals average of 8 cells.
///   - Degenerate (0x0x0) grid returns zero sample without crashing.
///
/// @ref phase7_7.11.0-vfx-grid2d-grid3d-di

#include <gtest/gtest.h>

#include "engine/vfx/data_interfaces/vfx_grid_di.h"

using namespace uz;

namespace {

Vec4 V(float a, float b, float c, float d) { return Vec4{a, b, c, d}; }

} // namespace

// ─── VfxGrid2D resize + read/write ─────────────────────────────────────────

TEST(VfxGrid2D, DefaultIsEmpty) {
    VfxGrid2D g;
    EXPECT_EQ(g.GetCellCount(), 0u);
}

TEST(VfxGrid2D, ResizeZeroInitialises) {
    VfxGrid2D g(4, 3);
    EXPECT_EQ(g.GetCellCount(), 12u);
    for (uint32_t y = 0; y < 3; ++y)
        for (uint32_t x = 0; x < 4; ++x) {
            const auto v = g.Read(static_cast<int32_t>(x), static_cast<int32_t>(y));
            EXPECT_FLOAT_EQ(v.x, 0.0f);
            EXPECT_FLOAT_EQ(v.w, 0.0f);
        }
}

TEST(VfxGrid2D, WriteReadRoundTrip) {
    VfxGrid2D g(8, 8);
    g.Write(3, 5, V(1.0f, 2.0f, 3.0f, 4.0f));
    const auto out = g.Read(3, 5);
    EXPECT_FLOAT_EQ(out.x, 1.0f);
    EXPECT_FLOAT_EQ(out.y, 2.0f);
    EXPECT_FLOAT_EQ(out.z, 3.0f);
    EXPECT_FLOAT_EQ(out.w, 4.0f);
}

TEST(VfxGrid2D, ClampAddressingClampsEdges) {
    VfxGrid2D g(4, 4);
    g.addressing = VfxGridAddressing::Clamp;
    g.Write(0, 0, V(1.0f, 0, 0, 0));
    g.Write(3, 3, V(9.0f, 0, 0, 0));
    EXPECT_FLOAT_EQ(g.Read(-5, -5).x, 1.0f);
    EXPECT_FLOAT_EQ(g.Read(10, 10).x, 9.0f);
}

TEST(VfxGrid2D, WrapAddressingWrapsModulo) {
    VfxGrid2D g(4, 4);
    g.addressing = VfxGridAddressing::Wrap;
    g.Write(1, 1, V(7.0f, 0, 0, 0));
    // (1+4, 1+4) == (1, 1) under wrap.
    EXPECT_FLOAT_EQ(g.Read(5, 5).x, 7.0f);
    // Negative coords: (-3, -3) == (1, 1) under wrap (extent 4).
    EXPECT_FLOAT_EQ(g.Read(-3, -3).x, 7.0f);
}

TEST(VfxGrid2D, ClearOverwritesEveryCell) {
    VfxGrid2D g(2, 2);
    g.Write(0, 0, V(5, 5, 5, 5));
    g.Write(1, 1, V(5, 5, 5, 5));
    g.Clear(V(9, 9, 9, 9));
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            EXPECT_FLOAT_EQ(g.Read(x, y).x, 9.0f);
}

// ─── VfxGrid2D bilinear sampling ──────────────────────────────────────────

TEST(VfxGrid2D, BilinearSampleAtCellCentreReturnsStoredValue) {
    // HLSL convention: cell (x, y) has centre at (x + 0.5, y + 0.5).
    VfxGrid2D g(4, 4);
    g.Write(2, 1, V(3.0f, 5.0f, 7.0f, 9.0f));
    const auto s = g.SampleBilinear(Vec2{2.5f, 1.5f});
    EXPECT_NEAR(s.x, 3.0f, 1e-3f);
    EXPECT_NEAR(s.w, 9.0f, 1e-3f);
}

TEST(VfxGrid2D, BilinearSampleAtMidpointIsAverageOfFourCells) {
    VfxGrid2D g(4, 4);
    g.Write(0, 0, V(1.0f, 0, 0, 0));
    g.Write(1, 0, V(3.0f, 0, 0, 0));
    g.Write(0, 1, V(5.0f, 0, 0, 0));
    g.Write(1, 1, V(7.0f, 0, 0, 0));
    // Midpoint of the 4 cells: at (1.0, 1.0) in continuous cell space
    // (centres at 0.5 / 1.5 / 0.5 / 1.5).
    const auto s = g.SampleBilinear(Vec2{1.0f, 1.0f});
    EXPECT_NEAR(s.x, (1.0f + 3.0f + 5.0f + 7.0f) * 0.25f, 1e-3f);
}

TEST(VfxGrid2D, EmptyGridSampleReturnsZero) {
    VfxGrid2D g;
    const auto s = g.SampleBilinear(Vec2{0.5f, 0.5f});
    EXPECT_FLOAT_EQ(s.x, 0.0f);
}

// ─── VfxGrid3D basics ──────────────────────────────────────────────────────

TEST(VfxGrid3D, ResizeProducesCorrectCount) {
    VfxGrid3D g(4, 3, 2);
    EXPECT_EQ(g.GetCellCount(), 24u);
}

TEST(VfxGrid3D, WriteReadRoundTrip) {
    VfxGrid3D g(4, 4, 4);
    g.Write(1, 2, 3, V(11.0f, 22.0f, 33.0f, 44.0f));
    const auto out = g.Read(1, 2, 3);
    EXPECT_FLOAT_EQ(out.x, 11.0f);
    EXPECT_FLOAT_EQ(out.z, 33.0f);
}

TEST(VfxGrid3D, TrilinearSampleAtCentreOfEightCellsAveragesThem) {
    VfxGrid3D g(4, 4, 4);
    g.Write(0, 0, 0, V(1.0f, 0, 0, 0));
    g.Write(1, 0, 0, V(2.0f, 0, 0, 0));
    g.Write(0, 1, 0, V(3.0f, 0, 0, 0));
    g.Write(1, 1, 0, V(4.0f, 0, 0, 0));
    g.Write(0, 0, 1, V(5.0f, 0, 0, 0));
    g.Write(1, 0, 1, V(6.0f, 0, 0, 0));
    g.Write(0, 1, 1, V(7.0f, 0, 0, 0));
    g.Write(1, 1, 1, V(8.0f, 0, 0, 0));
    // Centre of the 8-cell cube is at (1, 1, 1) in continuous coords.
    const auto s = g.SampleTrilinear(Vec3{1.0f, 1.0f, 1.0f});
    EXPECT_NEAR(s.x, (1.0f+2.0f+3.0f+4.0f+5.0f+6.0f+7.0f+8.0f) / 8.0f, 1e-3f);
}

TEST(VfxGrid3D, EmptyGridSampleReturnsZero) {
    VfxGrid3D g;
    const auto s = g.SampleTrilinear(Vec3{0.5f, 0.5f, 0.5f});
    EXPECT_FLOAT_EQ(s.x, 0.0f);
}

TEST(VfxGrid3D, ClampAddressingForOutOfRangeCoords) {
    VfxGrid3D g(4, 4, 4);
    g.addressing = VfxGridAddressing::Clamp;
    g.Write(3, 3, 3, V(42.0f, 0, 0, 0));
    EXPECT_FLOAT_EQ(g.Read(100, 100, 100).x, 42.0f);
    EXPECT_FLOAT_EQ(g.Read(-5, -5, -5).x, 0.0f);  // clamped to (0,0,0)
}
