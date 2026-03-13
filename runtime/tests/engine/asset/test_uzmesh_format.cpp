/// UzEngine .uzmesh Format — Unit Tests
///
/// Tests WriteUzMesh/ReadUzMesh roundtrip correctness, binary layout
/// constraints (magic, version, 64-byte alignment), and all error paths.

#include "uzmesh_format.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a minimal pre-filled UzMeshHeader with no geometry counts set.
static UzMeshHeader MakeMinimalHeader() {
    UzMeshHeader h;
    h.lodCount = 1;
    h.totalSubmeshCount = 1;
    h.vertexCount = 3;
    h.indexCount = 3;
    h.vertexStride = sizeof(VertexPosNormUV);
    h.indexStride = sizeof(uint32_t);
    return h;
}

/// Returns three VertexPosNormUV vertices forming a unit triangle on the XY plane.
static std::vector<uint8_t> MakeTriangleVertexBytes() {
    std::vector<VertexPosNormUV> verts(3);
    verts[0] = {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}};
    verts[1] = {{1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}};
    verts[2] = {{0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f}};
    std::vector<uint8_t> bytes(verts.size() * sizeof(VertexPosNormUV));
    std::memcpy(bytes.data(), verts.data(), bytes.size());
    return bytes;
}

/// Returns three uint32_t indices {0, 1, 2} as a raw byte vector.
static std::vector<uint8_t> MakeTriangleIndexBytes() {
    uint32_t idx[3] = {0, 1, 2};
    std::vector<uint8_t> bytes(sizeof(idx));
    std::memcpy(bytes.data(), idx, sizeof(idx));
    return bytes;
}

/// Returns a LOD with a single submesh at offset 0.
static UzMeshLOD MakeSingleLOD() {
    UzMeshLOD lod;
    lod.lodLevel = 0;
    lod.submeshOffset = 0;
    lod.submeshCount = 1;
    lod.screenHeightThreshold = 0.0f;
    return lod;
}

/// Returns a submesh covering the full triangle.
static UzMeshSubmesh MakeSingleSubmesh() {
    UzMeshSubmesh sub;
    sub.vertexOffset = 0;
    sub.indexOffset = 0;
    sub.indexCount = 3;
    sub.materialIndex = 0;
    return sub;
}

// ─────────────────────────────────────────────────────────────────────────────
// Header Default Initialization
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzMeshFormat, DefaultHeader_HasCorrectMagicAndVersion) {
    UzMeshHeader h;
    EXPECT_EQ(h.magic, kUzMeshMagic);
    EXPECT_EQ(h.version, kUzMeshVersion);
    EXPECT_EQ(h.flags, 0u);
    EXPECT_EQ(h.lodCount, 0u);
    EXPECT_EQ(h.totalSubmeshCount, 0u);
    EXPECT_EQ(h.vertexCount, 0u);
    EXPECT_EQ(h.indexCount, 0u);
    EXPECT_EQ(h.vertexStride, 0u);
    EXPECT_EQ(h.indexStride, 0u);
    EXPECT_EQ(h.vertexDataOffset, 0u);
    EXPECT_EQ(h.indexDataOffset, 0u);
}

TEST(UzMeshFormat, HeaderSize_IsExactly64Bytes) {
    EXPECT_EQ(sizeof(UzMeshHeader), 64u);
}

TEST(UzMeshFormat, LODSize_IsExactly16Bytes) {
    EXPECT_EQ(sizeof(UzMeshLOD), 16u);
}

TEST(UzMeshFormat, SubmeshSize_IsExactly16Bytes) {
    EXPECT_EQ(sizeof(UzMeshSubmesh), 16u);
}

TEST(UzMeshFormat, VertexPosNormUV_IsExactly32Bytes) {
    EXPECT_EQ(sizeof(VertexPosNormUV), 32u);
}

// ─────────────────────────────────────────────────────────────────────────────
// WriteUzMesh — binary layout
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzMeshFormat, Write_OutputStartsWithMagicAndVersion) {
    auto header = MakeMinimalHeader();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), MakeTriangleIndexBytes());

    ASSERT_GE(blob.size(), sizeof(UzMeshHeader));

    UzMeshHeader parsed;
    std::memcpy(&parsed, blob.data(), sizeof(UzMeshHeader));
    EXPECT_EQ(parsed.magic, kUzMeshMagic);
    EXPECT_EQ(parsed.magic, 0x534D5A55u);
    EXPECT_EQ(parsed.version, kUzMeshVersion);
    EXPECT_EQ(parsed.version, 1u);
}

