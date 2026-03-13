/// UzEngine Mesh Format — Load/Save/Build Implementation

#include "mesh.h"
#include "log.h"
#include "profiler.h"

#include <cmath>
#include <cstring>
#include <fstream>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// LoadMesh
// ─────────────────────────────────────────────────────────────────────────────

Mesh LoadMesh(const std::string& path) {
    UZ_PROFILE_FUNCTION();
    Mesh mesh{};

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        UZ_LOG(Renderer, Error, "Failed to open mesh file: path='{}'", path);
        return mesh;
    }

    // Read header
    file.read(reinterpret_cast<char*>(&mesh.header), sizeof(MeshHeader));
    if (!file || file.gcount() != sizeof(MeshHeader)) {
        UZ_LOG(Renderer, Error, "Truncated mesh header: path='{}'", path);
        mesh.header.magic = 0;
        return mesh;
    }

    // Validate magic
    if (mesh.header.magic != kMeshMagic) {
        UZ_LOG(Renderer, Error,
               "Invalid mesh magic: expected=0x{:08X}, got=0x{:08X}, path='{}'",
               kMeshMagic, mesh.header.magic, path);
        mesh.header.magic = 0;
        return mesh;
    }

    // Validate version
    if (mesh.header.version != kMeshVersion) {
        UZ_LOG(Renderer, Error,
               "Unsupported mesh version: expected={}, got={}, path='{}'",
               kMeshVersion, mesh.header.version, path);
        mesh.header.magic = 0;
        return mesh;
    }

    // Read vertex data
    uint64_t vertexSize = mesh.GetVertexDataSize();
    if (vertexSize > 0) {
        mesh.vertexData.resize(static_cast<size_t>(vertexSize));
        file.seekg(static_cast<std::streamoff>(mesh.header.vertexDataOffset));
        file.read(reinterpret_cast<char*>(mesh.vertexData.data()),
                  static_cast<std::streamsize>(vertexSize));
        if (!file) {
            UZ_LOG(Renderer, Error,
                   "Truncated vertex data: expected={} bytes, path='{}'",
                   vertexSize, path);
            mesh.header.magic = 0;
            return mesh;
        }
    }

    // Read index data
    uint64_t indexSize = mesh.GetIndexDataSize();
    if (indexSize > 0) {
        mesh.indexData.resize(static_cast<size_t>(indexSize));
        file.seekg(static_cast<std::streamoff>(mesh.header.indexDataOffset));
        file.read(reinterpret_cast<char*>(mesh.indexData.data()),
                  static_cast<std::streamsize>(indexSize));
        if (!file) {
            UZ_LOG(Renderer, Error,
                   "Truncated index data: expected={} bytes, path='{}'",
                   indexSize, path);
            mesh.header.magic = 0;
            return mesh;
        }
    }

    UZ_LOG(Renderer, Info,
           "Loaded mesh: vertices={}, indices={}, format={}, path='{}'",
           mesh.header.vertexCount, mesh.header.indexCount,
           static_cast<uint32_t>(mesh.header.vertexFormat), path);

    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// SaveMesh
// ─────────────────────────────────────────────────────────────────────────────

