/// Unit tests for VFX Procedural Mesh Generation.
///
/// Covers: GenerateCubeMesh vertex/index counts, GenerateSphereMesh vertex/index
/// counts, buffer validity, DestroyProceduralMesh cleanup, null device handling,
/// and failing device error paths. Uses MockRHIDevice (no GPU required).

#include <gtest/gtest.h>

#include "vfx/mesh/vfx_procedural_mesh.h"
#include "mock_rhi_device.h"

using namespace uz;

// =============================================================================
// VfxMeshVertex layout
// =============================================================================

TEST(UZ_VfxMeshVertex, SizeIs32Bytes) {
    EXPECT_EQ(sizeof(VfxMeshVertex), 32u)
        << "VfxMeshVertex must be exactly 32 bytes for GPU stride";
}

// =============================================================================
// GenerateCubeMesh
// =============================================================================

TEST(UZ_GenerateCubeMesh, ReturnsValid_OnMockDevice) {
    test::MockRHIDevice device;
    auto mesh = GenerateCubeMesh(&device);
    EXPECT_TRUE(mesh.IsValid());
    EXPECT_TRUE(mesh.vb.IsValid());
    EXPECT_TRUE(mesh.ib.IsValid());
    DestroyProceduralMesh(&device, mesh);
}

TEST(UZ_GenerateCubeMesh, VertexCount_Is24) {
    test::MockRHIDevice device;
    auto mesh = GenerateCubeMesh(&device);
    EXPECT_EQ(mesh.vertexCount, 24u)
        << "Cube must have 24 vertices (4 per face, 6 faces)";
    DestroyProceduralMesh(&device, mesh);
}

TEST(UZ_GenerateCubeMesh, IndexCount_Is36) {
    test::MockRHIDevice device;
    auto mesh = GenerateCubeMesh(&device);
    EXPECT_EQ(mesh.indexCount, 36u)
        << "Cube must have 36 indices (6 indices per face, 6 faces)";
    DestroyProceduralMesh(&device, mesh);
}

TEST(UZ_GenerateCubeMesh, NullDevice_ReturnsInvalid) {
    auto mesh = GenerateCubeMesh(nullptr);
    EXPECT_FALSE(mesh.IsValid());
    EXPECT_EQ(mesh.vertexCount, 0u);
    EXPECT_EQ(mesh.indexCount, 0u);
}

TEST(UZ_GenerateCubeMesh, FailingDevice_ReturnsInvalid) {
    test::FailingMockRHIDevice device;
    auto mesh = GenerateCubeMesh(&device);
    EXPECT_FALSE(mesh.IsValid());
}

TEST(UZ_GenerateCubeMesh, CreatesTwo_Buffers) {
    test::MockRHIDevice device;
    auto mesh = GenerateCubeMesh(&device);
    // VB + IB = 2 buffers
    EXPECT_EQ(device.buffersCreated.load(), 2u);
    DestroyProceduralMesh(&device, mesh);
}

// =============================================================================
// GenerateSphereMesh
// =============================================================================

TEST(UZ_GenerateSphereMesh, ReturnsValid_OnMockDevice) {
    test::MockRHIDevice device;
    auto mesh = GenerateSphereMesh(&device);
    EXPECT_TRUE(mesh.IsValid());
    EXPECT_TRUE(mesh.vb.IsValid());
    EXPECT_TRUE(mesh.ib.IsValid());
    DestroyProceduralMesh(&device, mesh);
}

TEST(UZ_GenerateSphereMesh, VertexCount_Is153) {
    // (8 + 1) * (16 + 1) = 9 * 17 = 153
    test::MockRHIDevice device;
    auto mesh = GenerateSphereMesh(&device);
    EXPECT_EQ(mesh.vertexCount, 153u)
        << "Sphere (8 stacks x 16 slices) must have 153 vertices";
    DestroyProceduralMesh(&device, mesh);
}

TEST(UZ_GenerateSphereMesh, IndexCount_Is768) {
    // 8 * 16 * 6 = 768
    test::MockRHIDevice device;
    auto mesh = GenerateSphereMesh(&device);
    EXPECT_EQ(mesh.indexCount, 768u)
        << "Sphere (8 stacks x 16 slices) must have 768 indices";
    DestroyProceduralMesh(&device, mesh);
}

TEST(UZ_GenerateSphereMesh, NullDevice_ReturnsInvalid) {
    auto mesh = GenerateSphereMesh(nullptr);
    EXPECT_FALSE(mesh.IsValid());
    EXPECT_EQ(mesh.vertexCount, 0u);
    EXPECT_EQ(mesh.indexCount, 0u);
}

TEST(UZ_GenerateSphereMesh, FailingDevice_ReturnsInvalid) {
    test::FailingMockRHIDevice device;
    auto mesh = GenerateSphereMesh(&device);
    EXPECT_FALSE(mesh.IsValid());
}

TEST(UZ_GenerateSphereMesh, CreatesTwo_Buffers) {
    test::MockRHIDevice device;
    auto mesh = GenerateSphereMesh(&device);
    EXPECT_EQ(device.buffersCreated.load(), 2u);
    DestroyProceduralMesh(&device, mesh);
}

// =============================================================================
// DestroyProceduralMesh
// =============================================================================

TEST(UZ_DestroyProceduralMesh, DestroysBuffers) {
    test::MockRHIDevice device;
    auto mesh = GenerateCubeMesh(&device);
    ASSERT_TRUE(mesh.IsValid());
    DestroyProceduralMesh(&device, mesh);
    EXPECT_FALSE(mesh.IsValid());
    EXPECT_FALSE(mesh.vb.IsValid());
    EXPECT_FALSE(mesh.ib.IsValid());
    EXPECT_EQ(mesh.vertexCount, 0u);
    EXPECT_EQ(mesh.indexCount, 0u);
    EXPECT_EQ(device.buffersDestroyed.load(), 2u);
}

TEST(UZ_DestroyProceduralMesh, NullDevice_NoOp) {
    test::MockRHIDevice device;
    auto mesh = GenerateCubeMesh(&device);
    ASSERT_TRUE(mesh.IsValid());
    // Should not crash with null device.
    DestroyProceduralMesh(nullptr, mesh);
    // Mesh should remain valid (no-op).
    EXPECT_TRUE(mesh.IsValid());
    // Cleanup properly.
    DestroyProceduralMesh(&device, mesh);
}

TEST(UZ_DestroyProceduralMesh, InvalidMesh_NoOp) {
    test::MockRHIDevice device;
    VfxProceduralMesh mesh;
    // Should not crash when destroying an invalid mesh.
    DestroyProceduralMesh(&device, mesh);
    EXPECT_EQ(device.buffersDestroyed.load(), 0u);
}

TEST(UZ_DestroyProceduralMesh, DoubleFree_SafeAfterReset) {
    test::MockRHIDevice device;
    auto mesh = GenerateCubeMesh(&device);
    DestroyProceduralMesh(&device, mesh);
    // Second destroy should be a no-op (handles are invalid).
    DestroyProceduralMesh(&device, mesh);
    EXPECT_EQ(device.buffersDestroyed.load(), 2u)
        << "Double destroy should not double-free";
}
