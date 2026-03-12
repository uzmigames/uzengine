/// Tests for MeshHeader, Mesh, MeshLoader, MeshBuilder, and .uzmesh file format.

#include <gtest/gtest.h>
#include "mesh.h"

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// MeshHeader layout tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(MeshHeaderTest, SizeIs128Bytes) {
    EXPECT_EQ(sizeof(MeshHeader), 128u);
}

TEST(MeshHeaderTest, MagicConstant) {
    // "UZMH" in little-endian
    EXPECT_EQ(kMeshMagic, 0x484D5A55u);
    const char* bytes = reinterpret_cast<const char*>(&kMeshMagic);
    EXPECT_EQ(bytes[0], 'U');
    EXPECT_EQ(bytes[1], 'Z');
    EXPECT_EQ(bytes[2], 'M');
    EXPECT_EQ(bytes[3], 'H');
}

TEST(MeshHeaderTest, VersionIs1) {
    EXPECT_EQ(kMeshVersion, 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh struct tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(MeshTest, DefaultIsInvalid) {
    Mesh mesh{};
    EXPECT_FALSE(mesh.IsValid());
    EXPECT_EQ(mesh.GetVertexCount(), 0u);
    EXPECT_EQ(mesh.GetIndexCount(), 0u);
}

TEST(MeshTest, GetVertexDataSize) {
    Mesh mesh{};
    mesh.header.vertexCount = 100;
    mesh.header.vertexStride = 48;
    EXPECT_EQ(mesh.GetVertexDataSize(), 4800u);
}

TEST(MeshTest, GetIndexDataSizeUInt16) {
    Mesh mesh{};
    mesh.header.indexCount = 300;
    mesh.header.indexFormat = MeshIndexFormat::UInt16;
    EXPECT_EQ(mesh.GetIndexDataSize(), 600u);
}

TEST(MeshTest, GetIndexDataSizeUInt32) {
    Mesh mesh{};
    mesh.header.indexCount = 300;
    mesh.header.indexFormat = MeshIndexFormat::UInt32;
    EXPECT_EQ(mesh.GetIndexDataSize(), 1200u);
}

TEST(MeshTest, IsValidRequiresMagicAndData) {
    Mesh mesh{};
    mesh.header.magic = kMeshMagic;
    mesh.header.vertexCount = 1;
    EXPECT_FALSE(mesh.IsValid()); // No vertex data

    mesh.vertexData.resize(48);
    EXPECT_TRUE(mesh.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// MeshBuilder tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(MeshBuilderTest, CreateFromVertices) {
    std::vector<Vertex> verts = {
        {{0, 0, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
        {{1, 0, 0}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
        {{0, 1, 0}, {0, 0, 1}, {0, 1}, {1, 0, 0, 1}},
    };
    std::vector<uint32_t> indices = {0, 1, 2};

    Mesh mesh = MeshBuilder::CreateFromVertices(verts, indices);

    EXPECT_TRUE(mesh.IsValid());
    EXPECT_EQ(mesh.header.magic, kMeshMagic);
    EXPECT_EQ(mesh.header.version, kMeshVersion);
    EXPECT_EQ(mesh.header.vertexFormat, MeshVertexFormat::Standard);
    EXPECT_EQ(mesh.header.vertexStride, sizeof(Vertex));
    EXPECT_EQ(mesh.header.vertexCount, 3u);
    EXPECT_EQ(mesh.header.indexCount, 3u);
    EXPECT_EQ(mesh.header.indexFormat, MeshIndexFormat::UInt32);
    EXPECT_EQ(mesh.vertexData.size(), 3 * sizeof(Vertex));
    EXPECT_EQ(mesh.indexData.size(), 3 * sizeof(uint32_t));
}

TEST(MeshBuilderTest, CreateFromVerticesBounds) {
    std::vector<Vertex> verts = {
        {{-2, -3, -1}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
        {{ 5,  4,  7}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
    };
    Mesh mesh = MeshBuilder::CreateFromVertices(verts, {});
    EXPECT_FLOAT_EQ(mesh.header.boundsMin.x, -2.0f);
    EXPECT_FLOAT_EQ(mesh.header.boundsMin.y, -3.0f);
    EXPECT_FLOAT_EQ(mesh.header.boundsMin.z, -1.0f);
    EXPECT_FLOAT_EQ(mesh.header.boundsMax.x, 5.0f);
    EXPECT_FLOAT_EQ(mesh.header.boundsMax.y, 4.0f);
    EXPECT_FLOAT_EQ(mesh.header.boundsMax.z, 7.0f);
}

TEST(MeshBuilderTest, CreateFromSimpleVertices) {
    std::vector<SimpleVertex> verts = {
        {{0, 0, 0}, {1, 0, 0, 1}, {0, 0}},
        {{1, 0, 0}, {0, 1, 0, 1}, {1, 0}},
        {{0, 1, 0}, {0, 0, 1, 1}, {0, 1}},
    };
    Mesh mesh = MeshBuilder::CreateFromSimpleVertices(verts, {0, 1, 2});

    EXPECT_TRUE(mesh.IsValid());
    EXPECT_EQ(mesh.header.vertexFormat, MeshVertexFormat::Simple);
    EXPECT_EQ(mesh.header.vertexStride, sizeof(SimpleVertex));
    EXPECT_EQ(mesh.header.vertexCount, 3u);
    EXPECT_EQ(mesh.header.indexCount, 3u);
}

TEST(MeshBuilderTest, CreateCube) {
    Mesh cube = MeshBuilder::CreateCube(1.0f);

    EXPECT_TRUE(cube.IsValid());
    EXPECT_EQ(cube.header.vertexCount, 24u);  // 6 faces × 4 verts
    EXPECT_EQ(cube.header.indexCount, 36u);    // 6 faces × 2 tris × 3
    EXPECT_EQ(cube.header.vertexFormat, MeshVertexFormat::Standard);
    EXPECT_EQ(cube.header.vertexStride, sizeof(Vertex));

    // Bounds: cube from (-1,-1,0) to (1,1,2)
    EXPECT_FLOAT_EQ(cube.header.boundsMin.x, -1.0f);
    EXPECT_FLOAT_EQ(cube.header.boundsMin.y, -1.0f);
    EXPECT_FLOAT_EQ(cube.header.boundsMin.z, 0.0f);
    EXPECT_FLOAT_EQ(cube.header.boundsMax.x, 1.0f);
    EXPECT_FLOAT_EQ(cube.header.boundsMax.y, 1.0f);
    EXPECT_FLOAT_EQ(cube.header.boundsMax.z, 2.0f);
}

TEST(MeshBuilderTest, CubeVertexDataIntegrity) {
    Mesh cube = MeshBuilder::CreateCube(1.0f);

    // Verify we can read back vertices
    ASSERT_EQ(cube.vertexData.size(), 24 * sizeof(Vertex));
    const Vertex* verts = reinterpret_cast<const Vertex*>(cube.vertexData.data());

    // All normals should be unit length
    for (uint32_t i = 0; i < 24; ++i) {
        float len = std::sqrt(verts[i].normal.x * verts[i].normal.x +
                              verts[i].normal.y * verts[i].normal.y +
                              verts[i].normal.z * verts[i].normal.z);
        EXPECT_NEAR(len, 1.0f, 0.001f) << "Vertex " << i << " normal not unit length";
    }
}

TEST(MeshBuilderTest, CubeIndexDataIntegrity) {
    Mesh cube = MeshBuilder::CreateCube(1.0f);

    ASSERT_EQ(cube.indexData.size(), 36 * sizeof(uint32_t));
    const uint32_t* idx = reinterpret_cast<const uint32_t*>(cube.indexData.data());

    // All indices must be in range [0, 23]
    for (uint32_t i = 0; i < 36; ++i) {
        EXPECT_LT(idx[i], 24u) << "Index " << i << " out of range";
    }
}

TEST(MeshBuilderTest, CubeDataOffsets) {
    Mesh cube = MeshBuilder::CreateCube(1.0f);

    EXPECT_EQ(cube.header.vertexDataOffset, sizeof(MeshHeader));
    EXPECT_EQ(cube.header.indexDataOffset,
              sizeof(MeshHeader) + 24 * sizeof(Vertex));
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O round-trip tests
// ─────────────────────────────────────────────────────────────────────────────

class MeshFileTest : public ::testing::Test {
protected:
    std::string m_testPath;

    void SetUp() override {
        m_testPath = (std::filesystem::temp_directory_path() / "test_mesh.uzmesh").string();
    }

    void TearDown() override {
        std::filesystem::remove(m_testPath);
    }
};

TEST_F(MeshFileTest, SaveAndLoadCube) {
    Mesh original = MeshBuilder::CreateCube(1.0f);

    ASSERT_TRUE(SaveMesh(m_testPath, original));

    Mesh loaded = LoadMesh(m_testPath);

    EXPECT_TRUE(loaded.IsValid());
    EXPECT_EQ(loaded.header.magic, kMeshMagic);
    EXPECT_EQ(loaded.header.version, kMeshVersion);
    EXPECT_EQ(loaded.header.vertexCount, original.header.vertexCount);
    EXPECT_EQ(loaded.header.indexCount, original.header.indexCount);
    EXPECT_EQ(loaded.header.vertexFormat, original.header.vertexFormat);
    EXPECT_EQ(loaded.header.vertexStride, original.header.vertexStride);
    EXPECT_EQ(loaded.vertexData.size(), original.vertexData.size());
    EXPECT_EQ(loaded.indexData.size(), original.indexData.size());

    // Byte-exact vertex data
    EXPECT_EQ(loaded.vertexData, original.vertexData);

    // Byte-exact index data
    EXPECT_EQ(loaded.indexData, original.indexData);
}

TEST_F(MeshFileTest, SaveAndLoadSimpleVertices) {
    std::vector<SimpleVertex> verts = {
        {{0, 0, 0}, {1, 0, 0, 1}, {0, 0}},
        {{1, 0, 0}, {0, 1, 0, 1}, {1, 0}},
        {{0, 0, 1}, {0, 0, 1, 1}, {0, 1}},
    };
    Mesh original = MeshBuilder::CreateFromSimpleVertices(verts, {0, 1, 2});

    ASSERT_TRUE(SaveMesh(m_testPath, original));

    Mesh loaded = LoadMesh(m_testPath);
    EXPECT_TRUE(loaded.IsValid());
    EXPECT_EQ(loaded.header.vertexFormat, MeshVertexFormat::Simple);
    EXPECT_EQ(loaded.vertexData, original.vertexData);
    EXPECT_EQ(loaded.indexData, original.indexData);
}

TEST_F(MeshFileTest, SaveAndLoadBoundsPreserved) {
    Mesh original = MeshBuilder::CreateCube(2.0f);
    ASSERT_TRUE(SaveMesh(m_testPath, original));
    Mesh loaded = LoadMesh(m_testPath);

    EXPECT_FLOAT_EQ(loaded.header.boundsMin.x, original.header.boundsMin.x);
    EXPECT_FLOAT_EQ(loaded.header.boundsMin.y, original.header.boundsMin.y);
    EXPECT_FLOAT_EQ(loaded.header.boundsMin.z, original.header.boundsMin.z);
    EXPECT_FLOAT_EQ(loaded.header.boundsMax.x, original.header.boundsMax.x);
    EXPECT_FLOAT_EQ(loaded.header.boundsMax.y, original.header.boundsMax.y);
    EXPECT_FLOAT_EQ(loaded.header.boundsMax.z, original.header.boundsMax.z);
}

TEST_F(MeshFileTest, LoadNonExistentFile) {
    Mesh mesh = LoadMesh("nonexistent_file.uzmesh");
    EXPECT_FALSE(mesh.IsValid());
}

TEST_F(MeshFileTest, LoadInvalidMagic) {
    // Write garbage header
    MeshHeader bad{};
    bad.magic = 0xDEADBEEF;
    std::ofstream f(m_testPath, std::ios::binary);
    f.write(reinterpret_cast<const char*>(&bad), sizeof(bad));
    f.close();

    Mesh mesh = LoadMesh(m_testPath);
    EXPECT_FALSE(mesh.IsValid());
}

TEST_F(MeshFileTest, LoadWrongVersion) {
    MeshHeader bad{};
    bad.magic = kMeshMagic;
    bad.version = 999;
    std::ofstream f(m_testPath, std::ios::binary);
    f.write(reinterpret_cast<const char*>(&bad), sizeof(bad));
    f.close();

    Mesh mesh = LoadMesh(m_testPath);
    EXPECT_FALSE(mesh.IsValid());
}

TEST_F(MeshFileTest, LoadTruncatedHeader) {
    // Write only 64 bytes (header needs 128)
    std::ofstream f(m_testPath, std::ios::binary);
    uint8_t buf[64] = {};
    f.write(reinterpret_cast<const char*>(buf), sizeof(buf));
    f.close();

    Mesh mesh = LoadMesh(m_testPath);
    EXPECT_FALSE(mesh.IsValid());
}

TEST_F(MeshFileTest, LoadTruncatedVertexData) {
    Mesh original = MeshBuilder::CreateCube(1.0f);

    // Write header + partial vertex data
    std::ofstream f(m_testPath, std::ios::binary);
    f.write(reinterpret_cast<const char*>(&original.header), sizeof(MeshHeader));
    // Write only half the vertex data
    f.write(reinterpret_cast<const char*>(original.vertexData.data()),
            static_cast<std::streamsize>(original.vertexData.size() / 2));
    f.close();

    Mesh mesh = LoadMesh(m_testPath);
    EXPECT_FALSE(mesh.IsValid());
}

TEST_F(MeshFileTest, NonIndexedMesh) {
    std::vector<Vertex> verts = {
        {{0, 0, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
        {{1, 0, 0}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
        {{0, 1, 0}, {0, 0, 1}, {0, 1}, {1, 0, 0, 1}},
    };
    Mesh mesh = MeshBuilder::CreateFromVertices(verts, {}); // No indices

    EXPECT_TRUE(mesh.IsValid());
    EXPECT_EQ(mesh.header.indexCount, 0u);
    EXPECT_EQ(mesh.GetIndexDataSize(), 0u);
    EXPECT_TRUE(mesh.indexData.empty());

    // Round-trip
    ASSERT_TRUE(SaveMesh(m_testPath, mesh));
    Mesh loaded = LoadMesh(m_testPath);
    EXPECT_TRUE(loaded.IsValid());
    EXPECT_EQ(loaded.header.indexCount, 0u);
    EXPECT_TRUE(loaded.indexData.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// File size validation
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(MeshFileTest, FileSizeMatchesExpected) {
    Mesh cube = MeshBuilder::CreateCube(1.0f);
    ASSERT_TRUE(SaveMesh(m_testPath, cube));

    auto fileSize = std::filesystem::file_size(m_testPath);
    uint64_t expected = sizeof(MeshHeader) + cube.GetVertexDataSize() + cube.GetIndexDataSize();
    EXPECT_EQ(fileSize, expected);
}