TEST(UzMeshFormat, Write_VertexDataOffset_Is64ByteAligned) {
    auto header = MakeMinimalHeader();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), MakeTriangleIndexBytes());

    UzMeshHeader parsed;
    std::memcpy(&parsed, blob.data(), sizeof(UzMeshHeader));
    EXPECT_EQ(parsed.vertexDataOffset % 64u, 0u)
        << "vertexDataOffset=" << parsed.vertexDataOffset << " is not 64-byte aligned";
}

TEST(UzMeshFormat, Write_IndexDataOffset_Is64ByteAligned) {
    auto header = MakeMinimalHeader();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), MakeTriangleIndexBytes());

    UzMeshHeader parsed;
    std::memcpy(&parsed, blob.data(), sizeof(UzMeshHeader));
    EXPECT_EQ(parsed.indexDataOffset % 64u, 0u)
        << "indexDataOffset=" << parsed.indexDataOffset << " is not 64-byte aligned";
}

TEST(UzMeshFormat, Write_IndexDataOffset_GreaterThan_VertexDataOffset) {
    auto header = MakeMinimalHeader();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), MakeTriangleIndexBytes());

    UzMeshHeader parsed;
    std::memcpy(&parsed, blob.data(), sizeof(UzMeshHeader));
    EXPECT_GT(parsed.indexDataOffset, parsed.vertexDataOffset);
}

TEST(UzMeshFormat, Write_OutputSize_AtLeastHeaderPlusData) {
    auto header = MakeMinimalHeader();
    auto verts = MakeTriangleVertexBytes();
    auto idxs = MakeTriangleIndexBytes();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()}, verts, idxs);
    EXPECT_GE(blob.size(), sizeof(UzMeshHeader) + verts.size() + idxs.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Roundtrip — basic triangle
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzMeshFormat, Roundtrip_Triangle_HeaderFieldsMatch) {
    auto header = MakeMinimalHeader();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), MakeTriangleIndexBytes());

    auto r = ReadUzMesh(blob);
    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(r.file.header.magic, kUzMeshMagic);
    EXPECT_EQ(r.file.header.version, kUzMeshVersion);
    EXPECT_EQ(r.file.header.flags, 0u);
    EXPECT_EQ(r.file.header.lodCount, 1u);
    EXPECT_EQ(r.file.header.totalSubmeshCount, 1u);
    EXPECT_EQ(r.file.header.vertexCount, 3u);
    EXPECT_EQ(r.file.header.indexCount, 3u);
    EXPECT_EQ(r.file.header.vertexStride, sizeof(VertexPosNormUV));
    EXPECT_EQ(r.file.header.indexStride, sizeof(uint32_t));
}

TEST(UzMeshFormat, Roundtrip_Triangle_LODFieldsMatch) {
    auto header = MakeMinimalHeader();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), MakeTriangleIndexBytes());

    auto r = ReadUzMesh(blob);
    ASSERT_TRUE(r.success) << r.error;
    ASSERT_EQ(r.file.lods.size(), 1u);
    EXPECT_EQ(r.file.lods[0].lodLevel, 0u);
    EXPECT_EQ(r.file.lods[0].submeshOffset, 0u);
    EXPECT_EQ(r.file.lods[0].submeshCount, 1u);
    EXPECT_FLOAT_EQ(r.file.lods[0].screenHeightThreshold, 0.0f);
}

TEST(UzMeshFormat, Roundtrip_Triangle_SubmeshFieldsMatch) {
    auto header = MakeMinimalHeader();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), MakeTriangleIndexBytes());

    auto r = ReadUzMesh(blob);
    ASSERT_TRUE(r.success) << r.error;
    ASSERT_EQ(r.file.submeshes.size(), 1u);
    EXPECT_EQ(r.file.submeshes[0].vertexOffset, 0u);
    EXPECT_EQ(r.file.submeshes[0].indexOffset, 0u);
    EXPECT_EQ(r.file.submeshes[0].indexCount, 3u);
    EXPECT_EQ(r.file.submeshes[0].materialIndex, 0u);
}

