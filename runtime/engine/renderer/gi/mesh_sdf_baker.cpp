/// UzEngine GI Mesh SDF Baker — GPU compute implementation.
///
/// Bakes signed distance fields from mesh triangles via a compute shader
/// (mesh_sdf_bake_cs.hlsl). Each thread handles one voxel and searches a
/// threaded BVH for closest-point-on-triangle distance.
///
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" — mesh SDF baking
/// @ref Ericson 2005 "Real-Time Collision Detection" Ch 5.1.5 — ClosestPointOnTriangle

#include "mesh_sdf_baker.h"
#include "mesh_sdf_bvh.h"

#include "log.h"
#include "rhi.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <DirectXMath.h>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

static bool OrientSdfFaceNormal(float* normal, const float* p0, const float* p1,
                               const float* p2, uint32_t vertexNormalOffset) {
    if (vertexNormalOffset == UINT32_MAX) return false;
    const uint32_t offset = vertexNormalOffset / sizeof(float);
    float reference[3] = {};
    for (const float* position : {p0, p1, p2}) {
        const float* n = position + offset;
        if (!std::isfinite(n[0]) || !std::isfinite(n[1]) || !std::isfinite(n[2])) continue;
        if (n[0] == 0.0f && n[1] == 0.0f && n[2] == 0.0f) continue;
        for (uint32_t axis = 0; axis < 3; ++axis) reference[axis] += n[axis];
    }
    const float alignment = normal[0] * reference[0] + normal[1] * reference[1]
                          + normal[2] * reference[2];
    if (alignment >= 0.0f || !std::isfinite(alignment)) return false;
    for (uint32_t axis = 0; axis < 3; ++axis) normal[axis] = -normal[axis];
    return true;
}

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
    uint32_t resolution,
    uint32_t vertexNormalOffset,
    bool twoSided)
{
    MeshSDF result;

    if (!device || !cmdList || !bakePipeline.IsValid() || !vertices || vertexCount < 3
        || resolution == 0 || resolution > UINT16_MAX || vertexStride < 3 * sizeof(float)
        || vertexStride % sizeof(float) != 0) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::BakeMeshGPU: invalid parameters");
        return result;
    }

    if (vertexNormalOffset != UINT32_MAX
        && (vertexNormalOffset > vertexStride - 3 * sizeof(float)
            || vertexNormalOffset % sizeof(float) != 0)) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::BakeMeshGPU: vertex normal offset exceeds vertex layout");
        return result;
    }

    if (indexCount == 0) indexCount = vertexCount;
    if (indexCount % 3 != 0) return result;

    const uint32_t triangleCount = indexCount / 3;
    const uint32_t strideFloats = vertexStride / sizeof(float);
    if (triangleCount > (UINT32_MAX - 1u) / 3u
        || triangleCount > std::numeric_limits<size_t>::max() / (3u * sizeof(MeshSdfGpuTriangle))
        || vertexCount > std::numeric_limits<size_t>::max() / vertexStride) return result;

    // ── Compute mesh AABB (SIMD) ─────────────────────────────────────────
    using namespace DirectX;

    XMVECTOR vBMin = XMVectorReplicate(std::numeric_limits<float>::max());
    XMVECTOR vBMax = XMVectorReplicate(std::numeric_limits<float>::lowest());

    for (uint32_t t = 0; t < triangleCount; ++t) {
        for (uint32_t vi = 0; vi < 3; ++vi) {
            uint32_t idx = indices ? indices[t * 3 + vi] : t * 3 + vi;
            if (idx >= vertexCount) return result;
            const float* p = vertices + static_cast<size_t>(idx) * strideFloats;
            if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) return result;
            XMVECTOR vP = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(p));
            vBMin = XMVectorMin(vBMin, vP);
            vBMax = XMVectorMax(vBMax, vP);
        }
    }

    float bMin[3], bMax[3];
    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(bMin), vBMin);
    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(bMax), vBMax);

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
    // The shader takes one voxel size: metadata must describe that same cube.
    for (int a = 0; a < 3; ++a) {
        volumeHalfExtent[a] = maxExtent * 0.5f;
        expandedMin[a] = volumeCenter[a] - volumeHalfExtent[a];
    }
    float voxelSize = maxExtent / static_cast<float>(resolution);
    if (!std::isfinite(maxExtent) || !std::isfinite(voxelSize)) return result;

    // ── Build GPU triangle buffer ────────────────────────────────────────
    std::vector<MeshSdfGpuTriangle> triangles(triangleCount);

    uint32_t orientedTriangleCount = 0;
    for (uint32_t t = 0; t < triangleCount; ++t) {
        uint32_t i0 = indices ? indices[t * 3 + 0] : t * 3 + 0;
        uint32_t i1 = indices ? indices[t * 3 + 1] : t * 3 + 1;
        uint32_t i2 = indices ? indices[t * 3 + 2] : t * 3 + 2;
        const float* p0 = vertices + static_cast<size_t>(i0) * strideFloats;
        const float* p1 = vertices + static_cast<size_t>(i1) * strideFloats;
        const float* p2 = vertices + static_cast<size_t>(i2) * strideFloats;

        auto& tri = triangles[t];
        std::memcpy(tri.v0, p0, 12);
        std::memcpy(tri.v1, p1, 12);
        std::memcpy(tri.v2, p2, 12);

        // Precompute face normal (SIMD)
        XMVECTOR v0 = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(p0));
        XMVECTOR v1 = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(p1));
        XMVECTOR v2 = XMLoadFloat3(reinterpret_cast<const XMFLOAT3*>(p2));
        XMVECTOR edge0 = XMVectorSubtract(v1, v0);
        XMVECTOR edge1 = XMVectorSubtract(v2, v0);
        XMVECTOR cross = XMVector3Cross(edge0, edge1);
        XMVECTOR crossLen = XMVector3Length(cross);
        // Degenerate triangle check — zero-area produces zero normal
        XMVECTOR isDegenerate = XMVectorLessOrEqual(crossLen, XMVectorReplicate(1e-8f));
        XMVECTOR normal = XMVectorSelect(XMVector3Normalize(cross), XMVectorZero(), isDegenerate);
        XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(tri.normal), normal);
        if (OrientSdfFaceNormal(tri.normal, p0, p1, p2, vertexNormalOffset)) {
            ++orientedTriangleCount;
        }
    }

    std::vector<MeshSdfGpuBvhNode> nodes;
    if (!BuildMeshSdfBvh(triangles, nodes)) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::BakeMeshGPU: invalid BVH geometry or buffer size");
        return result;
    }
    const size_t triangleBytes = triangles.size() * sizeof(MeshSdfGpuTriangle);
    const size_t nodeBytes = nodes.size() * sizeof(MeshSdfGpuBvhNode);
    RHIBufferDesc triBufDesc{};
    triBufDesc.sizeBytes = static_cast<uint64_t>(triangleBytes) + nodeBytes;
    triBufDesc.stride = sizeof(MeshSdfGpuTriangle);
    triBufDesc.usage = RHIBufferUsage::StructuredBuffer;
    triBufDesc.memory = RHIMemoryType::Upload;
    triBufDesc.debugName = "MeshSDF_TriBuffer";
    RHIBufferHandle triBuffer = device->CreateBuffer(triBufDesc);
    if (!triBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::BakeMeshGPU: failed to create triangle/BVH buffer");
        return result;
    }
    auto* mapped = static_cast<std::byte*>(device->MapBuffer(triBuffer));
    if (!mapped) {
        device->DestroyBuffer(triBuffer);
        return result;
    }
    std::memcpy(mapped, triangles.data(), triangleBytes);
    std::memcpy(mapped + triangleBytes, nodes.data(), nodeBytes);
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
    rc.triangleCount = triangleCount | kMeshSdfBvhFlag;
    rc.resolution    = resolution | (twoSided ? 0x80000000u : 0u);
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
    result.triangleSrv = triSrvIndex;
    result.triangleCountAndFlags = rc.triangleCount;

    // ── Fill result metadata ─────────────────────────────────────────────
    std::memcpy(result.volumeOrigin, volumeCenter, sizeof(result.volumeOrigin));
    std::memcpy(result.volumeExtent, volumeHalfExtent, sizeof(result.volumeExtent));
    result.resolution = resolution;
    result.valid = true;
    result.twoSided = twoSided;

    UZ_LOG(Renderer, Info,
           "MeshSdfBaker::BakeMeshGPU: baked {}^3 SDF ({} triangles, {} BVH nodes, {} normals reoriented, twoSided={}) on GPU, SRV={}",
           resolution, triangleCount, nodes.size(), orientedTriangleCount, twoSided, result.srvIndex);

    return result;
}

} // namespace uz
