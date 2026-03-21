/// UzEngine Mesh Format — Load/Save/Build Implementation

#include "mesh.h"
#include "log.h"
#include "profiler.h"

#include "file_system.h"

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
// LoadMeshFromMemory
// ─────────────────────────────────────────────────────────────────────────────

Mesh LoadMeshFromMemory(const std::vector<uint8_t>& data, const std::string& name) {
    UZ_PROFILE_FUNCTION();
    Mesh mesh{};

    if (data.size() < sizeof(MeshHeader)) {
        UZ_LOG(Renderer, Error, "Truncated mesh data: size={}, name='{}'", data.size(), name);
        return mesh;
    }

    std::memcpy(&mesh.header, data.data(), sizeof(MeshHeader));

    if (mesh.header.magic != kMeshMagic) {
        UZ_LOG(Renderer, Error, "Invalid mesh magic: expected=0x{:08X}, got=0x{:08X}, name='{}'",
               kMeshMagic, mesh.header.magic, name);
        mesh.header.magic = 0;
        return mesh;
    }

    if (mesh.header.version != kMeshVersion) {
        UZ_LOG(Renderer, Error, "Unsupported mesh version: expected={}, got={}, name='{}'",
               kMeshVersion, mesh.header.version, name);
        mesh.header.magic = 0;
        return mesh;
    }

    const uint64_t vertexSize = mesh.GetVertexDataSize();
    if (vertexSize > 0 && mesh.header.vertexDataOffset + vertexSize <= data.size()) {
        mesh.vertexData.resize(static_cast<size_t>(vertexSize));
        std::memcpy(mesh.vertexData.data(),
                    data.data() + mesh.header.vertexDataOffset,
                    static_cast<size_t>(vertexSize));
    }

    const uint64_t indexSize = mesh.GetIndexDataSize();
    if (indexSize > 0 && mesh.header.indexDataOffset + indexSize <= data.size()) {
        mesh.indexData.resize(static_cast<size_t>(indexSize));
        std::memcpy(mesh.indexData.data(),
                    data.data() + mesh.header.indexDataOffset,
                    static_cast<size_t>(indexSize));
    }

    UZ_LOG(Renderer, Info, "Loaded mesh from memory: vertices={}, indices={}, name='{}'",
           mesh.header.vertexCount, mesh.header.indexCount, name);
    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// SaveMesh
// ─────────────────────────────────────────────────────────────────────────────

bool SaveMesh(const std::string& path, const Mesh& mesh) {
    UZ_PROFILE_FUNCTION();

    // Build a single contiguous buffer so FileWriteAll can write atomically.
    const size_t totalSize = sizeof(MeshHeader)
                           + mesh.vertexData.size()
                           + mesh.indexData.size();
    std::vector<uint8_t> buffer;
    buffer.resize(totalSize);

    size_t offset = 0;
    std::memcpy(buffer.data() + offset, &mesh.header, sizeof(MeshHeader));
    offset += sizeof(MeshHeader);

    if (!mesh.vertexData.empty()) {
        std::memcpy(buffer.data() + offset, mesh.vertexData.data(), mesh.vertexData.size());
        offset += mesh.vertexData.size();
    }
    if (!mesh.indexData.empty()) {
        std::memcpy(buffer.data() + offset, mesh.indexData.data(), mesh.indexData.size());
    }

    const FileResult result = FileWriteAll(path.c_str(), buffer.data(), buffer.size());
    if (result != FileResult::Success) {
        UZ_LOG(Renderer, Error, "Write failed for mesh file: path='{}': {}",
               path, FileResultToString(result));
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// GenerateSphereMesh
// ─────────────────────────────────────────────────────────────────────────────

Mesh GenerateSphereMesh(uint32_t sectorCount, uint32_t stackCount) {
    UZ_PROFILE_FUNCTION();
    // UV sphere generation — based on songho.ca/opengl/gl_sphere.html
    // Adapted for UzEngine Z-up coordinate system.
    //
    // stackAngle goes from +PI/2 (north pole, Z=+1) to -PI/2 (south pole, Z=-1).
    // sectorAngle goes from 0 to 2*PI around the equator in XY plane.
    //
    // Position (Z-up, radius=1):
    //   xy = cos(stackAngle)        — distance from Z axis
    //   x  = xy * cos(sectorAngle)
    //   y  = xy * sin(sectorAngle)
    //   z  = sin(stackAngle)        — height (Z-up)

    constexpr float kPi  = 3.14159265358979323846f;
    constexpr float k2Pi = 6.28318530717958647693f;

    const float sectorStep = k2Pi / static_cast<float>(sectorCount);
    const float stackStep  = kPi  / static_cast<float>(stackCount);

    std::vector<Vertex> vertices;
    vertices.reserve((stackCount + 1) * (sectorCount + 1));

    for (uint32_t i = 0; i <= stackCount; ++i) {
        const float stackAngle = kPi / 2.0f - static_cast<float>(i) * stackStep;  // PI/2 to -PI/2
        const float xy = std::cos(stackAngle);   // cos(stackAngle) = distance from Z axis
        const float z  = std::sin(stackAngle);   // sin(stackAngle) = Z height

        for (uint32_t j = 0; j <= sectorCount; ++j) {
            const float sectorAngle = static_cast<float>(j) * sectorStep;  // 0 to 2PI

            const float x = xy * std::cos(sectorAngle);
            const float y = xy * std::sin(sectorAngle);

            // Normal = position (unit sphere)
            const float nx = x;
            const float ny = y;
            const float nz = z;

            // UV: u = sector / sectorCount, v = stack / stackCount
            const float u = static_cast<float>(j) / static_cast<float>(sectorCount);
            const float v = static_cast<float>(i) / static_cast<float>(stackCount);

            // Tangent: dP/dSectorAngle = (-y, x, 0) normalized
            const float tx = -std::sin(sectorAngle);
            const float ty =  std::cos(sectorAngle);

            Vertex vert{};
            vert.position = {x, y, z};
            vert.normal   = {nx, ny, nz};
            vert.texcoord = {u, v};
            vert.tangent  = {tx, ty, 0.0f, 1.0f};
            vertices.push_back(vert);
        }
    }

    // Index generation — songho.ca pattern with pole triangle skip.
    // k1 = first vertex of current stack row, k2 = first vertex of next stack row.
    // Top pole (i==0): only one triangle per sector (k1→k2→k1+1).
    // Bottom pole (i==stackCount-1): only one triangle per sector (k1+1→k2→k2+1).
    // Middle stacks: two triangles per sector (quad).
    std::vector<uint32_t> indices;
    indices.reserve(stackCount * sectorCount * 6);

    for (uint32_t i = 0; i < stackCount; ++i) {
        uint32_t k1 = i * (sectorCount + 1);
        uint32_t k2 = k1 + sectorCount + 1;

        for (uint32_t j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            // Skip degenerate triangles at poles.
            // Songho reference (CCW/OpenGL): k1→k2→k1+1 and k1+1→k2→k2+1
            // DX12 frontCounterClockwise=false → CW = front face → reverse winding
            if (i != 0) {
                indices.push_back(k1);
                indices.push_back(k2);
                indices.push_back(k1 + 1);
            }
            if (i != (stackCount - 1)) {
                indices.push_back(k1 + 1);
                indices.push_back(k2);
                indices.push_back(k2 + 1);
            }
        }
    }

    return MeshBuilder::CreateFromVertices(vertices, indices);
}

// ─────────────────────────────────────────────────────────────────────────────
// GeneratePlaneMesh
// ─────────────────────────────────────────────────────────────────────────────

Mesh GeneratePlaneMesh(float uvScale) {
    UZ_PROFILE_FUNCTION();
    // Flat quad lying in the XY plane (Z=0), Z-up coordinate system.
    //
    // Vertices span [-1, 1] in X and Y. The normal points up (+Z = up).
    // Tangent points along +X (forward). Binormal (derived from cross) = +Y.
    //
    // UV ranges from [0, uvScale] so that wrapping samplers tile the texture
    // uvScale times across the quad without shader changes.
    //
    // Winding: CCW from above (matches frontCounterClockwise = true default).

    const Vec3 normal  = {0.f, 0.f, 1.f};          // +Z up
    const Vec4 tangent = {1.f, 0.f, 0.f, 1.f};     // +X forward, w=1

    const std::vector<Vertex> vertices = {
        // Bottom-left
        {{-1.f, -1.f, 0.f}, normal, {0.f,       0.f      }, tangent},
        // Bottom-right
        {{ 1.f, -1.f, 0.f}, normal, {uvScale,   0.f      }, tangent},
        // Top-right
        {{ 1.f,  1.f, 0.f}, normal, {uvScale,   uvScale  }, tangent},
        // Top-left
        {{-1.f,  1.f, 0.f}, normal, {0.f,       uvScale  }, tangent},
    };

    // Two CCW triangles: 0-1-2 and 0-2-3
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    return MeshBuilder::CreateFromVertices(vertices, indices);
}

// ─────────────────────────────────────────────────────────────────────────────
// GenerateConeMesh
// ─────────────────────────────────────────────────────────────────────────────

Mesh GenerateConeMesh(float radius, float height, uint32_t segments) {
    UZ_PROFILE_FUNCTION();
    // Cone with apex at (0, 0, height) and base circle at Z=0.
    // Z-up coordinate system. Smooth normals on the side surface, flat -Z
    // normals on the base cap.
    //
    // Side normal derivation via implicit surface:
    //   F(x,y,z) = sqrt(x^2 + y^2) * height - z * radius = 0
    //   grad F = (height * x/r, height * y/r, -radius) where r = sqrt(x^2+y^2)
    //   At angle theta: grad = (height*cos(theta), height*sin(theta), -radius)
    //   Outward (away from axis) flips the z sign:
    //     Normal = (height*cos(theta), height*sin(theta), radius) / slantLength
    //   where slantLength = sqrt(height^2 + radius^2).

    constexpr float k2Pi = 6.28318530717958647693f;

    const float slantLength = std::sqrt(radius * radius + height * height);
    // Normal components: radial = height / slantLength, axial = radius / slantLength
    const float nRadialScale = height / slantLength;
    const float nAxial       = radius / slantLength;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Reserve: side = (segments + 1) * 2 + 1 apex, cap = segments + 1 center
    // Generous reserve
    vertices.reserve((segments + 1) * 2 + segments + 2);
    indices.reserve(segments * 3 * 2 + segments * 3);

    // --- Side surface ---
    // For each segment, we have a bottom ring vertex and an apex vertex.
    // To get correct UV seam, we duplicate the first/last column.
    // Bottom ring: one vertex per segment + 1 for UV wrap.
    // Apex: one vertex per segment + 1 for UV wrap (each with different u).

    const uint32_t sideBaseStart = 0;
    for (uint32_t i = 0; i <= segments; ++i) {
        const float theta = k2Pi * static_cast<float>(i) / static_cast<float>(segments);
        const float cosT  = std::cos(theta);
        const float sinT  = std::sin(theta);
        const float u     = static_cast<float>(i) / static_cast<float>(segments);

        // Bottom ring vertex
        const Vec3 pos    = {radius * cosT, radius * sinT, 0.0f};
        const Vec3 normal = {nRadialScale * cosT, nRadialScale * sinT, nAxial};

        // Tangent: dPos/dU along the circumference = (-sin(theta), cos(theta), 0)
        const Vec4 tangent = {-sinT, cosT, 0.0f, 1.0f};

        Vertex vert{};
        vert.position = pos;
        vert.normal   = normal;
        vert.texcoord = {u, 1.0f};
        vert.tangent  = tangent;
        vertices.push_back(vert);
    }

    const uint32_t sideApexStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        const float theta = k2Pi * static_cast<float>(i) / static_cast<float>(segments);
        const float cosT  = std::cos(theta);
        const float sinT  = std::sin(theta);
        const float u     = static_cast<float>(i) / static_cast<float>(segments);

        // Apex vertex — same normal as the corresponding base edge for smooth shading
        const Vec3 normal  = {nRadialScale * cosT, nRadialScale * sinT, nAxial};
        const Vec4 tangent = {-sinT, cosT, 0.0f, 1.0f};

        Vertex vert{};
        vert.position = {0.0f, 0.0f, height};
        vert.normal   = normal;
        vert.texcoord = {u, 0.0f};
        vert.tangent  = tangent;
        vertices.push_back(vert);
    }

    // Side indices: triangle fan from apex to base ring
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t b0 = sideBaseStart + i;
        const uint32_t b1 = sideBaseStart + i + 1;
        const uint32_t a0 = sideApexStart + i;

        // Triangle: apex, base[i+1], base[i] — CW winding when viewed from outside
        indices.push_back(a0);
        indices.push_back(b1);
        indices.push_back(b0);
    }

    // --- Base cap ---
    // Center vertex at (0, 0, 0) with normal (0, 0, -1)
    const uint32_t capCenterIdx = static_cast<uint32_t>(vertices.size());
    {
        Vertex center{};
        center.position = {0.0f, 0.0f, 0.0f};
        center.normal   = {0.0f, 0.0f, -1.0f};
        center.texcoord = {0.5f, 0.5f};
        center.tangent  = {1.0f, 0.0f, 0.0f, 1.0f};
        vertices.push_back(center);
    }

    const uint32_t capRingStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        const float theta = k2Pi * static_cast<float>(i) / static_cast<float>(segments);
        const float cosT  = std::cos(theta);
        const float sinT  = std::sin(theta);

        Vertex vert{};
        vert.position = {radius * cosT, radius * sinT, 0.0f};
        vert.normal   = {0.0f, 0.0f, -1.0f};
        // Planar UV: map circle to [0,1] range
        vert.texcoord = {0.5f + 0.5f * cosT, 0.5f + 0.5f * sinT};
        vert.tangent  = {1.0f, 0.0f, 0.0f, 1.0f};
        vertices.push_back(vert);
    }

    // Base cap indices: triangle fan from center, wound so -Z normal faces outward
    // When viewed from below (looking in +Z direction), winding should be CW
    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(capCenterIdx);
        indices.push_back(capRingStart + i);
        indices.push_back(capRingStart + i + 1);
    }

    return MeshBuilder::CreateFromVertices(vertices, indices);
}