TEST(UzMeshFormat, Roundtrip_Triangle_VertexDataMatches) {
    auto header = MakeMinimalHeader();
    auto vertexBytes = MakeTriangleVertexBytes();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            vertexBytes, MakeTriangleIndexBytes());

    auto r = ReadUzMesh(blob);
    ASSERT_TRUE(r.success) << r.error;
    ASSERT_EQ(r.file.vertexData.size(), vertexBytes.size());
    EXPECT_EQ(std::memcmp(r.file.vertexData.data(), vertexBytes.data(), vertexBytes.size()), 0)
        << "Vertex data did not survive roundtrip";
}

TEST(UzMeshFormat, Roundtrip_Triangle_IndexDataMatches) {
    auto header = MakeMinimalHeader();
    auto indexBytes = MakeTriangleIndexBytes();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), indexBytes);

    auto r = ReadUzMesh(blob);
    ASSERT_TRUE(r.success) << r.error;
    ASSERT_EQ(r.file.indexData.size(), indexBytes.size());
    EXPECT_EQ(std::memcmp(r.file.indexData.data(), indexBytes.data(), indexBytes.size()), 0)
        << "Index data did not survive roundtrip";
}

// ─────────────────────────────────────────────────────────────────────────────
// Roundtrip — multiple LODs and submeshes
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzMeshFormat, Roundtrip_MultipleLODsAndSubmeshes_HeaderCounts) {
    // Build a mesh with 2 LODs and 3 submeshes total (LOD0: 2, LOD1: 1)
    UzMeshHeader header;
    header.lodCount = 2;
    header.totalSubmeshCount = 3;
    header.vertexCount = 9;
    header.indexCount = 9;
    header.vertexStride = sizeof(VertexPosNormUV);
    header.indexStride = sizeof(uint32_t);

    std::vector<UzMeshLOD> lods(2);
    lods[0] = {0, 0, 2, 1.0f};
    lods[1] = {1, 2, 1, 0.25f};

    std::vector<UzMeshSubmesh> submeshes(3);
    submeshes[0] = {0, 0, 3, 0};
    submeshes[1] = {3, 3, 3, 1};
    submeshes[2] = {6, 6, 3, 0};

    std::vector<VertexPosNormUV> verts(9);
    for (int i = 0; i < 9; ++i) {
        verts[i] = {{float(i), 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}};
    }
    std::vector<uint8_t> vertBytes(9 * sizeof(VertexPosNormUV));
    std::memcpy(vertBytes.data(), verts.data(), vertBytes.size());

    uint32_t idx[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> idxBytes(sizeof(idx));
    std::memcpy(idxBytes.data(), idx, sizeof(idx));

    auto blob = WriteUzMesh(header, lods, submeshes, vertBytes, idxBytes);
    auto r = ReadUzMesh(blob);

    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(r.file.header.lodCount, 2u);
    EXPECT_EQ(r.file.header.totalSubmeshCount, 3u);
    EXPECT_EQ(r.file.header.vertexCount, 9u);
    EXPECT_EQ(r.file.header.indexCount, 9u);
}

TEST(UzMeshFormat, Roundtrip_MultipleLODsAndSubmeshes_LODFieldsPreserved) {
    UzMeshHeader header;
    header.lodCount = 2;
    header.totalSubmeshCount = 3;
    header.vertexCount = 9;
    header.indexCount = 9;
    header.vertexStride = sizeof(VertexPosNormUV);
    header.indexStride = sizeof(uint32_t);

    std::vector<UzMeshLOD> lods(2);
    lods[0] = {0, 0, 2, 1.0f};
    lods[1] = {1, 2, 1, 0.25f};

    std::vector<UzMeshSubmesh> submeshes(3);
    submeshes[0] = {0, 0, 3, 0};
    submeshes[1] = {3, 3, 3, 1};
    submeshes[2] = {6, 6, 3, 0};

    std::vector<uint8_t> vertBytes(9 * sizeof(VertexPosNormUV), 0);
    uint32_t idx[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> idxBytes(sizeof(idx));
    std::memcpy(idxBytes.data(), idx, sizeof(idx));

    auto blob = WriteUzMesh(header, lods, submeshes, vertBytes, idxBytes);
    auto r = ReadUzMesh(blob);

    ASSERT_TRUE(r.success) << r.error;
    ASSERT_EQ(r.file.lods.size(), 2u);
    EXPECT_EQ(r.file.lods[0].lodLevel, 0u);
    EXPECT_EQ(r.file.lods[0].submeshOffset, 0u);
    EXPECT_EQ(r.file.lods[0].submeshCount, 2u);
    EXPECT_FLOAT_EQ(r.file.lods[0].screenHeightThreshold, 1.0f);

    EXPECT_EQ(r.file.lods[1].lodLevel, 1u);
    EXPECT_EQ(r.file.lods[1].submeshOffset, 2u);
    EXPECT_EQ(r.file.lods[1].submeshCount, 1u);
    EXPECT_FLOAT_EQ(r.file.lods[1].screenHeightThreshold, 0.25f);
}

TEST(UzMeshFormat, Roundtrip_MultipleLODsAndSubmeshes_SubmeshFieldsPreserved) {
    UzMeshHeader header;
    header.lodCount = 2;
    header.totalSubmeshCount = 3;
    header.vertexCount = 9;
    header.indexCount = 9;
    header.vertexStride = sizeof(VertexPosNormUV);
    header.indexStride = sizeof(uint32_t);

    std::vector<UzMeshLOD> lods(2);
    lods[0] = {0, 0, 2, 1.0f};
    lods[1] = {1, 2, 1, 0.25f};

    std::vector<UzMeshSubmesh> submeshes(3);
    submeshes[0] = {0, 0, 3, 0};
    submeshes[1] = {3, 3, 3, 1};
    submeshes[2] = {6, 6, 3, 0};

    std::vector<uint8_t> vertBytes(9 * sizeof(VertexPosNormUV), 0);
    uint32_t idx[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> idxBytes(sizeof(idx));
    std::memcpy(idxBytes.data(), idx, sizeof(idx));

    auto blob = WriteUzMesh(header, lods, submeshes, vertBytes, idxBytes);
    auto r = ReadUzMesh(blob);

    ASSERT_TRUE(r.success) << r.error;
    ASSERT_EQ(r.file.submeshes.size(), 3u);

    EXPECT_EQ(r.file.submeshes[0].vertexOffset, 0u);
    EXPECT_EQ(r.file.submeshes[0].indexOffset, 0u);
    EXPECT_EQ(r.file.submeshes[0].indexCount, 3u);
    EXPECT_EQ(r.file.submeshes[0].materialIndex, 0u);

    EXPECT_EQ(r.file.submeshes[1].vertexOffset, 3u);
    EXPECT_EQ(r.file.submeshes[1].indexOffset, 3u);
    EXPECT_EQ(r.file.submeshes[1].indexCount, 3u);
    EXPECT_EQ(r.file.submeshes[1].materialIndex, 1u);

    EXPECT_EQ(r.file.submeshes[2].vertexOffset, 6u);
    EXPECT_EQ(r.file.submeshes[2].indexOffset, 6u);
    EXPECT_EQ(r.file.submeshes[2].indexCount, 3u);
    EXPECT_EQ(r.file.submeshes[2].materialIndex, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Error paths — ReadUzMesh
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzMeshFormat, Read_EmptyData_ReturnsError) {
    std::vector<uint8_t> empty;
    auto r = ReadUzMesh(empty);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

TEST(UzMeshFormat, Read_TruncatedHeader_ReturnsError) {
    std::vector<uint8_t> tiny(32, 0); // Less than 64-byte header
    auto r = ReadUzMesh(tiny);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

TEST(UzMeshFormat, Read_WrongMagic_ReturnsErrorWithMessage) {
    auto header = MakeMinimalHeader();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), MakeTriangleIndexBytes());

    // Corrupt magic at bytes 0..3
    blob[0] = 0xDE;
    blob[1] = 0xAD;
    blob[2] = 0xBE;
    blob[3] = 0xEF;

    auto r = ReadUzMesh(blob);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty()) << "Expected a non-empty error message for wrong magic";
}

TEST(UzMeshFormat, Read_WrongVersion_ReturnsErrorWithVersionNumber) {
    auto header = MakeMinimalHeader();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), MakeTriangleIndexBytes());

    // Overwrite version field (bytes 4..7) with version 99
    uint32_t badVersion = 99u;
    std::memcpy(blob.data() + 4, &badVersion, sizeof(badVersion));

    auto r = ReadUzMesh(blob);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("99"), std::string::npos)
        << "Error message should contain the bad version number; got: " << r.error;
}

