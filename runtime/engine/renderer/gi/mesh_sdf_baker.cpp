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

// ───────────────────────────────────────
// MeshSdfBaker::PrepareCPU — volume placement, triangle records, BVH (any thread)
// ───────────────────────────────────────

MeshSdfCpuBake MeshSdfBaker::PrepareCPU(
    const float* vertices,
    uint32_t vertexCount,
    uint32_t vertexStride,
    const uint32_t* indices,
    uint32_t indexCount,
    uint32_t resolution,
    uint32_t vertexNormalOffset)
{
    MeshSdfCpuBake out;

    if (!vertices || vertexCount < 3 || resolution == 0 || resolution > UINT16_MAX
        || vertexStride < 3 * sizeof(float) || vertexStride % sizeof(float) != 0) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::PrepareCPU: invalid parameters");
        return out;
    }

    if (vertexNormalOffset != UINT32_MAX
        && (vertexNormalOffset > vertexStride - 3 * sizeof(float)
            || vertexNormalOffset % sizeof(float) != 0)) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::PrepareCPU: vertex normal offset exceeds vertex layout");
        return out;
    }

    if (indexCount == 0) indexCount = vertexCount;
    if (indexCount % 3 != 0) return out;

    const uint32_t triangleCount = indexCount / 3;
    const uint32_t strideFloats = vertexStride / sizeof(float);
    if (triangleCount > (UINT32_MAX - 1u) / 3u
        || triangleCount > std::numeric_limits<size_t>::max() / (3u * sizeof(MeshSdfGpuTriangle))
        || vertexCount > std::numeric_limits<size_t>::max() / vertexStride) return out;

    // ── Compute mesh AABB (SIMD) ─────────────────────────────────────────
    using namespace DirectX;

    XMVECTOR vBMin = XMVectorReplicate(std::numeric_limits<float>::max());
    XMVECTOR vBMax = XMVectorReplicate(std::numeric_limits<float>::lowest());

    for (uint32_t t = 0; t < triangleCount; ++t) {
        for (uint32_t vi = 0; vi < 3; ++vi) {
            uint32_t idx = indices ? indices[t * 3 + vi] : t * 3 + vi;
            if (idx >= vertexCount) return out;
            const float* p = vertices + static_cast<size_t>(idx) * strideFloats;
            if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) return out;
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
    if (!std::isfinite(maxExtent) || !std::isfinite(voxelSize)) return out;

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
        UZ_LOG(Renderer, Error, "MeshSdfBaker::PrepareCPU: invalid BVH geometry or buffer size");
        return out;
    }
    const size_t triangleBytes = triangles.size() * sizeof(MeshSdfGpuTriangle);
    const size_t nodeBytes = nodes.size() * sizeof(MeshSdfGpuBvhNode);
    out.records.resize(triangleBytes + nodeBytes);
    std::memcpy(out.records.data(), triangles.data(), triangleBytes);
    std::memcpy(out.records.data() + triangleBytes, nodes.data(), nodeBytes);

    std::memcpy(out.volumeCenter, volumeCenter, sizeof(out.volumeCenter));
    std::memcpy(out.volumeHalfExtent, volumeHalfExtent, sizeof(out.volumeHalfExtent));
    std::memcpy(out.volumeMin, expandedMin, sizeof(out.volumeMin));
    out.voxelSize = voxelSize;
    out.resolution = resolution;
    out.triangleCount = triangleCount;
    out.nodeCount = static_cast<uint32_t>(nodes.size());
    out.orientedTriangleCount = orientedTriangleCount;
    out.ok = true;
    return out;
}

// ───────────────────────────────────────
// MeshSdfBaker::CreateResources — triangle/BVH buffer and R16F volume (main thread)
// ───────────────────────────────────────

