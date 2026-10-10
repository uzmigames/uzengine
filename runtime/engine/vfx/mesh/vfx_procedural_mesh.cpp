/// VFX Procedural Mesh Generation — cube and sphere implementation.
///
/// Generates CPU-side vertex/index data, creates Upload-heap GPU buffers,
/// maps them, and copies the data in. The small size of these meshes
/// (< 10 KB) makes Upload-heap suitable without a staging copy.

#include "vfx_procedural_mesh.h"

#include "engine/logging/log.h"
#include "engine/rhi/rhi_device.h"

#include <cmath>
#include <cstring>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

namespace {

/// Pi constant for sphere generation.
constexpr float kPi = 3.14159265358979323846f;

/// Half-extent for the unit cube (0.5 cm each side from center).
constexpr float kHalfExtent = 0.5f;

/// Number of stacks (latitude divisions) for the sphere.
constexpr uint32_t kSphereStacks = 8;

/// Number of slices (longitude divisions) for the sphere.
constexpr uint32_t kSphereSlices = 16;

/// Sphere radius in centimeters.
constexpr float kSphereRadius = 0.5f;

/// Creates a GPU buffer with Upload-heap memory and fills it with CPU data.
///
/// # Parameters
/// - `device`: RHI device.
/// - `data`: CPU-side data to upload.
/// - `sizeBytes`: Total size of the data in bytes.
/// - `stride`: Element stride for structured buffer view (0 for index buffers).
/// - `usage`: Buffer usage flags.
/// - `debugName`: GPU debug label.
///
/// # Returns
/// Valid handle on success, invalid on failure.
RHIBufferHandle CreateAndUploadBuffer(IRHIDevice* device,
                                       const void* data,
                                       uint64_t sizeBytes,
                                       uint32_t stride,
                                       RHIBufferUsage usage,
                                       const char* debugName)
{
    RHIBufferDesc desc{};
    desc.sizeBytes = sizeBytes;
    desc.stride = stride;
    desc.usage = usage;
    desc.memory = RHIMemoryType::Upload;
    desc.debugName = debugName;

    RHIBufferHandle handle = device->CreateBuffer(desc);
    if (!handle.IsValid()) {
        UZ_LOG(VFX, Error, "VfxProceduralMesh: failed to create buffer '{}'", debugName);
        return RHIBufferHandle::Invalid();
    }

    void* mapped = device->MapBuffer(handle);
    if (!mapped) {
        UZ_LOG(VFX, Error, "VfxProceduralMesh: failed to map buffer '{}'", debugName);
        device->DestroyBuffer(handle);
        return RHIBufferHandle::Invalid();
    }

    std::memcpy(mapped, data, static_cast<size_t>(sizeBytes));
    device->UnmapBuffer(handle);

    return handle;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// GenerateCubeMesh
// ─────────────────────────────────────────────────────────────────────────────

VfxProceduralMesh GenerateCubeMesh(IRHIDevice* device)
{
    VfxProceduralMesh mesh;

    if (!device) {
        UZ_LOG(VFX, Error, "GenerateCubeMesh: device is null");
        return mesh;
    }

    // 24 vertices: 4 per face, 6 faces.
    // Z-up left-handed coordinate system.
    // Each face has outward-pointing normal, and UVs covering [0,1]^2.
    constexpr float h = kHalfExtent;

    // clang-format off
    const VfxMeshVertex vertices[24] = {
        // +Z face (top) — normal (0, 0, +1)
        { -h,  -h,   h,   0.0f,  0.0f,  1.0f,   0.0f, 0.0f },
        {  h,  -h,   h,   0.0f,  0.0f,  1.0f,   1.0f, 0.0f },
        {  h,   h,   h,   0.0f,  0.0f,  1.0f,   1.0f, 1.0f },
        { -h,   h,   h,   0.0f,  0.0f,  1.0f,   0.0f, 1.0f },

        // -Z face (bottom) — normal (0, 0, -1)
        { -h,   h,  -h,   0.0f,  0.0f, -1.0f,   0.0f, 0.0f },
        {  h,   h,  -h,   0.0f,  0.0f, -1.0f,   1.0f, 0.0f },
        {  h,  -h,  -h,   0.0f,  0.0f, -1.0f,   1.0f, 1.0f },
        { -h,  -h,  -h,   0.0f,  0.0f, -1.0f,   0.0f, 1.0f },

        // +X face (right) — normal (+1, 0, 0)
        {  h,  -h,  -h,   1.0f,  0.0f,  0.0f,   0.0f, 0.0f },
        {  h,   h,  -h,   1.0f,  0.0f,  0.0f,   1.0f, 0.0f },
        {  h,   h,   h,   1.0f,  0.0f,  0.0f,   1.0f, 1.0f },
        {  h,  -h,   h,   1.0f,  0.0f,  0.0f,   0.0f, 1.0f },

        // -X face (left) — normal (-1, 0, 0)
        { -h,   h,  -h,  -1.0f,  0.0f,  0.0f,   0.0f, 0.0f },
        { -h,  -h,  -h,  -1.0f,  0.0f,  0.0f,   1.0f, 0.0f },
        { -h,  -h,   h,  -1.0f,  0.0f,  0.0f,   1.0f, 1.0f },
        { -h,   h,   h,  -1.0f,  0.0f,  0.0f,   0.0f, 1.0f },

        // +Y face (front) — normal (0, +1, 0)
        {  h,   h,  -h,   0.0f,  1.0f,  0.0f,   0.0f, 0.0f },
        { -h,   h,  -h,   0.0f,  1.0f,  0.0f,   1.0f, 0.0f },
        { -h,   h,   h,   0.0f,  1.0f,  0.0f,   1.0f, 1.0f },
        {  h,   h,   h,   0.0f,  1.0f,  0.0f,   0.0f, 1.0f },

        // -Y face (back) — normal (0, -1, 0)
        { -h,  -h,  -h,   0.0f, -1.0f,  0.0f,   0.0f, 0.0f },
        {  h,  -h,  -h,   0.0f, -1.0f,  0.0f,   1.0f, 0.0f },
        {  h,  -h,   h,   0.0f, -1.0f,  0.0f,   1.0f, 1.0f },
        { -h,  -h,   h,   0.0f, -1.0f,  0.0f,   0.0f, 1.0f },
    };

    // 36 indices: 2 triangles per face, 6 faces.
    // Left-handed winding: viewed from outside, vertices go clockwise.
    const uint16_t indices[36] = {
        // +Z
         0,  1,  2,   0,  2,  3,
        // -Z
         4,  5,  6,   4,  6,  7,
        // +X
         8,  9, 10,   8, 10, 11,
        // -X
        12, 13, 14,  12, 14, 15,
        // +Y
        16, 17, 18,  16, 18, 19,
        // -Y
        20, 21, 22,  20, 22, 23,
    };
    // clang-format on

    mesh.vertexCount = 24;
    mesh.indexCount = 36;

    mesh.vb = CreateAndUploadBuffer(
        device,
        vertices,
        sizeof(vertices),
        sizeof(VfxMeshVertex),
        RHIBufferUsage::VertexBuffer,
        "VFX_CubeMesh_VB");

    mesh.ib = CreateAndUploadBuffer(
        device,
        indices,
        sizeof(indices),
        sizeof(uint16_t),
        RHIBufferUsage::IndexBuffer,
        "VFX_CubeMesh_IB");

    if (!mesh.IsValid()) {
        UZ_LOG(VFX, Error, "GenerateCubeMesh: buffer creation failed");
        DestroyProceduralMesh(device, mesh);
        return mesh;
    }

    UZ_LOG(VFX, Info, "GenerateCubeMesh: {} vertices, {} indices",
           mesh.vertexCount, mesh.indexCount);
    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// GenerateSphereMesh
// ─────────────────────────────────────────────────────────────────────────────

VfxProceduralMesh GenerateSphereMesh(IRHIDevice* device)
{
    VfxProceduralMesh mesh;

    if (!device) {
        UZ_LOG(VFX, Error, "GenerateSphereMesh: device is null");
        return mesh;
    }

    // UV sphere: (stacks + 1) * (slices + 1) vertices.
    // stacks * slices * 2 triangles = stacks * slices * 6 indices.
    constexpr uint32_t stacks = kSphereStacks;
    constexpr uint32_t slices = kSphereSlices;
    constexpr uint32_t vertCount = (stacks + 1) * (slices + 1);
    constexpr uint32_t idxCount = stacks * slices * 6;

    std::vector<VfxMeshVertex> vertices;
    vertices.reserve(vertCount);

    std::vector<uint16_t> indices;
    indices.reserve(idxCount);

    // Generate vertices: sweep from top pole (stack=0, phi=0) to bottom pole.
    // Z-up: the sphere's north pole is at (0, 0, +radius).
    for (uint32_t stack = 0; stack <= stacks; ++stack) {
        // phi: angle from +Z axis, [0, pi].
        const float phi = static_cast<float>(stack) / static_cast<float>(stacks) * kPi;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        for (uint32_t slice = 0; slice <= slices; ++slice) {
            // theta: angle around Z axis in XY plane, [0, 2*pi].
            const float theta = static_cast<float>(slice) / static_cast<float>(slices) * 2.0f * kPi;
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            // Position on unit sphere (Z-up):
            //   x = sin(phi) * cos(theta)
            //   y = sin(phi) * sin(theta)
            //   z = cos(phi)
            const float nx = sinPhi * cosTheta;
            const float ny = sinPhi * sinTheta;
            const float nz = cosPhi;

            VfxMeshVertex vert{};
            vert.px = nx * kSphereRadius;
            vert.py = ny * kSphereRadius;
            vert.pz = nz * kSphereRadius;
            vert.nx = nx;
            vert.ny = ny;
            vert.nz = nz;
            vert.u = static_cast<float>(slice) / static_cast<float>(slices);
            vert.v = static_cast<float>(stack) / static_cast<float>(stacks);

            vertices.push_back(vert);
        }
    }

    // Generate indices: two triangles per quad.
    // Each quad spans [stack, stack+1] x [slice, slice+1].
    for (uint32_t stack = 0; stack < stacks; ++stack) {
        for (uint32_t slice = 0; slice < slices; ++slice) {
            const uint16_t topLeft     = static_cast<uint16_t>(stack * (slices + 1) + slice);
            const uint16_t topRight    = static_cast<uint16_t>(topLeft + 1);
            const uint16_t bottomLeft  = static_cast<uint16_t>((stack + 1) * (slices + 1) + slice);
            const uint16_t bottomRight = static_cast<uint16_t>(bottomLeft + 1);

            // First triangle (left-handed winding).
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            // Second triangle.
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    mesh.vertexCount = static_cast<uint32_t>(vertices.size());
    mesh.indexCount = static_cast<uint32_t>(indices.size());

    mesh.vb = CreateAndUploadBuffer(
        device,
        vertices.data(),
        static_cast<uint64_t>(vertices.size()) * sizeof(VfxMeshVertex),
        sizeof(VfxMeshVertex),
        RHIBufferUsage::VertexBuffer,
        "VFX_SphereMesh_VB");

    mesh.ib = CreateAndUploadBuffer(
        device,
        indices.data(),
        static_cast<uint64_t>(indices.size()) * sizeof(uint16_t),
        sizeof(uint16_t),
        RHIBufferUsage::IndexBuffer,
        "VFX_SphereMesh_IB");

    if (!mesh.IsValid()) {
        UZ_LOG(VFX, Error, "GenerateSphereMesh: buffer creation failed");
        DestroyProceduralMesh(device, mesh);
        return mesh;
    }

    UZ_LOG(VFX, Info, "GenerateSphereMesh: {} vertices, {} indices",
           mesh.vertexCount, mesh.indexCount);
    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// DestroyProceduralMesh
// ─────────────────────────────────────────────────────────────────────────────

void DestroyProceduralMesh(IRHIDevice* device, VfxProceduralMesh& mesh)
{
    if (!device) return;

    if (mesh.vb.IsValid()) {
        device->DestroyBuffer(mesh.vb);
        mesh.vb = RHIBufferHandle::Invalid();
    }

    if (mesh.ib.IsValid()) {
        device->DestroyBuffer(mesh.ib);
        mesh.ib = RHIBufferHandle::Invalid();
    }

    mesh.vertexCount = 0;
    mesh.indexCount = 0;
}

} // namespace uz
