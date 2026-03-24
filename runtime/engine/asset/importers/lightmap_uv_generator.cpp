/// UzEngine Lightmap UV Generator — xatlas integration
///
/// @ref xatlas — https://github.com/jpcy/xatlas

#include "lightmap_uv_generator.h"
#include "log.h"

#include <xatlas.h>

UZ_DECLARE_LOG_CATEGORY(Asset);

namespace uz {

LightmapUVOutput GenerateLightmapUVs(const LightmapUVInput& input,
                                      const LightmapUVOptions& options) {
    LightmapUVOutput output{};
    output.success = false;

    if (!input.positions || input.vertexCount == 0) {
        output.error = "No vertex positions provided";
        return output;
    }
    if (!input.indices || input.indexCount == 0 || input.indexCount % 3 != 0) {
        output.error = "Invalid index buffer (empty or not a multiple of 3)";
        return output;
    }

    // Create xatlas context
    xatlas::Atlas* atlas = xatlas::Create();
    if (!atlas) {
        output.error = "Failed to create xatlas atlas";
        return output;
    }

    // Add the single input mesh
    xatlas::MeshDecl meshDecl{};
    meshDecl.vertexCount          = input.vertexCount;
    meshDecl.vertexPositionData   = input.positions;
    meshDecl.vertexPositionStride = sizeof(float) * 3;

    if (input.normals) {
        meshDecl.vertexNormalData   = input.normals;
        meshDecl.vertexNormalStride = sizeof(float) * 3;
    }

    if (input.uvs) {
        meshDecl.vertexUvData   = input.uvs;
        meshDecl.vertexUvStride = sizeof(float) * 2;
    }

    meshDecl.indexCount  = input.indexCount;
    meshDecl.indexData   = input.indices;
    meshDecl.indexFormat = xatlas::IndexFormat::UInt32;

    xatlas::AddMeshError addErr = xatlas::AddMesh(atlas, meshDecl);
    if (addErr != xatlas::AddMeshError::Success) {
        output.error = "xatlas::AddMesh failed: " + std::string(xatlas::StringForEnum(addErr));
        UZ_LOG(Asset, Error, "xatlas: {}", output.error);
        xatlas::Destroy(atlas);
        return output;
    }

    // Configure chart and pack options
    xatlas::ChartOptions chartOpts{};
    if (options.maxChartSize > 0) {
        chartOpts.maxChartArea   = static_cast<float>(options.maxChartSize * options.maxChartSize);
    }

    xatlas::PackOptions packOpts{};
    packOpts.padding    = options.padding;
    packOpts.resolution = options.resolution;
    packOpts.bruteForce = options.bruteForce;

    // Generate UV atlas
    xatlas::Generate(atlas, chartOpts, packOpts);

    if (atlas->meshCount == 0 || atlas->meshes[0].vertexCount == 0) {
        output.error = "xatlas produced no output vertices";
        UZ_LOG(Asset, Error, "xatlas: {}", output.error);
        xatlas::Destroy(atlas);
        return output;
    }

    const xatlas::Mesh& outMesh = atlas->meshes[0];

    // Populate output
    output.vertexCount = outMesh.vertexCount;
    output.indexCount  = outMesh.indexCount;
    output.atlasWidth  = atlas->width;
    output.atlasHeight = atlas->height;
    output.chartCount  = atlas->chartCount;

    // UV1 coordinates (normalized to [0,1] by atlas dimensions)
    output.uvs.resize(outMesh.vertexCount * 2);
    const float invW = (atlas->width  > 0) ? 1.0f / static_cast<float>(atlas->width)  : 0.f;
    const float invH = (atlas->height > 0) ? 1.0f / static_cast<float>(atlas->height) : 0.f;
    for (uint32_t i = 0; i < outMesh.vertexCount; ++i) {
        output.uvs[i * 2 + 0] = outMesh.vertexArray[i].uv[0] * invW;
        output.uvs[i * 2 + 1] = outMesh.vertexArray[i].uv[1] * invH;
    }

    // Remapped index buffer
    output.indices.resize(outMesh.indexCount);
    for (uint32_t i = 0; i < outMesh.indexCount; ++i) {
        output.indices[i] = outMesh.indexArray[i];
    }

    // Remap vertex attributes using xref (xatlas may split vertices at chart boundaries)
    output.positions.resize(outMesh.vertexCount * 3);
    for (uint32_t i = 0; i < outMesh.vertexCount; ++i) {
        const uint32_t origIdx = outMesh.vertexArray[i].xref;
        output.positions[i * 3 + 0] = input.positions[origIdx * 3 + 0];
        output.positions[i * 3 + 1] = input.positions[origIdx * 3 + 1];
        output.positions[i * 3 + 2] = input.positions[origIdx * 3 + 2];
    }

    if (input.normals) {
        output.normals.resize(outMesh.vertexCount * 3);
        for (uint32_t i = 0; i < outMesh.vertexCount; ++i) {
            const uint32_t origIdx = outMesh.vertexArray[i].xref;
            output.normals[i * 3 + 0] = input.normals[origIdx * 3 + 0];
            output.normals[i * 3 + 1] = input.normals[origIdx * 3 + 1];
            output.normals[i * 3 + 2] = input.normals[origIdx * 3 + 2];
        }
    }

    if (input.uvs) {
        output.originalUVs.resize(outMesh.vertexCount * 2);
        for (uint32_t i = 0; i < outMesh.vertexCount; ++i) {
            const uint32_t origIdx = outMesh.vertexArray[i].xref;
            output.originalUVs[i * 2 + 0] = input.uvs[origIdx * 2 + 0];
            output.originalUVs[i * 2 + 1] = input.uvs[origIdx * 2 + 1];
        }
    }

    UZ_LOG(Asset, Info, "xatlas: {} verts → {} verts, {} charts, atlas {}x{}",
           input.vertexCount, output.vertexCount, output.chartCount,
           output.atlasWidth, output.atlasHeight);

    output.success = true;
    xatlas::Destroy(atlas);
    return output;
}

} // namespace uz
