/// Unit tests for TrailMeshBuilder and TrailMeshData.
///
/// Covers: Build with 0 points (empty result), Build with 1 point (empty),
/// Build with 2 points (4 vertices, 6 indices, 2 triangles), Build with N points
/// (2N vertices, 6(N-1) indices), UV layout (u=0 left, u=1 right, v=0 head, v=1 tail),
/// width interpolation (wider at head than tail), color interpolation (start at head,
/// end at tail), degenerate coincident points (no crash), zero totalDistance
/// (clamped, no divide-by-zero), TrailMeshData::Clear, GetVertexCount/GetIndexCount/
/// GetTriangleCount, IsEmpty, and null points pointer (empty result, no crash).
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "engine/vfx/trails/trail_mesh_builder.h"
#include "engine/vfx/trails/trail_point.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Constructs a straight-line array of N TrailPoints evenly spaced along the X axis.
/// Head is at x=0, tail is at x=(N-1)*spacing.
/// distanceFromHead is set proportionally so UV V computations work correctly.
std::vector<TrailPoint> MakeStraightPoints(uint32_t n, float spacing = 1.0f) {
    std::vector<TrailPoint> pts(n);
    for (uint32_t i = 0; i < n; ++i) {
        TrailPoint& p    = pts[i];
        p.position       = Vec3{static_cast<float>(i) * spacing, 0.0f, 0.0f};
        p.direction      = Vec3{1.0f, 0.0f, 0.0f};
        p.width          = 1.0f;
        p.color          = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
        p.age            = 0.0f;
        p.distanceFromHead = static_cast<float>(i) * spacing;
    }
    return pts;
}

/// Shared camera position placed well above the trail so the ribbon always
/// has a well-defined orientation (tangent not parallel to toCamera).
const Vec3 kCamera{0.0f, 10.0f, 0.0f};

} // anonymous namespace

// =============================================================================
// TrailMeshData — query methods on default-constructed instance
// =============================================================================

TEST(UZ_TrailMeshData, Default_IsEmpty) {
    TrailMeshData md;
    EXPECT_TRUE(md.IsEmpty());
}

TEST(UZ_TrailMeshData, Default_GetVertexCount_IsZero) {
    TrailMeshData md;
    EXPECT_EQ(md.GetVertexCount(), 0u);
}

TEST(UZ_TrailMeshData, Default_GetIndexCount_IsZero) {
    TrailMeshData md;
    EXPECT_EQ(md.GetIndexCount(), 0u);
}

TEST(UZ_TrailMeshData, Default_GetTriangleCount_IsZero) {
    TrailMeshData md;
    EXPECT_EQ(md.GetTriangleCount(), 0u);
}

// =============================================================================
// TrailMeshData::Clear
// =============================================================================

TEST(UZ_TrailMeshData, Clear_EmptiesVerticesAndIndices) {
    TrailMeshData md;
    md.vertices.push_back(TrailVertex{});
    md.indices.push_back(0u);
    md.Clear();
    EXPECT_TRUE(md.IsEmpty());
    EXPECT_EQ(md.GetVertexCount(), 0u);
    EXPECT_EQ(md.GetIndexCount(), 0u);
}

TEST(UZ_TrailMeshData, Clear_OnAlreadyEmpty_IsNoOp) {
    TrailMeshData md;
    md.Clear(); // must not crash
    EXPECT_TRUE(md.IsEmpty());
}

// =============================================================================
// TrailMeshData — GetTriangleCount == indices/3
// =============================================================================

TEST(UZ_TrailMeshData, GetTriangleCount_EqualsIndexCountDividedByThree) {
    TrailMeshData md;
    // Manually push 12 indices (4 triangles).
    for (uint32_t i = 0; i < 12; ++i) {
        md.indices.push_back(i);
    }
    EXPECT_EQ(md.GetTriangleCount(), 4u);
    EXPECT_EQ(md.GetIndexCount(), 12u);
}

// =============================================================================
// Build with 0 points → empty result
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_ZeroPoints_ReturnsEmptyMesh) {
    TrailMeshBuilder builder;
    const auto pts = MakeStraightPoints(0);
    const TrailMeshData mesh = builder.Build(
        pts.data(), 0u, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        0.0f, 2.0f
    );
    EXPECT_TRUE(mesh.IsEmpty());
    EXPECT_EQ(mesh.GetVertexCount(), 0u);
    EXPECT_EQ(mesh.GetIndexCount(), 0u);
}

