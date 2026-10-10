#pragma once

/// UzEngine Lightmap UV Generator — xatlas wrapper
///
/// Generates a secondary UV set (UV1) for lightmap baking using xatlas.
/// Called by MeshImporter when `--generate-lightmap-uvs` is enabled or
/// when the mesh is missing UV1.
///
/// @ref xatlas — https://github.com/jpcy/xatlas
///
/// # Thread Safety
/// Thread-safe — xatlas is reentrant. Each call allocates its own context.

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Input mesh data for lightmap UV generation.
struct LightmapUVInput {
    const float*    positions;   ///< Interleaved XYZ positions (3 floats per vertex)
    const float*    normals;     ///< Interleaved XYZ normals (3 floats per vertex, optional — can be nullptr)
    const float*    uvs;         ///< Existing UV0 channel (2 floats per vertex, optional — can be nullptr)
    const uint32_t* indices;     ///< Triangle index buffer
    uint32_t        vertexCount;
    uint32_t        indexCount;  ///< Must be a multiple of 3
};

/// Options for lightmap UV generation.
struct LightmapUVOptions {
    uint32_t maxChartSize = 0;        ///< Max chart dimension (0 = unlimited)
    uint32_t padding      = 2;        ///< Padding between charts in texels
    uint32_t resolution   = 1024;     ///< Target atlas resolution (for texel density)
    bool     bruteForce   = false;    ///< Use brute-force packing (slower, tighter)
};

/// Output of lightmap UV generation.
struct LightmapUVOutput {
    std::vector<float>    uvs;         ///< UV1 coordinates (2 floats per output vertex)
    std::vector<uint32_t> indices;     ///< Remapped index buffer
    std::vector<float>    positions;   ///< Remapped positions (3 floats per vertex, xatlas may split vertices)
    std::vector<float>    normals;     ///< Remapped normals (3 floats per vertex)
    std::vector<float>    originalUVs; ///< Remapped original UV0 (2 floats per vertex)
    uint32_t              vertexCount; ///< Output vertex count (>= input due to chart splits)
    uint32_t              indexCount;
    uint32_t              atlasWidth;  ///< Final atlas width in texels
    uint32_t              atlasHeight; ///< Final atlas height in texels
    uint32_t              chartCount;  ///< Number of UV charts generated
    bool                  success;
    std::string           error;       ///< Error message on failure
};

/// Generate lightmap UVs for a mesh using xatlas.
///
/// @param input   Mesh geometry data.
/// @param options Packing parameters.
/// @return Output with remapped vertices and UV1 coordinates.
LightmapUVOutput GenerateLightmapUVs(const LightmapUVInput& input,
                                      const LightmapUVOptions& options = {});

} // namespace uz