bool SaveMesh(const std::string& path, const Mesh& mesh) {
    UZ_PROFILE_FUNCTION();
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        UZ_LOG(Renderer, Error, "Failed to create mesh file: path='{}'", path);
        return false;
    }

    file.write(reinterpret_cast<const char*>(&mesh.header), sizeof(MeshHeader));
    if (!mesh.vertexData.empty()) {
        file.write(reinterpret_cast<const char*>(mesh.vertexData.data()),
                   static_cast<std::streamsize>(mesh.vertexData.size()));
    }
    if (!mesh.indexData.empty()) {
        file.write(reinterpret_cast<const char*>(mesh.indexData.data()),
                   static_cast<std::streamsize>(mesh.indexData.size()));
    }

    if (!file) {
        UZ_LOG(Renderer, Error, "Write failed for mesh file: path='{}'", path);
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// MeshBuilder helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Computes AABB from Vertex array.
static void ComputeBounds(const Vertex* verts, uint32_t count,
                          Vec3& outMin, Vec3& outMax) {
    if (count == 0) {
        outMin = outMax = Vec3{0, 0, 0};
        return;
    }
    outMin = outMax = verts[0].position;
    for (uint32_t i = 1; i < count; ++i) {
        const auto& p = verts[i].position;
        if (p.x < outMin.x) outMin.x = p.x;
        if (p.y < outMin.y) outMin.y = p.y;
        if (p.z < outMin.z) outMin.z = p.z;
        if (p.x > outMax.x) outMax.x = p.x;
        if (p.y > outMax.y) outMax.y = p.y;
        if (p.z > outMax.z) outMax.z = p.z;
    }
}

/// Computes AABB from SimpleVertex array.
static void ComputeBoundsSimple(const SimpleVertex* verts, uint32_t count,
                                Vec3& outMin, Vec3& outMax) {
    if (count == 0) {
        outMin = outMax = Vec3{0, 0, 0};
        return;
    }
    outMin = outMax = verts[0].position;
    for (uint32_t i = 1; i < count; ++i) {
        const auto& p = verts[i].position;
        if (p.x < outMin.x) outMin.x = p.x;
        if (p.y < outMin.y) outMin.y = p.y;
        if (p.z < outMin.z) outMin.z = p.z;
        if (p.x > outMax.x) outMax.x = p.x;
        if (p.y > outMax.y) outMax.y = p.y;
        if (p.z > outMax.z) outMax.z = p.z;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// MeshBuilder::CreateFromVertices
// ─────────────────────────────────────────────────────────────────────────────

Mesh MeshBuilder::CreateFromVertices(const std::vector<Vertex>& vertices,
                                      const std::vector<uint32_t>& indices) {
    UZ_PROFILE_FUNCTION();
    Mesh mesh{};
    auto& h = mesh.header;
    h.magic          = kMeshMagic;
    h.version        = kMeshVersion;
    h.vertexFormat   = MeshVertexFormat::Standard;
    h.vertexStride   = sizeof(Vertex);
    h.vertexCount    = static_cast<uint32_t>(vertices.size());
    h.indexFormat    = MeshIndexFormat::UInt32;
    h.indexCount     = static_cast<uint32_t>(indices.size());
    h.submeshCount   = 1;
    h.vertexDataOffset = sizeof(MeshHeader);
    h.indexDataOffset  = sizeof(MeshHeader) + mesh.GetVertexDataSize();
    std::memset(h.reserved, 0, sizeof(h.reserved));

    ComputeBounds(vertices.data(), h.vertexCount, h.boundsMin, h.boundsMax);

    // Copy vertex data
    mesh.vertexData.resize(static_cast<size_t>(mesh.GetVertexDataSize()));
    std::memcpy(mesh.vertexData.data(), vertices.data(), mesh.vertexData.size());

    // Copy index data
    if (!indices.empty()) {
        mesh.indexData.resize(static_cast<size_t>(mesh.GetIndexDataSize()));
        std::memcpy(mesh.indexData.data(), indices.data(), mesh.indexData.size());
    }

    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// MeshBuilder::CreateFromSimpleVertices
// ─────────────────────────────────────────────────────────────────────────────

Mesh MeshBuilder::CreateFromSimpleVertices(const std::vector<SimpleVertex>& vertices,
                                            const std::vector<uint32_t>& indices) {
    UZ_PROFILE_FUNCTION();
    Mesh mesh{};
    auto& h = mesh.header;
    h.magic          = kMeshMagic;
    h.version        = kMeshVersion;
    h.vertexFormat   = MeshVertexFormat::Simple;
    h.vertexStride   = sizeof(SimpleVertex);
    h.vertexCount    = static_cast<uint32_t>(vertices.size());
    h.indexFormat    = MeshIndexFormat::UInt32;
    h.indexCount     = static_cast<uint32_t>(indices.size());
    h.submeshCount   = 1;
    h.vertexDataOffset = sizeof(MeshHeader);
    h.indexDataOffset  = sizeof(MeshHeader) + mesh.GetVertexDataSize();
    std::memset(h.reserved, 0, sizeof(h.reserved));

    ComputeBoundsSimple(vertices.data(), h.vertexCount, h.boundsMin, h.boundsMax);

    mesh.vertexData.resize(static_cast<size_t>(mesh.GetVertexDataSize()));
    std::memcpy(mesh.vertexData.data(), vertices.data(), mesh.vertexData.size());

    if (!indices.empty()) {
        mesh.indexData.resize(static_cast<size_t>(mesh.GetIndexDataSize()));
        std::memcpy(mesh.indexData.data(), indices.data(), mesh.indexData.size());
    }

    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// MeshBuilder::CreateCube
// ─────────────────────────────────────────────────────────────────────────────

Mesh MeshBuilder::CreateCube(float size) {
    UZ_PROFILE_FUNCTION();
    // Cube with bottom at Z=0, centered on XY.
    // 6 faces × 4 vertices = 24 vertices, 6 faces × 2 triangles × 3 = 36 indices.
    // Z-up coordinate system: X-forward, Y-right, Z-up.

    const float s = size;

    // Positions: bottom Z=0, top Z=2*size, centered on XY
    const Vec3 p000{-s, -s, 0.0f};
    const Vec3 p100{ s, -s, 0.0f};
    const Vec3 p110{ s,  s, 0.0f};
    const Vec3 p010{-s,  s, 0.0f};
    const Vec3 p001{-s, -s, 2*s};
    const Vec3 p101{ s, -s, 2*s};
    const Vec3 p111{ s,  s, 2*s};
    const Vec3 p011{-s,  s, 2*s};

    // Normals (Z-up: +X forward, +Y right, +Z up)
    const Vec3 nFront { 1,  0,  0};  // +X face
    const Vec3 nBack  {-1,  0,  0};  // -X face
    const Vec3 nRight { 0,  1,  0};  // +Y face
    const Vec3 nLeft  { 0, -1,  0};  // -Y face
    const Vec3 nTop   { 0,  0,  1};  // +Z face
    const Vec3 nBottom{ 0,  0, -1};  // -Z face

    // Tangents (w=1 handedness)
    const Vec4 tFront { 0,  1,  0, 1};
    const Vec4 tBack  { 0, -1,  0, 1};
    const Vec4 tRight { 1,  0,  0, 1};
    const Vec4 tLeft  {-1,  0,  0, 1};
    const Vec4 tTop   { 1,  0,  0, 1};
    const Vec4 tBottom{ 1,  0,  0, 1};

    // UV corners
    const Vec2 uv00{0, 0}, uv10{1, 0}, uv11{1, 1}, uv01{0, 1};

    std::vector<Vertex> vertices = {
        // +X face (front)
        {p100, nFront, uv00, tFront}, {p110, nFront, uv10, tFront},
        {p111, nFront, uv11, tFront}, {p101, nFront, uv01, tFront},
        // -X face (back)
        {p010, nBack, uv00, tBack}, {p000, nBack, uv10, tBack},
        {p001, nBack, uv11, tBack}, {p011, nBack, uv01, tBack},
        // +Y face (right)
        {p110, nRight, uv00, tRight}, {p010, nRight, uv10, tRight},
        {p011, nRight, uv11, tRight}, {p111, nRight, uv01, tRight},
        // -Y face (left)
        {p000, nLeft, uv00, tLeft}, {p100, nLeft, uv10, tLeft},
        {p101, nLeft, uv11, tLeft}, {p001, nLeft, uv01, tLeft},
        // +Z face (top)
        {p001, nTop, uv00, tTop}, {p101, nTop, uv10, tTop},
        {p111, nTop, uv11, tTop}, {p011, nTop, uv01, tTop},
        // -Z face (bottom)
        {p010, nBottom, uv00, tBottom}, {p110, nBottom, uv10, tBottom},
        {p100, nBottom, uv11, tBottom}, {p000, nBottom, uv01, tBottom},
    };

    std::vector<uint32_t> indices;
    indices.reserve(36);
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    return CreateFromVertices(vertices, indices);
}

// ─────────────────────────────────────────────────────────────────────────────
// MeshBuilder::CreateSphere
// ─────────────────────────────────────────────────────────────────────────────

Mesh MeshBuilder::CreateSphere(float radius, uint32_t stacks, uint32_t slices) {
    UZ_PROFILE_FUNCTION();
    constexpr float kPi = 3.14159265358979f;

    std::vector<Vertex> vertices;
    vertices.reserve((stacks + 1) * (slices + 1));

    for (uint32_t i = 0; i <= stacks; ++i) {
        float phi = kPi * static_cast<float>(i) / static_cast<float>(stacks);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);

        for (uint32_t j = 0; j <= slices; ++j) {
            float theta = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(slices);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            Vec3 normal = {sinPhi * cosTheta, cosPhi, sinPhi * sinTheta};
            float u = static_cast<float>(j) / static_cast<float>(slices);
            float v = 1.0f - static_cast<float>(i) / static_cast<float>(stacks);

            Vertex vert{};
            vert.position = {normal.x * radius, normal.y * radius, normal.z * radius};
            vert.normal   = normal;
            vert.texcoord = {u, v};
            vert.tangent  = {-sinTheta, 0.0f, cosTheta, 1.0f};
            vertices.push_back(vert);
        }
    }

    // CW winding order (matches frontCounterClockwise = false)
    std::vector<uint32_t> indices;
    indices.reserve(stacks * slices * 6);
    for (uint32_t i = 0; i < stacks; ++i) {
        for (uint32_t j = 0; j < slices; ++j) {
            uint32_t a = i * (slices + 1) + j;
            uint32_t b = a + slices + 1;
            indices.push_back(a);
            indices.push_back(a + 1);
            indices.push_back(b);
            indices.push_back(a + 1);
            indices.push_back(b + 1);
            indices.push_back(b);
        }
    }

    return CreateFromVertices(vertices, indices);
}

} // namespace uz