// =============================================================================
// Build with 1 point → empty result (need >= 2 for a segment)
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_OnePoint_ReturnsEmptyMesh) {
    TrailMeshBuilder builder;
    const auto pts = MakeStraightPoints(1);
    const TrailMeshData mesh = builder.Build(
        pts.data(), 1u, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        1.0f, 2.0f
    );
    EXPECT_TRUE(mesh.IsEmpty());
    EXPECT_EQ(mesh.GetVertexCount(), 0u);
    EXPECT_EQ(mesh.GetIndexCount(), 0u);
    EXPECT_EQ(mesh.GetTriangleCount(), 0u);
}

// =============================================================================
// Build with 2 points → 4 vertices, 6 indices, 2 triangles, 1 quad
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_TwoPoints_FourVertices) {
    TrailMeshBuilder builder;
    const auto pts = MakeStraightPoints(2, 1.0f);
    const TrailMeshData mesh = builder.Build(
        pts.data(), 2u, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        1.0f, 2.0f
    );
    EXPECT_EQ(mesh.GetVertexCount(), 4u)
        << "2 points produce 2 vertices per point = 4 total";
}

TEST(UZ_TrailMeshBuilder, Build_TwoPoints_SixIndices) {
    TrailMeshBuilder builder;
    const auto pts = MakeStraightPoints(2, 1.0f);
    const TrailMeshData mesh = builder.Build(
        pts.data(), 2u, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        1.0f, 2.0f
    );
    EXPECT_EQ(mesh.GetIndexCount(), 6u)
        << "2 points produce 1 segment = 2 triangles = 6 indices";
}

TEST(UZ_TrailMeshBuilder, Build_TwoPoints_TwoTriangles) {
    TrailMeshBuilder builder;
    const auto pts = MakeStraightPoints(2, 1.0f);
    const TrailMeshData mesh = builder.Build(
        pts.data(), 2u, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        1.0f, 2.0f
    );
    EXPECT_EQ(mesh.GetTriangleCount(), 2u);
}

// =============================================================================
// Build with 3 points → 6 vertices, 12 indices, 4 triangles
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_ThreePoints_SixVertices) {
    TrailMeshBuilder builder;
    const auto pts = MakeStraightPoints(3, 1.0f);
    const TrailMeshData mesh = builder.Build(
        pts.data(), 3u, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        2.0f, 2.0f
    );
    EXPECT_EQ(mesh.GetVertexCount(), 6u);
}

TEST(UZ_TrailMeshBuilder, Build_ThreePoints_TwelveIndices) {
    TrailMeshBuilder builder;
    const auto pts = MakeStraightPoints(3, 1.0f);
    const TrailMeshData mesh = builder.Build(
        pts.data(), 3u, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        2.0f, 2.0f
    );
    EXPECT_EQ(mesh.GetIndexCount(), 12u)
        << "3 points = 2 segments = 4 triangles = 12 indices";
}

// =============================================================================
// Build with N points — formula: 2N vertices, 6(N-1) indices
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_NPoints_VertexAndIndexFormula) {
    TrailMeshBuilder builder;
    const Vec4 cs{1,1,1,1};
    const Vec4 ce{1,1,1,0};

    for (uint32_t n = 2u; n <= 10u; ++n) {
        const auto pts = MakeStraightPoints(n, 1.0f);
        const float totalDist = static_cast<float>(n - 1u);

        const TrailMeshData mesh = builder.Build(
            pts.data(), n, kCamera,
            1.0f, 0.1f, cs, ce, totalDist, 2.0f
        );

        EXPECT_EQ(mesh.GetVertexCount(), n * 2u)
            << "Expected " << n * 2u << " vertices for " << n << " points";
        EXPECT_EQ(mesh.GetIndexCount(), (n - 1u) * 6u)
            << "Expected " << (n - 1u) * 6u << " indices for " << n << " points";
    }
}

// =============================================================================
// UV layout — u and v coordinates
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_TwoPoints_LeftVertices_U_IsZero) {
    TrailMeshBuilder builder;
    const auto pts = MakeStraightPoints(2, 1.0f);
    const TrailMeshData mesh = builder.Build(
        pts.data(), 2u, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        1.0f, 2.0f
    );
    ASSERT_GE(mesh.GetVertexCount(), 2u);
    // Left vertices are at even indices: 0, 2, 4, ...
    EXPECT_NEAR(mesh.vertices[0].u, 0.0f, 1e-5f)
        << "Left vertex (even index) must have u = 0";
    EXPECT_NEAR(mesh.vertices[2].u, 0.0f, 1e-5f);
}