// ─────────────────────────────────────────────────────────────────────────────
// GenerateCylinderMesh
// ─────────────────────────────────────────────────────────────────────────────

Mesh GenerateCylinderMesh(float radius, float height, uint32_t segments) {
    UZ_PROFILE_FUNCTION();
    // Cylinder centered at origin, extends from Z = -height/2 to Z = +height/2.
    // Z-up coordinate system. Side normals point radially outward.
    // Top cap normal = +Z, bottom cap normal = -Z.

    constexpr float k2Pi = 6.28318530717958647693f;

    const float halfH = height * 0.5f;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Reserve: side = 2 * (segments+1), top cap = segments+2, bottom cap = segments+2
    vertices.reserve(2 * (segments + 1) + 2 * (segments + 2));
    indices.reserve(segments * 6 + segments * 3 * 2);

    // --- Side surface ---
    // Two rings: bottom (Z = -halfH) and top (Z = +halfH).
    // Vertices duplicated at the seam (i=0 and i=segments) for correct UVs.

    const uint32_t sideBottomStart = 0;
    for (uint32_t i = 0; i <= segments; ++i) {
        const float theta = k2Pi * static_cast<float>(i) / static_cast<float>(segments);
        const float cosT  = std::cos(theta);
        const float sinT  = std::sin(theta);
        const float u     = static_cast<float>(i) / static_cast<float>(segments);

        const Vec3 normal  = {cosT, sinT, 0.0f};
        const Vec4 tangent = {-sinT, cosT, 0.0f, 1.0f};

        // Bottom ring
        Vertex bot{};
        bot.position = {radius * cosT, radius * sinT, -halfH};
        bot.normal   = normal;
        bot.texcoord = {u, 1.0f};
        bot.tangent  = tangent;
        vertices.push_back(bot);
    }

    const uint32_t sideTopStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        const float theta = k2Pi * static_cast<float>(i) / static_cast<float>(segments);
        const float cosT  = std::cos(theta);
        const float sinT  = std::sin(theta);
        const float u     = static_cast<float>(i) / static_cast<float>(segments);

        const Vec3 normal  = {cosT, sinT, 0.0f};
        const Vec4 tangent = {-sinT, cosT, 0.0f, 1.0f};

        // Top ring
        Vertex top{};
        top.position = {radius * cosT, radius * sinT, halfH};
        top.normal   = normal;
        top.texcoord = {u, 0.0f};
        top.tangent  = tangent;
        vertices.push_back(top);
    }

    // Side indices: quads as two triangles
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t b0 = sideBottomStart + i;
        const uint32_t b1 = sideBottomStart + i + 1;
        const uint32_t t0 = sideTopStart + i;
        const uint32_t t1 = sideTopStart + i + 1;

        // Two triangles per quad — CW winding from outside
        indices.push_back(b0);
        indices.push_back(t0);
        indices.push_back(t1);

        indices.push_back(b0);
        indices.push_back(t1);
        indices.push_back(b1);
    }

    // --- Top cap (Z = +halfH, normal = +Z) ---
    const uint32_t topCenterIdx = static_cast<uint32_t>(vertices.size());
    {
        Vertex center{};
        center.position = {0.0f, 0.0f, halfH};
        center.normal   = {0.0f, 0.0f, 1.0f};
        center.texcoord = {0.5f, 0.5f};
        center.tangent  = {1.0f, 0.0f, 0.0f, 1.0f};
        vertices.push_back(center);
    }

    const uint32_t topCapRingStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        const float theta = k2Pi * static_cast<float>(i) / static_cast<float>(segments);
        const float cosT  = std::cos(theta);
        const float sinT  = std::sin(theta);

        Vertex vert{};
        vert.position = {radius * cosT, radius * sinT, halfH};
        vert.normal   = {0.0f, 0.0f, 1.0f};
        vert.texcoord = {0.5f + 0.5f * cosT, 0.5f - 0.5f * sinT};
        vert.tangent  = {1.0f, 0.0f, 0.0f, 1.0f};
        vertices.push_back(vert);
    }

    // Top cap: CCW from above (+Z looking down) so front face is +Z
    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(topCenterIdx);
        indices.push_back(topCapRingStart + i + 1);
        indices.push_back(topCapRingStart + i);
    }

    // --- Bottom cap (Z = -halfH, normal = -Z) ---
    const uint32_t botCenterIdx = static_cast<uint32_t>(vertices.size());
    {
        Vertex center{};
        center.position = {0.0f, 0.0f, -halfH};
        center.normal   = {0.0f, 0.0f, -1.0f};
        center.texcoord = {0.5f, 0.5f};
        center.tangent  = {1.0f, 0.0f, 0.0f, 1.0f};
        vertices.push_back(center);
    }

    const uint32_t botCapRingStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        const float theta = k2Pi * static_cast<float>(i) / static_cast<float>(segments);
        const float cosT  = std::cos(theta);
        const float sinT  = std::sin(theta);

        Vertex vert{};
        vert.position = {radius * cosT, radius * sinT, -halfH};
        vert.normal   = {0.0f, 0.0f, -1.0f};
        vert.texcoord = {0.5f + 0.5f * cosT, 0.5f + 0.5f * sinT};
        vert.tangent  = {1.0f, 0.0f, 0.0f, 1.0f};
        vertices.push_back(vert);
    }

    // Bottom cap: CW from below (-Z looking up) so front face is -Z
    for (uint32_t i = 0; i < segments; ++i) {
        indices.push_back(botCenterIdx);
        indices.push_back(botCapRingStart + i);
        indices.push_back(botCapRingStart + i + 1);
    }

    return MeshBuilder::CreateFromVertices(vertices, indices);
}

