/// uzgltf — glTF/GLB to .uzmesh converter
///
/// Standalone CLI tool: reads .gltf/.glb via fastgltf, extracts geometry,
/// applies Y-up → Z-up axis conversion, generates tangents, and writes
/// the engine's runtime .uzmesh format.
///
/// # Axis Conversion
/// glTF is Y-up, right-handed (X-right, Y-up, -Z-forward).
/// UzEngine is Z-up, left-handed (X-forward, Y-right, Z-up).
/// Mapping: Engine.X = -glTF.Z, Engine.Y = glTF.X, Engine.Z = glTF.Y
/// This is an odd permutation, so triangle winding is reversed (CW → CCW).
///
/// # Usage
/// ```
/// uzgltf --input <path.gltf|.glb> --output <path.uzmesh>
/// ```

#include "mesh.h"
#include "log.h"
#include "profiler.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(Cook);
UZ_DEFINE_LOG_CATEGORY(Cook, Info);

int main(int argc, char** argv) {
    std::string inputPath, outputPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc)   inputPath = argv[++i];
        if (arg == "--output" && i + 1 < argc)  outputPath = argv[++i];
    }

    if (inputPath.empty() || outputPath.empty()) {
        std::cerr << "Usage: uzgltf --input <path.gltf|.glb> --output <path.uzmesh>\n";
        return 1;
    }

    // 1. Parse glTF/GLB via fastgltf
    auto parentDir = std::filesystem::path(inputPath).parent_path();

    fastgltf::Parser parser;
    auto dataResult = fastgltf::GltfDataBuffer::FromPath(inputPath);
    if (dataResult.error() != fastgltf::Error::None) {
        std::cerr << "Error: cannot read " << inputPath << "\n";
        return 1;
    }

    auto type = fastgltf::determineGltfFileType(dataResult.get());
    fastgltf::Expected<fastgltf::Asset> assetResult(fastgltf::Error::None);

    auto options = fastgltf::Options::LoadExternalBuffers
                 | fastgltf::Options::GenerateMeshIndices;

    if (type == fastgltf::GltfType::glTF) {
        assetResult = parser.loadGltf(dataResult.get(), parentDir, options);
    } else if (type == fastgltf::GltfType::GLB) {
        assetResult = parser.loadGltfBinary(dataResult.get(), parentDir, options);
    } else {
        std::cerr << "Error: unrecognized file format\n";
        return 1;
    }

    if (assetResult.error() != fastgltf::Error::None) {
        std::cerr << "Error: fastgltf parse failed\n";
        return 1;
    }

    auto& gltf = assetResult.get();

    if (gltf.meshes.empty()) {
        std::cerr << "Error: no meshes in glTF\n";
        return 1;
    }

    std::cout << "glTF: " << gltf.meshes.size() << " meshes, "
              << gltf.nodes.size() << " nodes\n";

    // 2. Extract geometry from first mesh, all primitives
    const auto& mesh = gltf.meshes[0];
    std::vector<uz::Vertex> vertices;
    std::vector<uint32_t> indices;

    for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
        const auto& prim = mesh.primitives[primIdx];

        // POSITION (required)
        auto posIt = prim.findAttribute("POSITION");
        if (posIt == prim.attributes.end()) {
            std::cerr << "Warning: primitive " << primIdx << " missing POSITION — skipping\n";
            continue;
        }

        const auto& posAccessor = gltf.accessors[posIt->accessorIndex];
        const size_t vertCount = posAccessor.count;
        const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());

        vertices.resize(baseVertex + vertCount);

        // Read positions with Y-up RH → Z-up LH axis conversion
        // glTF: X-right, Y-up, -Z-forward (right-handed)
        // UzEngine: X-forward, Y-right, Z-up (left-handed)
        // Same cyclic remap as FBX: Engine.X = glTF.Z, Engine.Y = glTF.X, Engine.Z = glTF.Y
        // (glTF forward -Z → engine -X; models face -X naturally, rotated by scene if needed)
        // This is det=+1 (even permutation), applied to RH source = RH result.
        // Winding flip (below) converts RH CCW → LH CCW.
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            gltf, posAccessor, [&](fastgltf::math::fvec3 pos, std::size_t idx) {
                auto& v = vertices[baseVertex + idx];
                v.position = {pos[2], pos[0], pos[1]};
            });

        // NORMAL (optional)
        bool hasNormals = false;
        auto normIt = prim.findAttribute("NORMAL");
        if (normIt != prim.attributes.end()) {
            const auto& normAccessor = gltf.accessors[normIt->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                gltf, normAccessor, [&](fastgltf::math::fvec3 n, std::size_t idx) {
                    auto& v = vertices[baseVertex + idx];
                    v.normal = {n[2], n[0], n[1]};
                });
            hasNormals = true;
        }

        // TEXCOORD_0 (optional)
        auto uvIt = prim.findAttribute("TEXCOORD_0");
        if (uvIt != prim.attributes.end()) {
            const auto& uvAccessor = gltf.accessors[uvIt->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                gltf, uvAccessor, [&](fastgltf::math::fvec2 uv, std::size_t idx) {
                    auto& v = vertices[baseVertex + idx];
                    v.texcoord = {uv[0], uv[1]};
                });
        }

        // Indices
        if (prim.indicesAccessor.has_value()) {
            const auto& idxAccessor = gltf.accessors[prim.indicesAccessor.value()];
            const size_t baseIndex = indices.size();
            indices.resize(baseIndex + idxAccessor.count);
            fastgltf::iterateAccessorWithIndex<uint32_t>(
                gltf, idxAccessor, [&](uint32_t val, std::size_t idx) {
                    indices[baseIndex + idx] = baseVertex + val;
                });
        } else {
            // Non-indexed: generate sequential
            for (uint32_t i = 0; i < vertCount; ++i) {
                indices.push_back(baseVertex + i);
            }
        }

        // Generate flat normals if not provided
        if (!hasNormals) {
            for (size_t t = 0; t + 2 < indices.size(); t += 3) {
                auto& v0 = vertices[indices[t]];
                auto& v1 = vertices[indices[t + 1]];
                auto& v2 = vertices[indices[t + 2]];

                float e1x = v1.position.x - v0.position.x;
                float e1y = v1.position.y - v0.position.y;
                float e1z = v1.position.z - v0.position.z;
                float e2x = v2.position.x - v0.position.x;
                float e2y = v2.position.y - v0.position.y;
                float e2z = v2.position.z - v0.position.z;

                float nx = e1y * e2z - e1z * e2y;
                float ny = e1z * e2x - e1x * e2z;
                float nz = e1x * e2y - e1y * e2x;
                float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }

                v0.normal = {nx, ny, nz};
                v1.normal = {nx, ny, nz};
                v2.normal = {nx, ny, nz};
            }
        }
    }

    std::cout << "Extracted: " << vertices.size() << " vertices, "
              << indices.size() << " indices\n";

    // 3. Reverse triangle winding (axis remap is odd permutation → flip handedness)
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        std::swap(indices[t + 1], indices[t + 2]);
    }

    // 4. Generate tangents
    uz::GenerateTangents(vertices, indices);

    // 5. Compute AABB
    uz::Vec3 bMin = {std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max()};
    uz::Vec3 bMax = {std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest()};
    for (const auto& v : vertices) {
        bMin.x = std::min(bMin.x, v.position.x); bMax.x = std::max(bMax.x, v.position.x);
        bMin.y = std::min(bMin.y, v.position.y); bMax.y = std::max(bMax.y, v.position.y);
        bMin.z = std::min(bMin.z, v.position.z); bMax.z = std::max(bMax.z, v.position.z);
    }

    const uint32_t vertexCount = static_cast<uint32_t>(vertices.size());
    const uint32_t indexCount = static_cast<uint32_t>(indices.size());

    // 6. Build Mesh and save
    uz::Mesh outMesh;
    std::memset(&outMesh.header, 0, sizeof(uz::MeshHeader));
    outMesh.header.magic            = uz::kMeshMagic;
    outMesh.header.version          = uz::kMeshVersion;
    outMesh.header.vertexFormat     = uz::MeshVertexFormat::Standard;
    outMesh.header.vertexStride     = sizeof(uz::Vertex);
    outMesh.header.vertexCount      = vertexCount;
    outMesh.header.indexFormat      = uz::MeshIndexFormat::UInt32;
    outMesh.header.indexCount       = indexCount;
    outMesh.header.submeshCount     = 0;
    outMesh.header.boundsMin        = bMin;
    outMesh.header.boundsMax        = bMax;
    outMesh.header.vertexDataOffset = sizeof(uz::MeshHeader);
    outMesh.header.indexDataOffset  = sizeof(uz::MeshHeader) +
        static_cast<uint64_t>(vertexCount) * sizeof(uz::Vertex);

    outMesh.vertexData.resize(vertexCount * sizeof(uz::Vertex));
    std::memcpy(outMesh.vertexData.data(), vertices.data(), outMesh.vertexData.size());

    outMesh.indexData.resize(indexCount * sizeof(uint32_t));
    std::memcpy(outMesh.indexData.data(), indices.data(), outMesh.indexData.size());

    if (!uz::SaveMesh(outputPath, outMesh)) {
        std::cerr << "Error: failed to write " << outputPath << "\n";
        return 1;
    }

    std::cout << "Converted: " << vertexCount << " vertices, " << indexCount
              << " indices, bounds=[" << bMin.x << "," << bMin.y << "," << bMin.z
              << "]→[" << bMax.x << "," << bMax.y << "," << bMax.z
              << "] → " << outputPath << "\n";
    return 0;
}