TEST(UZ_TrailMeshBuilder, Build_TwoPoints_RightVertices_U_IsOne) {
    TrailMeshBuilder builder;
    const auto pts = MakeStraightPoints(2, 1.0f);
    const TrailMeshData mesh = builder.Build(
        pts.data(), 2u, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        1.0f, 2.0f
    );
    // Right vertices are at odd indices: 1, 3, 5, ...
    EXPECT_NEAR(mesh.vertices[1].u, 1.0f, 1e-5f)
        << "Right vertex (odd index) must have u = 1";
    EXPECT_NEAR(mesh.vertices[3].u, 1.0f, 1e-5f);
}

TEST(UZ_TrailMeshBuilder, Build_HeadVertices_V_IsZero) {
    // Head is point 0 — it has distanceFromHead=0, so v = 0/totalDistance = 0.
    TrailMeshBuilder builder;
    const auto pts = MakeStraightPoints(3, 1.0f);
    const TrailMeshData mesh = builder.Build(
        pts.data(), 3u, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        2.0f, 2.0f
    );
    // Vertices 0 (left) and 1 (right) correspond to point 0 (head).
    EXPECT_NEAR(mesh.vertices[0].v, 0.0f, 1e-4f)
        << "Head left vertex must have v = 0";
    EXPECT_NEAR(mesh.vertices[1].v, 0.0f, 1e-4f)
        << "Head right vertex must have v = 0";
}

TEST(UZ_TrailMeshBuilder, Build_TailVertices_V_IsOne) {
    // Tail point has distanceFromHead == totalDistance, so v = 1.
    TrailMeshBuilder builder;
    const uint32_t n = 3u;
    auto pts = MakeStraightPoints(n, 1.0f);
    const float totalDist = 2.0f; // pts[2].distanceFromHead = 2.0
    const TrailMeshData mesh = builder.Build(
        pts.data(), n, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        totalDist, 2.0f
    );
    // Tail vertices: indices 2*(n-1) and 2*(n-1)+1 = 4 and 5.
    EXPECT_NEAR(mesh.vertices[4].v, 1.0f, 1e-4f)
        << "Tail left vertex must have v = 1";
    EXPECT_NEAR(mesh.vertices[5].v, 1.0f, 1e-4f)
        << "Tail right vertex must have v = 1";
}

// =============================================================================
// Width interpolation — head wider than tail
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_WidthInterpolation_HeadIsWiderThanTail) {
    TrailMeshBuilder builder;
    const uint32_t n = 4u;
    const auto pts = MakeStraightPoints(n, 1.0f);
    const float totalDist = 3.0f;
    const Vec4 cs{1,1,1,1};
    const Vec4 ce{1,1,1,1};

    const TrailMeshData mesh = builder.Build(
        pts.data(), n, kCamera,
        2.0f,  // widthStart
        0.0f,  // widthEnd
        cs, ce,
        totalDist, 2.0f
    );

    // For each point i, left and right vertices are 2i and 2i+1.
    // Width at point i == distance between left and right vertex positions.
    auto segWidth = [&](uint32_t i) {
        const Vec3 l = mesh.vertices[2u * i].position;
        const Vec3 r = mesh.vertices[2u * i + 1u].position;
        const float dx = r.x - l.x;
        const float dy = r.y - l.y;
        const float dz = r.z - l.z;
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    };

    const float headWidth = segWidth(0);
    const float tailWidth = segWidth(n - 1u);

    EXPECT_GT(headWidth, tailWidth)
        << "Head must be wider than tail when widthStart > widthEnd";
}

