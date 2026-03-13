/// UzEngine Mesh Importer — Implementation
///
/// Parses glTF/GLB via fastgltf, extracts geometry from the first primitive,
/// applies meshoptimizer cache/fetch optimizations, and writes .uzmesh format.

#include "mesh_importer.h"
#include "log.h"
#include "profiler.h"
#include "uzmesh_format.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace uz {

namespace {

/// Generates flat (faceted) normals for a triangle mesh in-place.
///
/// For each triangle, computes the face normal from the cross product of
/// two edges and assigns it to all three vertices. Assumes the vertex array
/// is already populated with positions.
void GenerateFlatNormals(std::vector<VertexPosNormUV>& vertices, const std::vector<uint32_t>& indices) {
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        auto& v0 = vertices[indices[i]];
        auto& v1 = vertices[indices[i + 1]];
        auto& v2 = vertices[indices[i + 2]];

        // Edge vectors
        const float e1x = v1.position[0] - v0.position[0];
        const float e1y = v1.position[1] - v0.position[1];
        const float e1z = v1.position[2] - v0.position[2];

        const float e2x = v2.position[0] - v0.position[0];
        const float e2y = v2.position[1] - v0.position[1];
        const float e2z = v2.position[2] - v0.position[2];

        // Cross product
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;

        // Normalize
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8f) {
            nx /= len;
            ny /= len;
            nz /= len;
        }

