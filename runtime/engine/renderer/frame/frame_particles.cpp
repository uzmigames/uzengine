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

    // ── Import GPU particle buffers into the render graph ────────────────
    // NOTE: Ping-pong swap, GPU buffer initialization, and simulation (spawn,
    // update, indirect args) are now dispatched inline on the main command list
    // by DispatchVFXSimulation() BEFORE the render graph. This function only
    // handles rendering (billboard draw, sort, trails, mesh particles).
    // Each buffer handle must be imported exactly once so the graph has a
    // single RgResourceId per physical buffer for state tracking. We build
    // a per-emitter struct holding all imported IDs, then distribute them
    // to the render pass descriptors.
    //
    // Post-inline-sim resource states (set by DispatchVFXSimulation):
    //   Write set = UAV (spawn + update CS wrote to it).
    //   Read set  = UAV (sim read via bindless; tracked state unchanged from UAV).
    //   aliveCount = UAV (sim wrote to it; indirect args read via bindless).
    //   freeList/freeListHead/idToIndex = UAV (sim wrote to them).
    //   indirectDrawArgs/Dispatch = IndirectArgument (TransitionIndirectArgs).
    //
    // The render pass reads the write set (post-sim data) as SRV, so the
    // render graph will transition UAV -> SRV. Indirect args are already in
    // IndirectArgument state from the inline dispatch.

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

        // After DispatchVFXSimulation() ran inline on the command list:
        // - Write set attributes are in UAV (spawn + update CS wrote to them).
        // - Read set attributes: first frame = UAV (from Create); subsequent
        //   frames = SRV (billboard render pass left them in SRV last frame).
        //   Inline sim reads them via bindless (doesn't change tracked state).
        // - aliveCount is in UAV (sim wrote to it).
        // - indirectDrawArgs/Dispatch are in IndirectArgument
        //   (TransitionIndirectArgs barriered them after indirect args CS).
        constexpr RHIResourceState writeSetState  = RHIResourceState::UnorderedAccess;
        const RHIResourceState readSetState = firstCycle
            ? RHIResourceState::UnorderedAccess
            : RHIResourceState::ShaderResource;
        constexpr RHIResourceState aliveState     = RHIResourceState::UnorderedAccess;
        constexpr RHIResourceState indirectState  = RHIResourceState::IndirectArgument;

        ImportedEmitterIds ids{};
        ids.buffer = &gpuBuf;

        // Import write-set attribute buffers.
        const auto& ws = gpuBuf.WriteSet();

        // Validate required buffer handles before importing (null → segfault).
        if (!ws.positions.IsValid() || !gpuBuf.aliveCount.IsValid() ||
            !gpuBuf.freeList.IsValid() || !gpuBuf.indirectDrawArgs.IsValid()) {
            UZ_LOG(Renderer, Error, "RegisterParticlePasses: emitter has invalid GPU buffers, skipping");
            continue;
        }

        ids.wrPositions  = rgBuilder.ImportBuffer("VFX_WrPos",  ws.positions,  writeSetState);
        ids.wrVelocities = rgBuilder.ImportBuffer("VFX_WrVel",  ws.velocities, writeSetState);
        ids.wrAges       = rgBuilder.ImportBuffer("VFX_WrAge",  ws.ages,       writeSetState);
        ids.wrLifespans  = rgBuilder.ImportBuffer("VFX_WrLife", ws.lifespans,  writeSetState);
        ids.wrSizes      = rgBuilder.ImportBuffer("VFX_WrSz",   ws.sizes,      writeSetState);
        ids.wrColors     = rgBuilder.ImportBuffer("VFX_WrCol",  ws.colors,     writeSetState);
        ids.wrRotations  = rgBuilder.ImportBuffer("VFX_WrRot",  ws.rotations,  writeSetState);

        // Import read-set attribute buffers.
        const auto& rs = gpuBuf.WriteSet();
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


    // Simulation (spawn, update, indirect args) is dispatched inline by
    // DispatchVFXSimulation() in frame_vfx_inline.cpp.


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
            sd.positionsSrvIndex  = m_device->GetBufferSRVIndex(gpuBuf.WriteSet().positions);
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
    // shader compilation error), skip the render pass entirely.
    if (!billboardPipeline.IsValid()) return;

    ParticleRenderPassDesc renderDesc{};
    renderDesc.billboardPipeline  = billboardPipeline;
    renderDesc.colorTarget        = ctx.hdrRes;
    renderDesc.depthTarget        = ctx.depthRes;
    renderDesc.colorTexture       = m_hdrSceneColor;
    renderDesc.depthTexture       = m_gbuffer.depth;
    renderDesc.sceneDepthTexture  = m_gbuffer.depth;
    renderDesc.perFrameCB         = ctx.perFrameAlloc;
    renderDesc.renderWidth        = ctx.renderWidth;
    renderDesc.renderHeight       = ctx.renderHeight;

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

        // Billboard CB (b2) -- unified VS + PS constant buffer (304 bytes).
        // Contains invView for billboard orientation, emitter params,
        // and PS rendering params (soft fade, erosion, lighting, etc.).
        ParticleBillboardCB bbCBData{};

        // Compute inverse view matrix for billboard right/up extraction.
        // The VS reads rows 0 and 1 as camera right and camera up vectors.
        const Mat4 invViewMat = viewMatrix.Inverse();
        bbCBData.invView = invViewMat;

        // Emitter position (will be transform-driven later).
        bbCBData.emitterPosition[0] = 0.0f;
        bbCBData.emitterPosition[1] = 0.0f;
        bbCBData.emitterPosition[2] = 0.0f;
        bbCBData.globalSizeMultiplier = 1.0f;

        // Previous frame view-projection for TAA motion vectors.
        bbCBData.prevViewProj = projMatrix * viewMatrix;

        // PS rendering parameters.
        bbCBData.softFadeDistance   = emitter->softParticleFadeRange;

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
        // Draw ALL particles and let the VS cull dead ones.
        // The spawn shader allocates from the top of the free list (index N-1 down),
        // so alive particles are scattered across the full buffer range.
        renderData.cpuVertexCount  = gpuBuf.capacity * 6;

        // Bindless indices CB (b0) -- SRV indices for read-set attribute buffers
        // and PS texture indices. Must match HLSL VFXBillboardConstants (48 bytes).
        const auto& readSet = gpuBuf.WriteSet();
        ParticleBillboardIndices indicesData{};
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
        indicesData.spriteTextureIdx     = UINT32_MAX; // No sprite texture yet.
        indicesData.depthBufferIdx       = m_device->GetTextureSRVIndex(m_gbuffer.depth);
        // Use positions SRV as fallback — prevents ResourceDescriptorHeap[0] access
        // which corrupts the descriptor ring. The VS guards dynParams reads anyway.
        indicesData.dynamicParamsBufferIdx = indicesData.positionBufferIdx;
        indicesData._vfxPad1             = 0;

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
            const auto& readSet = gpuBuf.WriteSet();

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

            // Skip mesh particle rendering if neither PSO compiled successfully.
            if (!meshPipeline.IsValid()) {
                continue;
            }

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

    // hasCompletedCycle is now set by DispatchVFXSimulation() (frame_vfx_inline.cpp).
}

} // namespace uz