// =============================================================================
// Color interpolation — colorStart at head, colorEnd at tail
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_ColorInterpolation_HeadColorMatchesStart) {
    TrailMeshBuilder builder;
    const auto pts = MakeStraightPoints(3, 1.0f);
    const Vec4 cs{1.0f, 0.0f, 0.0f, 1.0f};
    const Vec4 ce{0.0f, 0.0f, 1.0f, 0.0f};

    const TrailMeshData mesh = builder.Build(
        pts.data(), 3u, kCamera,
        1.0f, 0.1f, cs, ce, 2.0f, 2.0f
    );

    ASSERT_GE(mesh.GetVertexCount(), 2u);
    // Head left vertex (index 0): v=0 → color == colorStart
    EXPECT_NEAR(mesh.vertices[0].color.x, cs.x, 1e-4f);
    EXPECT_NEAR(mesh.vertices[0].color.y, cs.y, 1e-4f);
    EXPECT_NEAR(mesh.vertices[0].color.z, cs.z, 1e-4f);
    EXPECT_NEAR(mesh.vertices[0].color.w, cs.w, 1e-4f);
}

TEST(UZ_TrailMeshBuilder, Build_ColorInterpolation_TailColorMatchesEnd) {
    TrailMeshBuilder builder;
    const uint32_t n = 3u;
    const auto pts = MakeStraightPoints(n, 1.0f);
    const Vec4 cs{1.0f, 0.0f, 0.0f, 1.0f};
    const Vec4 ce{0.0f, 0.0f, 1.0f, 0.0f};

    const TrailMeshData mesh = builder.Build(
        pts.data(), n, kCamera,
        1.0f, 0.1f, cs, ce, 2.0f, 2.0f
    );

    // Tail left vertex is at index 2*(n-1) = 4
    const uint32_t tailLeftIdx = 2u * (n - 1u);
    EXPECT_NEAR(mesh.vertices[tailLeftIdx].color.x, ce.x, 1e-4f);
    EXPECT_NEAR(mesh.vertices[tailLeftIdx].color.z, ce.z, 1e-4f);
    EXPECT_NEAR(mesh.vertices[tailLeftIdx].color.w, ce.w, 1e-4f);
}

// =============================================================================
// Degenerate case — all points at same position (no crash)
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_AllPointsSamePosition_DoesNotCrash) {
    TrailMeshBuilder builder;
    const uint32_t n = 5u;
    std::vector<TrailPoint> pts(n);
    for (auto& p : pts) {
        p.position        = Vec3{0.0f, 0.0f, 0.0f};
        p.direction       = Vec3{0.0f, 1.0f, 0.0f};
        p.width           = 1.0f;
        p.color           = Vec4{1,1,1,1};
        p.age             = 0.0f;
        p.distanceFromHead = 0.0f;
    }
    const TrailMeshData mesh = builder.Build(
        pts.data(), n, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        0.0f, 2.0f
    );
    // Geometry may still be generated — all we verify is no crash and correct counts.
    EXPECT_EQ(mesh.GetVertexCount(), n * 2u);
    EXPECT_EQ(mesh.GetIndexCount(), (n - 1u) * 6u);
}

// =============================================================================
// Zero totalDistance — clamped internally, no divide-by-zero
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_ZeroTotalDistance_NoCrash_MeshIsValid) {
    TrailMeshBuilder builder;
    const auto pts = MakeStraightPoints(3, 1.0f);
    const TrailMeshData mesh = builder.Build(
        pts.data(), 3u, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        0.0f, 2.0f  // totalDistance=0 — must clamp to kMinTotalDistance internally
    );
    // Result must be a valid mesh with 6 vertices and 12 indices.
    EXPECT_EQ(mesh.GetVertexCount(), 6u);
    EXPECT_EQ(mesh.GetIndexCount(), 12u);
}

// =============================================================================
// Null points pointer — empty result, no crash
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_NullPoints_ReturnsEmptyMesh_NoCrash) {
    TrailMeshBuilder builder;
    const TrailMeshData mesh = builder.Build(
        nullptr, 5u, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        4.0f, 2.0f
    );
    EXPECT_TRUE(mesh.IsEmpty());
}

