/// Tests for GPUMesh struct defaults, validity predicates, and null-safe destroy/upload.
///
/// All tests are CPU-only — no GPU device is created. They verify:
/// - Default field values for a zero-initialized GPUMesh
/// - IsValid() and IsIndexed() logic branches
/// - DestroyGPUMesh(nullptr, mesh) is a safe no-op
/// - UploadMesh(nullptr, nullptr, mesh) returns an invalid GPUMesh

#include <gtest/gtest.h>

#include "gpu_mesh.h"
#include "mesh.h"
#include "rhi_types.h"

#include <cstdint>
#include <limits>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a GPUMesh with a valid vertex buffer handle and one vertex.
static GPUMesh MakeValidVertexOnlyMesh() {
    GPUMesh m;
    m.vertexBuffer.index      = 1u;
    m.vertexBuffer.generation = 1u;
    m.vertexCount             = 3u;
    return m;
}

/// Returns a GPUMesh with valid vertex and index buffers.
static GPUMesh MakeValidIndexedMesh() {
    GPUMesh m = MakeValidVertexOnlyMesh();
    m.indexBuffer.index      = 2u;
    m.indexBuffer.generation = 1u;
    m.indexCount             = 3u;
    return m;
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUMesh default state
// ─────────────────────────────────────────────────────────────────────────────

/// Default-constructed GPUMesh vertex buffer handle must be invalid.
TEST(GPUMesh, Default_VertexBuffer_IsInvalid) {
    GPUMesh m;
    EXPECT_FALSE(m.vertexBuffer.IsValid())
        << "Default GPUMesh must have an invalid vertex buffer handle";
}

/// Default-constructed GPUMesh index buffer handle must be invalid.
TEST(GPUMesh, Default_IndexBuffer_IsInvalid) {
    GPUMesh m;
    EXPECT_FALSE(m.indexBuffer.IsValid())
        << "Default GPUMesh must have an invalid index buffer handle";
}

/// Default vbSrvIndex must be UINT32_MAX (sentinel for 'no bindless SRV').
TEST(GPUMesh, Default_VbSrvIndex_IsMaxUint32) {
    GPUMesh m;
    EXPECT_EQ(m.vbSrvIndex, UINT32_MAX);
}

/// Default indexCount must be 0.
TEST(GPUMesh, Default_IndexCount_IsZero) {
    GPUMesh m;
    EXPECT_EQ(m.indexCount, 0u);
}

/// Default vertexCount must be 0.
TEST(GPUMesh, Default_VertexCount_IsZero) {
    GPUMesh m;
    EXPECT_EQ(m.vertexCount, 0u);
}

/// Default indexFormat must be RHIIndexFormat::Uint32.
TEST(GPUMesh, Default_IndexFormat_IsUint32) {
    GPUMesh m;
    EXPECT_EQ(m.indexFormat, RHIIndexFormat::Uint32);
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUMesh::IsValid()
// ─────────────────────────────────────────────────────────────────────────────

/// Default GPUMesh must not be valid.
TEST(GPUMesh, Default_IsValid_ReturnsFalse) {
    GPUMesh m;
    EXPECT_FALSE(m.IsValid());
}

/// GPUMesh with a valid vertex buffer must report IsValid() == true.
TEST(GPUMesh, ValidVertexBuffer_IsValid_ReturnsTrue) {
    GPUMesh m = MakeValidVertexOnlyMesh();
    EXPECT_TRUE(m.IsValid());
}

/// GPUMesh with an invalid vertex buffer but valid index buffer is not valid.
TEST(GPUMesh, InvalidVertexBuffer_ValidIndexBuffer_IsNotValid) {
    GPUMesh m;
    m.indexBuffer.index      = 1u;
    m.indexBuffer.generation = 1u;
    m.indexCount             = 3u;
    EXPECT_FALSE(m.IsValid())
        << "IsValid() must return false when only the index buffer is valid";
}

/// Resetting vertex buffer generation to 0 must make previously-valid mesh invalid.
TEST(GPUMesh, AfterInvalidatingVertexBuffer_IsValid_ReturnsFalse) {
    GPUMesh m = MakeValidVertexOnlyMesh();
    ASSERT_TRUE(m.IsValid());

    m.vertexBuffer.generation = 0u;
    EXPECT_FALSE(m.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// GPUMesh::IsIndexed()
// ─────────────────────────────────────────────────────────────────────────────

/// Default GPUMesh must not be indexed.
TEST(GPUMesh, Default_IsIndexed_ReturnsFalse) {
    GPUMesh m;
    EXPECT_FALSE(m.IsIndexed());
}

/// GPUMesh with valid index buffer and nonzero indexCount must be indexed.
TEST(GPUMesh, ValidIndexBuffer_NonZeroCount_IsIndexed_ReturnsTrue) {
    GPUMesh m = MakeValidIndexedMesh();
    EXPECT_TRUE(m.IsIndexed());
}

/// GPUMesh with valid index buffer but indexCount==0 must not be indexed.
TEST(GPUMesh, ValidIndexBuffer_ZeroCount_IsIndexed_ReturnsFalse) {
    GPUMesh m;
    m.indexBuffer.index      = 1u;
    m.indexBuffer.generation = 1u;
    m.indexCount             = 0u;
    EXPECT_FALSE(m.IsIndexed())
        << "IsIndexed() must require indexCount > 0";
}

/// GPUMesh with nonzero indexCount but invalid index buffer must not be indexed.
TEST(GPUMesh, NonZeroCount_InvalidIndexBuffer_IsIndexed_ReturnsFalse) {
    GPUMesh m;
    m.indexCount = 6u;
    // indexBuffer left as default (invalid)
    EXPECT_FALSE(m.IsIndexed())
        << "IsIndexed() must require a valid index buffer handle";
}

/// IsIndexed() is independent of IsValid() — vertex-only mesh is not indexed.
TEST(GPUMesh, VertexOnlyMesh_IsNotIndexed) {
    GPUMesh m = MakeValidVertexOnlyMesh();
    EXPECT_TRUE(m.IsValid());
    EXPECT_FALSE(m.IsIndexed());
}

/// Uint16 index format on an indexed mesh is preserved and mesh remains indexed.
TEST(GPUMesh, Uint16IndexFormat_Preserved) {
    GPUMesh m = MakeValidIndexedMesh();
    m.indexFormat = RHIIndexFormat::Uint16;
    EXPECT_EQ(m.indexFormat, RHIIndexFormat::Uint16);
    EXPECT_TRUE(m.IsIndexed());
}

// ─────────────────────────────────────────────────────────────────────────────
// DestroyGPUMesh — null device safety
// ─────────────────────────────────────────────────────────────────────────────

/// DestroyGPUMesh with null device on a default mesh must not crash.
TEST(DestroyGPUMesh, NullDevice_DefaultMesh_DoesNotCrash) {
    GPUMesh m;
    EXPECT_NO_FATAL_FAILURE({
        DestroyGPUMesh(nullptr, m);
    });
}

/// DestroyGPUMesh with null device on a 'valid' mesh must not crash.
TEST(DestroyGPUMesh, NullDevice_ValidMesh_DoesNotCrash) {
    GPUMesh m = MakeValidIndexedMesh();
    EXPECT_NO_FATAL_FAILURE({
        DestroyGPUMesh(nullptr, m);
    });
}

/// Calling DestroyGPUMesh twice on the same mesh must not crash.
TEST(DestroyGPUMesh, CalledTwice_DoesNotCrash) {
    GPUMesh m;
    EXPECT_NO_FATAL_FAILURE({
        DestroyGPUMesh(nullptr, m);
        DestroyGPUMesh(nullptr, m);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// UploadMesh — null device / null cmdList returns invalid mesh
// ─────────────────────────────────────────────────────────────────────────────

/// UploadMesh with null device and null cmdList must return an invalid GPUMesh.
TEST(UploadMesh, NullDevice_NullCmdList_ReturnsInvalidMesh) {
    Mesh emptyMesh{};
    GPUMesh result = UploadMesh(nullptr, nullptr, emptyMesh);

    EXPECT_FALSE(result.IsValid())
        << "UploadMesh with null device must return invalid GPUMesh";
}

/// UploadMesh with null device must return mesh with invalid vertex buffer.
TEST(UploadMesh, NullDevice_VertexBuffer_IsInvalid) {
    Mesh emptyMesh{};
    GPUMesh result = UploadMesh(nullptr, nullptr, emptyMesh);

    EXPECT_FALSE(result.vertexBuffer.IsValid());
}

/// UploadMesh with null device must return mesh with invalid index buffer.
TEST(UploadMesh, NullDevice_IndexBuffer_IsInvalid) {
    Mesh emptyMesh{};
    GPUMesh result = UploadMesh(nullptr, nullptr, emptyMesh);

    EXPECT_FALSE(result.indexBuffer.IsValid());
}

/// UploadMesh with null device must return zero indexCount.
TEST(UploadMesh, NullDevice_IndexCount_IsZero) {
    Mesh emptyMesh{};
    GPUMesh result = UploadMesh(nullptr, nullptr, emptyMesh);

    EXPECT_EQ(result.indexCount, 0u);
}

/// UploadMesh with null device must return zero vertexCount.
TEST(UploadMesh, NullDevice_VertexCount_IsZero) {
    Mesh emptyMesh{};
    GPUMesh result = UploadMesh(nullptr, nullptr, emptyMesh);

    EXPECT_EQ(result.vertexCount, 0u);
}

/// UploadMesh with null device must not crash even when called repeatedly.
TEST(UploadMesh, NullDevice_CalledMultipleTimes_DoesNotCrash) {
    Mesh emptyMesh{};
    EXPECT_NO_FATAL_FAILURE({
        for (int i = 0; i < 5; ++i) {
            GPUMesh r = UploadMesh(nullptr, nullptr, emptyMesh);
            (void)r;
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// RHIBufferHandle — validity semantics (used by GPUMesh internally)
// ─────────────────────────────────────────────────────────────────────────────

/// RHIBufferHandle::Invalid() must report IsValid() == false.
TEST(RHIBufferHandle, Invalid_IsNotValid) {
    auto h = RHIBufferHandle::Invalid();
    EXPECT_FALSE(h.IsValid());
}

/// RHIBufferHandle with generation=0 must not be valid regardless of index.
TEST(RHIBufferHandle, ZeroGeneration_AnyIndex_IsNotValid) {
    RHIBufferHandle h;
    h.index      = 1000u;
    h.generation = 0u;
    EXPECT_FALSE(h.IsValid());
}

/// RHIBufferHandle with generation=1 must be valid.
TEST(RHIBufferHandle, NonZeroGeneration_IsValid) {
    RHIBufferHandle h;
    h.index      = 5u;
    h.generation = 1u;
    EXPECT_TRUE(h.IsValid());
}

/// Two equal RHIBufferHandle values compare equal.
TEST(RHIBufferHandle, EqualHandles_CompareEqual) {
    RHIBufferHandle a{3u, 7u};
    RHIBufferHandle b{3u, 7u};
    EXPECT_EQ(a, b);
}

/// Two RHIBufferHandle values with different generation compare not equal.
TEST(RHIBufferHandle, DifferentGeneration_CompareNotEqual) {
    RHIBufferHandle a{1u, 1u};
    RHIBufferHandle b{1u, 2u};
    EXPECT_NE(a, b);
}
