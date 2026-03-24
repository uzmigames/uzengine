/// Unit tests for the xatlas lightmap UV generator.

#include <gtest/gtest.h>
#include "lightmap_uv_generator.h"

#include <cmath>

namespace {

/// Create a simple quad (two triangles) for testing.
struct QuadMesh {
    std::vector<float>    positions;
    std::vector<float>    normals;
    std::vector<float>    uvs;
    std::vector<uint32_t> indices;
};

QuadMesh MakeQuad() {
    QuadMesh q;
    // 4 vertices, Z-up plane
    q.positions = {
        0.f, 0.f, 0.f,
        1.f, 0.f, 0.f,
        1.f, 1.f, 0.f,
        0.f, 1.f, 0.f,
    };
    q.normals = {
        0.f, 0.f, 1.f,
        0.f, 0.f, 1.f,
        0.f, 0.f, 1.f,
        0.f, 0.f, 1.f,
    };
    q.uvs = {
        0.f, 0.f,
        1.f, 0.f,
        1.f, 1.f,
        0.f, 1.f,
    };
    q.indices = {0, 1, 2, 0, 2, 3};
    return q;
}

} // namespace

TEST(LightmapUVTest, QuadProducesValidOutput) {
    auto quad = MakeQuad();

    uz::LightmapUVInput input{};
    input.positions   = quad.positions.data();
    input.normals     = quad.normals.data();
    input.uvs         = quad.uvs.data();
    input.indices     = quad.indices.data();
    input.vertexCount = 4;
    input.indexCount  = 6;

    auto output = uz::GenerateLightmapUVs(input);

    ASSERT_TRUE(output.success) << output.error;
    EXPECT_GE(output.vertexCount, 4u);
    EXPECT_EQ(output.indexCount, 6u);
    EXPECT_GT(output.atlasWidth, 0u);
    EXPECT_GT(output.atlasHeight, 0u);
    EXPECT_GT(output.chartCount, 0u);

    // UV1 should be normalized [0,1]
    for (uint32_t i = 0; i < output.vertexCount; ++i) {
        float u = output.uvs[i * 2 + 0];
        float v = output.uvs[i * 2 + 1];
        EXPECT_GE(u, 0.f);
        EXPECT_LE(u, 1.f);
        EXPECT_GE(v, 0.f);
        EXPECT_LE(v, 1.f);
    }

    // Remapped positions should match originals via xref
    EXPECT_EQ(output.positions.size(), output.vertexCount * 3);
    EXPECT_EQ(output.normals.size(), output.vertexCount * 3);
    EXPECT_EQ(output.originalUVs.size(), output.vertexCount * 2);
}

TEST(LightmapUVTest, EmptyInputFails) {
    uz::LightmapUVInput input{};
    auto output = uz::GenerateLightmapUVs(input);
    EXPECT_FALSE(output.success);
    EXPECT_FALSE(output.error.empty());
}

TEST(LightmapUVTest, CustomResolution) {
    auto quad = MakeQuad();

    uz::LightmapUVInput input{};
    input.positions   = quad.positions.data();
    input.normals     = quad.normals.data();
    input.indices     = quad.indices.data();
    input.vertexCount = 4;
    input.indexCount  = 6;

    uz::LightmapUVOptions opts{};
    opts.resolution = 256;
    opts.padding    = 4;

    auto output = uz::GenerateLightmapUVs(input, opts);
    ASSERT_TRUE(output.success) << output.error;
    EXPECT_GE(output.vertexCount, 4u);
}
