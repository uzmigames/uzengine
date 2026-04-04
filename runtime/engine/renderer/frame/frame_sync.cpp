/// UzEngine Scene Renderer — ECS → draw list population, GPU instance batching,
/// instance buffer upload, and GPU occlusion culling buffer setup
/// (bounding sphere upload, cull CB).

#include "scene_renderer.h"

#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

#include "camera.h"
#include "gpu_mesh.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"
#include "vfs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace uz {

void SceneRenderer::SyncDrawListAndInstances(FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    // ── Build draw list from ECS ────────────────────────────────────────
    ctx.drawList.Clear();
    SyncWorldToDrawList(ctx.world, ctx.vfs, ctx.cmdList, ctx.drawList);
    ctx.drawList.Sort();

    // ── GPU Instancing — batch identical mesh+material draw commands ────
    BuildInstanceBatches(ctx.drawList, ctx.instanceBatches, ctx.instanceDataVec);

    // Ensure instance buffer has enough capacity (recreate only when too small)
    const auto instanceCount = static_cast<uint32_t>(ctx.instanceDataVec.size());
    // Disable instancing when multiple mesh types are loaded — the instanced path
    // currently only supports homogeneous batches (all mesh[0]). Per-mesh batching
    // is future work. Fall back to per-draw-command path for multi-mesh scenes.
    // Instancing disabled — the single-mesh-type instanced GBuffer path
    // produces invisible geometry. Per-draw-command path works correctly.
    // Re-enable after debugging the instanced pipeline.
    ctx.useInstancing = false;

    // Triple-buffered instance data: CPU writes to frame N's buffer while GPU
    // reads from frame N-2. No WaitIdle needed — fence sync in the app layer
    // guarantees frame N-2 is complete before we reach this point in frame N.
    const uint32_t ibSlot = ctx.frameIndex % kInstanceBufferCount;

    if (ctx.useInstancing && instanceCount > m_instanceBufferCapacity) {
        // Need bigger buffers — wait for GPU idle before destroying all slots
        m_device->WaitIdle();
        for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
            if (m_instanceBuffers[i].IsValid()) {
                m_device->DestroyBuffer(m_instanceBuffers[i]);
                m_instanceBuffers[i] = RHIBufferHandle{};
                m_instanceBufferSrvIndices[i] = UINT32_MAX;
            }
        }

        // Round up to next power of two for stable allocation
        uint32_t newCapacity = instanceCount;
        newCapacity |= (newCapacity >> 1);
        newCapacity |= (newCapacity >> 2);
        newCapacity |= (newCapacity >> 4);
        newCapacity |= (newCapacity >> 8);
        newCapacity |= (newCapacity >> 16);
        newCapacity++;
        if (newCapacity < 16) newCapacity = 16;

        RHIBufferDesc ibDesc{};
        ibDesc.sizeBytes = newCapacity * sizeof(InstanceData);
        ibDesc.stride    = sizeof(InstanceData);
        ibDesc.usage     = RHIBufferUsage::StructuredBuffer;
        ibDesc.memory    = RHIMemoryType::Upload;

        bool allCreated = true;
        for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
            ibDesc.debugName = (i == 0) ? "InstanceBuffer[0]"
                             : (i == 1) ? "InstanceBuffer[1]"
                                        : "InstanceBuffer[2]";
            m_instanceBuffers[i] = m_device->CreateBuffer(ibDesc);
            if (m_instanceBuffers[i].IsValid()) {
                m_instanceBufferSrvIndices[i] = m_device->GetBufferSRVIndex(m_instanceBuffers[i]);
            } else {
                allCreated = false;
            }
        }
        m_instanceBufferCapacity = allCreated ? newCapacity : 0;
    }

    // Upload instance data to the current frame's buffer (no GPU sync needed)
    ctx.instanceBufferSrvIndex = UINT32_MAX;
    if (ctx.useInstancing && m_instanceBuffers[ibSlot].IsValid()) {
        void* mapped = m_device->MapBuffer(m_instanceBuffers[ibSlot]);
        if (mapped) {
            std::memcpy(mapped, ctx.instanceDataVec.data(),
                        instanceCount * sizeof(InstanceData));
            m_device->UnmapBuffer(m_instanceBuffers[ibSlot]);
        }
        ctx.instanceBufferSrvIndex = m_instanceBufferSrvIndices[ibSlot];
    }

    // ── GPU Occlusion Culling — bounding sphere upload + cull constants ──
    ctx.gpuCullingActive = false;
    if (m_gpuOcclusionCullingEnabled && m_depthPrepassEnabled && instanceCount > 0 && ctx.bindless) {
        UZ_PROFILE_SCOPE("GpuCull_Setup");

        // Grow GPU cull buffers if instance count exceeds current capacity.
        if (instanceCount > m_gpuCullBufferCapacity) {
            m_device->WaitIdle();
            // Round up to next power of two for stable allocation.
            uint32_t newCap = instanceCount;
            newCap |= (newCap >> 1);
            newCap |= (newCap >> 2);
            newCap |= (newCap >> 4);
            newCap |= (newCap >> 8);
            newCap |= (newCap >> 16);
            newCap++;
            if (newCap < 256) newCap = 256;
            CreateGpuCullBuffers(newCap);
        }

        // Compute world-space bounding spheres from per-instance transforms + mesh AABB.
        // Each sphere packs as float4(center.xyz, radius).
        std::vector<Vec4> boundingSpheres(instanceCount);
        uint32_t batchIdx = 0;
        uint32_t batchInstanceBase = 0;
        for (uint32_t i = 0; i < instanceCount; ++i) {
            // Find which batch this instance belongs to (walk batches sequentially).
            while (batchIdx < ctx.instanceBatches.size()
                   && i >= batchInstanceBase + ctx.instanceBatches[batchIdx].instanceCount) {
                batchInstanceBase += ctx.instanceBatches[batchIdx].instanceCount;
                ++batchIdx;
            }

            const uint32_t meshId = (batchIdx < ctx.instanceBatches.size())
                ? ctx.instanceBatches[batchIdx].meshId : 0;

            // Object-space AABB center and radius.
            Vec3 bMin{}, bMax{};
            if (meshId < m_meshBoundsMin.size()) {
                bMin = m_meshBoundsMin[meshId];
                bMax = m_meshBoundsMax[meshId];
            }
            const Vec3 center_os{
                (bMin.x + bMax.x) * 0.5f,
                (bMin.y + bMax.y) * 0.5f,
                (bMin.z + bMax.z) * 0.5f
            };
            const Vec3 extent{
                bMax.x - center_os.x,
                bMax.y - center_os.y,
                bMax.z - center_os.z
            };
            const float radius_os = extent.Length();

            // Transform center to world space and extract max axis scale.
            const Mat4& world = ctx.instanceDataVec[i].world;
            // center_ws = world * float4(center_os, 1.0)
            const float cx = world.m.m[0][0] * center_os.x + world.m.m[1][0] * center_os.y + world.m.m[2][0] * center_os.z + world.m.m[3][0];
            const float cy = world.m.m[0][1] * center_os.x + world.m.m[1][1] * center_os.y + world.m.m[2][1] * center_os.z + world.m.m[3][1];
            const float cz = world.m.m[0][2] * center_os.x + world.m.m[1][2] * center_os.y + world.m.m[2][2] * center_os.z + world.m.m[3][2];

            // Max axis scale: largest column vector length of the 3x3 rotation/scale.
            const float scaleX = std::sqrt(world.m.m[0][0] * world.m.m[0][0] + world.m.m[0][1] * world.m.m[0][1] + world.m.m[0][2] * world.m.m[0][2]);
            const float scaleY = std::sqrt(world.m.m[1][0] * world.m.m[1][0] + world.m.m[1][1] * world.m.m[1][1] + world.m.m[1][2] * world.m.m[1][2]);
            const float scaleZ = std::sqrt(world.m.m[2][0] * world.m.m[2][0] + world.m.m[2][1] * world.m.m[2][1] + world.m.m[2][2] * world.m.m[2][2]);
            const float maxScale = std::max({scaleX, scaleY, scaleZ});

            boundingSpheres[i] = Vec4{cx, cy, cz, radius_os * maxScale};
        }

        // Upload bounding spheres to the current frame's buffer.
        if (m_boundingSphereBuffers[ibSlot].IsValid()) {
            void* mapped = m_device->MapBuffer(m_boundingSphereBuffers[ibSlot]);
            if (mapped) {
                std::memcpy(mapped, boundingSpheres.data(),
                            instanceCount * sizeof(Vec4));
                m_device->UnmapBuffer(m_boundingSphereBuffers[ibSlot]);
            }
        }

        auto& camera = *m_camera;

        // ── Phase 1 Cull Constants — use previous frame's HiZ ─────────────
        // Phase 1 tests against last frame's HiZ pyramid to identify objects
        // that are "definitely visible" (were visible last frame and still pass
        // frustum + occlusion). This is conservative: objects newly revealed
        // by camera movement may be incorrectly rejected.
        // If no previous HiZ exists (first frame), Phase 1 runs frustum-only.
        GpuCullConstants phase1Constants{};
        phase1Constants.viewProjection = camera.GetViewProjection();
        phase1Constants.viewMatrix     = camera.GetView();
        phase1Constants.viewToClip     = camera.GetProjection();
        phase1Constants.instanceCount  = instanceCount;
        phase1Constants.hizEnabled     = (m_prevHizMipCount > 0) ? 1u : 0u;
        if (m_prevHizMipCount > 0) {
            phase1Constants.hizWidth    = m_prevHizWidth;
            phase1Constants.hizHeight   = m_prevHizHeight;
            phase1Constants.hizMipCount = m_prevHizMipCount;
            for (uint32_t m = 0; m < m_prevHizMipCount && m < kGpuCullMaxHiZMips; ++m) {
                phase1Constants.hizMipSrvIndices[m] = m_prevHizMipSrvIndices[m];
            }
        }

        ctx.gpuCullCBAlloc = m_cbRing->Allocate(sizeof(GpuCullConstants));
        if (ctx.gpuCullCBAlloc.cpuPtr) {
            std::memcpy(ctx.gpuCullCBAlloc.cpuPtr, &phase1Constants, sizeof(GpuCullConstants));
        }

        // ── Phase 2 Cull Constants — use current frame's HiZ ─────────────
        // Phase 2 re-tests ALL instances against the updated HiZ pyramid built
        // from Phase 1 visible geometry. Objects that pass Phase 2 but failed
        // Phase 1 are "newly visible" (e.g., revealed by camera movement).
        // The GBuffer draws both Phase 1 and Phase 2 visible objects.
        GpuCullConstants phase2Constants{};
        phase2Constants.viewProjection = camera.GetViewProjection();
        phase2Constants.viewMatrix     = camera.GetView();
        phase2Constants.viewToClip     = camera.GetProjection();
        phase2Constants.instanceCount  = instanceCount;
        phase2Constants.hizEnabled     = (m_hizMipCount > 0) ? 1u : 0u;
        if (m_hizMipCount > 0) {
            phase2Constants.hizWidth    = m_hizMipLevels[0].width;
            phase2Constants.hizHeight   = m_hizMipLevels[0].height;
            phase2Constants.hizMipCount = m_hizMipCount;
            for (uint32_t m = 0; m < m_hizMipCount && m < kGpuCullMaxHiZMips; ++m) {
                phase2Constants.hizMipSrvIndices[m] = m_hizMipLevels[m].srvIndex;
            }
        }

        ctx.gpuCullCBAlloc2 = m_cbRing->Allocate(sizeof(GpuCullConstants));
        if (ctx.gpuCullCBAlloc2.cpuPtr) {
            std::memcpy(ctx.gpuCullCBAlloc2.cpuPtr, &phase2Constants, sizeof(GpuCullConstants));
        }

        // Populate FrameContext fields for downstream pass registration.
        ctx.boundingSphereSrvIndex = m_boundingSphereSrvIndices[ibSlot];
        ctx.visibleIndexUavIndex   = m_visibleIndexUavIndices[ibSlot];
        ctx.indirectArgsUavIndex   = m_indirectArgsUavIndices[ibSlot];
        ctx.visibleIndexBuffer     = m_visibleIndexBuffers[ibSlot];
        ctx.indirectArgsBuffer     = m_indirectArgsBuffers[ibSlot];

        // Phase 2 output buffers.
        ctx.visibleIndexUavIndex2  = m_visibleIndexUavIndices2[ibSlot];
        ctx.indirectArgsUavIndex2  = m_indirectArgsUavIndices2[ibSlot];
        ctx.visibleIndexBuffer2    = m_visibleIndexBuffers2[ibSlot];
        ctx.indirectArgsBuffer2    = m_indirectArgsBuffers2[ibSlot];

        // Two-phase is active when previous frame's HiZ data is available.
        ctx.twoPhaseOcclusion = (m_prevHizMipCount > 0);
        ctx.gpuCullingActive  = true;
    }
}

} // namespace uz
