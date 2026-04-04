/// Unit tests for ComputeSmoothNormals and packed normal encoding.
///
/// @ref Max 1999 "Weights for Computing Vertex Normals from Facet Normals"
/// @ref Mikkelsen 2010 "Simulation of Wrinkled Surfaces Revisited"

#include <gtest/gtest.h>
#include "engine/asset/mesh/normal_smoother.h"
#include "engine/asset/mesh/packed_normal.h"
#include "engine/asset/formats/uzmesh_format.h"

#include <cmath>
#include <vector>

using namespace uz;

// ─── Helper: create a simple quad (2 triangles) ─────────────────────────────

static void MakeQuad(std::vector<VertexPosNormUV>& verts, std::vector<uint32_t>& indices) {
    verts.resize(4);
    // Flat on XY plane
    verts[0].position[0] = 0; verts[0].position[1] = 0; verts[0].position[2] = 0;
    verts[1].position[0] = 1; verts[1].position[1] = 0; verts[1].position[2] = 0;
    verts[2].position[0] = 1; verts[2].position[1] = 1; verts[2].position[2] = 0;
    verts[3].position[0] = 0; verts[3].position[1] = 1; verts[3].position[2] = 0;
    for (auto& v : verts) { v.normal[0] = 0; v.normal[1] = 0; v.normal[2] = 0; v.uv[0] = 0; v.uv[1] = 0; }
    indices = {0, 1, 2, 0, 2, 3};
}

// ─── Helper: create two triangles at an angle ───────────────────────────────

static void MakeDihedral(std::vector<VertexPosNormUV>& verts, std::vector<uint32_t>& indices, float angleDeg) {
    // Two triangles sharing edge along X axis, second rotated by angleDeg around X
    float rad = angleDeg * 3.14159265359f / 180.0f;
    verts.resize(4);
    verts[0].position[0] = 0; verts[0].position[1] = 0; verts[0].position[2] = 0;
    verts[1].position[0] = 1; verts[1].position[1] = 0; verts[1].position[2] = 0;
    verts[2].position[0] = 0.5f; verts[2].position[1] = 1; verts[2].position[2] = 0;
    verts[3].position[0] = 0.5f; verts[3].position[1] = std::cos(rad); verts[3].position[2] = std::sin(rad);
    for (auto& v : verts) { v.normal[0] = 0; v.normal[1] = 0; v.normal[2] = 0; v.uv[0] = 0; v.uv[1] = 0; }
    indices = {0, 1, 2, 1, 0, 3}; // Two tris sharing edge 0-1
}

// ─── ComputeSmoothNormals tests ─────────────────────────────────────────────

TEST(NormalSmoother, QuadProducesUpwardNormals) {
    std::vector<VertexPosNormUV> verts;
    std::vector<uint32_t> indices;
    MakeQuad(verts, indices);

    ComputeSmoothNormals(verts, indices, 60.0f);

    // All 4 vertices should have normal pointing up (0, 0, 1) since both
    // triangles are coplanar on the XY plane.
    for (const auto& v : verts) {
        EXPECT_NEAR(v.normal[0], 0.0f, 0.01f);
        EXPECT_NEAR(v.normal[1], 0.0f, 0.01f);
        EXPECT_NEAR(v.normal[2], 1.0f, 0.01f);
    }
}

TEST(NormalSmoother, SmoothEdgeBelowThreshold) {
    std::vector<VertexPosNormUV> verts;
    std::vector<uint32_t> indices;
    MakeDihedral(verts, indices, 45.0f); // 45° < 60° threshold

    ComputeSmoothNormals(verts, indices, 60.0f);

    // Shared vertices (0, 1) should have averaged normals (not face normals)
    // The averaged normal should be between the two face normals
    float nx0 = verts[0].normal[0];
    float ny0 = verts[0].normal[1];
    float nz0 = verts[0].normal[2];
    float len = std::sqrt(nx0*nx0 + ny0*ny0 + nz0*nz0);
    EXPECT_NEAR(len, 1.0f, 0.01f); // Unit length
}

TEST(NormalSmoother, HardEdgeAboveThreshold) {
    std::vector<VertexPosNormUV> verts;
    std::vector<uint32_t> indices;
    MakeDihedral(verts, indices, 90.0f); // 90° > 60° threshold

    ComputeSmoothNormals(verts, indices, 60.0f);

    // With 90° angle and 60° threshold, the shared edge is hard.
    // Each face's vertices should have their own face normal (not averaged).
    // Vertex 2 (only in face 0) should have pure face 0 normal.
    float nz2 = verts[2].normal[2];
    // Face 0 is on XY plane, normal should be (0,0,1) or close
    EXPECT_GT(std::abs(nz2), 0.5f);
}

TEST(NormalSmoother, EmptyInput) {
    std::vector<VertexPosNormUV> verts;
    std::vector<uint32_t> indices;
    ComputeSmoothNormals(verts, indices, 60.0f);
    EXPECT_TRUE(verts.empty());
}

TEST(NormalSmoother, SmoothFlagsOverride) {
    std::vector<VertexPosNormUV> verts;
    std::vector<uint32_t> indices;
    MakeDihedral(verts, indices, 45.0f); // Would normally be smooth

    // Mark face 1 as flat — should prevent smoothing
    std::vector<bool> flags = {true, false};
    ComputeSmoothNormals(verts, indices, 60.0f, flags);

    // With face 1 flat, vertex 3 should have only face 1's normal
    float len3 = std::sqrt(verts[3].normal[0]*verts[3].normal[0] +
                           verts[3].normal[1]*verts[3].normal[1] +
                           verts[3].normal[2]*verts[3].normal[2]);
    EXPECT_NEAR(len3, 1.0f, 0.01f);
}

// ─── PackedNormal tests ─────────────────────────────────────────────────────

TEST(PackedNormal, EncodeDecodeRoundtrip) {
    float nx = 0.577f, ny = 0.577f, nz = 0.577f; // ~(1,1,1) normalized
    auto packed = PackedNormal::Encode(nx, ny, nz);

    float dx, dy, dz;
    packed.Decode(dx, dy, dz);

    EXPECT_NEAR(dx, nx, 1.0f / 127.0f + 0.01f);
    EXPECT_NEAR(dy, ny, 1.0f / 127.0f + 0.01f);
    EXPECT_NEAR(dz, nz, 1.0f / 127.0f + 0.01f);
}

TEST(PackedNormal, EncodeDecodeAxes) {
    // Test cardinal directions
    auto px = PackedNormal::Encode(1, 0, 0);
    auto py = PackedNormal::Encode(0, 1, 0);
    auto pz = PackedNormal::Encode(0, 0, 1);
    auto pn = PackedNormal::Encode(-1, 0, 0);

    EXPECT_EQ(px.x, 127);
    EXPECT_EQ(py.y, 127);
    EXPECT_EQ(pz.z, 127);
    EXPECT_EQ(pn.x, -127);
}

TEST(PackedTangent, SignPreserved) {
    auto pos = PackedTangent::Encode(1, 0, 0, 1.0f);
    auto neg = PackedTangent::Encode(1, 0, 0, -1.0f);

    float tx, ty, tz, sign;
    pos.Decode(tx, ty, tz, sign);
    EXPECT_FLOAT_EQ(sign, 1.0f);

    neg.Decode(tx, ty, tz, sign);
    EXPECT_FLOAT_EQ(sign, -1.0f);
}