// =============================================================================
// Camera-facing — left and right vertices are symmetric about the trail axis
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_CameraFacing_LeftAndRight_SymmetricAboutPoint) {
    // Camera is directly above (0, 10, 0). Trail runs along X. Ribbon should
    // expand along Z (cross of X-tangent and Y-toCamera = Z direction).
    TrailMeshBuilder builder;

    std::vector<TrailPoint> pts(2);
    pts[0].position        = Vec3{0.0f, 0.0f, 0.0f};
    pts[0].direction       = Vec3{1.0f, 0.0f, 0.0f};
    pts[0].distanceFromHead = 0.0f;
    pts[0].width = 1.0f;
    pts[0].color = Vec4{1,1,1,1};
    pts[0].age   = 0.0f;

    pts[1].position        = Vec3{1.0f, 0.0f, 0.0f};
    pts[1].direction       = Vec3{1.0f, 0.0f, 0.0f};
    pts[1].distanceFromHead = 1.0f;
    pts[1].width = 1.0f;
    pts[1].color = Vec4{1,1,1,1};
    pts[1].age   = 0.0f;

    const TrailMeshData mesh = builder.Build(
        pts.data(), 2u,
        Vec3{0.0f, 10.0f, 0.0f},  // camera directly above
        2.0f, 2.0f,                // uniform width for easy symmetry check
        Vec4{1,1,1,1}, Vec4{1,1,1,1},
        1.0f, 2.0f
    );

    ASSERT_EQ(mesh.GetVertexCount(), 4u);

    // Left (index 0) and right (index 1) for the head point.
    const Vec3 leftHead  = mesh.vertices[0].position;
    const Vec3 rightHead = mesh.vertices[1].position;

    // Both vertices must be at the same Y (no vertical expansion for horizontal trail + overhead camera).
    EXPECT_NEAR(leftHead.y,  0.0f, 1e-3f);
    EXPECT_NEAR(rightHead.y, 0.0f, 1e-3f);

    // They must be symmetric about x=0 in the Z direction.
    EXPECT_NEAR(leftHead.z, -rightHead.z, 1e-3f)
        << "Left and right vertices must be symmetric about the trail axis";
}

// =============================================================================
// Index winding — valid in-range indices
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_Indices_AreWithinVertexRange) {
    TrailMeshBuilder builder;
    const uint32_t n = 5u;
    const auto pts = MakeStraightPoints(n, 1.0f);
    const TrailMeshData mesh = builder.Build(
        pts.data(), n, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        4.0f, 2.0f
    );

    const uint32_t vertexCount = mesh.GetVertexCount();
    for (uint32_t idx : mesh.indices) {
        EXPECT_LT(idx, vertexCount)
            << "Index " << idx << " is out of range (vertexCount=" << vertexCount << ")";
    }
}

// =============================================================================
// Multi-strand — vertex and index count formulas
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_MultiStrand_VertexAndIndexFormula) {
    TrailMeshBuilder builder;
    const uint32_t n = 4u;
    const auto pts = MakeStraightPoints(n, 1.0f);
    const float totalDist = 3.0f;
    const Vec4 cs{1,1,1,1};
    const Vec4 ce{1,1,1,0};

    for (uint32_t strands = 1u; strands <= 4u; ++strands) {
        const TrailMeshData mesh = builder.Build(
            pts.data(), n, kCamera,
            1.0f, 0.1f, cs, ce, totalDist, 2.0f,
            strands, 0.2f
        );
        EXPECT_EQ(mesh.GetVertexCount(), n * 2u * strands)
            << "Expected " << n * 2u * strands << " vertices for "
            << n << " points and " << strands << " strands";
        EXPECT_EQ(mesh.GetIndexCount(), (n - 1u) * 6u * strands)
            << "Expected " << (n - 1u) * 6u * strands << " indices for "
            << n << " points and " << strands << " strands";
    }
}

// =============================================================================
// Multi-strand — strand index values
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_MultiStrand_StrandIndicesCorrect) {
    TrailMeshBuilder builder;
    const uint32_t n = 3u;
    const uint32_t strands = 3u;
    const auto pts = MakeStraightPoints(n, 1.0f);
    const Vec4 cs{1,1,1,1};
    const Vec4 ce{1,1,1,0};

    const TrailMeshData mesh = builder.Build(
        pts.data(), n, kCamera,
        1.0f, 0.1f, cs, ce, 2.0f, 2.0f,
        strands, 0.2f
    );

    ASSERT_EQ(mesh.GetVertexCount(), n * 2u * strands);

    // Each strand s has n*2 vertices starting at offset s*n*2.
    for (uint32_t s = 0; s < strands; ++s) {
        for (uint32_t i = 0; i < n * 2u; ++i) {
            const uint32_t idx = s * n * 2u + i;
            EXPECT_EQ(mesh.vertices[idx].strandIndex, s)
                << "Vertex " << idx << " should belong to strand " << s;
        }
    }
}