MeshSDF MeshSdfBaker::CreateResources(IRHIDevice* device, const MeshSdfCpuBake& cpu, bool twoSided) {
    MeshSDF result;
    if (!device || !cpu.ok || cpu.records.empty() || cpu.resolution == 0) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::CreateResources: invalid parameters");
        return result;
    }

    RHIBufferDesc triBufDesc{};
    triBufDesc.sizeBytes = static_cast<uint64_t>(cpu.records.size());
    triBufDesc.stride = sizeof(MeshSdfGpuTriangle);
    triBufDesc.usage = RHIBufferUsage::StructuredBuffer;
    triBufDesc.memory = RHIMemoryType::Upload;
    triBufDesc.debugName = "MeshSDF_TriBuffer";
    RHIBufferHandle triBuffer = device->CreateBuffer(triBufDesc);
    if (!triBuffer.IsValid()) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::CreateResources: failed to create triangle/BVH buffer");
        return result;
    }
    auto* mapped = static_cast<std::byte*>(device->MapBuffer(triBuffer));
    if (!mapped) {
        device->DestroyBuffer(triBuffer);
        return result;
    }
    std::memcpy(mapped, cpu.records.data(), cpu.records.size());
    device->UnmapBuffer(triBuffer);

    // ── Create output R16F 3D texture ────────────────────────────────────
    // Copy source for the cache readback, copy destination for a cache hit (block B.3).
    RHITextureDesc texDesc{};
    texDesc.width            = cpu.resolution;
    texDesc.height           = cpu.resolution;
    texDesc.depthOrArraySize = static_cast<uint16_t>(cpu.resolution);
    texDesc.mipLevels        = 1;
    texDesc.format           = RHIFormat::R16Float;
    texDesc.usage            = RHITextureUsage::ShaderResource
                             | RHITextureUsage::UnorderedAccess
                             | RHITextureUsage::CopySrc
                             | RHITextureUsage::CopyDst;
    texDesc.is3D             = true;
    texDesc.debugName        = "MeshSDF_3D";

    result.texture3D = device->CreateTexture(texDesc);
    if (!result.texture3D.IsValid()) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::CreateResources: failed to create the volume");
        device->DestroyBuffer(triBuffer);
        return result;
    }

    result.srvIndex = device->GetTextureSRVIndex(result.texture3D);
    result.pendingTriBuffer = triBuffer;
    result.triangleSrv = device->GetBufferSRVIndex(triBuffer);
    result.triangleCountAndFlags = cpu.triangleCount | kMeshSdfBvhFlag;
    std::memcpy(result.volumeOrigin, cpu.volumeCenter, sizeof(result.volumeOrigin));
    std::memcpy(result.volumeExtent, cpu.volumeHalfExtent, sizeof(result.volumeExtent));
    result.resolution = cpu.resolution;
    result.twoSided = twoSided;
    result.valid = false;
    return result;
}

// ───────────────────────────────────────
// MeshSdfBaker::RecordBakeSlices — the dispatch, whole or in Z slabs
// ───────────────────────────────────────

void MeshSdfBaker::RecordBakeSlices(
    IRHICommandList* cmdList,
    RHIPipelineHandle bakePipeline,
    IRHIDevice* device,
    const MeshSDF& sdf,
    const MeshSdfCpuBake& cpu,
    uint32_t firstSlice,
    uint32_t sliceCount)
{
    if (!cmdList || !device || !bakePipeline.IsValid() || !sdf.texture3D.IsValid()
        || sliceCount == 0 || firstSlice >= sdf.resolution) return;

    cmdList->SetPipeline(bakePipeline);

    MeshSdfBakeRootConstants rc{};
    rc.outputUav     = device->GetTextureUAVIndex(sdf.texture3D);
    rc.trianglesSrv  = sdf.triangleSrv;
    rc.triangleCount = sdf.triangleCountAndFlags;
    rc.resolution    = sdf.resolution | (sdf.twoSided ? 0x80000000u : 0u);
    rc.volumeMinX    = cpu.volumeMin[0];
    rc.volumeMinY    = cpu.volumeMin[1];
    rc.volumeMinZ    = cpu.volumeMin[2];
    rc.voxelSize     = cpu.voxelSize;
    rc.sliceOffset   = firstSlice;
    cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), kRootConstantCount);

    const uint32_t groups = (sdf.resolution + 3) / 4;
    const uint32_t slices = std::min(sliceCount, sdf.resolution - firstSlice);
    cmdList->Dispatch(groups, groups, (slices + 3) / 4);
}

// ───────────────────────────────────────
// MeshSdfBaker::BakeMeshGPU — whole bake on the calling thread
// ───────────────────────────────────────

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
    if (!device || !cmdList || !bakePipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "MeshSdfBaker::BakeMeshGPU: invalid parameters");
        return result;
    }

    const MeshSdfCpuBake cpu = PrepareCPU(vertices, vertexCount, vertexStride, indices, indexCount,
                                          resolution, vertexNormalOffset);
    if (!cpu.ok) return result;

    result = CreateResources(device, cpu, twoSided);
    if (!result.texture3D.IsValid()) return result;

    // ── Transition to UAV, dispatch compute, transition to SRV ──────────
    RHIBarrierDesc toUav{};
    toUav.texture     = result.texture3D;
    toUav.stateBefore = RHIResourceState::Undefined;
    toUav.stateAfter  = RHIResourceState::UnorderedAccess;
    cmdList->ResourceBarrier(&toUav, 1);

    RecordBakeSlices(cmdList, bakePipeline, device, result, cpu, 0, resolution);

    RHIBarrierDesc toSrv{};
    toSrv.texture     = result.texture3D;
    toSrv.stateBefore = RHIResourceState::UnorderedAccess;
    toSrv.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&toSrv, 1);

    // The triangle buffer is referenced by the recorded dispatch command.
    // It must NOT be destroyed until the GPU finishes executing this command list.
    // Return the buffer handle in result.pendingTriBuffer for deferred destruction
    // by the caller (typically at start of next frame after the fence completes).
    result.valid = true;

    UZ_LOG(Renderer, Info,
           "MeshSdfBaker::BakeMeshGPU: baked {}^3 SDF ({} triangles, {} BVH nodes, {} normals reoriented, twoSided={}) on GPU, SRV={}",
           resolution, cpu.triangleCount, cpu.nodeCount, cpu.orientedTriangleCount, twoSided, result.srvIndex);

    return result;
}

} // namespace uz
