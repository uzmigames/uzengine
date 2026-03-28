/// VFX Viewport Demo — Procedural Mesh Generation Implementation
///
/// Generates sphere, cube, plane, cylinder, and torus meshes and uploads
/// them to GPU buffers via the RHI abstraction.

#include "vfx_viewport_mesh.h"
#include "rhi_device.h"
#include "rhi_command_list.h"
#include "upload_context.h"

#include <cmath>
#include <cstring>

namespace uz {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Tangent computation
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Lengyel 2001 "Computing Tangent Space Basis Vectors for an Arbitrary Mesh"
void ComputeTangents(ProceduralMeshData& mesh) {
    const size_t vertCount = mesh.vertices.size();
    const size_t triCount = mesh.indices.size() / 3;

    // Accumulation buffers for tangent (along U) and bitangent (along V)
    std::vector<float> tanU(vertCount * 3, 0.0f);
    std::vector<float> tanV(vertCount * 3, 0.0f);

    for (size_t t = 0; t < triCount; ++t) {
        const uint32_t i0 = mesh.indices[t * 3 + 0];
        const uint32_t i1 = mesh.indices[t * 3 + 1];
        const uint32_t i2 = mesh.indices[t * 3 + 2];

        const auto& v0 = mesh.vertices[i0];
        const auto& v1 = mesh.vertices[i1];
        const auto& v2 = mesh.vertices[i2];

        // Edge vectors in position space
        const float e1x = v1.position[0] - v0.position[0];
        const float e1y = v1.position[1] - v0.position[1];
        const float e1z = v1.position[2] - v0.position[2];
        const float e2x = v2.position[0] - v0.position[0];
        const float e2y = v2.position[1] - v0.position[1];
        const float e2z = v2.position[2] - v0.position[2];

        // UV deltas
        const float du1 = v1.uv[0] - v0.uv[0];
        const float dv1 = v1.uv[1] - v0.uv[1];
        const float du2 = v2.uv[0] - v0.uv[0];
        const float dv2 = v2.uv[1] - v0.uv[1];

        float det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-8f) {
            det = 1e-8f; // Avoid division by zero for degenerate UV triangles
        }
        const float invDet = 1.0f / det;

        // Tangent direction (partial derivative of position with respect to U)
        const float tx = (dv2 * e1x - dv1 * e2x) * invDet;
        const float ty = (dv2 * e1y - dv1 * e2y) * invDet;
        const float tz = (dv2 * e1z - dv1 * e2z) * invDet;

        // Bitangent direction (partial derivative of position with respect to V)
        const float bx = (du1 * e2x - du2 * e1x) * invDet;
        const float by = (du1 * e2y - du2 * e1y) * invDet;
        const float bz = (du1 * e2z - du2 * e1z) * invDet;

        // Accumulate for all 3 vertices of the triangle
        for (const uint32_t idx : {i0, i1, i2}) {
            tanU[idx * 3 + 0] += tx;
            tanU[idx * 3 + 1] += ty;
            tanU[idx * 3 + 2] += tz;
            tanV[idx * 3 + 0] += bx;
            tanV[idx * 3 + 1] += by;
            tanV[idx * 3 + 2] += bz;
        }
    }

