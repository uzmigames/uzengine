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
#include "engine/renderer/material/material_pipeline.h"   // ParticleMaterialDesc, GetParticleMeshPSO
#include "engine/renderer/material/material_types.h"      // EShadingModel, EBlendMode
#include "engine/asset/vfs.h"                             // IVFS::Read for inline .uzmat parse
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/gpu_particle_buffer.h"
#include "engine/vfx/core/vfx_cb_structs.h"
#include "engine/vfx/core/gpu_particle_sort.h"
#include "engine/vfx/modules/mesh_render_module.h"
#include "engine/vfx/modules/sprite_render_module.h"
#include "engine/vfx/modules/spawn_module.h"
#include "engine/vfx/modules/update_module.h"
#include "engine/vfx/modules/turbulence_module.h"
#include "engine/vfx/modules/orbit_module.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/trails/trail_system.h"

#include <json.hpp>     // nlohmann::json — used for inline `.uzmat` parse in mesh PSO resolver

#include <algorithm>
#include <cstdio>      // std::snprintf — emitter-pointer formatting for warning dedup
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

void SceneRenderer::RegisterParticlePasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    if (!m_showFlags.Get(ShowFlag::Particles)) return;
    UZ_PROFILE_FUNCTION();

    // Resolve active VFX system: the non-owning override (set via SetVFXSystem)
    // takes precedence over the renderer-owned system so that an external
    // emitter pool managed by the game layer is used when present.
    VFXSystem* activeVFX = m_vfxSystemOverride ? m_vfxSystemOverride : m_vfxSystem.get();

    // Skip if no VFX system is available or it has no active emitters.
    if (!activeVFX || !activeVFX->IsInitialized()) return;

    const auto& emitters = activeVFX->GetActiveEmitters();
    if (emitters.empty()) return;

    static bool loggedRP = false;
    if (!loggedRP) {
        UZ_LOG(VFX, Info, "RegisterParticlePasses: {} active emitters", emitters.size());
        loggedRP = true;
    }

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
        // Per-instance color buffer is owned by `MeshRenderModule`, not
        // `GPUParticleBuffer`. Imported here so the auto-barrier scheduler
        // emits the UAV→ShaderResource transition between the mesh
        // instance CS write and the mesh render pass's PS read (bindless
        // `ResourceDescriptorHeap[g_instanceColorsBufferIdx]`). The handle
        // is invalid when the module has not yet allocated the buffer
        // (lazy init); we only import valid handles below.
        // @ref phase0_vfx-mesh-particle-visibility-debug
        rg::RgResourceId meshInstanceColors;
    };

    std::vector<ImportedEmitterIds> importedIds;
    importedIds.reserve(emitters.size());

    for (auto* emitter : emitters) {
        if (!emitter || !emitter->initialized) continue;
        if (emitter->cullSuppress) continue; // phase 10.5.0 frustum cull gate

        // Skip emitters needing initial upload — their buffers will be
        // transitioned by InitFreeList this frame. Don't import them into
        // the render graph to avoid conflicting barrier insertions.
        if (emitter->needsInitialUpload) continue;

        auto& gpuBuf = emitter->buffer;
        if (!gpuBuf.IsValid()) continue;

        // Skip render graph imports for VFX particle attribute / indirect-draw
        // buffers. The VFX billboard shaders use bindless access
        // (`ResourceDescriptorHeap[index]`) and don't need render graph state
        // tracking; manual barriers in the inline sim handle the intra-pass
        // transitions. Importing those buffers into the graph caused
        // conflicting barriers and TDR on the first frame.
        //
        // Mesh-particle emitters (`hasMeshBuffers == true`) are an exception:
        // the mesh-instance CS writes the mesh indirect-args buffer as a UAV,
        // and the mesh render pass has to consume it as IndirectArgument. We
        // need a tracked `RgResourceId` for that single buffer so the
        // auto-barrier scheduler emits the UAV→IndirectArgument transition
        // between `VFX_MeshInstanceCS` and `VFX_ParticleMeshRender`. Without
        // it the indirect draw lands on a buffer still in UAV state and the
        // debug layer raises CORRUPTED_PARAMETER (or, on RC2 hardware, a
        // device-removed). Re-enabled in
        // `phase0_re-enable-mesh-particle-render-path-rc3` — we keep the
        // billboard-side skip but fall through into the mesh-buffers branch
        // below for mesh emitters only.
        if (!gpuBuf.hasMeshBuffers) continue;

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

        const auto& ws = gpuBuf.WriteSet();

        if (!ws.positions.IsValid()) {
            UZ_LOG(Renderer, Error, "RegisterParticlePasses: emitter has invalid GPU buffers, skipping");
            continue;
        }

        // Mesh-only import: the mesh instance CS writes these as UAV and the
        // mesh render pass consumes the indirect-args buffer in
        // IndirectArgument state. Importing them gives the auto-barrier
        // scheduler a tracked `RgResourceId` so it emits the
        // UAV→IndirectArgument transition automatically. The billboard /
        // sort / particle-attribute buffers are NOT imported here — the
        // inline sim manages their state, and the billboard VS reads them
        // bindlessly. See `mesh-particle-render-path` task notes for the
        // pre-RC3 import-everywhere path that caused TDR on first frame.
        ids.meshInstanceTransforms = rgBuilder.ImportTrackedBuffer(
            "VFX_MeshTransforms", gpuBuf.meshInstanceTransforms);
        ids.meshInstanceCount = rgBuilder.ImportTrackedBuffer(
            "VFX_MeshInstCount", gpuBuf.meshInstanceCount);
        ids.meshIndirectDrawArgs = rgBuilder.ImportTrackedBuffer(
            "VFX_MeshIndDraw", gpuBuf.meshIndirectDrawArgs);

        // Per-instance color buffer (owned by `MeshRenderModule`, not
        // `GPUParticleBuffer`). Same import pattern as the transforms
        // buffer: the registry accounts for buffer decay to Common on submit,
        // and the graph transitions it to UAV before the mesh instance CS.
        // @ref phase0_vfx-mesh-particle-visibility-debug
        if (emitter->moduleStack.renderModule &&
            std::strcmp(emitter->moduleStack.renderModule->GetName(),
                        "mesh_render") == 0) {
            const auto* meshModule = static_cast<const MeshRenderModule*>(
                emitter->moduleStack.renderModule.get());
            const RHIBufferHandle instanceColors =
                meshModule->GetInstanceColorBuffer();
            if (instanceColors.IsValid()) {
                ids.meshInstanceColors = rgBuilder.ImportTrackedBuffer(
                    "VFX_MeshInstColors", instanceColors);
            }
        }

        importedIds.push_back(ids);
        // Silence "unused" warnings for state-vars referenced only when the
        // billboard-side imports above were live. They are kept so re-enabling
        // those imports later is a one-liner.
        (void)writeSetState;
        (void)readSetState;
        (void)aliveState;
        (void)indirectState;
    }


    // Simulation (spawn, update, indirect args) is dispatched inline by
    // DispatchVFXSimulation() in frame_vfx_inline.cpp.

    // ── Sort pass (compute) ──────────────────────────────────────────
    // Dispatches the bitonic merge sort for each active emitter so particles
    // are rendered in back-to-front order for correct alpha blending.
    // Runs after simulation (reads post-sim positions) and before rendering
    // (billboard VS reads sorted indices).

    // Sort pass disabled — particle buffers are not tracked by render graph.
    // Will re-enable when ping-pong + proper resource imports are restored.
    if (false) {
    const auto sortPipeline = ctx.pipelines->GetParticleSortCS();
    if (sortPipeline.IsValid()) {
        // Camera position and forward direction for depth-key generation.
        const Vec3 sortCamPos{
            ctx.perFrame.cameraPos.x,
            ctx.perFrame.cameraPos.y,
            ctx.perFrame.cameraPos.z
        };
        const Vec3 sortCamFwd = m_cameraManager->GetActiveCamera().GetForward();

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
        if (emitter->cullSuppress) continue; // phase 10.5.0 frustum cull gate
            if (emitter->needsInitialUpload) continue;

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
                                // Legacy disabled-RG path. Kept compilable for future
                                // re-enable, but the inline mesh sort dispatch in
                                // `frame_vfx_inline.cpp` is the live path.
                                GPUParticleSort::DispatchParams sp{};
                                sp.sortMode              = GpuSortMode::ViewDepth;
                                sp.keyGenCount           = d.aliveCount;
                                sp.cameraPos             = camPosArr;
                                sp.cameraForward         = camFwdArr;
                                sp.positionsSrvIndex     = d.positionsSrvIndex;
                                sp.agesSrvIndex          = d.positionsSrvIndex; // placeholder for compile
                                sp.lifespansSrvIndex     = d.positionsSrvIndex; // placeholder for compile
                                sp.customSortKeySrvIndex = 0xFFFFFFFFu;
                                d.sort->DispatchSort(
                                    m_device, cmd, sortPipeline,
                                    *m_cbRing, sp);
                            }
                        });
                });
        }
    }
    } // if (false) — sort pass disabled

    // ── Render pass (transparent phase) ────────────────────────────────
    // Draws particles as camera-facing billboards after opaque geometry
    // and lighting. Sorted back-to-front for correct alpha blending.

    // Select PSO based on the first emitter's blend mode.
    const auto alphaBlendPSO  = ctx.pipelines->GetParticleBillboard();
    const auto additivePSO    = ctx.pipelines->GetParticleBillboardAdditive();
    const auto premulAlphaPSO = ctx.pipelines->GetParticleBillboardPremulAlpha();

    RHIPipelineHandle activePipeline = alphaBlendPSO;
    for (auto* em : emitters) {
        if (!em || !em->initialized) continue;
        if (em->blendMode == 1 && additivePSO.IsValid()) activePipeline = additivePSO;
        else if (em->blendMode == 2 && premulAlphaPSO.IsValid()) activePipeline = premulAlphaPSO;
        break; // Use first emitter's blend mode for now
    }

    if (!activePipeline.IsValid()) return;

    ParticleRenderPassDesc renderDesc{};
    renderDesc.billboardPipeline  = activePipeline;
    renderDesc.colorTarget        = ctx.hdrRes;
    renderDesc.depthTarget        = ctx.depthRes;
    renderDesc.colorTexture       = m_hdrSceneColor;
    renderDesc.depthTexture       = m_gbuffer.depth;
    renderDesc.sceneDepthTexture  = m_gbuffer.depth;
    // Motion-vector MRT: bind the gbuffer velocity target so particles emit
    // per-pixel motion vectors consumed by TAA / DLSS / FSR. The render
    // graph transitions velocityRes from ShaderResource (previous passes) to
    // RenderTarget (this pass) and back to ShaderResource for post-process.
    // @ref UE5 NiagaraSpriteVertexFactory motion-vector output
    renderDesc.velocityTarget     = ctx.velocityRes;
    renderDesc.velocityTexture    = m_gbuffer.velocity;
    renderDesc.perFrameCB         = ctx.perFrameAlloc;
    renderDesc.renderWidth        = ctx.renderWidth;
    renderDesc.renderHeight       = ctx.renderHeight;
    renderDesc.viewportWidth      = m_width;
    renderDesc.viewportHeight     = m_height;
    renderDesc.perFrameAlloc      = ctx.perFrameAlloc;

    // Camera position for distance sorting (Vec4 -> Vec3 xyz).
    const Vec3 camPos{
        ctx.perFrame.cameraPos.x,
        ctx.perFrame.cameraPos.y,
        ctx.perFrame.cameraPos.z
    };

    // View and projection matrices for billboard orientation.
    const Mat4& viewMatrix = m_cameraManager->GetActiveCamera().GetView();
    const Mat4& projMatrix = m_cameraManager->GetActiveCamera().GetProjection();

    renderDesc.emitters.reserve(emitters.size());

    for (auto* emitter : emitters) {
        if (!emitter || !emitter->initialized) continue;
        if (emitter->cullSuppress) continue; // phase 10.5.0 frustum cull gate
        if (emitter->needsInitialUpload) continue; // Skip first-frame emitters

        // ── Skip mesh-render emitters in the billboard pass ──────────────
        // Mesh emitters render through `AddParticleMeshRenderPass` further
        // below (one full mesh per particle). Without this guard they
        // ALSO contributed 6 vertices each to the billboard batch as a
        // silent fallback — the RC3 disable of the mesh render pass made
        // those white quads the only visible geometry, masking the real
        // mesh draw. The hardware-validation log line `vtx=1200` for a
        // 200-particle mesh emitter (200 * 6 = 1200) was the smoking gun.
        // For mesh emitters we want billboard contribution = 0 vertices.
        if (emitter->moduleStack.renderModule &&
            std::strcmp(emitter->moduleStack.renderModule->GetName(),
                        "mesh_render") == 0) {
            continue;
        }

        auto& gpuBuf = emitter->buffer;
        if (!gpuBuf.IsValid()) continue;

        // Billboard constant buffer (b2): invView + emitter params + rendering params.
        // Must match VFXBillboardCB in particle_billboard_vs.hlsl byte-for-byte.
        VFXBillboardCB bbCBData{};

        // Inverse view matrix — rows 0,1 give camera right/up for billboard orientation.
        const Mat4 invView = viewMatrix.Inverse();
        std::memcpy(bbCBData.invView, &invView, sizeof(float) * 16);

        bbCBData.emitterPositionX = emitter->worldPosition.x;
        bbCBData.emitterPositionY = emitter->worldPosition.y;
        bbCBData.emitterPositionZ = emitter->worldPosition.z;
        // Size multiplier from sprite render module (width as scale factor).
        bbCBData.globalSizeMultiplier = 1.0f;
        if (emitter->moduleStack.renderModule) {
            auto* sr = dynamic_cast<SpriteRenderModule*>(emitter->moduleStack.renderModule.get());
            if (sr) bbCBData.globalSizeMultiplier = sr->spriteWidth;
        }

        // Global color tint (default white — no tinting).
        bbCBData.globalColorTintR = 1.0f;
        bbCBData.globalColorTintG = 1.0f;
        bbCBData.globalColorTintB = 1.0f;
        bbCBData.globalColorTintA = 1.0f;

        // Previous frame viewProj for TAA motion vectors.
        const Mat4& particlePrevViewProj = ctx.temporal.matrixValid
            ? ctx.temporal.prevViewProj : ctx.unjitteredViewProj;
        std::memcpy(bbCBData.prevViewProj, &particlePrevViewProj, sizeof(float) * 16);

        // ── PS rendering parameters ─────────────────────────────────────
        bbCBData.softFadeDistance      = emitter->softParticleFadeRange;
        bbCBData.emissiveIntensity     = emitter->emissiveIntensity;
        bbCBData.litMode               = emitter->litMode;

        // ── Camera offset & pivot ───────────────────────────────────────
        bbCBData.cameraOffset          = emitter->cameraOffset;
        bbCBData.pivotOffsetX          = emitter->pivotOffsetX;
        bbCBData.pivotOffsetY          = emitter->pivotOffsetY;

        // ── Size by speed ───────────────────────────────────────────────
        bbCBData.sizeBySpeedEnabled    = emitter->sizeBySpeedEnabled;
        bbCBData.sizeBySpeedMin        = emitter->sizeBySpeedMin;
        bbCBData.sizeBySpeedMax        = emitter->sizeBySpeedMax;
        bbCBData.speedRangeMax         = emitter->speedRangeMax;

        // ── Size over lifetime ──────────────────────────────────────────
        bbCBData.sizeOverLifeStart     = emitter->sizeOverLifeStart;
        bbCBData.sizeOverLifePeak      = emitter->sizeOverLifePeak;
        bbCBData.sizeOverLifeEnd       = emitter->sizeOverLifeEnd;
        bbCBData.sizeOverLifePeakTime  = emitter->sizeOverLifePeakTime;

        // ── Color over lifetime ─────────────────────────────────────────
        bbCBData.colorLifeStartR       = emitter->colorLifeStartR;
        bbCBData.colorLifeStartG       = emitter->colorLifeStartG;
        bbCBData.colorLifeStartB       = emitter->colorLifeStartB;
        bbCBData.colorLifeStartA       = emitter->colorLifeStartA;
        bbCBData.colorLifeEndR         = emitter->colorLifeEndR;
        bbCBData.colorLifeEndG         = emitter->colorLifeEndG;
        bbCBData.colorLifeEndB         = emitter->colorLifeEndB;
        bbCBData.colorLifeEndA         = emitter->colorLifeEndA;

        // ── Billboard facing + stretching ───────────────────────────────
        bbCBData.facingMode            = emitter->facingMode;
        bbCBData.customFacingX         = emitter->customFacingX;
        bbCBData.customFacingY         = emitter->customFacingY;
        bbCBData.customFacingZ         = emitter->customFacingZ;
        bbCBData.stretchFactor         = emitter->stretchFactor;
        bbCBData.blendModeHint         = emitter->blendMode;

        // ── Color by speed + screen-space size ──────────────────────────
        bbCBData.colorBySpeedEnabled   = emitter->colorBySpeedEnabled;
        bbCBData.screenSpaceSizeEnabled = emitter->screenSpaceSizeEnabled;
        bbCBData.screenSpacePixelSize  = emitter->screenSpacePixelSize;

        // ── Flipbook / SubUV animation ──────────────────────────────────
        bbCBData.flipbookRows          = emitter->flipbookRows;
        bbCBData.flipbookCols          = emitter->flipbookCols;
        bbCBData.flipbookTotalFrames   = emitter->flipbookTotalFrames;
        bbCBData.flipbookFPS           = emitter->flipbookFPS;
        bbCBData.spriteChannelMask     = emitter->spriteChannelMask;
        bbCBData.vfxTotalTime          = m_totalTime;

        const CBAllocation bbAlloc = m_cbRing->Allocate(sizeof(bbCBData));
        if (!bbAlloc.cpuPtr) {
            UZ_LOG(VFX, Warning,
                   "RegisterParticlePasses: CB ring full, skipping emitter render");
            continue;
        }
        std::memcpy(bbAlloc.cpuPtr, &bbCBData, sizeof(bbCBData));

        EmitterRenderData renderData{};
        // Per-emitter PSO so emitters with different blendModes render
        // with their own blend state in the same pass. Falls back to
        // alphaBlend if the chosen PSO isn't available.
        if (emitter->blendMode == 1 && additivePSO.IsValid()) {
            renderData.pipeline = additivePSO;
        } else if (emitter->blendMode == 2 && premulAlphaPSO.IsValid()) {
            renderData.pipeline = premulAlphaPSO;
        } else if (alphaBlendPSO.IsValid()) {
            renderData.pipeline = alphaBlendPSO;
        }
        renderData.buffer          = &gpuBuf;
        renderData.cpuAliveCount   = emitter->cpuAliveCount;
        renderData.renderCB        = bbAlloc;
        renderData.renderPriority  = emitter->renderPriority;
        // Draw ALL particles and let the VS cull dead ones.
        // The spawn shader allocates from the top of the free list (index N-1 down),
        // so alive particles are scattered across the full buffer range.
        renderData.cpuVertexCount  = gpuBuf.capacity * 6;

        // Bindless indices CB (b0) -- SRV indices for read-set attribute buffers
        // and PS texture indices. Must match HLSL VFXBillboardConstants (48 bytes).
        // Uses ParticleBillboardIndices struct from particle_render_pass.h.
        const auto& readSet = gpuBuf.WriteSet();
        ParticleBillboardIndices indicesData{};
        indicesData.positionBufferIdx    = m_device->GetBufferSRVIndex(readSet.positions);
        static bool logBB = false;
        if (!logBB) {
            UZ_LOG(VFX, Info, "Billboard posSRV={} readIdx={} writeIdx={}",
                   indicesData.positionBufferIdx, gpuBuf.readIndex, gpuBuf.writeIndex);
            logBB = true;
        }
        indicesData.sizeBufferIdx        = m_device->GetBufferSRVIndex(readSet.sizes);
        indicesData.colorBufferIdx       = m_device->GetBufferSRVIndex(readSet.colors);
        indicesData.rotationBufferIdx    = m_device->GetBufferSRVIndex(readSet.rotations);
        // BRING-UP: bypass sort indirection — particles weren't appearing
        // on hardware because sortedIndices[N] was returning unspawned slot
        // indices for the lower N range (spawn shader allocates from the
        // top of the free list, so alive particles cluster at high indices,
        // and the bitonic sort hadn't been validated end-to-end yet).
        // Identity mapping (UINT32_MAX sentinel) lets the VS iterate every
        // pool slot directly and the per-vertex `particleLife <= 0` cull
        // discards dead/uninitialized slots. Re-enable sort once the bitonic
        // pipeline's output has been visually verified.
        indicesData.sortedIndicesSrvIndex = UINT32_MAX;
        indicesData.velocityBufferIdx    = m_device->GetBufferSRVIndex(readSet.velocities);
        indicesData.agesBufferIdx        = m_device->GetBufferSRVIndex(readSet.ages);
        indicesData.lifespansBufferIdx   = m_device->GetBufferSRVIndex(readSet.lifespans);
        // Use the emitter's loaded sprite texture SRV, or UINT32_MAX for procedural circle.
        indicesData.spriteTextureIdx     = emitter->spriteTextureSrvIndex;
        indicesData.depthBufferIdx       = m_device->GetTextureSRVIndex(m_gbuffer.depth);
        // Use positions SRV as fallback — prevents ResourceDescriptorHeap[0] access
        // which corrupts the descriptor ring. The VS guards dynParams reads anyway.
        indicesData.dynamicParamsBufferIdx = indicesData.positionBufferIdx;
        // SubImageIndex SoA channel — VS reads per-particle float index written by update CS.
        // @ref Niagara SubImageIndex module — per-particle flipbook channel
        indicesData.subImageIndexBufferIdx = m_device->GetBufferSRVIndex(readSet.subImageIndices);

        const CBAllocation indicesAlloc = m_cbRing->Allocate(sizeof(indicesData));
        if (indicesAlloc.cpuPtr) {
            std::memcpy(indicesAlloc.cpuPtr, &indicesData, sizeof(indicesData));
            renderData.indicesCB = indicesAlloc;
        }

        // SM5 fallback path: bind descriptor table starting at the position buffer's
        // SRV index. The SM5 shader expects contiguous t0..t3 bindings.
        renderData.readSetSrvIndex = m_device->GetBufferSRVIndex(readSet.positions);

        // Distance to camera for back-to-front sorting.
        const Vec3 emitterPos = emitter->worldPosition;
        const Vec3 diff{
            emitterPos.x - camPos.x,
            emitterPos.y - camPos.y,
            emitterPos.z - camPos.z
        };
        renderData.distanceToCamera = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;

        // Only wire indirect draw args (the only resource tracked by the RG).
        // Particle attribute buffers are managed by the inline sim dispatch.
        for (const auto& imported : importedIds) {
            if (imported.buffer == &gpuBuf) {
                renderData.rgIndirectDrawArgs = imported.indirectDrawArgs;
                break;
            }
        }

        renderDesc.emitters.push_back(renderData);
    }

    if (!renderDesc.emitters.empty()) {
        static bool loggedAdd = false;
        if (!loggedAdd) {
            UZ_LOG(VFX, Info, "AddParticleRenderPass: {} emitters, vtx={}, pipeline={}",
                   renderDesc.emitters.size(),
                   renderDesc.emitters[0].cpuVertexCount,
                   renderDesc.billboardPipeline.IsValid());
            loggedAdd = true;
        }
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
                    // AddTrailRenderPass(rgBuilder, trailDesc); // DISABLED: RC3 — needs BeginRenderPass fix
                }
            }
        }
    }

    // ── Mesh particle instance CS + render pass ─────────────────────────────
    // For emitters using MeshRenderModule, dispatch the instance transform CS
    // (reads from the post-sim read set, writes instance transforms + indirect
    // args), then register the mesh render pass that consumes them.

    const auto meshInstanceCSPipeline = ctx.pipelines->GetParticleMeshInstanceCS();
    // Engine-fixed PSOs — kept as the fallback path when the per-emitter
    // material resolver fails. Materials that parse cleanly route through
    // `MaterialPipeline::GetParticleMeshPSO`; only parse / asset-load
    // failures fall back here. See the per-emitter resolution block below
    // for the full data flow.
    const auto meshOpaquePipelineFallback     = ctx.pipelines->GetParticleMeshOpaque();
    const auto meshTransparentPipelineFallback = ctx.pipelines->GetParticleMeshTransparent();

    // Per-frame `.uzmat` parse cache. Two emitters referencing the same
    // material share a single JSON parse — important when 200 mesh emitters
    // all use `materials/test_glass.uzmat` (the sort-pipeline test asset).
    // Entries are keyed by the resolved VFS-relative path; `cached.valid`
    // indicates whether the parse succeeded. Lifetime is the loop body.
    struct CachedMaterialDesc {
        ParticleMaterialDesc desc{};
        bool valid = false;
    };
    std::unordered_map<std::string, CachedMaterialDesc> uzmatCache;
    // Warning dedup — one log line per (emitter * materialPath) pair so a
    // misconfigured emitter that ticks for hundreds of frames does not
    // flood the log. The pair key collapses both inputs into a single
    // string and the same `unordered_set` is reused across the rest of
    // this frame.
    static std::unordered_set<std::string> warnedMaterialLoad;

    if (meshInstanceCSPipeline.IsValid()) {
        // CPU-side mirror of the VFXMeshInstanceConstants cbuffer in
        // particle_mesh_instance_cs.hlsl (64 bytes, 16-byte aligned rows).
        // Layout MUST match the HLSL `VFXMeshInstanceConstants` cbuffer in
        // particle_mesh_instance_cs.hlsl byte-for-byte. Earlier the CPU struct
        // ended at offset 64 while HLSL declared 80 bytes — `colorsSrvIndex`
        // and `instanceColorsUavIndex` were read as zero, so per-particle
        // mesh colors silently bound to whatever sat at descriptor heap
        // index 0. This struct is the corrected 80-byte layout.
        struct VFXMeshInstanceConstants {
            uint32_t aliveCount;                  //  0
            float    meshScaleX;                  //  4
            float    meshScaleY;                  //  8
            float    meshScaleZ;                  // 12
            uint32_t rotationMode;                // 16
            uint32_t meshIndexCount;              // 20
            uint32_t sortMode;                    // 24 MeshSortMode ordinal
            uint32_t sortedIndicesSrvIndex;       // 28 UINT32_MAX when sortMode==None
            uint32_t positionsSrvIndex;           // 32
            uint32_t velocitiesSrvIndex;          // 36
            uint32_t agesSrvIndex;                // 40
            uint32_t lifespansSrvIndex;           // 44
            uint32_t colorsSrvIndex;              // 48
            uint32_t _pad_srv;                    // 52
            uint32_t instanceTransformsUavIndex;  // 56
            uint32_t instanceCountUavIndex;       // 60
            uint32_t indirectArgsUavIndex;        // 64
            uint32_t instanceColorsUavIndex;      // 68
            uint32_t _pad2;                       // 72
            uint32_t _pad3;                       // 76
        };
        static_assert(sizeof(VFXMeshInstanceConstants) == 80,
                       "VFXMeshInstanceConstants must be exactly 80 bytes "
                       "to match particle_mesh_instance_cs.hlsl cbuffer layout");

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
            rg::RgResourceId rgMeshColors;
        };

        std::vector<MeshEmitterDispatch> meshDispatches;
        ParticleMeshPassDesc meshDesc{};
        meshDesc.colorTarget   = ctx.hdrRes;
        meshDesc.depthTarget   = ctx.depthRes;
        // Physical handles + render-resolution viewport for `BeginRenderPass`
        // — added in phase0_re-enable-mesh-particle-render-path-rc3 phase 3b
        // alongside the BeginRenderPass/SetViewport/SetScissor scaffolding
        // in particle_mesh_render_pass.cpp. Same fields the billboard pass
        // populates ~400 lines above (`renderDesc.colorTexture/...`).
        meshDesc.colorTexture  = m_hdrSceneColor;
        meshDesc.depthTexture  = m_gbuffer.depth;
        meshDesc.renderWidth   = ctx.renderWidth;
        meshDesc.renderHeight  = ctx.renderHeight;
        meshDesc.isTransparent = false;

        for (auto* emitter : emitters) {
            if (!emitter || !emitter->initialized) continue;
        if (emitter->cullSuppress) continue; // phase 10.5.0 frustum cull gate

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

            // ── Resolve `meshConfig.meshPath` → `GPUMesh` via SceneRenderer.
            // The renderer-owned mesh cache (`SceneRenderer::LoadMesh` +
            // `GetGPUMesh`) is idempotent and shared across all subsystems,
            // so 200 mesh emitters referencing the same `.uzmesh` upload
            // exactly once. An empty `meshPath` or a load failure produces
            // an invalid `GPUMesh*` — we still build the batch with invalid
            // buffer handles so `IsBatchItemRenderable()` skips it cleanly,
            // and we emit one Warning per emitter (not per frame) so the
            // log does not flood when a misconfigured emitter ticks for
            // hundreds of frames.
            const GPUMesh* gpuMesh = nullptr;
            if (ctx.sceneRenderer && !meshConfig.meshPath.empty()) {
                const uint32_t meshId = ctx.sceneRenderer->LoadMesh(
                    meshConfig.meshPath, ctx.vfs, ctx.cmdList);
                if (meshId != UINT32_MAX) {
                    gpuMesh = ctx.sceneRenderer->GetGPUMesh(meshId);
                }
            }
            if (!gpuMesh || !gpuMesh->IsValid() || !gpuMesh->IsIndexed()) {
                static std::unordered_set<const ParticleEmitter*> warnedMeshLoad;
                if (warnedMeshLoad.insert(emitter).second) {
                    UZ_LOG(VFX, Warning,
                           "RegisterParticlePasses: mesh emitter '{}' has no usable "
                           "GPU mesh for path='{}' (LoadMesh failed or mesh is "
                           "non-indexed) — skipping mesh draw for this emitter",
                           emitter->name.empty() ? "<unnamed>" : emitter->name.c_str(),
                           meshConfig.meshPath.empty() ? "<empty>"
                                                       : meshConfig.meshPath.c_str());
                }
                // Fall through with gpuMesh == nullptr; the batch below
                // is built with `Invalid()` handles and the index count
                // forwarded to the indirect-args CS stays zero, so the
                // mesh render pass treats the batch as a no-op once it
                // is re-enabled in phase 3.
            }

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
            // LOD0 index count from the resolved GPU mesh — drives the
            // `IndexCountPerInstance` field of the `DrawIndexedInstancedIndirect`
            // arg buffer that the instance CS writes (see
            // `particle_mesh_instance_cs.hlsl:314` /
            // `INDIRECT_INDEX_COUNT_OFFSET`). When the mesh failed to
            // resolve (`gpuMesh == nullptr`), we leave it zero so the
            // indirect draw issues zero indices per instance — the GPU
            // does no work and no garbled triangles spawn.
            instanceCBData.meshIndexCount =
                gpuMesh ? gpuMesh->indexCount : 0u;

            // Sort wiring — `DispatchVFXSort` runs inline before this pass
            // is registered, populating `gpuBuf.sort.sortIndices` for any
            // mesh emitter whose `MeshSortMode != None`. The instance CS
            // reads that buffer to translate logical sorted slot N back to
            // the source particle index, producing a back-to-front instance
            // transform stream for correct alpha blending.
            //
            // CustomAttribute mode: the dispatch path currently downgrades
            // to None when `customSortAttribute` is unset (section 4 of the
            // refactor task wires the user-attribute SRV resolver). Until
            // then, CustomAttribute behaves as None at runtime, but the
            // config field round-trips through `.uzfx` correctly.
            instanceCBData.sortMode = static_cast<uint32_t>(meshConfig.sortMode);
            instanceCBData.sortedIndicesSrvIndex =
                (gpuBuf.sort.IsValid() &&
                 meshConfig.sortMode != MeshSortMode::None)
                ? m_device->GetBufferSRVIndex(gpuBuf.sort.sortIndices)
                : UINT32_MAX;
            // Defensive: if the SRV is not actually bindable, downgrade to None
            // so the CS does not dereference an invalid descriptor.
            if (instanceCBData.sortedIndicesSrvIndex == UINT32_MAX) {
                instanceCBData.sortMode = static_cast<uint32_t>(MeshSortMode::None);
            }

            // Bindless SRV indices for the read-set particle attribute buffers.
            instanceCBData.positionsSrvIndex  = m_device->GetBufferSRVIndex(readSet.positions);
            instanceCBData.velocitiesSrvIndex = m_device->GetBufferSRVIndex(readSet.velocities);
            instanceCBData.agesSrvIndex       = m_device->GetBufferSRVIndex(readSet.ages);
            instanceCBData.lifespansSrvIndex  = m_device->GetBufferSRVIndex(readSet.lifespans);
            instanceCBData.colorsSrvIndex     = m_device->GetBufferSRVIndex(readSet.colors);
            instanceCBData._pad_srv           = 0;

            // Bindless UAV indices for the mesh output buffers.
            instanceCBData.instanceTransformsUavIndex =
                m_device->GetBufferUAVIndex(gpuBuf.meshInstanceTransforms);
            instanceCBData.instanceCountUavIndex =
                m_device->GetBufferUAVIndex(gpuBuf.meshInstanceCount);
            instanceCBData.indirectArgsUavIndex =
                m_device->GetBufferUAVIndex(gpuBuf.meshIndirectDrawArgs);

            // Per-instance vertex color buffer. The CS writes RGBA per
            // alive instance; the mesh PS reads it via SV_InstanceID. When
            // the MeshRenderModule has not allocated the instance-color
            // buffer (lifecycle issue, lazy init), fall back to the read
            // colors SRV so the descriptor index is at least valid — the
            // CS will overwrite it with the per-particle color anyway, so
            // the fallback only matters for the (rare) skipped frame.
            const RHIBufferHandle instanceColorBuf =
                meshModule->GetInstanceColorBuffer();
            instanceCBData.instanceColorsUavIndex = instanceColorBuf.IsValid()
                ? m_device->GetBufferUAVIndex(instanceColorBuf)
                : 0u;
            instanceCBData._pad2 = 0;
            instanceCBData._pad3 = 0;

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
            dispatch.rgMeshColors     = importedPtr->meshInstanceColors;
            meshDispatches.push_back(dispatch);

            // Build the mesh render pass batch item for this emitter.
            // The CB carries only the two bindless descriptor indices —
            // viewProjection is read from PerFrameCB at b1 by the VS, so
            // we don't repeat it here (the b0 root-constants slot is
            // capped at 16 DWORDs / 64 bytes by the bindless root sig).
            ParticleMeshPassConstants meshPassCB{};
            meshPassCB.instanceTransformsBufferIdx =
                m_device->GetBufferSRVIndex(gpuBuf.meshInstanceTransforms);
            meshPassCB.instanceColorsBufferIdx =
                meshModule->GetInstanceColorBuffer().IsValid()
                    ? m_device->GetBufferSRVIndex(meshModule->GetInstanceColorBuffer())
                    : UINT32_MAX;

            const CBAllocation meshPassAlloc = m_cbRing->Allocate(sizeof(meshPassCB));
            if (!meshPassAlloc.cpuPtr) {
                UZ_LOG(VFX, Warning,
                       "RegisterParticlePasses: CB ring full, skipping mesh render batch");
                continue;
            }
            std::memcpy(meshPassAlloc.cpuPtr, &meshPassCB, sizeof(meshPassCB));

            // ── Resolve `MeshRenderModuleConfig::materialPath` → PSO ──────
            // The per-emitter material drives the PSO via
            // `MaterialPipeline::GetParticleMeshPSO`, which caches by the
            // (shadingModel, blendMode, twoSided, layout) tuple — identical
            // tuples share a PSO so 200 emitters using the same material
            // produce exactly one PSO compile. On parse / load failure we
            // fall back to the engine-fixed `GetParticleMeshOpaque` PSO and
            // emit one warning per (emitter × material path) pair.
            //
            // The `.uzmat` is read via `ctx.vfs->Read` — the existing
            // `MaterialManager` produces a `MaterialGraph` but does not
            // currently parse the JSON's top-level `twoSided` / `blendMode`
            // fields, so we extract them here directly. The parse result is
            // cached per-frame in `uzmatCache` so duplicate paths cost a
            // single read + parse regardless of emitter count.
            ParticleMaterialDesc matDesc{};
            bool materialResolved = false;
            const std::string& materialPath = meshConfig.materialPath;

            if (!materialPath.empty()) {
                auto cacheIt = uzmatCache.find(materialPath);
                if (cacheIt == uzmatCache.end()) {
                    CachedMaterialDesc cached{};
                    if (ctx.vfs && ctx.vfs->Exists(materialPath)) {
                        const auto bytes = ctx.vfs->Read(materialPath);
                        if (!bytes.empty()) {
                            try {
                                const auto root = nlohmann::json::parse(
                                    bytes.begin(), bytes.end(),
                                    /*cb=*/nullptr, /*allow_exceptions=*/true,
                                    /*ignore_comments=*/true);
                                // shadingModel — string keyed in `.uzmat`,
                                // mapped to the renderer's EShadingModel.
                                // The graph-style enum
                                // (`MaterialShadingModel`) only spells out
                                // DefaultLit / Unlit / Subsurface; everything
                                // else falls back to DefaultLit which is
                                // exactly what the particle-mesh PS
                                // currently implements.
                                const std::string sm =
                                    root.value("shadingModel", "DefaultLit");
                                if (sm == "Unlit") {
                                    cached.desc.shadingModel = EShadingModel::Unlit;
                                } else if (sm == "Subsurface") {
                                    cached.desc.shadingModel = EShadingModel::Subsurface;
                                } else {
                                    cached.desc.shadingModel = EShadingModel::DefaultLit;
                                }
                                // blendMode — optional; default Opaque.
                                const std::string bm =
                                    root.value("blendMode", "Opaque");
                                if (bm == "Masked") {
                                    cached.desc.blendMode = EBlendMode::Masked;
                                } else if (bm == "Translucent") {
                                    cached.desc.blendMode = EBlendMode::Translucent;
                                } else if (bm == "Additive") {
                                    cached.desc.blendMode = EBlendMode::Additive;
                                } else if (bm == "Modulate") {
                                    cached.desc.blendMode = EBlendMode::Modulate;
                                } else {
                                    cached.desc.blendMode = EBlendMode::Opaque;
                                }
                                cached.desc.twoSided = root.value("twoSided", false);
                                // FNV-1a 64-bit over the path bytes, used
                                // only for warning-dedup keying.
                                uint64_t hash = 1469598103934665603ull;
                                for (char c : materialPath) {
                                    hash ^= static_cast<uint64_t>(static_cast<uint8_t>(c));
                                    hash *= 1099511628211ull;
                                }
                                cached.desc.materialPathHash = hash;
                                cached.valid = true;
                            } catch (const std::exception& e) {
                                UZ_LOG(VFX, Warning,
                                       "MeshRender: material '{}' failed to parse "
                                       "({}) — falling back to opaque PSO",
                                       materialPath, e.what());
                            }
                        }
                    }
                    cacheIt = uzmatCache.emplace(materialPath, cached).first;
                }
                if (cacheIt->second.valid) {
                    matDesc = cacheIt->second.desc;
                    materialResolved = true;
                }
            }

            // Resolve the PSO. Today only one vertex layout is supported
            // (`VfxMesh_PosNormalUV`); `MeshVertexLayout` is wired through
            // so future per-mesh layout variants ride the same cache key.
            RHIPipelineHandle meshPipeline = RHIPipelineHandle::Invalid();
            if (materialResolved && m_materialPipeline) {
                meshPipeline = m_materialPipeline->GetParticleMeshPSO(
                    matDesc, MeshVertexLayout::VfxMesh_PosNormalUV);
            }

            // ── Fallback path ─────────────────────────────────────────
            // Material missing / failed to parse / resolver returned
            // Invalid — fall back to the engine-fixed opaque PSO. We log
            // exactly once per (emitter × materialPath) pair so the
            // warning cannot flood the frame log.
            if (!meshPipeline.IsValid()) {
                std::string warnKey;
                warnKey.reserve(materialPath.size() + 24);
                warnKey.append(materialPath);
                warnKey.push_back('|');
                // emitter pointer as hex — stable for the lifetime of the
                // emitter object; collisions across re-spawned emitters
                // would only re-emit a warning that is still useful.
                char ptrBuf[24];
                std::snprintf(ptrBuf, sizeof(ptrBuf), "%p",
                              static_cast<const void*>(emitter));
                warnKey.append(ptrBuf);

                if (warnedMaterialLoad.insert(warnKey).second) {
                    UZ_LOG(VFX, Warning,
                           "MeshRender: material '{}' failed to load — "
                           "falling back to opaque PSO (emitter='{}')",
                           materialPath.empty() ? "<empty>" : materialPath.c_str(),
                           emitter->name.empty() ? "<unnamed>" : emitter->name.c_str());
                }
                meshPipeline = meshOpaquePipelineFallback.IsValid()
                    ? meshOpaquePipelineFallback
                    : meshTransparentPipelineFallback;
            }

            // Skip mesh particle rendering if even the fallback PSO failed
            // to compile — there is nothing renderable left for this batch.
            if (!meshPipeline.IsValid()) {
                continue;
            }

            ParticleMeshBatchItem batch{};
            batch.pipeline         = meshPipeline;
            // Forward the resolved LOD0 GPU buffers + index format. When
            // the mesh failed to resolve (warning emitted above), both
            // handles stay invalid; the consuming render pass's
            // `IsBatchItemRenderable()` check (in
            // `particle_mesh_render_pass.cpp`) skips the batch silently.
            // Cached once per `meshPath` by `SceneRenderer::LoadMesh` —
            // no per-frame upload cost.
            batch.meshVertexBuffer = gpuMesh ? gpuMesh->vertexBuffer
                                             : RHIBufferHandle::Invalid();
            batch.meshIndexBuffer  = gpuMesh ? gpuMesh->indexBuffer
                                             : RHIBufferHandle::Invalid();
            batch.meshIndexFormat  = gpuMesh ? gpuMesh->indexFormat
                                             : RHIIndexFormat::Uint16;
            batch.instanceBufferSrvIndex =
                m_device->GetBufferSRVIndex(gpuBuf.meshInstanceTransforms);
            batch.materialTexturesSrvIndex = UINT32_MAX;
            batch.indirectArgsBuffer   = gpuBuf.meshIndirectDrawArgs;
            // Render graph id for the same buffer above — drives the
            // automatic UAV→IndirectArgument barrier between
            // VFX_MeshInstanceCS and VFX_ParticleMeshRender. Without
            // this id set the debug layer raises CORRUPTED_PARAMETER on
            // `DrawIndexedIndirect` because the buffer is still in
            // UnorderedAccess state from the preceding compute write.
            batch.indirectArgsBufferRg = importedPtr->meshIndirectDrawArgs;
            // Render graph IDs for the per-instance transform + color
            // buffers. Without these the auto-barrier scheduler leaves
            // the buffers in `UnorderedAccess` after the mesh-instance
            // CS write, the bindless SRV reads in the VS / PS return
            // garbage / NaN, and the rasterizer produces clip-space
            // spanning triangles — visually blanking the HDR composite
            // to black with normal lighting.
            // @ref phase0_vfx-mesh-particle-visibility-debug
            batch.instanceTransformsBufferRg = importedPtr->meshInstanceTransforms;
            batch.instanceColorsBufferRg     = importedPtr->meshInstanceColors;
            batch.passCB               = meshPassAlloc;

            meshDesc.batches.push_back(batch);
        }

        // Register the mesh instance CS compute pass if any mesh emitters exist.
        if (!meshDispatches.empty()) {
            rgBuilder.AddPass("VFX_MeshInstanceCS",
                [meshInstanceCSPipeline,
                 dispatches = std::move(meshDispatches)]
                (rg::RgPassBuilder& pb) {
                    // Declare resource access for barrier tracking.
                    //
                    // The read-set particle attribute buffers (positions,
                    // velocities, ages, lifespans) are accessed bindlessly
                    // via `ResourceDescriptorHeap[]` inside the CS — they
                    // are not imported into the render graph (the inline
                    // sim pre-RG manages their state) and so their
                    // `RgResourceId`s here are intentionally `Invalid()`.
                    // Skip the `pb.Read` declarations for those — the CB
                    // carries the descriptor indices and the graph never
                    // needs to track those resources. Only the mesh
                    // output buffers (transforms / count / indirect args),
                    // which the mesh render pass consumes downstream, get
                    // a tracked Write here so the auto-barrier scheduler
                    // emits the UAV→IndirectArgument transition for the
                    // indirect-args buffer.
                    for (const auto& d : dispatches) {
                        if (d.rgReadPositions.IsValid())
                            pb.Read(d.rgReadPositions,  RHIResourceState::ShaderResource);
                        if (d.rgReadVelocities.IsValid())
                            pb.Read(d.rgReadVelocities, RHIResourceState::ShaderResource);
                        if (d.rgReadAges.IsValid())
                            pb.Read(d.rgReadAges,       RHIResourceState::ShaderResource);
                        if (d.rgReadLifespans.IsValid())
                            pb.Read(d.rgReadLifespans,  RHIResourceState::ShaderResource);
                        if (d.rgMeshTransforms.IsValid())
                            pb.Write(d.rgMeshTransforms,  RHIResourceState::UnorderedAccess);
                        if (d.rgMeshCount.IsValid())
                            pb.Write(d.rgMeshCount,       RHIResourceState::UnorderedAccess);
                        if (d.rgMeshIndirectArgs.IsValid())
                            pb.Write(d.rgMeshIndirectArgs, RHIResourceState::UnorderedAccess);
                        if (d.rgMeshColors.IsValid())
                            pb.Write(d.rgMeshColors, RHIResourceState::UnorderedAccess);
                    }

                    pb.SetExecutionCallback(
                        [meshInstanceCSPipeline, dispatches]
                        (IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
                            UZ_PROFILE_SCOPE("VFX_MeshInstanceCS");
                            cmd->SetPipeline(meshInstanceCSPipeline);

                            for (size_t i = 0; i < dispatches.size(); ++i) {
                                const auto& d = dispatches[i];

                                // Slot 1 (NOT 0) — slot 0 is RootConstants in
                                // the bindless RS (dx12_pso_creation.cpp:182-187).
                                // The mesh-instance CS's cbuffer is at register(b1).
                                cmd->SetRootCBV(1,
                                    d.instanceCB.bufferHandle,
                                    d.instanceCB.offsetBytes,
                                    d.instanceCB.cpuPtr,
                                    d.instanceCB.sizeBytes);

                                cmd->Dispatch(d.dispatchGroupsX, 1, 1);

                                // UAV barrier between emitter dispatches to
                                // prevent write-after-write hazards on the
                                // mesh output buffers.
                                // Global UAV barrier between mesh instance CS dispatches.
                                // No resource handle = global UAV flush (DX12 null resource).
                                // Disabled: render graph handles inter-pass barriers.
                                // if (i + 1 < dispatches.size()) {
                                //     RHIBarrierDesc barrier{};
                                //     barrier.stateBefore = RHIResourceState::UnorderedAccess;
                                //     barrier.stateAfter  = RHIResourceState::UnorderedAccess;
                                //     cmd->ResourceBarrier(&barrier, 1);
                                // }
                            }
                        });
                });
        }

        // Register the mesh render pass if any batch items were created.
        if (!meshDesc.batches.empty()) {
            // Re-enabled in phase0_re-enable-mesh-particle-render-path-rc3 —
            // BeginRenderPass + viewport scaffolding added in
            // particle_mesh_render_pass.cpp; physical RHI handles +
            // render-resolution viewport are populated on `meshDesc` above.
            AddParticleMeshRenderPass(rgBuilder, meshDesc);
        }
    }

    // hasCompletedCycle is now set by DispatchVFXSimulation() (frame_vfx_inline.cpp).
}

} // namespace uz
