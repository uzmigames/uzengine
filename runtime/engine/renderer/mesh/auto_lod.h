/// UzEngine Auto-LOD Generation — meshoptimizer-based mesh simplification.
///
/// Generates LOD1-3 index buffers from a LOD0 mesh using meshopt_simplify.
/// LOD0 = original, LOD1 = 50%, LOD2 = 25%, LOD3 = 12.5%.
/// Used by all mesh import paths (uzfbx, uzgltf, mesh_importer, fbx_importer)
/// to ensure every imported mesh has automatic LOD data.
///
/// # Thread Safety
/// All functions are stateless and thread-safe.

#pragma once

#include "mesh.h"
#include "vertex.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace uz {

/// Maximum number of auto-generated LOD levels (LOD1, LOD2, LOD3).
static constexpr uint32_t kAutoLODCount = 3;

/// Index reduction ratios for each LOD level relative to LOD0.
static constexpr float kAutoLODReductions[kAutoLODCount] = { 0.50f, 0.25f, 0.125f };

/// Result of auto-LOD generation for a single mesh.
struct AutoLODResult {
    /// Per-LOD index buffers (LOD1, LOD2, LOD3). Empty if generation failed.
    std::vector<uint32_t> lodIndices[kAutoLODCount];
    /// Number of LODs actually generated (0-3).
    uint32_t generatedCount = 0;
};

/// Generates auto-LOD index buffers from LOD0 vertex/index data.
///
/// Extracts positions from Vertex array (48B stride), runs meshopt_simplify
/// at 50%, 25%, 12.5% reduction targets, and optimizes each LOD for vertex
/// cache. Stops early if a LOD produces fewer than 3 indices.
///
/// # Arguments
/// * `vertices` — LOD0 vertex array (position used for simplification).
/// * `vertexCount` — Number of vertices.
/// * `indices` — LOD0 index buffer.
/// * `indexCount` — Number of LOD0 indices.
///
/// # Returns
/// AutoLODResult with 0-3 generated LOD index buffers.
[[nodiscard]] inline AutoLODResult GenerateAutoLODs(
    const Vertex* vertices, uint32_t vertexCount,
    const uint32_t* indices, uint32_t indexCount)
{
    AutoLODResult result;

    if (!vertices || !indices || vertexCount == 0 || indexCount < 3) {
        return result;
    }

    // Extract flat position array for meshopt_simplify
    std::vector<float> posFlat(static_cast<size_t>(vertexCount) * 3);
    for (uint32_t i = 0; i < vertexCount; ++i) {
        posFlat[i * 3 + 0] = vertices[i].position.x;
        posFlat[i * 3 + 1] = vertices[i].position.y;
        posFlat[i * 3 + 2] = vertices[i].position.z;
    }

    const float simplifyScale = meshopt_simplifyScale(
        posFlat.data(), vertexCount, sizeof(float) * 3);

    for (uint32_t lod = 0; lod < kAutoLODCount; ++lod) {
        const size_t targetCount =
            static_cast<size_t>(static_cast<float>(indexCount) * kAutoLODReductions[lod]);
        if (targetCount < 3) break;

        std::vector<uint32_t> lodIdx(indexCount);
        float resultError = 0.0f;
        const size_t actualCount = meshopt_simplify(
            lodIdx.data(), indices, indexCount,
            posFlat.data(), vertexCount, sizeof(float) * 3,
            targetCount, 1e10f * simplifyScale, 0, &resultError);

        if (actualCount < 3) break;

        lodIdx.resize(actualCount);
        meshopt_optimizeVertexCache(
            lodIdx.data(), lodIdx.data(), lodIdx.size(), vertexCount);

        result.lodIndices[lod] = std::move(lodIdx);
        ++result.generatedCount;
    }

    return result;
}

/// Applies AutoLODResult to a Mesh header and index data buffer.
///
/// Concatenates LOD1-3 indices after LOD0 in the mesh's indexData,
/// and fills header.lodCount + header.lods[] fields.
///
/// # Arguments
/// * `mesh` — Mesh with LOD0 already in indexData. Modified in-place.
/// * `lods` — Result from GenerateAutoLODs().
inline void ApplyAutoLODs(Mesh& mesh, const AutoLODResult& lods) {
    if (lods.generatedCount == 0) {
        mesh.header.lodCount = 1;
        return;
    }

    mesh.header.lodCount = 1 + lods.generatedCount;
    const uint32_t indexStride = (mesh.header.indexFormat == MeshIndexFormat::UInt32) ? 4 : 2;
    const size_t lod0Bytes = static_cast<size_t>(mesh.header.indexCount) * indexStride;

    // Calculate total additional index bytes needed
    size_t extraBytes = 0;
    for (uint32_t i = 0; i < lods.generatedCount; ++i) {
        extraBytes += lods.lodIndices[i].size() * sizeof(uint32_t);
    }

    // Resize index data to hold LOD0 + LOD1-3
    const size_t oldSize = mesh.indexData.size();
    mesh.indexData.resize(oldSize + extraBytes);

    // Append LOD indices and fill header descriptors
    size_t writeOff = lod0Bytes;
    for (uint32_t i = 0; i < lods.generatedCount; ++i) {
        const size_t lodBytes = lods.lodIndices[i].size() * sizeof(uint32_t);
        mesh.header.lods[i].indexOffset = static_cast<uint32_t>(writeOff);
        mesh.header.lods[i].indexCount  = static_cast<uint32_t>(lods.lodIndices[i].size());
        std::memcpy(mesh.indexData.data() + writeOff,
                    lods.lodIndices[i].data(), lodBytes);
        writeOff += lodBytes;
    }
}

} // namespace uz