    // Gram-Schmidt orthonormalize tangent against normal, compute handedness
    for (size_t i = 0; i < vertCount; ++i) {
        const float nx = mesh.vertices[i].normal[0];
        const float ny = mesh.vertices[i].normal[1];
        const float nz = mesh.vertices[i].normal[2];

        float tx = tanU[i * 3 + 0];
        float ty = tanU[i * 3 + 1];
        float tz = tanU[i * 3 + 2];

        // Gram-Schmidt: T' = T - N * dot(N, T)
        const float nDotT = nx * tx + ny * ty + nz * tz;
        tx -= nx * nDotT;
        ty -= ny * nDotT;
        tz -= nz * nDotT;

        // Normalize the orthogonalized tangent
        float len = std::sqrt(tx * tx + ty * ty + tz * tz);
        if (len > 1e-8f) {
            const float invLen = 1.0f / len;
            tx *= invLen;
            ty *= invLen;
            tz *= invLen;
        } else {
            // Degenerate case — pick an arbitrary tangent perpendicular to normal.
            // Choose a basis vector least aligned with the normal for the cross product.
            if (std::abs(nx) < 0.9f) {
                tx = 0.0f; ty = -nz; tz = ny;
            } else {
                tx = nz; ty = 0.0f; tz = -nx;
            }
            len = std::sqrt(tx * tx + ty * ty + tz * tz);
            if (len > 1e-8f) {
                tx /= len;
                ty /= len;
                tz /= len;
            }
        }

        // Handedness: w = sign(dot(cross(N, T'), B))
        const float cx = ny * tz - nz * ty;
        const float cy = nz * tx - nx * tz;
        const float cz = nx * ty - ny * tx;
        const float bx = tanV[i * 3 + 0];
        const float by = tanV[i * 3 + 1];
        const float bz = tanV[i * 3 + 2];
        const float handedness = (cx * bx + cy * by + cz * bz) < 0.0f ? -1.0f : 1.0f;

        mesh.vertices[i].tangent[0] = tx;
        mesh.vertices[i].tangent[1] = ty;
        mesh.vertices[i].tangent[2] = tz;
        mesh.vertices[i].tangent[3] = handedness;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Sphere
// ─────────────────────────────────────────────────────────────────────────────

ProceduralMeshData GenerateSphere(uint32_t slices, uint32_t stacks) {
    ProceduralMeshData mesh;

    // Generate vertices
    for (uint32_t stack = 0; stack <= stacks; ++stack) {
        const float phi = kPi * static_cast<float>(stack) / static_cast<float>(stacks);
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        for (uint32_t slice = 0; slice <= slices; ++slice) {
            const float theta = kTwoPi * static_cast<float>(slice) / static_cast<float>(slices);
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            ProceduralVertex v{};
            v.position[0] = sinPhi * cosTheta;
            v.position[1] = cosPhi;
            v.position[2] = sinPhi * sinTheta;
            v.normal[0] = v.position[0];
            v.normal[1] = v.position[1];
            v.normal[2] = v.position[2];
            v.uv[0] = static_cast<float>(slice) / static_cast<float>(slices);
            v.uv[1] = static_cast<float>(stack) / static_cast<float>(stacks);
            mesh.vertices.push_back(v);
        }
    }

    // Generate indices (triangle strips -> triangles)
    for (uint32_t stack = 0; stack < stacks; ++stack) {
        for (uint32_t slice = 0; slice < slices; ++slice) {
            const uint32_t row0 = stack * (slices + 1);
            const uint32_t row1 = (stack + 1) * (slices + 1);

            // First triangle
            mesh.indices.push_back(row0 + slice);
            mesh.indices.push_back(row1 + slice);
            mesh.indices.push_back(row0 + slice + 1);

            // Second triangle
            mesh.indices.push_back(row0 + slice + 1);
            mesh.indices.push_back(row1 + slice);
            mesh.indices.push_back(row1 + slice + 1);
        }
    }

    ComputeTangents(mesh);
    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cube
// ─────────────────────────────────────────────────────────────────────────────

ProceduralMeshData GenerateCube() {
    ProceduralMeshData mesh;

    // 6 faces x 4 vertices each = 24 vertices, 6 faces x 6 indices = 36 indices
    struct FaceData {
        float normal[3];
        float positions[4][3]; // 4 corners
        float uvs[4][2];
    };

    // Each face: vertices wound counter-clockwise when viewed from outside
    const FaceData faces[] = {
        // +X face
        {{ 1, 0, 0}, {{ 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1}, { 1,-1, 1}},
         {{0,0}, {1,0}, {1,1}, {0,1}}},
        // -X face
        {{-1, 0, 0}, {{-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1}, {-1,-1,-1}},
         {{0,0}, {1,0}, {1,1}, {0,1}}},
        // +Y face
        {{ 0, 1, 0}, {{-1, 1,-1}, { 1, 1,-1}, { 1, 1, 1}, {-1, 1, 1}},
         {{0,0}, {1,0}, {1,1}, {0,1}}},
        // -Y face
        {{ 0,-1, 0}, {{-1,-1, 1}, { 1,-1, 1}, { 1,-1,-1}, {-1,-1,-1}},
         {{0,0}, {1,0}, {1,1}, {0,1}}},
        // +Z face
        {{ 0, 0, 1}, {{ 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}, {-1,-1, 1}},
         {{0,0}, {1,0}, {1,1}, {0,1}}},
        // -Z face
        {{ 0, 0,-1}, {{-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1}, { 1,-1,-1}},
         {{0,0}, {1,0}, {1,1}, {0,1}}},
    };

    for (const auto& face : faces) {
        const auto baseIdx = static_cast<uint32_t>(mesh.vertices.size());

        for (int c = 0; c < 4; ++c) {
            ProceduralVertex v{};
            v.position[0] = face.positions[c][0];
            v.position[1] = face.positions[c][1];
            v.position[2] = face.positions[c][2];
            v.normal[0] = face.normal[0];
            v.normal[1] = face.normal[1];
            v.normal[2] = face.normal[2];
            v.uv[0] = face.uvs[c][0];
            v.uv[1] = face.uvs[c][1];
            mesh.vertices.push_back(v);
        }

        // Two triangles per face
        mesh.indices.push_back(baseIdx + 0);
        mesh.indices.push_back(baseIdx + 1);
        mesh.indices.push_back(baseIdx + 2);
        mesh.indices.push_back(baseIdx + 0);
        mesh.indices.push_back(baseIdx + 2);
        mesh.indices.push_back(baseIdx + 3);
    }

    ComputeTangents(mesh);
    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// Plane
// ─────────────────────────────────────────────────────────────────────────────

ProceduralMeshData GeneratePlane(float halfExtent, uint32_t divisions) {
    ProceduralMeshData mesh;

    const uint32_t vertsPerSide = divisions + 1;

    for (uint32_t z = 0; z < vertsPerSide; ++z) {
        for (uint32_t x = 0; x < vertsPerSide; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(divisions);
            const float v = static_cast<float>(z) / static_cast<float>(divisions);

            ProceduralVertex vert{};
            vert.position[0] = (u * 2.0f - 1.0f) * halfExtent;
            vert.position[1] = 0.0f;
            vert.position[2] = (v * 2.0f - 1.0f) * halfExtent;
            vert.normal[0] = 0.0f;
            vert.normal[1] = 1.0f;
            vert.normal[2] = 0.0f;
            vert.uv[0] = u;
            vert.uv[1] = v;
            mesh.vertices.push_back(vert);
        }
    }

    for (uint32_t z = 0; z < divisions; ++z) {
        for (uint32_t x = 0; x < divisions; ++x) {
            const uint32_t i00 = z * vertsPerSide + x;
            const uint32_t i10 = i00 + 1;
            const uint32_t i01 = (z + 1) * vertsPerSide + x;
            const uint32_t i11 = i01 + 1;

            mesh.indices.push_back(i00);
            mesh.indices.push_back(i01);
            mesh.indices.push_back(i10);

            mesh.indices.push_back(i10);
            mesh.indices.push_back(i01);
            mesh.indices.push_back(i11);
        }
    }

    ComputeTangents(mesh);
    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cylinder
// ─────────────────────────────────────────────────────────────────────────────

ProceduralMeshData GenerateCylinder(float radius, float height, uint32_t slices) {
    ProceduralMeshData mesh;
    const float halfH = height * 0.5f;

    // Side vertices: two rings (top and bottom)
    for (uint32_t i = 0; i <= slices; ++i) {
        const float theta = kTwoPi * static_cast<float>(i) / static_cast<float>(slices);
        const float c = std::cos(theta);
        const float s = std::sin(theta);
        const float u = static_cast<float>(i) / static_cast<float>(slices);

        // Bottom ring
        ProceduralVertex vBot{};
        vBot.position[0] = c * radius;
        vBot.position[1] = -halfH;
        vBot.position[2] = s * radius;
        vBot.normal[0] = c;
        vBot.normal[1] = 0.0f;
        vBot.normal[2] = s;
        vBot.uv[0] = u;
        vBot.uv[1] = 1.0f;
        mesh.vertices.push_back(vBot);

        // Top ring
        ProceduralVertex vTop{};
        vTop.position[0] = c * radius;
        vTop.position[1] = halfH;
        vTop.position[2] = s * radius;
        vTop.normal[0] = c;
        vTop.normal[1] = 0.0f;
        vTop.normal[2] = s;
        vTop.uv[0] = u;
        vTop.uv[1] = 0.0f;
        mesh.vertices.push_back(vTop);
    }

    // Side indices
    for (uint32_t i = 0; i < slices; ++i) {
        const uint32_t bot0 = i * 2;
        const uint32_t top0 = i * 2 + 1;
        const uint32_t bot1 = (i + 1) * 2;
        const uint32_t top1 = (i + 1) * 2 + 1;

        mesh.indices.push_back(bot0);
        mesh.indices.push_back(bot1);
        mesh.indices.push_back(top0);

        mesh.indices.push_back(top0);
        mesh.indices.push_back(bot1);
        mesh.indices.push_back(top1);
    }

    // Top cap
    {
        const auto centerIdx = static_cast<uint32_t>(mesh.vertices.size());
        ProceduralVertex center{};
        center.position[1] = halfH;
        center.normal[1] = 1.0f;
        center.uv[0] = 0.5f;
        center.uv[1] = 0.5f;
        mesh.vertices.push_back(center);

        const auto ringStart = static_cast<uint32_t>(mesh.vertices.size());
        for (uint32_t i = 0; i <= slices; ++i) {
            const float theta = kTwoPi * static_cast<float>(i) / static_cast<float>(slices);
            const float c = std::cos(theta);
            const float s = std::sin(theta);

            ProceduralVertex v{};
            v.position[0] = c * radius;
            v.position[1] = halfH;
            v.position[2] = s * radius;
            v.normal[1] = 1.0f;
            v.uv[0] = c * 0.5f + 0.5f;
            v.uv[1] = s * 0.5f + 0.5f;
            mesh.vertices.push_back(v);
        }

        for (uint32_t i = 0; i < slices; ++i) {
            mesh.indices.push_back(centerIdx);
            mesh.indices.push_back(ringStart + i);
            mesh.indices.push_back(ringStart + i + 1);
        }
    }

    // Bottom cap
    {
        const auto centerIdx = static_cast<uint32_t>(mesh.vertices.size());
        ProceduralVertex center{};
        center.position[1] = -halfH;
        center.normal[1] = -1.0f;
        center.uv[0] = 0.5f;
        center.uv[1] = 0.5f;
        mesh.vertices.push_back(center);

        const auto ringStart = static_cast<uint32_t>(mesh.vertices.size());
        for (uint32_t i = 0; i <= slices; ++i) {
            const float theta = kTwoPi * static_cast<float>(i) / static_cast<float>(slices);
            const float c = std::cos(theta);
            const float s = std::sin(theta);

            ProceduralVertex v{};
            v.position[0] = c * radius;
            v.position[1] = -halfH;
            v.position[2] = s * radius;
            v.normal[1] = -1.0f;
            v.uv[0] = c * 0.5f + 0.5f;
            v.uv[1] = s * 0.5f + 0.5f;
            mesh.vertices.push_back(v);
        }

        for (uint32_t i = 0; i < slices; ++i) {
            mesh.indices.push_back(centerIdx);
            mesh.indices.push_back(ringStart + i + 1);
            mesh.indices.push_back(ringStart + i);
        }
    }

    ComputeTangents(mesh);
    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// Torus
// ─────────────────────────────────────────────────────────────────────────────

ProceduralMeshData GenerateTorus(float majorRadius, float minorRadius,
                                  uint32_t majorSegments, uint32_t minorSegments) {
    ProceduralMeshData mesh;

    for (uint32_t i = 0; i <= majorSegments; ++i) {
        const float theta = kTwoPi * static_cast<float>(i) / static_cast<float>(majorSegments);
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);

        for (uint32_t j = 0; j <= minorSegments; ++j) {
            const float phi = kTwoPi * static_cast<float>(j) / static_cast<float>(minorSegments);
            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);

            // Position on torus surface
            const float r = majorRadius + minorRadius * cosPhi;

            ProceduralVertex v{};
            v.position[0] = r * cosTheta;
            v.position[1] = minorRadius * sinPhi;
            v.position[2] = r * sinTheta;

            // Normal = direction from tube center to surface point
            v.normal[0] = cosPhi * cosTheta;
            v.normal[1] = sinPhi;
            v.normal[2] = cosPhi * sinTheta;

            v.uv[0] = static_cast<float>(i) / static_cast<float>(majorSegments);
            v.uv[1] = static_cast<float>(j) / static_cast<float>(minorSegments);
            mesh.vertices.push_back(v);
        }
    }

    for (uint32_t i = 0; i < majorSegments; ++i) {
        for (uint32_t j = 0; j < minorSegments; ++j) {
            const uint32_t row0 = i * (minorSegments + 1);
            const uint32_t row1 = (i + 1) * (minorSegments + 1);

            mesh.indices.push_back(row0 + j);
            mesh.indices.push_back(row1 + j);
            mesh.indices.push_back(row0 + j + 1);

            mesh.indices.push_back(row0 + j + 1);
            mesh.indices.push_back(row1 + j);
            mesh.indices.push_back(row1 + j + 1);
        }
    }

    ComputeTangents(mesh);
    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU Upload
// ─────────────────────────────────────────────────────────────────────────────

ProceduralMesh UploadProceduralMesh(IRHIDevice* device,
                                     IRHICommandList* cmdList,
                                     UploadContext& upload,
                                     const ProceduralMeshData& data,
                                     const char* name) {
    ProceduralMesh result;
    result.name = name;
    result.indexCount = static_cast<uint32_t>(data.indices.size());
    result.vertexStride = sizeof(ProceduralVertex);

    // Create vertex buffer
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = data.vertices.size() * sizeof(ProceduralVertex);
        desc.stride = sizeof(ProceduralVertex);
        desc.usage = RHIBufferUsage::VertexBuffer | RHIBufferUsage::StructuredBuffer | RHIBufferUsage::CopyDst;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = name;

        result.vertexBuffer = device->CreateBuffer(desc);
        upload.UploadBuffer(cmdList, result.vertexBuffer,
                            data.vertices.data(),
                            desc.sizeBytes);
        result.vbSrvIndex = device->GetBufferSRVIndex(result.vertexBuffer);
    }

    // Create index buffer
    {
        RHIBufferDesc desc{};
        desc.sizeBytes = data.indices.size() * sizeof(uint32_t);
        desc.stride = sizeof(uint32_t);
        desc.usage = RHIBufferUsage::IndexBuffer | RHIBufferUsage::CopyDst;
        desc.memory = RHIMemoryType::Default;
        desc.debugName = name;

        result.indexBuffer = device->CreateBuffer(desc);
        upload.UploadBuffer(cmdList, result.indexBuffer,
                            data.indices.data(),
                            desc.sizeBytes);
    }

    return result;
}

} // namespace uz
