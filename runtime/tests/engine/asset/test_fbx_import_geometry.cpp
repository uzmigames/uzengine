#include <gtest/gtest.h>
#include "engine/asset/formats/uzmesh_format.h"
#include "engine/asset/importers/fbx_import_geometry.h"
#include "engine/math/vector3.h"

using namespace uz;

namespace {

void MakeOutwardTetrahedron(
    std::vector<VertexPosNormUV>& vertices,
    std::vector<uint32_t>& indices) {
    // Centroid is the origin; all four faces are RH/CCW-outward.
    const float positions[4][3] = {{1, 1, 1}, {-1, -1, 1}, {-1, 1, -1}, {1, -1, -1}};
    const uint32_t corners[] = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    for (uint32_t corner : corners) {
        VertexPosNormUV vertex{};
        std::copy_n(positions[corner], 3, vertex.position);
        indices.push_back(static_cast<uint32_t>(vertices.size()));
        vertices.push_back(vertex);
    }
}

float RadialNormal(const VertexPosNormUV& vertex) {
    return vertex.normal[0] * vertex.position[0]
         + vertex.normal[1] * vertex.position[1]
         + vertex.normal[2] * vertex.position[2];
}

} // namespace

TEST(FbxImportGeometry, OutwardNormalsBeforeClockwiseConversion) {
    for (float angle : {60.0f, 180.0f}) {
        for (const auto& flags : {std::vector<bool>{}, std::vector<bool>(4, true), std::vector<bool>(4, false)}) {
            std::vector<VertexPosNormUV> vertices;
            std::vector<uint32_t> indices;
            MakeOutwardTetrahedron(vertices, indices);
            const auto sourceIndices = indices;
            PrepareFbxGeometry(vertices, indices, true, angle, flags, true);

            for (const auto& vertex : vertices) {
                EXPECT_GT(RadialNormal(vertex), 0.0f);
            }
            for (size_t t = 0; t < indices.size(); t += 3) {
                EXPECT_EQ(indices[t], sourceIndices[t]);
                EXPECT_EQ(indices[t + 1], sourceIndices[t + 2]);
                EXPECT_EQ(indices[t + 2], sourceIndices[t + 1]);
                const auto position = [&](uint32_t index) {
                    const auto& p = vertices[index].position;
                    return Vec3{p[0], p[1], p[2]};
                };
                const Vec3 a = position(indices[t]);
                const Vec3 b = position(indices[t + 1]);
                const Vec3 c = position(indices[t + 2]);
                // CW viewed from outside: RH cross product points inward.
                EXPECT_LT((b - a).Cross(c - a).Dot(a + b + c), 0.0f);
            }
        }
    }
}

TEST(FbxImportGeometry, OldOrderProducesInwardNormals) {
    std::vector<VertexPosNormUV> vertices;
    std::vector<uint32_t> indices;
    MakeOutwardTetrahedron(vertices, indices);
    for (size_t t = 0; t < indices.size(); t += 3) {
        std::swap(indices[t + 1], indices[t + 2]);
    }
    ComputeSmoothNormals(vertices, indices, 180.0f, std::vector<bool>(4, true));
    for (const auto& vertex : vertices) {
        EXPECT_LT(RadialNormal(vertex), 0.0f);
    }
}

TEST(FbxImportGeometry, IdentityPreservesWindingAndImportedNormals) {
    std::vector<VertexPosNormUV> vertices;
    std::vector<uint32_t> indices;
    MakeOutwardTetrahedron(vertices, indices);
    const auto sourceIndices = indices;
    PrepareFbxGeometry(vertices, indices, true, 180.0f, {}, false);
    EXPECT_EQ(indices, sourceIndices);
    const auto sourceVertices = vertices;
    PrepareFbxGeometry(vertices, indices, false, 60.0f, {}, true);
    for (size_t v = 0; v < vertices.size(); ++v) {
        for (size_t axis = 0; axis < 3; ++axis) {
            EXPECT_FLOAT_EQ(vertices[v].normal[axis], sourceVertices[v].normal[axis]);
        }
        EXPECT_GT(RadialNormal(vertices[v]), 0.0f);
    }
}

TEST(FbxImportGeometry, MikkTangentsPreserveUvFrameAfterWindingConversion) {
    for (bool reverse : {false, true}) {
        for (float uvSign : {-1.0f, 1.0f}) {
            std::vector<VertexPosNormUV> vertices = {
                {{0, 0, 0}, {}, {0, 0}},
                {{1, 0, 0}, {}, {1, 0}},
                {{0, 1, 0}, {}, {0, uvSign}},
            };
            std::vector<uint32_t> indices = {0, 1, 2};
            PrepareFbxGeometry(vertices, indices, true, 60.0f, {true}, reverse);
            std::vector<float> tangents;
            ComputeFbxMikkTSpaceTangents(vertices, indices, tangents, reverse);
            ASSERT_EQ(tangents.size(), 12u);
            for (size_t v = 0; v < vertices.size(); ++v) {
                EXPECT_NEAR(vertices[v].normal[2], 1.0f, 1e-6f);
                EXPECT_NEAR(tangents[v * 4], 1.0f, 1e-6f);
                EXPECT_NEAR(tangents[v * 4 + 1], 0.0f, 1e-6f);
                EXPECT_NEAR(tangents[v * 4 + 2], 0.0f, 1e-6f);
                // B = w * (N x T) must follow increasing V, including mirrored UVs.
                EXPECT_FLOAT_EQ(tangents[v * 4 + 3], uvSign);
            }
        }
    }
}
