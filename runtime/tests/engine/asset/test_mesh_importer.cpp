/// UzEngine Mesh Importer — Unit Tests
///
/// Tests MeshImporter interface contract (Name/Version/Extensions) and
/// the Import() pipeline for valid GLB, empty data, and invalid data.
/// The minimal GLB is constructed in memory — no file I/O required.

#include "mesh_importer.h"
#include "uzmesh_format.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Minimal valid GLB binary (1 triangle, POSITION + uint32 indices)
//
// Generated from:
//   {"asset":{"version":"2.0"},
//    "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
//    "accessors":[
//      {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",
//       "max":[1.0,1.0,0.0],"min":[0.0,0.0,0.0]},
//      {"bufferView":1,"componentType":5125,"count":3,"type":"SCALAR"}
//    ],
//    "bufferViews":[
//      {"buffer":0,"byteOffset":0,"byteLength":36},
//      {"buffer":0,"byteOffset":36,"byteLength":12}
//    ],
//    "buffers":[{"byteLength":48}]}
//
// BIN: 3×vec3 float32 positions (36 bytes) + 3×uint32 indices (12 bytes)
// ─────────────────────────────────────────────────────────────────────────────

static const uint8_t kMinimalGlbBytes[] = {
0x67, 0x6C, 0x54, 0x46, 0x02, 0x00, 0x00, 0x00, 0xE8, 0x01, 0x00, 0x00, 0x9C,
0x01, 0x00, 0x00, 0x4A, 0x53, 0x4F, 0x4E, 0x7B, 0x22, 0x61, 0x73, 0x73, 0x65,
0x74, 0x22, 0x3A, 0x7B, 0x22, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x22,
0x3A, 0x22, 0x32, 0x2E, 0x30, 0x22, 0x7D, 0x2C, 0x22, 0x6D, 0x65, 0x73, 0x68,
0x65, 0x73, 0x22, 0x3A, 0x5B, 0x7B, 0x22, 0x70, 0x72, 0x69, 0x6D, 0x69, 0x74,
0x69, 0x76, 0x65, 0x73, 0x22, 0x3A, 0x5B, 0x7B, 0x22, 0x61, 0x74, 0x74, 0x72,
0x69, 0x62, 0x75, 0x74, 0x65, 0x73, 0x22, 0x3A, 0x7B, 0x22, 0x50, 0x4F, 0x53,
0x49, 0x54, 0x49, 0x4F, 0x4E, 0x22, 0x3A, 0x30, 0x7D, 0x2C, 0x22, 0x69, 0x6E,
0x64, 0x69, 0x63, 0x65, 0x73, 0x22, 0x3A, 0x31, 0x7D, 0x5D, 0x7D, 0x5D, 0x2C,
0x22, 0x61, 0x63, 0x63, 0x65, 0x73, 0x73, 0x6F, 0x72, 0x73, 0x22, 0x3A, 0x5B,
0x7B, 0x22, 0x62, 0x75, 0x66, 0x66, 0x65, 0x72, 0x56, 0x69, 0x65, 0x77, 0x22,
0x3A, 0x30, 0x2C, 0x22, 0x63, 0x6F, 0x6D, 0x70, 0x6F, 0x6E, 0x65, 0x6E, 0x74,
0x54, 0x79, 0x70, 0x65, 0x22, 0x3A, 0x35, 0x31, 0x32, 0x36, 0x2C, 0x22, 0x63,
0x6F, 0x75, 0x6E, 0x74, 0x22, 0x3A, 0x33, 0x2C, 0x22, 0x74, 0x79, 0x70, 0x65,
0x22, 0x3A, 0x22, 0x56, 0x45, 0x43, 0x33, 0x22, 0x2C, 0x22, 0x6D, 0x61, 0x78,
0x22, 0x3A, 0x5B, 0x31, 0x2E, 0x30, 0x2C, 0x31, 0x2E, 0x30, 0x2C, 0x30, 0x2E,
0x30, 0x5D, 0x2C, 0x22, 0x6D, 0x69, 0x6E, 0x22, 0x3A, 0x5B, 0x30, 0x2E, 0x30,
0x2C, 0x30, 0x2E, 0x30, 0x2C, 0x30, 0x2E, 0x30, 0x5D, 0x7D, 0x2C, 0x7B, 0x22,
0x62, 0x75, 0x66, 0x66, 0x65, 0x72, 0x56, 0x69, 0x65, 0x77, 0x22, 0x3A, 0x31,
0x2C, 0x22, 0x63, 0x6F, 0x6D, 0x70, 0x6F, 0x6E, 0x65, 0x6E, 0x74, 0x54, 0x79,
0x70, 0x65, 0x22, 0x3A, 0x35, 0x31, 0x32, 0x35, 0x2C, 0x22, 0x63, 0x6F, 0x75,
0x6E, 0x74, 0x22, 0x3A, 0x33, 0x2C, 0x22, 0x74, 0x79, 0x70, 0x65, 0x22, 0x3A,
0x22, 0x53, 0x43, 0x41, 0x4C, 0x41, 0x52, 0x22, 0x7D, 0x5D, 0x2C, 0x22, 0x62,
0x75, 0x66, 0x66, 0x65, 0x72, 0x56, 0x69, 0x65, 0x77, 0x73, 0x22, 0x3A, 0x5B,
0x7B, 0x22, 0x62, 0x75, 0x66, 0x66, 0x65, 0x72, 0x22, 0x3A, 0x30, 0x2C, 0x22,
0x62, 0x79, 0x74, 0x65, 0x4F, 0x66, 0x66, 0x73, 0x65, 0x74, 0x22, 0x3A, 0x30,
0x2C, 0x22, 0x62, 0x79, 0x74, 0x65, 0x4C, 0x65, 0x6E, 0x67, 0x74, 0x68, 0x22,
0x3A, 0x33, 0x36, 0x7D, 0x2C, 0x7B, 0x22, 0x62, 0x75, 0x66, 0x66, 0x65, 0x72,
0x22, 0x3A, 0x30, 0x2C, 0x22, 0x62, 0x79, 0x74, 0x65, 0x4F, 0x66, 0x66, 0x73,
0x65, 0x74, 0x22, 0x3A, 0x33, 0x36, 0x2C, 0x22, 0x62, 0x79, 0x74, 0x65, 0x4C,
0x65, 0x6E, 0x67, 0x74, 0x68, 0x22, 0x3A, 0x31, 0x32, 0x7D, 0x5D, 0x2C, 0x22,
0x62, 0x75, 0x66, 0x66, 0x65, 0x72, 0x73, 0x22, 0x3A, 0x5B, 0x7B, 0x22, 0x62,
0x79, 0x74, 0x65, 0x4C, 0x65, 0x6E, 0x67, 0x74, 0x68, 0x22, 0x3A, 0x34, 0x38,
0x7D, 0x5D, 0x7D, 0x30, 0x00, 0x00, 0x00, 0x42, 0x49, 0x4E, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00
};