        // Assign to all three vertices
        v0.normal[0] = nx; v0.normal[1] = ny; v0.normal[2] = nz;
        v1.normal[0] = nx; v1.normal[1] = ny; v1.normal[2] = nz;
        v2.normal[0] = nx; v2.normal[1] = ny; v2.normal[2] = nz;
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// MeshImporter
// ─────────────────────────────────────────────────────────────────────────────

MeshImporter::MeshImporter()
    : m_extensions({".glb", ".gltf"}) {}

const char* MeshImporter::Name() const {
    return "mesh";
}

uint32_t MeshImporter::Version() const {
    return 1;
}

const std::vector<std::string>& MeshImporter::Extensions() const {
    return m_extensions;
}

AssetImportResult MeshImporter::Import(
    const std::vector<uint8_t>& sourceData,
    const std::string& sourceName,
    const AssetImportOptions& /*options*/) {
    UZ_PROFILE_FUNCTION();

    AssetImportResult result;

    if (sourceData.empty()) {
        result.error = "Empty source data";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // Parse glTF/GLB
    fastgltf::Parser parser;

    auto dataBuffer = fastgltf::GltfDataBuffer::FromBytes(
        reinterpret_cast<const std::byte*>(sourceData.data()), sourceData.size());
    if (dataBuffer.error() != fastgltf::Error::None) {
        result.error = "Failed to create glTF data buffer";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    auto asset = parser.loadGltf(dataBuffer.get(), {}, fastgltf::Options::None);
    if (asset.error() != fastgltf::Error::None) {
        result.error = "Failed to parse glTF: error code " +
                       std::to_string(static_cast<int>(asset.error()));
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    auto& gltf = asset.get();

    if (gltf.meshes.empty()) {
        result.error = "No meshes found in glTF file";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    const auto& mesh = gltf.meshes[0];
    if (mesh.primitives.empty()) {
        result.error = "First mesh has no primitives";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    const auto& prim = mesh.primitives[0];

    // Find POSITION attribute (required)
    auto posIt = prim.findAttribute("POSITION");
    if (posIt == prim.attributes.end()) {
        result.error = "Primitive missing POSITION attribute";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    const auto& posAccessor = gltf.accessors[posIt->accessorIndex];
    const size_t vertexCount = posAccessor.count;

    // Allocate vertices
    std::vector<VertexPosNormUV> vertices(vertexCount);

    // Read positions
    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
        gltf, posAccessor, [&](fastgltf::math::fvec3 pos, std::size_t idx) {
            vertices[idx].position[0] = pos[0];
            vertices[idx].position[1] = pos[1];
            vertices[idx].position[2] = pos[2];
        });

    // Read normals (optional)
    bool hasNormals = false;
    auto normIt = prim.findAttribute("NORMAL");
    if (normIt != prim.attributes.end()) {
        const auto& normAccessor = gltf.accessors[normIt->accessorIndex];
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            gltf, normAccessor, [&](fastgltf::math::fvec3 n, std::size_t idx) {
                vertices[idx].normal[0] = n[0];
                vertices[idx].normal[1] = n[1];
                vertices[idx].normal[2] = n[2];
            });
        hasNormals = true;
    }

    // Read UVs (optional, default to 0,0)
    auto uvIt = prim.findAttribute("TEXCOORD_0");
    if (uvIt != prim.attributes.end()) {
        const auto& uvAccessor = gltf.accessors[uvIt->accessorIndex];
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
            gltf, uvAccessor, [&](fastgltf::math::fvec2 uv, std::size_t idx) {
                vertices[idx].uv[0] = uv[0];
                vertices[idx].uv[1] = uv[1];
            });
    }

    // Read indices
    std::vector<uint32_t> indices;
    if (prim.indicesAccessor.has_value()) {
        const auto& idxAccessor = gltf.accessors[prim.indicesAccessor.value()];
        indices.resize(idxAccessor.count);
        fastgltf::iterateAccessorWithIndex<uint32_t>(
            gltf, idxAccessor, [&](uint32_t val, std::size_t idx) {
                indices[idx] = val;
            });
    } else {
        // Non-indexed: generate sequential indices
        indices.resize(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i) {
            indices[i] = static_cast<uint32_t>(i);
        }
    }

    // Generate flat normals if not provided
    if (!hasNormals) {
        GenerateFlatNormals(vertices, indices);
    }

    // Meshopt: optimize vertex cache ordering
    std::vector<uint32_t> optimizedIndices(indices.size());
    meshopt_optimizeVertexCache(
        optimizedIndices.data(), indices.data(),
        indices.size(), vertices.size());

    // Meshopt: optimize vertex fetch (reorders vertices for cache locality)
    std::vector<VertexPosNormUV> optimizedVertices(vertices.size());
    const size_t finalVertexCount = meshopt_optimizeVertexFetch(
        optimizedVertices.data(), optimizedIndices.data(),
        optimizedIndices.size(), vertices.data(),
        vertices.size(), sizeof(VertexPosNormUV));

    optimizedVertices.resize(finalVertexCount);

    // Build .uzmesh
    UzMeshHeader header;
    header.lodCount = 1;
    header.totalSubmeshCount = 1;
    header.vertexCount = static_cast<uint32_t>(finalVertexCount);
    header.indexCount = static_cast<uint32_t>(optimizedIndices.size());
    header.vertexStride = sizeof(VertexPosNormUV);
    header.indexStride = sizeof(uint32_t);

    UzMeshLOD lod;
    lod.lodLevel = 0;
    lod.submeshOffset = 0;
    lod.submeshCount = 1;
    lod.screenHeightThreshold = 0.0f;

    UzMeshSubmesh submesh;
    submesh.vertexOffset = 0;
    submesh.indexOffset = 0;
    submesh.indexCount = static_cast<uint32_t>(optimizedIndices.size());
    submesh.materialIndex = 0;

    // Serialize vertex/index data as raw byte arrays
    std::vector<uint8_t> vertexData(finalVertexCount * sizeof(VertexPosNormUV));
    std::memcpy(vertexData.data(), optimizedVertices.data(), vertexData.size());

    std::vector<uint8_t> indexData(optimizedIndices.size() * sizeof(uint32_t));
    std::memcpy(indexData.data(), optimizedIndices.data(), indexData.size());

    result.data = WriteUzMesh(header, {lod}, {submesh}, vertexData, indexData);
    result.success = true;

    UZ_LOG(Asset, Info, "Imported mesh '{}': {} vertices, {} indices, {} bytes",
           sourceName, finalVertexCount, optimizedIndices.size(), result.data.size());

    return result;
}

} // namespace uz