// =============================================================================
// Multi-strand — strands are offset from each other
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_MultiStrand_StrandsAreOffset) {
    TrailMeshBuilder builder;
    const uint32_t n = 2u;
    const uint32_t strands = 3u;
    const float spacing = 0.5f;
    const auto pts = MakeStraightPoints(n, 1.0f);
    const Vec4 cs{1,1,1,1};
    const Vec4 ce{1,1,1,1}; // Uniform color so no alpha differences

    const TrailMeshData mesh = builder.Build(
        pts.data(), n, kCamera,
        1.0f, 1.0f, cs, ce, 1.0f, 2.0f,
        strands, spacing
    );

    ASSERT_EQ(mesh.GetVertexCount(), n * 2u * strands);

    // Head left vertex of each strand (indices 0, 4, 8 for n=2).
    // These should differ in position (offset along the up vector).
    const Vec3 s0 = mesh.vertices[0].position;            // Strand 0, head, left
    const Vec3 s1 = mesh.vertices[n * 2u].position;       // Strand 1, head, left
    const Vec3 s2 = mesh.vertices[n * 2u * 2u].position;  // Strand 2, head, left

    // The offset between strand 0 and strand 1 should equal the offset
    // between strand 1 and strand 2 (uniform spacing).
    const float dx01 = s1.x - s0.x;
    const float dy01 = s1.y - s0.y;
    const float dz01 = s1.z - s0.z;
    const float dist01 = std::sqrt(dx01*dx01 + dy01*dy01 + dz01*dz01);

    const float dx12 = s2.x - s1.x;
    const float dy12 = s2.y - s1.y;
    const float dz12 = s2.z - s1.z;
    const float dist12 = std::sqrt(dx12*dx12 + dy12*dy12 + dz12*dz12);

    EXPECT_NEAR(dist01, spacing, 0.05f)
        << "Distance between strand 0 and 1 should be approximately strandSpacing";
    EXPECT_NEAR(dist01, dist12, 0.01f)
        << "Spacing between strands should be uniform";
}

// =============================================================================
// Multi-strand — single strand produces same result as default
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_SingleStrand_MatchesDefault) {
    TrailMeshBuilder builder;
    const uint32_t n = 3u;
    const auto pts = MakeStraightPoints(n, 1.0f);
    const Vec4 cs{1,0.5f,0,1};
    const Vec4 ce{0,0,1,0};

    // Default call (no strand params)
    const TrailMeshData meshDefault = builder.Build(
        pts.data(), n, kCamera,
        1.0f, 0.1f, cs, ce, 2.0f, 2.0f
    );

    // Explicit single strand
    const TrailMeshData meshSingle = builder.Build(
        pts.data(), n, kCamera,
        1.0f, 0.1f, cs, ce, 2.0f, 2.0f,
        1, 0.0f
    );

    ASSERT_EQ(meshDefault.GetVertexCount(), meshSingle.GetVertexCount());
    ASSERT_EQ(meshDefault.GetIndexCount(), meshSingle.GetIndexCount());

    for (uint32_t i = 0; i < meshDefault.GetVertexCount(); ++i) {
        EXPECT_NEAR(meshDefault.vertices[i].position.x,
                    meshSingle.vertices[i].position.x, 1e-5f);
        EXPECT_NEAR(meshDefault.vertices[i].position.y,
                    meshSingle.vertices[i].position.y, 1e-5f);
        EXPECT_NEAR(meshDefault.vertices[i].position.z,
                    meshSingle.vertices[i].position.z, 1e-5f);
        EXPECT_EQ(meshSingle.vertices[i].strandIndex, 0u);
    }
}

// =============================================================================
// Multi-strand — all indices are within vertex range
// =============================================================================

TEST(UZ_TrailMeshBuilder, Build_MultiStrand_IndicesInRange) {
    TrailMeshBuilder builder;
    const uint32_t n = 5u;
    const uint32_t strands = 4u;
    const auto pts = MakeStraightPoints(n, 1.0f);

    const TrailMeshData mesh = builder.Build(
        pts.data(), n, kCamera,
        1.0f, 0.1f,
        Vec4{1,1,1,1}, Vec4{1,1,1,0},
        4.0f, 2.0f,
        strands, 0.15f
    );

    const uint32_t vertexCount = mesh.GetVertexCount();
    for (uint32_t idx : mesh.indices) {
        EXPECT_LT(idx, vertexCount)
            << "Multi-strand index " << idx
            << " is out of range (vertexCount=" << vertexCount << ")";
    }
}