// ─────────────────────────────────────────────────────────────────────────────
// GenerateTorusMesh
// ─────────────────────────────────────────────────────────────────────────────

Mesh GenerateTorusMesh(float majorRadius, float minorRadius,
                        uint32_t majorSegments, uint32_t minorSegments) {
    UZ_PROFILE_FUNCTION();
    // Standard parametric torus in Z-up coordinate system.
    // Ring lies in the XY plane, tube sweeps around it.
    //
    // Parametric surface (u = major angle, v = minor angle):
    //   x = (R + r * cos(v)) * cos(u)
    //   y = (R + r * cos(v)) * sin(u)
    //   z = r * sin(v)
    //
    // where R = majorRadius, r = minorRadius.
    //
    // Partial derivatives for normal computation:
    //   dP/du = (-(R + r*cos(v))*sin(u), (R + r*cos(v))*cos(u), 0)
    //   dP/dv = (-r*sin(v)*cos(u), -r*sin(v)*sin(u), r*cos(v))
    //
    //   Normal = dP/du x dP/dv (outward-pointing):
    //     nx = cos(v) * cos(u)
    //     ny = cos(v) * sin(u)
    //     nz = sin(v)
    //   (already unit length since it's just the direction from tube center to surface)

    constexpr float k2Pi = 6.28318530717958647693f;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    vertices.reserve((majorSegments + 1) * (minorSegments + 1));
    indices.reserve(majorSegments * minorSegments * 6);

    for (uint32_t i = 0; i <= majorSegments; ++i) {
        const float u     = k2Pi * static_cast<float>(i) / static_cast<float>(majorSegments);
        const float cosU  = std::cos(u);
        const float sinU  = std::sin(u);
        const float texU  = static_cast<float>(i) / static_cast<float>(majorSegments);

        for (uint32_t j = 0; j <= minorSegments; ++j) {
            const float v     = k2Pi * static_cast<float>(j) / static_cast<float>(minorSegments);
            const float cosV  = std::cos(v);
            const float sinV  = std::sin(v);
            const float texV  = static_cast<float>(j) / static_cast<float>(minorSegments);

            // Position on torus surface
            const float dist = majorRadius + minorRadius * cosV;
            const Vec3 pos   = {dist * cosU, dist * sinU, minorRadius * sinV};

            // Normal: direction from tube center to surface point
            const Vec3 normal = {cosV * cosU, cosV * sinU, sinV};

            // Tangent: dP/du normalized = (-sin(u), cos(u), 0)
            // This is the direction along the major circle at this point.
            const Vec4 tangent = {-sinU, cosU, 0.0f, 1.0f};

            Vertex vert{};
            vert.position = pos;
            vert.normal   = normal;
            vert.texcoord = {texU, texV};
            vert.tangent  = tangent;
            vertices.push_back(vert);
        }
    }

    // Indices: quads between adjacent rings
    const uint32_t ringSize = minorSegments + 1;
    for (uint32_t i = 0; i < majorSegments; ++i) {
        for (uint32_t j = 0; j < minorSegments; ++j) {
            const uint32_t a = i * ringSize + j;
            const uint32_t b = a + ringSize;

            // Two triangles per quad
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(a + 1);

            indices.push_back(a + 1);
            indices.push_back(b);
            indices.push_back(b + 1);
        }
    }

    return MeshBuilder::CreateFromVertices(vertices, indices);
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

// ─────────────────────────────────────────────────────────────────────────────
// GenerateTangents
// ─────────────────────────────────────────────────────────────────────────────

void GenerateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    UZ_PROFILE_FUNCTION();

    if (vertices.empty() || indices.size() < 3) {
        return;
    }

    const size_t vertexCount = vertices.size();

    // Accumulated tangent and bitangent vectors per vertex (not yet normalized).
    // Using separate arrays avoids polluting the Vertex struct during accumulation.
    std::vector<Vec3> tangentAccum(vertexCount, Vec3{0.f, 0.f, 0.f});
    std::vector<Vec3> bitangentAccum(vertexCount, Vec3{0.f, 0.f, 0.f});

    // Accumulate tangent/bitangent contributions from each triangle.
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];

        // Bounds check — skip invalid indices rather than crashing.
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            continue;
        }

        const Vec3& p0 = vertices[i0].position;
        const Vec3& p1 = vertices[i1].position;
        const Vec3& p2 = vertices[i2].position;

        const Vec2& uv0 = vertices[i0].texcoord;
        const Vec2& uv1 = vertices[i1].texcoord;
        const Vec2& uv2 = vertices[i2].texcoord;

        // Edge vectors in position space
        const float e1x = p1.x - p0.x;
        const float e1y = p1.y - p0.y;
        const float e1z = p1.z - p0.z;

        const float e2x = p2.x - p0.x;
        const float e2y = p2.y - p0.y;
        const float e2z = p2.z - p0.z;

        // UV deltas
        const float du1 = uv1.x - uv0.x;
        const float dv1 = uv1.y - uv0.y;
        const float du2 = uv2.x - uv0.x;
        const float dv2 = uv2.y - uv0.y;

        // Determinant of the UV matrix. If near-zero, the triangle has
        // degenerate UVs (zero-area in UV space) — skip it.
        const float det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-12f) {
            continue;
        }
        const float invDet = 1.0f / det;

        // Tangent: T = (e1 * dv2 - e2 * dv1) / det
        const Vec3 t = {
            (e1x * dv2 - e2x * dv1) * invDet,
            (e1y * dv2 - e2y * dv1) * invDet,
            (e1z * dv2 - e2z * dv1) * invDet,
        };

        // Bitangent: B = (e2 * du1 - e1 * du2) / det
        const Vec3 b = {
            (e2x * du1 - e1x * du2) * invDet,
            (e2y * du1 - e1y * du2) * invDet,
            (e2z * du1 - e1z * du2) * invDet,
        };

        // Accumulate for all three vertices of the triangle.
        // Weighting by triangle area is implicit in the unnormalized edge vectors.
        tangentAccum[i0].x += t.x; tangentAccum[i0].y += t.y; tangentAccum[i0].z += t.z;
        tangentAccum[i1].x += t.x; tangentAccum[i1].y += t.y; tangentAccum[i1].z += t.z;
        tangentAccum[i2].x += t.x; tangentAccum[i2].y += t.y; tangentAccum[i2].z += t.z;

        bitangentAccum[i0].x += b.x; bitangentAccum[i0].y += b.y; bitangentAccum[i0].z += b.z;
        bitangentAccum[i1].x += b.x; bitangentAccum[i1].y += b.y; bitangentAccum[i1].z += b.z;
        bitangentAccum[i2].x += b.x; bitangentAccum[i2].y += b.y; bitangentAccum[i2].z += b.z;
    }

    // Orthogonalize, normalize, and compute bitangent sign per vertex.
    for (size_t v = 0; v < vertexCount; ++v) {
        const Vec3& n = vertices[v].normal;
        Vec3 t = tangentAccum[v];
        const Vec3& b = bitangentAccum[v];

        // Gram-Schmidt orthogonalization: T' = T - N * dot(N, T)
        const float nDotT = n.x * t.x + n.y * t.y + n.z * t.z;
        t.x -= n.x * nDotT;
        t.y -= n.y * nDotT;
        t.z -= n.z * nDotT;

        // Normalize the tangent
        const float len = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        if (len > 1e-8f) {
            const float invLen = 1.0f / len;
            t.x *= invLen;
            t.y *= invLen;
            t.z *= invLen;
        } else {
            // Fallback: generate an arbitrary tangent perpendicular to the normal.
            // Choose the axis least aligned with the normal to minimize numerical error.
            const float absX = std::abs(n.x);
            const float absY = std::abs(n.y);
            const float absZ = std::abs(n.z);

            Vec3 up;
            if (absX <= absY && absX <= absZ) {
                up = Vec3{1.f, 0.f, 0.f};
            } else if (absY <= absZ) {
                up = Vec3{0.f, 1.f, 0.f};
            } else {
                up = Vec3{0.f, 0.f, 1.f};
            }

            // t = normalize(up - n * dot(n, up))
            const float d = n.x * up.x + n.y * up.y + n.z * up.z;
            t.x = up.x - n.x * d;
            t.y = up.y - n.y * d;
            t.z = up.z - n.z * d;

            const float fallbackLen = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
            if (fallbackLen > 1e-8f) {
                const float invFallbackLen = 1.0f / fallbackLen;
                t.x *= invFallbackLen;
                t.y *= invFallbackLen;
                t.z *= invFallbackLen;
            } else {
                // Extremely degenerate case (zero normal) — use +X as tangent.
                t = Vec3{1.f, 0.f, 0.f};
            }
        }

        // Compute bitangent sign: w = sign(dot(cross(N, T), B))
        // cross(N, T):
        const float cx = n.y * t.z - n.z * t.y;
        const float cy = n.z * t.x - n.x * t.z;
        const float cz = n.x * t.y - n.y * t.x;
        // dot(cross(N, T), B):
        const float sign = cx * b.x + cy * b.y + cz * b.z;
        const float w = (sign < 0.0f) ? -1.0f : 1.0f;

        vertices[v].tangent = Vec4{t.x, t.y, t.z, w};
    }
}

} // namespace uz