static std::vector<uint8_t> MakeMinimalGlbData() {
    return std::vector<uint8_t>(kMinimalGlbBytes,
                                kMinimalGlbBytes + sizeof(kMinimalGlbBytes));
}

// ─────────────────────────────────────────────────────────────────────────────
// Interface contract
// ─────────────────────────────────────────────────────────────────────────────

TEST(MeshImporter, Name_ReturnsExactString) {
    MeshImporter imp;
    EXPECT_STREQ(imp.Name(), "mesh");
}

TEST(MeshImporter, Version_ReturnsTwo) {
    MeshImporter imp;
    EXPECT_EQ(imp.Version(), 2u);
}

TEST(MeshImporter, Extensions_ContainsGlb) {
    MeshImporter imp;
    const auto& exts = imp.Extensions();
    EXPECT_NE(std::find(exts.begin(), exts.end(), ".glb"), exts.end())
        << ".glb must be in Extensions()";
}

TEST(MeshImporter, Extensions_ContainsGltf) {
    MeshImporter imp;
    const auto& exts = imp.Extensions();
    EXPECT_NE(std::find(exts.begin(), exts.end(), ".gltf"), exts.end())
        << ".gltf must be in Extensions()";
}

TEST(MeshImporter, Extensions_HasAtLeastTwoEntries) {
    MeshImporter imp;
    EXPECT_GE(imp.Extensions().size(), 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Error paths
// ─────────────────────────────────────────────────────────────────────────────

TEST(MeshImporter, Import_EmptySourceData_ReturnsFalseWithError) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import({}, "empty.glb", opts);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
    EXPECT_TRUE(r.data.empty());
}

TEST(MeshImporter, Import_InvalidData_NotGltf_ReturnsFalseWithError) {
    MeshImporter imp;
    AssetImportOptions opts;

    // Random non-glTF bytes
    std::vector<uint8_t> junk = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03,
                                  0xFF, 0xFE, 0xFD, 0xFC, 0xAB, 0xCD, 0xEF, 0x00};
    auto r = imp.Import(junk, "bad.glb", opts);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
    EXPECT_TRUE(r.data.empty());
}