TEST(UzMeshFormat, Read_TruncatedLODArray_ReturnsError) {
    // Craft a header claiming 100 LODs but provide minimal data
    UzMeshHeader h = MakeMinimalHeader();
    h.lodCount = 100;
    h.totalSubmeshCount = 0;
    h.vertexCount = 0;
    h.indexCount = 0;

    // Write just the header (64 bytes) — insufficient for 100 LODs
    std::vector<uint8_t> blob(sizeof(UzMeshHeader), 0);
    std::memcpy(blob.data(), &h, sizeof(h));

    auto r = ReadUzMesh(blob);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

TEST(UzMeshFormat, Read_TruncatedSubmeshArray_ReturnsError) {
    // Header with 1 LOD and 100 submeshes, data truncated after the LOD
    UzMeshHeader h = MakeMinimalHeader();
    h.lodCount = 1;
    h.totalSubmeshCount = 100;
    h.vertexCount = 0;
    h.indexCount = 0;

    std::vector<uint8_t> blob(sizeof(UzMeshHeader) + sizeof(UzMeshLOD), 0);
    std::memcpy(blob.data(), &h, sizeof(h));

    auto r = ReadUzMesh(blob);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

TEST(UzMeshFormat, Read_TruncatedVertexData_ReturnsError) {
    auto header = MakeMinimalHeader();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), MakeTriangleIndexBytes());

    // Truncate right after the index data offset to cut off vertex data
    UzMeshHeader parsed;
    std::memcpy(&parsed, blob.data(), sizeof(UzMeshHeader));
    blob.resize(parsed.vertexDataOffset + 1); // Leave only 1 byte of vertex data

    auto r = ReadUzMesh(blob);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

TEST(UzMeshFormat, Read_TruncatedIndexData_ReturnsError) {
    auto header = MakeMinimalHeader();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), MakeTriangleIndexBytes());

    // Truncate right after vertex data ends to cut off index data
    UzMeshHeader parsed;
    std::memcpy(&parsed, blob.data(), sizeof(UzMeshHeader));
    blob.resize(parsed.indexDataOffset + 1); // Leave only 1 byte of index data

    auto r = ReadUzMesh(blob);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

