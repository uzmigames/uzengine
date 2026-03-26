/// UzEngine Scene Renderer — Particle passes: GPU simulation (compute),
/// billboard rendering (transparent phase, back-to-front sorted),
/// trail ribbon rendering, and instanced mesh particle rendering.

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

#include "camera.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "pipeline_library.h"
#include "profiler.h"
#include "log.h"

#include "engine/renderer/passes/vfx/particle_simulation_pass.h"
#include "engine/renderer/passes/vfx/particle_render_pass.h"
#include "engine/renderer/passes/vfx/trail_render_pass.h"
#include "engine/renderer/passes/vfx/particle_mesh_render_pass.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/gpu_particle_buffer.h"
#include "engine/vfx/core/gpu_particle_sort.h"
#include "engine/vfx/modules/mesh_render_module.h"
#include "engine/vfx/trails/trail_system.h"

#include <algorithm>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

void SceneRenderer::RegisterParticlePasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    // Resolve active VFX system: the non-owning override (set via SetVFXSystem)
    // takes precedence over the renderer-owned system so that an external
    // emitter pool managed by the game layer is used when present.
    VFXSystem* activeVFX = m_vfxSystemOverride ? m_vfxSystemOverride : m_vfxSystem.get();

    // Skip if no VFX system is available or it has no active emitters.
    if (!activeVFX || !activeVFX->IsInitialized()) return;

    const auto& emitters = activeVFX->GetActiveEmitters();
    if (emitters.empty()) return;

    // ── Swap ping-pong buffers at the TOP of frame registration ────────
    // After the swap, WriteSet() returns the set that was read last frame
    // (left in SRV by the render pass), and ReadSet() returns the set that
    // was written last frame (left in UAV by the sim pass). The render graph
    // import below uses these post-swap states.
    for (auto* emitter : emitters) {
        if (emitter && emitter->initialized && emitter->buffer.IsValid()) {
            emitter->buffer.Swap();
        }
    }

    // ── Import GPU particle buffers into the render graph ────────────────
    // Each buffer handle must be imported exactly once so the graph has a
    // single RgResourceId per physical buffer for state tracking. We build
    // a per-emitter struct holding all imported IDs, then distribute them
    // to both the simulation and render pass descriptors.
    //
    // Post-swap resource states:
    //   Write set = was previous frame's read set, left in SRV by render pass.
    //   Read set  = was previous frame's write set, left in UAV by sim pass.
    //   aliveCount = was SRV (render read it).
    //   freeList/freeListHead/idToIndex = always UAV (only sim touches).
    //   indirectDrawArgs/Dispatch = was IndirectArgument (sim's EmitIndirectArgsBarriers).
    //
    // First-frame exception: all buffers start in UAV from Create().

    struct ImportedEmitterIds {
        GPUParticleBuffer* buffer = nullptr;

        // Write-set attribute buffers (sim writes these as UAV).
        rg::RgResourceId wrPositions;
        rg::RgResourceId wrVelocities;
        rg::RgResourceId wrAges;
        rg::RgResourceId wrLifespans;
        rg::RgResourceId wrSizes;
        rg::RgResourceId wrColors;
        rg::RgResourceId wrRotations;

        // Read-set attribute buffers (render reads these as SRV).
        rg::RgResourceId rdPositions;
        rg::RgResourceId rdVelocities;
        rg::RgResourceId rdAges;
        rg::RgResourceId rdLifespans;
        rg::RgResourceId rdSizes;
        rg::RgResourceId rdColors;
        rg::RgResourceId rdRotations;

        // Shared bookkeeping buffers (single import, used by both passes).
        rg::RgResourceId aliveCount;
        rg::RgResourceId freeList;
        rg::RgResourceId freeListHead;
        rg::RgResourceId idToIndex;
        rg::RgResourceId indirectDrawArgs;
        rg::RgResourceId indirectDispatchArgs;

        // Sort buffers (for depth-ordered transparent rendering).
        rg::RgResourceId sortKeys;
        rg::RgResourceId sortIndices;

        // Mesh instance buffers (only valid when buffer->hasMeshBuffers is true).
        rg::RgResourceId meshInstanceTransforms;
        rg::RgResourceId meshInstanceCount;
        rg::RgResourceId meshIndirectDrawArgs;
    };

    std::vector<ImportedEmitterIds> importedIds;
    importedIds.reserve(emitters.size());

    for (auto* emitter : emitters) {
        if (!emitter || !emitter->initialized) continue;

        auto& gpuBuf = emitter->buffer;
        if (!gpuBuf.IsValid()) continue;

        const bool firstCycle = !gpuBuf.hasCompletedCycle;

        // Write set: on frame 0, UAV (from Create); subsequent frames, SRV
        // (render pass read this set last frame before swap made it the write set).
        const RHIResourceState writeSetState = firstCycle
            ? RHIResourceState::UnorderedAccess
            : RHIResourceState::ShaderResource;

        // Read set: always UAV. On frame 0, UAV from Create. On subsequent
        // frames, UAV because the sim wrote to this set last frame (before
        // swap made it the read set).
        const RHIResourceState readSetState = RHIResourceState::UnorderedAccess;

        // aliveCount: on frame 0, UAV. Subsequent frames, SRV (render read it).
        const RHIResourceState aliveState = firstCycle
            ? RHIResourceState::UnorderedAccess
            : RHIResourceState::ShaderResource;

        // Indirect args: on frame 0, UAV. Subsequent frames, IndirectArgument
        // (sim's EmitIndirectArgsBarriers left them there for the render pass).
        const RHIResourceState indirectState = firstCycle
            ? RHIResourceState::UnorderedAccess
            : RHIResourceState::IndirectArgument;

        ImportedEmitterIds ids{};
        ids.buffer = &gpuBuf;

        // Import write-set attribute buffers.
        const auto& ws = gpuBuf.WriteSet();
        ids.wrPositions  = rgBuilder.ImportBuffer("VFX_WrPos",  ws.positions,  writeSetState);
        ids.wrVelocities = rgBuilder.ImportBuffer("VFX_WrVel",  ws.velocities, writeSetState);
        ids.wrAges       = rgBuilder.ImportBuffer("VFX_WrAge",  ws.ages,       writeSetState);
        ids.wrLifespans  = rgBuilder.ImportBuffer("VFX_WrLife", ws.lifespans,  writeSetState);
        ids.wrSizes      = rgBuilder.ImportBuffer("VFX_WrSz",   ws.sizes,      writeSetState);
        ids.wrColors     = rgBuilder.ImportBuffer("VFX_WrCol",  ws.colors,     writeSetState);
        ids.wrRotations  = rgBuilder.ImportBuffer("VFX_WrRot",  ws.rotations,  writeSetState);

        // Import read-set attribute buffers.
        const auto& rs = gpuBuf.ReadSet();
        ids.rdPositions  = rgBuilder.ImportBuffer("VFX_RdPos",  rs.positions,  readSetState);
        ids.rdVelocities = rgBuilder.ImportBuffer("VFX_RdVel",  rs.velocities, readSetState);
        ids.rdAges       = rgBuilder.ImportBuffer("VFX_RdAge",  rs.ages,       readSetState);
        ids.rdLifespans  = rgBuilder.ImportBuffer("VFX_RdLife", rs.lifespans,  readSetState);
        ids.rdSizes      = rgBuilder.ImportBuffer("VFX_RdSz",   rs.sizes,      readSetState);
        ids.rdColors     = rgBuilder.ImportBuffer("VFX_RdCol",  rs.colors,     readSetState);
        ids.rdRotations  = rgBuilder.ImportBuffer("VFX_RdRot",  rs.rotations,  readSetState);

        // Import shared bookkeeping buffers (single import, shared by both passes).
        ids.aliveCount         = rgBuilder.ImportBuffer("VFX_Alive",    gpuBuf.aliveCount,         aliveState);
        ids.freeList           = rgBuilder.ImportBuffer("VFX_FreeList", gpuBuf.freeList,            RHIResourceState::UnorderedAccess);
        ids.freeListHead       = rgBuilder.ImportBuffer("VFX_FreeHead", gpuBuf.freeListHead,       RHIResourceState::UnorderedAccess);
        ids.idToIndex          = rgBuilder.ImportBuffer("VFX_IdToIdx",  gpuBuf.idToIndex,          RHIResourceState::UnorderedAccess);
        ids.indirectDrawArgs   = rgBuilder.ImportBuffer("VFX_IndDraw",  gpuBuf.indirectDrawArgs,   indirectState);
        ids.indirectDispatchArgs = rgBuilder.ImportBuffer("VFX_IndDisp", gpuBuf.indirectDispatchArgs, indirectState);

        // Import sort buffers. On frame 0, UAV from Create(). On subsequent
        // frames, the sort pass leaves them in UAV after the last dispatch.
        if (gpuBuf.sort.IsValid()) {
            ids.sortKeys   = rgBuilder.ImportBuffer("VFX_SortKeys",   gpuBuf.sort.sortKeys,   RHIResourceState::UnorderedAccess);
            ids.sortIndices = rgBuilder.ImportBuffer("VFX_SortIdx",   gpuBuf.sort.sortIndices, RHIResourceState::UnorderedAccess);
        }

        // Import optional mesh instance buffers for MeshRenderModule emitters.
        // On frame 0, all mesh buffers start in UAV (from CreateMeshBuffers).
        // On subsequent frames, the instance CS left them in UAV and the mesh
        // render pass read transforms as SRV and indirect args as IndirectArgument.
        if (gpuBuf.hasMeshBuffers) {
            const RHIResourceState meshTransformsState = firstCycle
                ? RHIResourceState::UnorderedAccess
                : RHIResourceState::ShaderResource;
            const RHIResourceState meshCountState = RHIResourceState::UnorderedAccess;
            const RHIResourceState meshIndirectState = firstCycle
                ? RHIResourceState::UnorderedAccess
                : RHIResourceState::IndirectArgument;

            ids.meshInstanceTransforms = rgBuilder.ImportBuffer(
                "VFX_MeshTransforms", gpuBuf.meshInstanceTransforms, meshTransformsState);
            ids.meshInstanceCount = rgBuilder.ImportBuffer(
                "VFX_MeshInstCount", gpuBuf.meshInstanceCount, meshCountState);
            ids.meshIndirectDrawArgs = rgBuilder.ImportBuffer(
                "VFX_MeshIndDraw", gpuBuf.meshIndirectDrawArgs, meshIndirectState);
        }

        importedIds.push_back(ids);
    }

    // ── Simulation pass (compute) ──────────────────────────────────────
    // Dispatches spawn, update, and indirect args compute shaders for
    // each active emitter. Runs before any rendering to fill particle
    // attribute buffers.

    ParticleSimulationPassDesc simDesc{};
    simDesc.spawnPipeline        = ctx.pipelines->GetParticleSpawnCS();
    simDesc.updatePipeline       = ctx.pipelines->GetParticleUpdateCS();
    simDesc.indirectArgsPipeline = ctx.pipelines->GetParticleIndirectArgsCS();

    if (simDesc.spawnPipeline.IsValid() && simDesc.updatePipeline.IsValid()
        && simDesc.indirectArgsPipeline.IsValid()) {

        simDesc.emitters.reserve(emitters.size());

        for (auto* emitter : emitters) {
            if (!emitter || !emitter->initialized) continue;

            auto& gpuBuf = emitter->buffer;
            if (!gpuBuf.IsValid()) continue;

            // Spawn constants.
            ParticleSpawnConstants spawnCBData{};
            spawnCBData.spawnCount = emitter->CalculateSpawnCount(ctx.dt);
            spawnCBData.randomSeed = ctx.frameIndex;
            spawnCBData.capacity   = gpuBuf.capacity;

            const CBAllocation spawnAlloc = m_cbRing->Allocate(sizeof(spawnCBData));
            if (!spawnAlloc.cpuPtr) {
                UZ_LOG(VFX, Warning,
                       "RegisterParticlePasses: CB ring full, skipping emitter sim (spawn)");
                continue;
            }
            std::memcpy(spawnAlloc.cpuPtr, &spawnCBData, sizeof(spawnCBData));

            // Update constants.
            ParticleUpdateConstants updateCBData{};
            updateCBData.deltaTime  = ctx.dt;
            updateCBData.aliveCount = emitter->cpuAliveCount;
            updateCBData.capacity   = gpuBuf.capacity;

            const CBAllocation updateAlloc = m_cbRing->Allocate(sizeof(updateCBData));
            if (!updateAlloc.cpuPtr) {
                UZ_LOG(VFX, Warning,
                       "RegisterParticlePasses: CB ring full, skipping emitter sim (update)");
                continue;
            }
            std::memcpy(updateAlloc.cpuPtr, &updateCBData, sizeof(updateCBData));

            // Indirect args constants.
            ParticleIndirectArgsConstants indirectCBData{};
            indirectCBData.verticesPerParticle = 6;
            indirectCBData.threadGroupSize     = 64;

            const CBAllocation indirectAlloc = m_cbRing->Allocate(sizeof(indirectCBData));
            if (!indirectAlloc.cpuPtr) {
                UZ_LOG(VFX, Warning,
                       "RegisterParticlePasses: CB ring full, skipping emitter sim (indirect)");
                continue;
            }
            std::memcpy(indirectAlloc.cpuPtr, &indirectCBData, sizeof(indirectCBData));

            // Build per-emitter dispatch data.
            EmitterSimDispatch simData{};
            simData.buffer     = &gpuBuf;
            simData.spawnCB    = spawnAlloc;
            simData.updateCB   = updateAlloc;
            simData.indirectCB = indirectAlloc;
            simData.spawnCount = spawnCBData.spawnCount;
            simData.aliveCount = emitter->cpuAliveCount;

            // Bindless descriptor indices for the simulation compute shaders.
            // Each buffer's SRV/UAV index is retrieved from the RHI device's
            // descriptor heap so compute shaders can access them via
            // ResourceDescriptorHeap[] (bindless) or as a descriptor table base (SM5).
            simData.readSetSrvIndex  = m_device->GetBufferSRVIndex(gpuBuf.ReadSet().positions);
            simData.writeSetUavIndex = m_device->GetBufferUAVIndex(gpuBuf.WriteSet().positions);
            simData.aliveCountUavIndex           = m_device->GetBufferUAVIndex(gpuBuf.aliveCount);
            simData.freeListUavIndex             = m_device->GetBufferUAVIndex(gpuBuf.freeList);
            simData.indirectDrawArgsUavIndex     = m_device->GetBufferUAVIndex(gpuBuf.indirectDrawArgs);
            simData.indirectDispatchArgsUavIndex = m_device->GetBufferUAVIndex(gpuBuf.indirectDispatchArgs);

            // Wire render graph resource IDs from the imported buffers.
            // Find the matching imported IDs for this emitter's buffer.
            for (const auto& imported : importedIds) {
                if (imported.buffer == &gpuBuf) {
                    simData.rgWritePositions       = imported.wrPositions;
                    simData.rgWriteVelocities      = imported.wrVelocities;
                    simData.rgWriteAges            = imported.wrAges;
                    simData.rgWriteLifespans       = imported.wrLifespans;
                    simData.rgWriteSizes           = imported.wrSizes;
                    simData.rgWriteColors          = imported.wrColors;
                    simData.rgWriteRotations       = imported.wrRotations;
                    simData.rgAliveCount           = imported.aliveCount;
                    simData.rgFreeList             = imported.freeList;
                    simData.rgFreeListHead         = imported.freeListHead;
                    simData.rgIdToIndex            = imported.idToIndex;
                    simData.rgIndirectDrawArgs     = imported.indirectDrawArgs;
                    simData.rgIndirectDispatchArgs = imported.indirectDispatchArgs;
                    break;
                }
            }

            simDesc.emitters.push_back(simData);
        }

        if (!simDesc.emitters.empty()) {
            AddParticleSimulationPass(rgBuilder, simDesc);
        }
    }

    // ── Sort pass (compute) ──────────────────────────────────────────
    // Dispatches the bitonic merge sort for each active emitter so particles
    // are rendered in back-to-front order for correct alpha blending.
    // Runs after simulation (reads post-sim positions) and before rendering
    // (billboard VS reads sorted indices).

    const auto sortPipeline = ctx.pipelines->GetParticleSortCS();

    if (sortPipeline.IsValid()) {
        // Camera position and forward direction for depth-key generation.
        const Vec3 sortCamPos{
            ctx.perFrame.cameraPos.x,
            ctx.perFrame.cameraPos.y,
            ctx.perFrame.cameraPos.z
        };
        const Vec3 sortCamFwd = m_camera->GetForward();

        // Per-emitter sort dispatch data for the render graph compute pass.
        struct SortEmitterDispatch {
            GPUParticleSort* sort;
            uint32_t aliveCount;
            uint32_t positionsSrvIndex;
            rg::RgResourceId rgReadPositions;
            rg::RgResourceId rgSortKeys;
            rg::RgResourceId rgSortIndices;
        };

        std::vector<SortEmitterDispatch> sortDispatches;
        sortDispatches.reserve(emitters.size());

        for (auto* emitter : emitters) {
            if (!emitter || !emitter->initialized) continue;

            auto& gpuBuf = emitter->buffer;
            if (!gpuBuf.IsValid() || !gpuBuf.sort.IsValid()) continue;
            if (emitter->cpuAliveCount == 0) continue;

            // Find the imported render graph IDs for this emitter's buffer.
            const ImportedEmitterIds* importedPtr = nullptr;
            for (const auto& imported : importedIds) {
                if (imported.buffer == &gpuBuf) {
                    importedPtr = &imported;
                    break;
                }
            }
            if (!importedPtr || !importedPtr->sortKeys.IsValid()) continue;

            SortEmitterDispatch sd{};
            sd.sort               = &gpuBuf.sort;
            sd.aliveCount         = emitter->cpuAliveCount;
            sd.positionsSrvIndex  = m_device->GetBufferSRVIndex(gpuBuf.ReadSet().positions);
            sd.rgReadPositions    = importedPtr->rdPositions;
            sd.rgSortKeys         = importedPtr->sortKeys;
            sd.rgSortIndices      = importedPtr->sortIndices;
            sortDispatches.push_back(sd);
        }

        if (!sortDispatches.empty()) {
            // Capture camera vectors as float arrays for the sort CB.
            const float camPosArr[3]  = { sortCamPos.x, sortCamPos.y, sortCamPos.z };
            const float camFwdArr[3]  = { sortCamFwd.x, sortCamFwd.y, sortCamFwd.z };

            rgBuilder.AddPass("VFX_ParticleSort",
                [this, sortPipeline,
                 dispatches = std::move(sortDispatches),
                 camPosArr, camFwdArr]
                (rg::RgPassBuilder& pb) {
                    // Declare resource access for barrier tracking.
                    for (const auto& d : dispatches) {
                        pb.Read(d.rgReadPositions, RHIResourceState::ShaderResource);
                        pb.Write(d.rgSortKeys,     RHIResourceState::UnorderedAccess);
                        pb.Write(d.rgSortIndices,   RHIResourceState::UnorderedAccess);
                    }

                    pb.SetExecutionCallback(
                        [this, sortPipeline, dispatches, camPosArr, camFwdArr]
                        (IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
                            UZ_PROFILE_SCOPE("VFX_ParticleSort");

                            for (const auto& d : dispatches) {
                                d.sort->DispatchSort(
                                    m_device, cmd, sortPipeline,
                                    *m_cbRing, d.aliveCount,
                                    camPosArr, camFwdArr,
                                    d.positionsSrvIndex);
                            }
                        });
                });
        }
    }

    // ── Render pass (transparent phase) ────────────────────────────────
    // Draws particles as camera-facing billboards after opaque geometry
    // and lighting. Sorted back-to-front for correct alpha blending.

    const auto billboardPipeline = ctx.pipelines->GetParticleBillboard();

    // If the billboard pipeline isn't compiled yet (first few frames or
    // shader compilation error), skip the render pass. Also skip marking
    // hasCompletedCycle because the render pass didn't run -- the read-set
    // buffers won't be left in the expected SRV state for next frame's import.
    if (!billboardPipeline.IsValid()) return;

    ParticleRenderPassDesc renderDesc{};
    renderDesc.billboardPipeline  = billboardPipeline;
    renderDesc.colorTarget        = ctx.hdrRes;
    renderDesc.depthTarget        = ctx.depthRes;
    renderDesc.sceneDepthTexture  = m_gbuffer.depth;

    // Camera position for distance sorting (Vec4 -> Vec3 xyz).
    const Vec3 camPos{
        ctx.perFrame.cameraPos.x,
        ctx.perFrame.cameraPos.y,
        ctx.perFrame.cameraPos.z
    };

    // View and projection matrices for billboard orientation.
    const Mat4& viewMatrix = m_camera->GetView();
    const Mat4& projMatrix = m_camera->GetProjection();

    renderDesc.emitters.reserve(emitters.size());

    for (auto* emitter : emitters) {
        if (!emitter || !emitter->initialized) continue;

        auto& gpuBuf = emitter->buffer;
        if (!gpuBuf.IsValid()) continue;

        // Billboard constant buffer: view/proj matrices + viewport dims.
        ParticleBillboardConstants bbCBData{};
        std::memcpy(bbCBData.viewMatrix, &viewMatrix, sizeof(float) * 16);
        std::memcpy(bbCBData.projMatrix, &projMatrix, sizeof(float) * 16);
        bbCBData.viewportWidth         = ctx.renderWidth;
        bbCBData.viewportHeight        = ctx.renderHeight;
        bbCBData.softParticleFadeRange = emitter->softParticleFadeRange;

        const CBAllocation bbAlloc = m_cbRing->Allocate(sizeof(bbCBData));
        if (!bbAlloc.cpuPtr) {
            UZ_LOG(VFX, Warning,
                   "RegisterParticlePasses: CB ring full, skipping emitter render");
            continue;
        }
        std::memcpy(bbAlloc.cpuPtr, &bbCBData, sizeof(bbCBData));

        EmitterRenderData renderData{};
        renderData.buffer          = &gpuBuf;
        renderData.renderCB        = bbAlloc;

        // Bindless indices CB (b0) -- SRV indices for read-set attribute buffers.
        // The billboard vertex shader reads these via ConstantBuffer<VFXBillboardIndices>
        // at register(b0) and uses ResourceDescriptorHeap[] to access particle data.
        struct VFXBillboardIndices {
            uint32_t positionBufferIdx;
            uint32_t sizeBufferIdx;
            uint32_t colorBufferIdx;
            uint32_t rotationBufferIdx;
            uint32_t sortedIndicesSrvIndex;
            uint32_t velocityBufferIdx;
            uint32_t agesBufferIdx;
            uint32_t lifespansBufferIdx;
        };

        const auto& readSet = gpuBuf.ReadSet();
        VFXBillboardIndices indicesData{};
        indicesData.positionBufferIdx    = m_device->GetBufferSRVIndex(readSet.positions);
        indicesData.sizeBufferIdx        = m_device->GetBufferSRVIndex(readSet.sizes);
        indicesData.colorBufferIdx       = m_device->GetBufferSRVIndex(readSet.colors);
        indicesData.rotationBufferIdx    = m_device->GetBufferSRVIndex(readSet.rotations);
        indicesData.sortedIndicesSrvIndex = gpuBuf.sort.IsValid()
            ? m_device->GetBufferSRVIndex(gpuBuf.sort.sortIndices)
            : UINT32_MAX;
        indicesData.velocityBufferIdx    = m_device->GetBufferSRVIndex(readSet.velocities);
        indicesData.agesBufferIdx        = m_device->GetBufferSRVIndex(readSet.ages);
        indicesData.lifespansBufferIdx   = m_device->GetBufferSRVIndex(readSet.lifespans);

        const CBAllocation indicesAlloc = m_cbRing->Allocate(sizeof(indicesData));
        if (indicesAlloc.cpuPtr) {
            std::memcpy(indicesAlloc.cpuPtr, &indicesData, sizeof(indicesData));
            renderData.indicesCB = indicesAlloc;
        }

        // SM5 fallback path: bind descriptor table starting at the position buffer's
        // SRV index. The SM5 shader expects contiguous t0..t3 bindings.
        renderData.readSetSrvIndex = m_device->GetBufferSRVIndex(readSet.positions);

        // Distance to camera for back-to-front sorting.
        // ParticleEmitter does not yet carry a world-space position
        // (it's an ECS component; position comes from the transform).
        // Use origin as emitter position until transform wiring is added.
        const Vec3 emitterPos{0.0f, 0.0f, 0.0f};
        const Vec3 diff{
            emitterPos.x - camPos.x,
            emitterPos.y - camPos.y,
            emitterPos.z - camPos.z
        };
        renderData.distanceToCamera = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;

        // Wire render graph resource IDs for read-set attributes and shared
        // buffers. Reuses the same RgResourceIds imported above (one import
        // per physical buffer).
        for (const auto& imported : importedIds) {
            if (imported.buffer == &gpuBuf) {
                renderData.rgReadPositions  = imported.rdPositions;
                renderData.rgReadVelocities = imported.rdVelocities;
                renderData.rgReadAges       = imported.rdAges;
                renderData.rgReadLifespans  = imported.rdLifespans;
                renderData.rgReadSizes      = imported.rdSizes;
                renderData.rgReadColors     = imported.rdColors;
                renderData.rgReadRotations  = imported.rdRotations;
                renderData.rgAliveCount     = imported.aliveCount;
                renderData.rgIndirectDrawArgs = imported.indirectDrawArgs;
                renderData.rgSortIndices     = imported.sortIndices;
                break;
            }
        }

        renderDesc.emitters.push_back(renderData);
    }

    if (!renderDesc.emitters.empty()) {
        AddParticleRenderPass(rgBuilder, renderDesc);
    }

    // ── Trail render pass (transparent phase) ─────────────────────────────
    // Draws ribbon trails as camera-facing quad strips after particle
    // billboards. Sorted back-to-front by distance to camera.

    if (m_trailSystem && m_trailSystem->GetActiveTrailCount() > 0) {
        const auto trailPipeline = ctx.pipelines->GetTrailRibbon();
        if (trailPipeline.IsValid()) {
            const auto trailRenderData = m_trailSystem->GetRenderData();

            if (!trailRenderData.empty()) {
                // Per-trail constant buffer: view-projection matrix.
                const Mat4 viewProj = projMatrix * viewMatrix;

                TrailPassDesc trailDesc{};
                trailDesc.colorTarget = ctx.hdrRes;
                trailDesc.depthTarget = ctx.depthRes;
                trailDesc.items.reserve(trailRenderData.size());

                for (const auto& trd : trailRenderData) {
                    if (trd.vertexCount == 0 || trd.indexCount == 0) continue;

                    TrailPassConstants trailCB{};
                    std::memcpy(trailCB.viewProjection, &viewProj, sizeof(float) * 16);

                    const CBAllocation trailAlloc = m_cbRing->Allocate(sizeof(TrailPassConstants));
                    if (!trailAlloc.cpuPtr) {
                        UZ_LOG(VFX, Warning,
                               "RegisterParticlePasses: CB ring full, skipping trail '{}'",
                               trd.materialPath ? trd.materialPath : "<unknown>");
                        continue;
                    }
                    std::memcpy(trailAlloc.cpuPtr, &trailCB, sizeof(TrailPassConstants));

                    TrailPassRenderItem item{};
                    item.pipeline       = trailPipeline;
                    item.vertexCount    = trd.vertexCount;
                    item.indexCount     = trd.indexCount;
                    item.constantBuffer = trailAlloc;
                    item.distanceToCamera = trd.distanceToCamera;
                    item.sortOrder        = trd.sortOrder;

                    // SRV indices for trail vertex/index structured buffers.
                    // TrailSystem produces CPU-side vertex/index arrays; GPU upload
                    // and descriptor allocation are pending TrailSystem GPU integration.
                    item.vertexBufferSrvIndex = UINT32_MAX;
                    item.indexBufferSrvIndex  = UINT32_MAX;

                    trailDesc.items.push_back(item);
                }

                if (!trailDesc.items.empty()) {
                    AddTrailRenderPass(rgBuilder, trailDesc);
                }
            }
        }
    }

    // ── Mesh particle instance CS + render pass ─────────────────────────────
    // For emitters using MeshRenderModule, dispatch the instance transform CS
    // (reads from the post-sim read set, writes instance transforms + indirect
    // args), then register the mesh render pass that consumes them.

    const auto meshInstanceCSPipeline = ctx.pipelines->GetParticleMeshInstanceCS();
    const auto meshOpaquePipeline     = ctx.pipelines->GetParticleMeshOpaque();
    const auto meshTransparentPipeline = ctx.pipelines->GetParticleMeshTransparent();

    if (meshInstanceCSPipeline.IsValid()) {
        // CPU-side mirror of the VFXMeshInstanceConstants cbuffer in
        // particle_mesh_instance_cs.hlsl (64 bytes, 16-byte aligned rows).
        struct VFXMeshInstanceConstants {
            uint32_t aliveCount;               //  0
            float    meshScaleX;               //  4
            float    meshScaleY;               //  8
            float    meshScaleZ;               // 12
            uint32_t rotationMode;             // 16
            uint32_t meshIndexCount;           // 20
            uint32_t _pad0;                    // 24
            uint32_t _pad1;                    // 28
            uint32_t positionsSrvIndex;        // 32
            uint32_t velocitiesSrvIndex;       // 36
            uint32_t agesSrvIndex;             // 40
            uint32_t lifespansSrvIndex;        // 44
            uint32_t instanceTransformsUavIndex; // 48
            uint32_t instanceCountUavIndex;    // 52
            uint32_t indirectArgsUavIndex;     // 56
            uint32_t _pad2;                    // 60
        };
        static_assert(sizeof(VFXMeshInstanceConstants) == 64,
                       "VFXMeshInstanceConstants must be exactly 64 bytes");

        // View-projection matrix for the mesh render pass CB.
        const Mat4 viewProj = projMatrix * viewMatrix;

        // Collect mesh emitter dispatch data for the instance CS compute pass.
        // The compute pass is a single render graph pass that dispatches once
        // per mesh emitter (similar to the sim pass pattern).
        struct MeshEmitterDispatch {
            CBAllocation instanceCB;
            uint32_t dispatchGroupsX;
            // Render graph resource IDs for barrier tracking.
            rg::RgResourceId rgReadPositions;
            rg::RgResourceId rgReadVelocities;
            rg::RgResourceId rgReadAges;
            rg::RgResourceId rgReadLifespans;
            rg::RgResourceId rgMeshTransforms;
            rg::RgResourceId rgMeshCount;
            rg::RgResourceId rgMeshIndirectArgs;
        };

        std::vector<MeshEmitterDispatch> meshDispatches;
        ParticleMeshPassDesc meshDesc{};
        meshDesc.colorTarget = ctx.hdrRes;
        meshDesc.depthTarget = ctx.depthRes;
        meshDesc.isTransparent = false;

        for (auto* emitter : emitters) {
            if (!emitter || !emitter->initialized) continue;

            // Only process emitters with a MeshRenderModule.
            if (!emitter->moduleStack.renderModule) continue;
            if (std::strcmp(emitter->moduleStack.renderModule->GetName(), "mesh_render") != 0) {
                continue;
            }

            auto& gpuBuf = emitter->buffer;
            if (!gpuBuf.IsValid() || !gpuBuf.hasMeshBuffers) continue;

            // Retrieve the MeshRenderModule config for scale and rotation mode.
            const auto* meshModule = static_cast<const MeshRenderModule*>(
                emitter->moduleStack.renderModule.get());
            const auto& meshConfig = meshModule->GetConfig();

            // Find the imported render graph IDs for this emitter's buffer.
            const ImportedEmitterIds* importedPtr = nullptr;
            for (const auto& imported : importedIds) {
                if (imported.buffer == &gpuBuf) {
                    importedPtr = &imported;
                    break;
                }
            }
            if (!importedPtr) continue;

            // Fill the instance CS constant buffer.
            const auto& readSet = gpuBuf.ReadSet();

            VFXMeshInstanceConstants instanceCBData{};
            instanceCBData.aliveCount    = emitter->cpuAliveCount;
            instanceCBData.meshScaleX    = meshConfig.meshScale.x;
            instanceCBData.meshScaleY    = meshConfig.meshScale.y;
            instanceCBData.meshScaleZ    = meshConfig.meshScale.z;
            instanceCBData.rotationMode  = static_cast<uint32_t>(meshConfig.rotationMode);
            instanceCBData.meshIndexCount = 0; // Populated once mesh asset system is wired.
            instanceCBData._pad0         = 0;
            instanceCBData._pad1         = 0;

            // Bindless SRV indices for the read-set particle attribute buffers.
            instanceCBData.positionsSrvIndex  = m_device->GetBufferSRVIndex(readSet.positions);
            instanceCBData.velocitiesSrvIndex = m_device->GetBufferSRVIndex(readSet.velocities);
            instanceCBData.agesSrvIndex       = m_device->GetBufferSRVIndex(readSet.ages);
            instanceCBData.lifespansSrvIndex  = m_device->GetBufferSRVIndex(readSet.lifespans);

            // Bindless UAV indices for the mesh output buffers.
            instanceCBData.instanceTransformsUavIndex =
                m_device->GetBufferUAVIndex(gpuBuf.meshInstanceTransforms);
            instanceCBData.instanceCountUavIndex =
                m_device->GetBufferUAVIndex(gpuBuf.meshInstanceCount);
            instanceCBData.indirectArgsUavIndex =
                m_device->GetBufferUAVIndex(gpuBuf.meshIndirectDrawArgs);
            instanceCBData._pad2 = 0;

            const CBAllocation instanceAlloc = m_cbRing->Allocate(sizeof(instanceCBData));
            if (!instanceAlloc.cpuPtr) {
                UZ_LOG(VFX, Warning,
                       "RegisterParticlePasses: CB ring full, skipping mesh instance CS");
                continue;
            }
            std::memcpy(instanceAlloc.cpuPtr, &instanceCBData, sizeof(instanceCBData));

            // Dispatch group count: ceil(aliveCount / 256).
            constexpr uint32_t kMeshInstanceGroupSize = 256;
            const uint32_t groupsX = (emitter->cpuAliveCount + kMeshInstanceGroupSize - 1)
                                     / kMeshInstanceGroupSize;

            MeshEmitterDispatch dispatch{};
            dispatch.instanceCB      = instanceAlloc;
            dispatch.dispatchGroupsX = (groupsX > 0) ? groupsX : 1;
            dispatch.rgReadPositions  = importedPtr->rdPositions;
            dispatch.rgReadVelocities = importedPtr->rdVelocities;
            dispatch.rgReadAges       = importedPtr->rdAges;
            dispatch.rgReadLifespans  = importedPtr->rdLifespans;
            dispatch.rgMeshTransforms = importedPtr->meshInstanceTransforms;
            dispatch.rgMeshCount      = importedPtr->meshInstanceCount;
            dispatch.rgMeshIndirectArgs = importedPtr->meshIndirectDrawArgs;
            meshDispatches.push_back(dispatch);

            // Build the mesh render pass batch item for this emitter.
            ParticleMeshPassConstants meshPassCB{};
            std::memcpy(meshPassCB.viewProjection, &viewProj, sizeof(float) * 16);

            const CBAllocation meshPassAlloc = m_cbRing->Allocate(sizeof(meshPassCB));
            if (!meshPassAlloc.cpuPtr) {
                UZ_LOG(VFX, Warning,
                       "RegisterParticlePasses: CB ring full, skipping mesh render batch");
                continue;
            }
            std::memcpy(meshPassAlloc.cpuPtr, &meshPassCB, sizeof(meshPassCB));

            // Determine the pipeline based on MeshRenderModule transparency.
            // MeshRenderModuleConfig does not yet have an `isTransparent` field;
            // default to opaque for solid debris mesh particles.
            const RHIPipelineHandle meshPipeline = meshOpaquePipeline.IsValid()
                ? meshOpaquePipeline : meshTransparentPipeline;

            ParticleMeshBatchItem batch{};
            batch.pipeline         = meshPipeline;
            batch.meshVertexBuffer = RHIBufferHandle::Invalid(); // Mesh asset not yet wired.
            batch.meshIndexBuffer  = RHIBufferHandle::Invalid(); // Mesh asset not yet wired.
            batch.meshIndexFormat  = RHIIndexFormat::Uint16;
            batch.instanceBufferSrvIndex =
                m_device->GetBufferSRVIndex(gpuBuf.meshInstanceTransforms);
            batch.materialTexturesSrvIndex = UINT32_MAX;
            batch.indirectArgsBuffer = gpuBuf.meshIndirectDrawArgs;
            batch.passCB             = meshPassAlloc;

            meshDesc.batches.push_back(batch);
        }

        // Register the mesh instance CS compute pass if any mesh emitters exist.
        if (!meshDispatches.empty()) {
            rgBuilder.AddPass("VFX_MeshInstanceCS",
                [meshInstanceCSPipeline,
                 dispatches = std::move(meshDispatches)]
                (rg::RgPassBuilder& pb) {
                    // Declare resource access for barrier tracking.
                    for (const auto& d : dispatches) {
                        pb.Read(d.rgReadPositions,  RHIResourceState::ShaderResource);
                        pb.Read(d.rgReadVelocities, RHIResourceState::ShaderResource);
                        pb.Read(d.rgReadAges,       RHIResourceState::ShaderResource);
                        pb.Read(d.rgReadLifespans,  RHIResourceState::ShaderResource);
                        pb.Write(d.rgMeshTransforms,  RHIResourceState::UnorderedAccess);
                        pb.Write(d.rgMeshCount,       RHIResourceState::UnorderedAccess);
                        pb.Write(d.rgMeshIndirectArgs, RHIResourceState::UnorderedAccess);
                    }

                    pb.SetExecutionCallback(
                        [meshInstanceCSPipeline, dispatches]
                        (IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
                            UZ_PROFILE_SCOPE("VFX_MeshInstanceCS");
                            cmd->SetPipeline(meshInstanceCSPipeline);

                            for (size_t i = 0; i < dispatches.size(); ++i) {
                                const auto& d = dispatches[i];

                                cmd->SetRootCBV(0,
                                    d.instanceCB.bufferHandle,
                                    d.instanceCB.offsetBytes,
                                    d.instanceCB.cpuPtr,
                                    d.instanceCB.sizeBytes);

                                cmd->Dispatch(d.dispatchGroupsX, 1, 1);

                                // UAV barrier between emitter dispatches to
                                // prevent write-after-write hazards on the
                                // mesh output buffers.
                                if (i + 1 < dispatches.size()) {
                                    RHIBarrierDesc barrier{};
                                    barrier.stateBefore = RHIResourceState::UnorderedAccess;
                                    barrier.stateAfter  = RHIResourceState::UnorderedAccess;
                                    cmd->ResourceBarrier(&barrier, 1);
                                }
                            }
                        });
                });
        }

        // Register the mesh render pass if any batch items were created.
        if (!meshDesc.batches.empty()) {
            AddParticleMeshRenderPass(rgBuilder, meshDesc);
        }
    }

    // Mark all emitter buffers as having completed their first sim+render
    // cycle. After this point, the render graph import will use steady-state
    // resource states (SRV for write set, IndirectArgument for indirect args)
    // instead of the first-frame UAV-everywhere state.
    // Note: Swap() was moved to the TOP of this function so the render graph
    // import sees post-swap buffer assignments.
    for (auto* emitter : emitters) {
        if (emitter && emitter->initialized && emitter->buffer.IsValid()) {
            emitter->buffer.hasCompletedCycle = true;
        }
    }
}

} // namespace uz