TEST(MeshImporter, Import_TextData_NotGltf_ReturnsFalseWithError) {
    MeshImporter imp;
    AssetImportOptions opts;

    // Plain text — not valid glTF
    const std::string text = "this is not a mesh file at all";
    std::vector<uint8_t> data(text.begin(), text.end());
    auto r = imp.Import(data, "text.glb", opts);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Valid GLB import
// ─────────────────────────────────────────────────────────────────────────────

TEST(MeshImporter, Import_ValidGlb_ReturnsSuccess) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalGlbData(), "triangle.glb", opts);
    EXPECT_TRUE(r.success) << "Import failed: " << r.error;
    EXPECT_TRUE(r.error.empty());
    EXPECT_FALSE(r.data.empty());
}

TEST(MeshImporter, Import_ValidGlb_OutputIsValidUzMeshFile) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalGlbData(), "triangle.glb", opts);
    ASSERT_TRUE(r.success) << r.error;

    // Output must parse as a valid .uzmesh
    auto parsed = ReadUzMesh(r.data);
    EXPECT_TRUE(parsed.success) << "ReadUzMesh failed: " << parsed.error;
}

TEST(MeshImporter, Import_ValidGlb_OutputHasCorrectMagic) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalGlbData(), "triangle.glb", opts);
    ASSERT_TRUE(r.success) << r.error;
    ASSERT_GE(r.data.size(), sizeof(UzMeshHeader));

    UzMeshHeader h;
    std::memcpy(&h, r.data.data(), sizeof(h));
    EXPECT_EQ(h.magic, kUzMeshMagic);
    EXPECT_EQ(h.version, kUzMeshVersion);
}

TEST(MeshImporter, Import_ValidGlb_HasOneLOD) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalGlbData(), "triangle.glb", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzMesh(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    EXPECT_EQ(parsed.file.header.lodCount, 1u);
    ASSERT_EQ(parsed.file.lods.size(), 1u);
    EXPECT_EQ(parsed.file.lods[0].lodLevel, 0u);
    EXPECT_EQ(parsed.file.lods[0].submeshCount, 1u);
}

TEST(MeshImporter, Import_ValidGlb_HasOneSubmesh) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalGlbData(), "triangle.glb", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzMesh(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    EXPECT_EQ(parsed.file.header.totalSubmeshCount, 1u);
    ASSERT_EQ(parsed.file.submeshes.size(), 1u);
    EXPECT_EQ(parsed.file.submeshes[0].indexCount, 3u);
}