TEST(UzMeshFormat, Read_OnSuccess_ErrorStringIsEmpty) {
    auto header = MakeMinimalHeader();
    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), MakeTriangleIndexBytes());
    auto r = ReadUzMesh(blob);
    ASSERT_TRUE(r.success);
    EXPECT_TRUE(r.error.empty()) << "On success, error should be empty; got: " << r.error;
}

// ─────────────────────────────────────────────────────────────────────────────
// uint16 index stride roundtrip
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzMeshFormat, Roundtrip_Uint16Indices_StrideStoredCorrectly) {
    UzMeshHeader header;
    header.lodCount = 1;
    header.totalSubmeshCount = 1;
    header.vertexCount = 3;
    header.indexCount = 3;
    header.vertexStride = sizeof(VertexPosNormUV);
    header.indexStride = sizeof(uint16_t);

    uint16_t idx16[3] = {0, 1, 2};
    std::vector<uint8_t> idxBytes(sizeof(idx16));
    std::memcpy(idxBytes.data(), idx16, sizeof(idx16));

    auto blob = WriteUzMesh(header, {MakeSingleLOD()}, {MakeSingleSubmesh()},
                            MakeTriangleVertexBytes(), idxBytes);
    auto r = ReadUzMesh(blob);

    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(r.file.header.indexStride, sizeof(uint16_t));
    ASSERT_EQ(r.file.indexData.size(), sizeof(idx16));

    uint16_t readBack[3];
    std::memcpy(readBack, r.file.indexData.data(), sizeof(readBack));
    EXPECT_EQ(readBack[0], 0u);
    EXPECT_EQ(readBack[1], 1u);
    EXPECT_EQ(readBack[2], 2u);
}
