/// UzEngine GI Mesh SDF Baker — GPU compute implementation.
///
/// Bakes signed distance fields from mesh triangles via a compute shader
/// (mesh_sdf_bake_cs.hlsl). Each thread handles one voxel, brute-force
/// iterating all triangles for closest-point-on-triangle distance.
///
/// CPU bake path removed — GPU path is 2500x faster (6ms vs 15.5s for
/// a 3968-triangle sphere at 32^3 on RTX 4090).
///
/// @source UE5 MeshDistanceFieldUtilities.cpp — mesh SDF bake dispatch
/// @source Real-Time Collision Detection, Ch 5.1.5 — ClosestPointOnTriangle

#include "mesh_sdf_baker.h"

#include "log.h"
#include "rhi.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// MeshSdfBaker::BakeMeshGPU — GPU compute shader bake
// ─────────────────────────────────────────────────────────────────────────────

MeshSDF MeshSdfBaker::BakeMeshGPU(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    RHIPipelineHandle bakePipeline,
    const float* vertices,
    uint32_t vertexCount,
    uint32_t vertexStride,
    const uint32_t* indices,
    uint32_t indexCount,
    uint32_t resolution)
{
    MeshSDF result;

    if (!device || !cmdList || !bakePipeline.IsValid() || !vertices || vertexCount < 3) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::BakeMeshGPU: invalid parameters");
        return result;
    }

    if (indexCount == 0) indexCount = vertexCount;
    if (indexCount % 3 != 0) return result;

    const uint32_t triangleCount = indexCount / 3;
    const uint32_t strideFloats = vertexStride / sizeof(float);

    // ── Compute mesh AABB ────────────────────────────────────────────────
    float bMin[3] = { std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max()};
    float bMax[3] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

    for (uint32_t t = 0; t < triangleCount; ++t) {
        for (uint32_t vi = 0; vi < 3; ++vi) {
            uint32_t idx = indices ? indices[t * 3 + vi] : t * 3 + vi;
            if (idx >= vertexCount) return result;
            const float* p = vertices + idx * strideFloats;
            for (int a = 0; a < 3; ++a) {
                bMin[a] = std::min(bMin[a], p[a]);
                bMax[a] = std::max(bMax[a], p[a]);
            }
        }
    }

    // ── Expand AABB by 10% + minimum 1cm padding ────────────────────────
    float extent[3], padding[3], expandedMin[3], expandedMax[3];
    for (int a = 0; a < 3; ++a) {
        extent[a] = bMax[a] - bMin[a];
        padding[a] = std::max(extent[a] * 0.1f, 1.0f);
        expandedMin[a] = bMin[a] - padding[a];
        expandedMax[a] = bMax[a] + padding[a];
    }

    float volumeCenter[3], volumeHalfExtent[3];
    for (int a = 0; a < 3; ++a) {
        volumeCenter[a] = (expandedMin[a] + expandedMax[a]) * 0.5f;
        volumeHalfExtent[a] = (expandedMax[a] - expandedMin[a]) * 0.5f;
    }

    // Use the maximum extent for uniform voxel size (cubic grid)
    float maxExtent = std::max({expandedMax[0] - expandedMin[0],
                                expandedMax[1] - expandedMin[1],
                                expandedMax[2] - expandedMin[2]});
    float voxelSize = maxExtent / static_cast<float>(resolution);

    // ── Build GPU triangle buffer ────────────────────────────────────────
    // GPUTriangle: 4x float4 = 64 bytes (matches shader struct)
    struct GPUTriangle {
        float v0[3]; float _pad0;
        float v1[3]; float _pad1;
        float v2[3]; float _pad2;
        float normal[3]; float _pad3;
    };
    static_assert(sizeof(GPUTriangle) == 64, "GPUTriangle must be 64 bytes");

    const uint32_t triBufferSize = triangleCount * sizeof(GPUTriangle);

    RHIBufferDesc triBufDesc{};
    triBufDesc.sizeBytes = triBufferSize;
    triBufDesc.stride    = sizeof(GPUTriangle);
    triBufDesc.usage     = RHIBufferUsage::StructuredBuffer;
    triBufDesc.memory    = RHIMemoryType::Upload;
    triBufDesc.debugName = "MeshSDF_TriBuffer";

    RHIBufferHandle triBuffer = device->CreateBuffer(triBufDesc);
    if (!triBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::BakeMeshGPU: failed to create triangle buffer");
        return result;
    }

    auto* mappedTris = static_cast<GPUTriangle*>(device->MapBuffer(triBuffer));
    if (!mappedTris) {
        device->DestroyBuffer(triBuffer);
        return result;
    }

    for (uint32_t t = 0; t < triangleCount; ++t) {
        uint32_t i0 = indices ? indices[t * 3 + 0] : t * 3 + 0;
        uint32_t i1 = indices ? indices[t * 3 + 1] : t * 3 + 1;
        uint32_t i2 = indices ? indices[t * 3 + 2] : t * 3 + 2;
        const float* p0 = vertices + i0 * strideFloats;
        const float* p1 = vertices + i1 * strideFloats;
        const float* p2 = vertices + i2 * strideFloats;

        auto& tri = mappedTris[t];
        std::memcpy(tri.v0, p0, 12); tri._pad0 = 0;
        std::memcpy(tri.v1, p1, 12); tri._pad1 = 0;
        std::memcpy(tri.v2, p2, 12); tri._pad2 = 0;

        // Precompute face normal
        float e0[3] = {p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
        float e1[3] = {p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};
        float nx = e0[1]*e1[2] - e0[2]*e1[1];
        float ny = e0[2]*e1[0] - e0[0]*e1[2];
        float nz = e0[0]*e1[1] - e0[1]*e1[0];
        float len = std::sqrt(nx*nx + ny*ny + nz*nz);
        if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
        tri.normal[0] = nx; tri.normal[1] = ny; tri.normal[2] = nz;
        tri._pad3 = 0;
    }
    device->UnmapBuffer(triBuffer);

    uint32_t triSrvIndex = device->GetBufferSRVIndex(triBuffer);

    // ── Create output R16F 3D texture ────────────────────────────────────
    RHITextureDesc texDesc{};
    texDesc.width            = resolution;
    texDesc.height           = resolution;
    texDesc.depthOrArraySize = static_cast<uint16_t>(resolution);
    texDesc.mipLevels        = 1;
    texDesc.format           = RHIFormat::R16Float;
    texDesc.usage            = RHITextureUsage::ShaderResource
                             | RHITextureUsage::UnorderedAccess;
    texDesc.is3D             = true;
    texDesc.debugName        = "MeshSDF_3D";

    result.texture3D = device->CreateTexture(texDesc);
    if (!result.texture3D.IsValid()) {
        device->DestroyBuffer(triBuffer);
        return result;
    }

    result.srvIndex = device->GetTextureSRVIndex(result.texture3D);
    uint32_t outputUav = device->GetTextureUAVIndex(result.texture3D);

    // ── Transition to UAV, dispatch compute, transition to SRV ──────────
    RHIBarrierDesc toUav{};
    toUav.texture     = result.texture3D;
    toUav.stateBefore = RHIResourceState::Undefined;
    toUav.stateAfter  = RHIResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(&toUav, 1);

    cmdList->SetPipeline(bakePipeline);

    // Root constants (8 uint32s at slot 0)
    struct BakeRootConstants {
        uint32_t outputUav;
        uint32_t trianglesSrv;
        uint32_t triangleCount;
        uint32_t resolution;
        float    volumeMinX, volumeMinY, volumeMinZ;
        float    voxelSize;
    };
    static_assert(sizeof(BakeRootConstants) == 32, "Must be 8 uint32s");

    BakeRootConstants rc{};
    rc.outputUav     = outputUav;
    rc.trianglesSrv  = triSrvIndex;
    rc.triangleCount = triangleCount;
    rc.resolution    = resolution;
    rc.volumeMinX    = expandedMin[0];
    rc.volumeMinY    = expandedMin[1];
    rc.volumeMinZ    = expandedMin[2];
    rc.voxelSize     = voxelSize;

    cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

    const uint32_t groups = (resolution + 3) / 4;
    cmdList->Dispatch(groups, groups, groups);

    RHIBarrierDesc toSrv{};
    toSrv.texture     = result.texture3D;
    toSrv.stateBefore = RHIResourceState::UnorderedAccess;
    toSrv.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&toSrv, 1);

    // The triangle buffer is referenced by the recorded dispatch command.
    // It must NOT be destroyed until the GPU finishes executing this command list.
    // Return the buffer handle in result.pendingTriBuffer for deferred destruction
    // by the caller (typically at start of next frame after the fence completes).
    result.pendingTriBuffer = triBuffer;

    // ── Fill result metadata ─────────────────────────────────────────────
    std::memcpy(result.volumeOrigin, volumeCenter, sizeof(result.volumeOrigin));
    std::memcpy(result.volumeExtent, volumeHalfExtent, sizeof(result.volumeExtent));
    result.resolution = resolution;
    result.valid = true;

    UZ_LOG(Renderer, Info,
           "MeshSdfBaker::BakeMeshGPU: baked {}^3 SDF ({} triangles) on GPU, SRV={}",
           resolution, triangleCount, result.srvIndex);

    return result;
}

} // namespace uz