TEST(MeshImporter, Import_ValidGlb_ThreeVertices) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalGlbData(), "triangle.glb", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzMesh(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;

    // meshoptimizer may deduplicate — count must be >= 1 and <= 3
    EXPECT_GE(parsed.file.header.vertexCount, 1u);
    EXPECT_LE(parsed.file.header.vertexCount, 3u);
}

TEST(MeshImporter, Import_ValidGlb_ThreeIndices) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalGlbData(), "triangle.glb", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzMesh(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    EXPECT_EQ(parsed.file.header.indexCount, 3u);
}

TEST(MeshImporter, Import_ValidGlb_VertexStrideIs32Bytes) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalGlbData(), "triangle.glb", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzMesh(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    EXPECT_EQ(parsed.file.header.vertexStride, sizeof(VertexPosNormUV));
    EXPECT_EQ(parsed.file.header.vertexStride, 32u);
}

TEST(MeshImporter, Import_ValidGlb_IndexStrideIs4Bytes) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalGlbData(), "triangle.glb", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzMesh(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    EXPECT_EQ(parsed.file.header.indexStride, sizeof(uint32_t));
    EXPECT_EQ(parsed.file.header.indexStride, 4u);
}

TEST(MeshImporter, Import_ValidGlb_VertexDataOffset_Is64ByteAligned) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalGlbData(), "triangle.glb", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzMesh(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    EXPECT_EQ(parsed.file.header.vertexDataOffset % 64u, 0u)
        << "vertexDataOffset not 64-byte aligned: " << parsed.file.header.vertexDataOffset;
}

TEST(MeshImporter, Import_ValidGlb_IndexDataOffset_Is64ByteAligned) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalGlbData(), "triangle.glb", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzMesh(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;
    EXPECT_EQ(parsed.file.header.indexDataOffset % 64u, 0u)
        << "indexDataOffset not 64-byte aligned: " << parsed.file.header.indexDataOffset;
}

TEST(MeshImporter, Import_ValidGlb_FlatNormalsGenerated_NonZero) {
    // The minimal GLB has no NORMAL attribute — importer must generate flat normals.
    // For a triangle in the XY plane, the normal should be (0, 0, ±1).
    MeshImporter imp;
    AssetImportOptions opts;
    auto r = imp.Import(MakeMinimalGlbData(), "triangle.glb", opts);
    ASSERT_TRUE(r.success) << r.error;

    auto parsed = ReadUzMesh(r.data);
    ASSERT_TRUE(parsed.success) << parsed.error;

    // Read first vertex normal from vertex data
    ASSERT_GE(parsed.file.vertexData.size(), sizeof(VertexPosNormUV));
    VertexPosNormUV v0;
    std::memcpy(&v0, parsed.file.vertexData.data(), sizeof(v0));

    // Normal must not be a zero vector
    const float nx = v0.normal[0];
    const float ny = v0.normal[1];
    const float nz = v0.normal[2];
    const float len2 = nx * nx + ny * ny + nz * nz;
    EXPECT_GT(len2, 0.9f) << "Generated flat normal is near-zero; nx=" << nx
                           << " ny=" << ny << " nz=" << nz;
}

TEST(MeshImporter, Import_IsDeterministic_SameInputSameOutput) {
    MeshImporter imp;
    AssetImportOptions opts;
    auto glbData = MakeMinimalGlbData();

    auto r1 = imp.Import(glbData, "triangle.glb", opts);
    auto r2 = imp.Import(glbData, "triangle.glb", opts);

    ASSERT_TRUE(r1.success) << r1.error;
    ASSERT_TRUE(r2.success) << r2.error;
    ASSERT_EQ(r1.data.size(), r2.data.size());
    EXPECT_EQ(std::memcmp(r1.data.data(), r2.data.data(), r1.data.size()), 0)
        << "Import is not deterministic — identical inputs produced different outputs";
}
