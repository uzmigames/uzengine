/// UzEngine Scene Renderer -- Inline VFX compute dispatch (spawn + update + indirect args).
///
/// Dispatches particle simulation as inline compute on the main command list,
/// BEFORE the render graph. This matches the standalone VFX demo's dispatch
/// pattern which works correctly with thousands of particles, unlike the
/// render-graph-based simulation path which only produces 1 visible particle.
///
/// The render graph still handles rendering (billboard draw) and the necessary
/// UAV -> SRV / IndirectArgument barrier transitions for the draw pass.

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

#include "pipeline_library.h"
#include "profiler.h"
#include "log.h"

#include "engine/renderer/passes/vfx/particle_simulation_pass.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/core/gpu_particle_buffer.h"
#include "engine/vfx/core/gpu_particle_sort.h"
#include "engine/vfx/lighting/vfx_ddgi_emissive_feed.h"
#include "engine/vfx/lighting/vfx_ddgi_injection.h"
#include "engine/vfx/modules/mesh_render_module.h"
#include "engine/core/cvar.h"

#include <algorithm>
#include <cstring>
#include <cmath>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ── Thread group sizes (must match HLSL [numthreads]) ──────────────────────
static constexpr uint32_t kSpawnGroupSize  = 256;
static constexpr uint32_t kUpdateGroupSize = 256;

/// Emits a UAV-to-UAV barrier on a single buffer.
///
/// A UAV barrier (same state before/after = UAV) flushes all pending UAV
/// writes so they are visible to subsequent dispatches. This is required
/// between spawn -> update and update -> indirect args to ensure atomic
/// counters (aliveCount, freeListHead) written by one pass are visible to
/// the next.
static void EmitUAVBarrier(IRHICommandList* cmd, RHIBufferHandle buffer) {
    RHIBarrierDesc barrier{};
    barrier.buffer      = buffer;
    barrier.stateBefore = RHIResourceState::UnorderedAccess;
    barrier.stateAfter  = RHIResourceState::UnorderedAccess;
    cmd->ResourceBarrier(&barrier, 1);
}

/// Transitions indirect argument buffers from UAV to IndirectArgument state.
///
/// After the indirect args CS writes the draw/dispatch args, the GPU needs
/// these buffers in IndirectArgument state for ExecuteIndirect / DrawIndirect.
static void TransitionIndirectArgs(IRHICommandList* cmd, const GPUParticleBuffer& buffer) {
    RHIBarrierDesc barriers[2] = {};
    uint32_t count = 0;

    if (buffer.indirectDrawArgs.IsValid()) {
        barriers[count].buffer      = buffer.indirectDrawArgs;
        barriers[count].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[count].stateAfter  = RHIResourceState::IndirectArgument;
        ++count;
    }

    if (buffer.indirectDispatchArgs.IsValid()) {
        barriers[count].buffer      = buffer.indirectDispatchArgs;
        barriers[count].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[count].stateAfter  = RHIResourceState::IndirectArgument;
        ++count;
    }

    if (count > 0) {
        cmd->ResourceBarrier(barriers, count);
    }
}

void SceneRenderer::DispatchVFXSimulation(IRHICommandList* cmdList,
                                           float dt, uint32_t frameIndex) {
    UZ_PROFILE_FUNCTION();

    // Resolve active VFX system (override takes precedence over owned).
    VFXSystem* activeVFX = m_vfxSystemOverride ? m_vfxSystemOverride : m_vfxSystem.get();
    if (!activeVFX || !activeVFX->IsInitialized()) return;

    const auto& emitters = activeVFX->GetActiveEmitters();
    if (emitters.empty()) return;

    // ── Apply global VFX scalability (sampled by VFXSystem::Tick) ───────
    // `r.VFX.SimulatePaused` short-circuits dt to zero so existing GPU
    // particles freeze in place while the render path keeps drawing them.
    // `r.VFX.TimeScale` then multiplies the (already-paused-or-not) dt
    // before the per-emitter `timeScale` and `isPaused` filters apply.
    // This ordering lets a sequencer pause everything globally and still
    // run a specific emitter via direct C API control if the caller
    // resets the per-emitter `isPaused`.
    float globalDt = dt;
    if (activeVFX->IsSimulationPaused()) {
        globalDt = 0.0f;
    } else {
        globalDt *= activeVFX->GetTimeScale();
    }

    // `r.VFX.AllowGPUSim` — when 0 we skip the GPU compute dispatch
    // entirely. The condition gates the early-out below so the rest of
    // the function stays unchanged for the common (allowed) case.
    if (!activeVFX->IsGPUSimAllowed()) {
        static bool warnedGPUSimDisabled = false;
        if (!warnedGPUSimDisabled) {
            UZ_LOG(VFX, Warning,
                   "r.VFX.AllowGPUSim=0: skipping GPU particle simulation. "
                   "Existing alive particles will continue to render but "
                   "will not advance. There is no CPU sim fallback yet — "
                   "this is intended for compute-less platforms / debug.");
            warnedGPUSimDisabled = true;
        }
        return;
    }

    // Pipeline state objects for the three compute shaders.
    const auto spawnPSO        = m_pipelines->GetParticleSpawnCS();
    const auto updatePSO       = m_pipelines->GetParticleUpdateCS();
    const auto indirectArgsPSO = m_pipelines->GetParticleIndirectArgsCS();

    if (!spawnPSO.IsValid() || !updatePSO.IsValid() || !indirectArgsPSO.IsValid()) {
        return;
    }

    // Replace `dt` for the rest of the function with the scaled value so
    // every subsequent reference (spawn count, update CB, etc.) sees the
    // post-scalability dt without further plumbing.
    dt = globalDt;

    // Lazy-create a dummy dynParams buffer for emitters that don't have one.
    // This buffer is written by spawn/update shaders as RWStructuredBuffer<float4>.
    // Using a separate buffer avoids corrupting positions or sizes.
    if (!m_vfxDummyDynParams.IsValid()) {
        RHIBufferDesc dummyDesc{};
        dummyDesc.sizeBytes = 16 * 16384; // 16384 float4 entries (covers max emitter capacity)
        dummyDesc.stride = 16;           // sizeof(float4)
        dummyDesc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        dummyDesc.memory = RHIMemoryType::Default;
        dummyDesc.debugName = "VFX_DummyDynParams";
        m_vfxDummyDynParams = m_device->CreateBuffer(dummyDesc);
    }

    // Lazy-create kill volume buffer (16 KillVolumes × 32 bytes).
    if (!m_vfxKillVolumeBuffer.IsValid()) {
        RHIBufferDesc kvDesc{};
        kvDesc.sizeBytes = 16 * 32;
        kvDesc.stride = 32;
        kvDesc.usage = RHIBufferUsage::StructuredBuffer;
        kvDesc.memory = RHIMemoryType::Default;
        kvDesc.debugName = "VFX_KillVolumes";
        m_vfxKillVolumeBuffer = m_device->CreateBuffer(kvDesc);
    }

    // Lazy-create death event buffer + counter (4096 events × 32 bytes).
    if (!m_vfxDeathEventBuffer.IsValid()) {
        RHIBufferDesc deDesc{};
        deDesc.sizeBytes = 4096 * 32;
        deDesc.stride = 32;
        deDesc.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess;
        deDesc.memory = RHIMemoryType::Default;
        deDesc.debugName = "VFX_DeathEvents";
        m_vfxDeathEventBuffer = m_device->CreateBuffer(deDesc);

        deDesc.sizeBytes = sizeof(uint32_t);
        deDesc.stride = sizeof(uint32_t);
        deDesc.debugName = "VFX_DeathEventCount";
        m_vfxDeathEventCountBuffer = m_device->CreateBuffer(deDesc);
    }

    if (m_gpuProfileEnabled) {
        m_device->GpuProfilerBeginTimestamp(cmdList, "VFX_InlineSimulation");
    }

    for (auto* emitter : emitters) {
        if (!emitter || !emitter->initialized) continue;

        auto& gpuBuf = emitter->buffer;
        if (!gpuBuf.IsValid()) continue;

        // Ping-pong swap: after swap, WriteSet has the buffers that were ReadSet
        // last frame. Spawn writes to WriteSet, Update reads ReadSet → writes WriteSet.
        // Match standalone: do NOT swap. Always use read=0, write=1.
        // gpuBuf.Swap();
        // Force fixed assignment matching standalone demo (m_pingPong=0 never changes).
        gpuBuf.readIndex = 0;
        gpuBuf.writeIndex = 1;

        // ── Deferred GPU buffer initialization cleanup ────────────────
        // Staging buffers from InitializeData must survive until the GPU
        // finishes executing the CopyBuffer commands. With triple-buffered
        // frame pacing, frame 0's GPU work may still be in flight when
        // frame 1 starts. D3D12MA frees heap memory immediately on
        // Allocation::Release(), corrupting in-flight copies.
        // Wait kFramesInFlight (3) frames before destroying.
        constexpr uint32_t kFramesInFlight = 3;
        if (!gpuBuf.needsInitialization && gpuBuf.freeListStaging.IsValid()
            && gpuBuf.initFrameIndex != UINT32_MAX
            && frameIndex >= gpuBuf.initFrameIndex + kFramesInFlight) {
            m_device->DestroyBuffer(gpuBuf.freeListStaging);
            gpuBuf.freeListStaging = RHIBufferHandle::Invalid();
            if (gpuBuf.freeListHeadStaging.IsValid()) {
                m_device->DestroyBuffer(gpuBuf.freeListHeadStaging);
                gpuBuf.freeListHeadStaging = RHIBufferHandle::Invalid();
            }
            if (gpuBuf.zeroStagingBuffer.IsValid()) {
                m_device->DestroyBuffer(gpuBuf.zeroStagingBuffer);
                gpuBuf.zeroStagingBuffer = RHIBufferHandle::Invalid();
            }
            // Keep aliveCountStaging for per-frame reset!
        }

        // Upload initial data if this buffer has never been initialized.
        if (gpuBuf.needsInitialization) {
            if (!gpuBuf.InitializeData(m_device, cmdList)) {
                UZ_LOG(VFX, Error,
                       "DispatchVFXSimulation: failed to initialize GPU buffer for emitter");
                continue;
            }
            // Record which frame InitializeData ran so staging buffers are
            // deferred for destruction by kFramesInFlight frames.
            gpuBuf.initFrameIndex = frameIndex;

            // Clear the merge's upload flag so RegisterParticlePasses doesn't skip this emitter.
            emitter->needsInitialUpload = false;

#if defined(UZ_VFX_DEBUG_READBACK)
            // Diagnostic: GPU readback of freeListHead after InitializeData.
            //
            // Verifies that the Upload→Default CopyBuffer actually delivered
            // the initial value `capacity` to freeListHead on the GPU. If the
            // readback reports a different value, the staging buffer was
            // destroyed before the GPU finished executing CopyBuffer, which
            // is the root cause this task was created to fix (the "only 2-3
            // particles visible" bug). With the deferred-destroy fix in
            // place (tasks 1.1–1.4) this readback MUST report `capacity`.
            //
            // @ref phase0_fix-vfx-spawn-multi-particle task 2
            // Gated by UZ_VFX_DEBUG_READBACK so release builds pay zero
            // runtime cost. Enable via `-DUZ_VFX_DEBUG_READBACK=1` in the
            // build to re-verify the fix.
            {
                RHIBufferDesc rbDesc{};
                rbDesc.sizeBytes = sizeof(uint32_t);
                rbDesc.usage     = RHIBufferUsage::CopyDst | RHIBufferUsage::ReadBack;
                rbDesc.debugName = "VFX_FreeListHead_Readback";
                RHIBufferHandle rb = m_device->CreateBuffer(rbDesc);
                if (rb.IsValid()) {
                    RHIBarrierDesc b{};
                    b.buffer      = gpuBuf.freeListHead;
                    b.stateBefore = RHIResourceState::CopyDst;
                    b.stateAfter  = RHIResourceState::CopySrc;
                    cmdList->ResourceBarrier(&b, 1);

                    cmdList->CopyBuffer(gpuBuf.freeListHead, 0, rb, 0, sizeof(uint32_t));

                    b.buffer      = gpuBuf.freeListHead;
                    b.stateBefore = RHIResourceState::CopySrc;
                    b.stateAfter  = RHIResourceState::UnorderedAccess;
                    cmdList->ResourceBarrier(&b, 1);

                    // NOTE: Full fence-wait + map omitted for simplicity —
                    // the user is expected to break in PIX/RenderDoc and
                    // inspect the readback buffer manually. A full blocking
                    // validation would require a device->WaitIdle here,
                    // which stalls the frame. Leave for build-time-only.
                    m_device->DestroyBuffer(rb);
                }
            }
#endif // UZ_VFX_DEBUG_READBACK
        }

        // ── Reset aliveCount to 0 each frame ──────────────────────────
        // The spawn and update shaders atomically increment aliveCount.
        // Without resetting to 0 each frame, the counter grows unbounded
        // and the free list eventually exhausts (particles stop spawning).
        // This matches the standalone demo's per-frame aliveCount reset.
        // Reset aliveCount to 0 via CopyBuffer (matches standalone pattern).
        // DX12 implicit state promotion handles Common→CopyDst for the copy.
        // CopyDst→UAV barrier after so spawn CS can InterlockedAdd.
        if (gpuBuf.aliveCountStaging.IsValid()) {
            cmdList->CopyBuffer(gpuBuf.aliveCountStaging, 0,
                                gpuBuf.aliveCount, 0, sizeof(uint32_t));
            RHIBarrierDesc b{};
            b.buffer = gpuBuf.aliveCount;
            b.stateBefore = RHIResourceState::CopyDst;
            b.stateAfter  = RHIResourceState::UnorderedAccess;
            cmdList->ResourceBarrier(&b, 1);
        }

        // ── Per-emitter dt (pause + time-scale) ────────────────────────
        // Apply the per-emitter `isPaused` and `timeScale` after the global
        // `r.VFX.SimulatePaused` / `r.VFX.TimeScale` filters at the top of
        // this function. A paused emitter sees dt=0 (alive particles freeze
        // in place); time-scale scales the residual dt linearly. The same
        // value is consumed by `CalculateSpawnCount` (spawn rate) and the
        // update CS (age / position advance), so a slow-mo emitter is
        // visually slow-mo across both spawn AND update.
        float emitterDt = emitter->isPaused ? 0.0f : (dt * emitter->timeScale);

        // ── Spawn dispatch ─────────────────────────────────────────────
        // Check start delay for compound effects (delayed sub-emitters).
        const bool delayActive = emitter->startDelay > 0.0f
                                && emitter->timeSinceRegistration < emitter->startDelay;
        const uint32_t spawnCount = delayActive ? 0 : emitter->CalculateSpawnCount(emitterDt);

        // cpuAliveCount is already updated inside CalculateSpawnCount().

        ParticleSpawnConstants spawnCBData{};

        // Emitter parameters from ParticleEmitter config.
        spawnCBData.emitterPos[0]     = emitter->worldPosition.x;
        spawnCBData.emitterPos[1]     = emitter->worldPosition.y;
        spawnCBData.emitterPos[2]     = emitter->worldPosition.z;
        spawnCBData.spawnRadius       = emitter->spawnRadius;
        spawnCBData.spawnVelocity[0]  = emitter->spawnVelocity.x;
        spawnCBData.spawnVelocity[1]  = emitter->spawnVelocity.y;
        spawnCBData.spawnVelocity[2]  = emitter->spawnVelocity.z;
        spawnCBData.velocityVariation = emitter->velocityVariation;

        constexpr float kDegToRad = 3.14159265358979f / 180.0f;
        spawnCBData.spreadAngle       = emitter->spreadAngle * kDegToRad;

        const float minLife = (emitter->lifetime - emitter->lifetimeVariation > 0.01f)
                            ? (emitter->lifetime - emitter->lifetimeVariation)
                            : 0.01f;
        spawnCBData.minLifetime       = minLife;
        spawnCBData.maxLifetime       = emitter->lifetime;
        spawnCBData.initialSize       = emitter->initialSize;

        spawnCBData.initialColor[0]   = emitter->initialColor.x;
        spawnCBData.initialColor[1]   = emitter->initialColor.y;
        spawnCBData.initialColor[2]   = emitter->initialColor.z;
        spawnCBData.initialColor[3]   = emitter->initialColor.w;

        spawnCBData.dt                = emitterDt;
        spawnCBData.frameNumber       = frameIndex;
        spawnCBData.spawnCount        = spawnCount;
        spawnCBData.maxParticles      = gpuBuf.capacity;

        // Bindless descriptor indices for write-set attribute buffers.
        const auto& writeSet = gpuBuf.WriteSet();
        spawnCBData.freeListSrvIndex       = m_device->GetBufferSRVIndex(gpuBuf.freeList);
        spawnCBData.positionsUavIndex       = m_device->GetBufferUAVIndex(writeSet.positions);
        spawnCBData.velocitiesUavIndex      = m_device->GetBufferUAVIndex(writeSet.velocities);
        spawnCBData.agesUavIndex            = m_device->GetBufferUAVIndex(writeSet.ages);
        spawnCBData.lifespansUavIndex       = m_device->GetBufferUAVIndex(writeSet.lifespans);
        spawnCBData.sizesUavIndex           = m_device->GetBufferUAVIndex(writeSet.sizes);
        spawnCBData.colorsUavIndex          = m_device->GetBufferUAVIndex(writeSet.colors);
        spawnCBData.rotationsUavIndex       = m_device->GetBufferUAVIndex(writeSet.rotations);
        spawnCBData.subImageIndicesUavIndex = m_device->GetBufferUAVIndex(writeSet.subImageIndices);
        spawnCBData.freeListHeadUavIndex    = m_device->GetBufferUAVIndex(gpuBuf.freeListHead);
        spawnCBData.aliveCountUavIndex      = m_device->GetBufferUAVIndex(gpuBuf.aliveCount);

        // Spawn shape defaults (point emitter).
        spawnCBData.spawnShape        = emitter->spawnShape;
        spawnCBData.spawnOnSurface    = 0;

        // Flipbook total frames for SubImageIndex computation in spawn shader.
        spawnCBData.flipbookTotalFrames = emitter->flipbookTotalFrames;

        // Dynamic params UAV -- use positions UAV as fallback to prevent
        // writes to descriptor 0 (which corrupts the descriptor ring).
        // Use dedicated dummy buffer — CANNOT use positions/sizes (spawn
        // writes dynamicParams[idx] which would overwrite that attribute).
        spawnCBData.dynamicParamsUavIndex = m_device->GetBufferUAVIndex(m_vfxDummyDynParams);

        // User-declared per-particle attributes (Niagara "Parameters" parity).
        // Each declared slot publishes a bindless UAV to the spawn CS, which
        // writes the declared default into every newly allocated particle.
        for (const auto& ua : emitter->userAttributes) {
            if (ua.slotIndex == UINT32_MAX) continue;
            if (ua.type == UserAttributeType::Float) {
                const uint32_t slot = ua.slotIndex;
                if (slot < kMaxUserFloatAttrs &&
                    writeSet.userFloat[slot].IsValid()) {
                    spawnCBData.userFloatUav[slot] =
                        m_device->GetBufferUAVIndex(writeSet.userFloat[slot]);
                    spawnCBData.userFloatDefault[slot] = ua.defaultFloat;
                }
            } else {
                const uint32_t slot = ua.slotIndex;
                if (slot < kMaxUserVec4Attrs &&
                    writeSet.userVec4[slot].IsValid()) {
                    spawnCBData.userVec4Uav[slot] =
                        m_device->GetBufferUAVIndex(writeSet.userVec4[slot]);
                    float* dst = (slot == 0)
                        ? spawnCBData.userVec4Default0
                        : spawnCBData.userVec4Default1;
                    dst[0] = ua.defaultVec4.x;
                    dst[1] = ua.defaultVec4.y;
                    dst[2] = ua.defaultVec4.z;
                    dst[3] = ua.defaultVec4.w;
                }
            }
        }

        const CBAllocation spawnAlloc = m_cbRing->Allocate(sizeof(spawnCBData));
        if (!spawnAlloc.cpuPtr) {
            UZ_LOG(VFX, Warning,
                   "DispatchVFXSimulation: CB ring full, skipping emitter (spawn)");
            continue;
        }
        std::memcpy(spawnAlloc.cpuPtr, &spawnCBData, sizeof(spawnCBData));

        if (spawnCount > 0) {
            // Transition WriteSet attributes Common→UAV before compute writes.
            // Uses Common as before-state every frame (matches standalone demo).
            // DX12 buffer state decay means buffers return to Common after
            // the command list finishes execution, regardless of last explicit state.
            {
                const auto& ws = gpuBuf.WriteSet();
                RHIBarrierDesc uavBarriers[7] = {};
                uint32_t ub = 0;
                auto addUav = [&](RHIBufferHandle buf) {
                    uavBarriers[ub].buffer = buf;
                    uavBarriers[ub].stateBefore = RHIResourceState::Common;
                    uavBarriers[ub].stateAfter  = RHIResourceState::UnorderedAccess;
                    ++ub;
                };
                addUav(ws.positions); addUav(ws.velocities); addUav(ws.ages);
                addUav(ws.lifespans); addUav(ws.sizes); addUav(ws.colors);
                addUav(ws.rotations);
                cmdList->ResourceBarrier(uavBarriers, ub);
            }

            // freeList is read by spawn CS as StructuredBuffer (SRV).
            // Transition from UAV→SRV so the read works correctly.
            {
                RHIBarrierDesc flBarrier{};
                flBarrier.buffer = gpuBuf.freeList;
                flBarrier.stateBefore = RHIResourceState::UnorderedAccess;
                flBarrier.stateAfter  = RHIResourceState::ShaderResource;
                cmdList->ResourceBarrier(&flBarrier, 1);
            }

            cmdList->SetPipeline(spawnPSO);
            cmdList->SetRootCBV(1, spawnAlloc.gpuAddr);
            const uint32_t spawnGroups = (spawnCount + kSpawnGroupSize - 1) / kSpawnGroupSize;
            cmdList->Dispatch(spawnGroups, 1, 1);

            // freeList back to UAV for update's dead particle recycling.
            {
                RHIBarrierDesc flBarrier{};
                flBarrier.buffer = gpuBuf.freeList;
                flBarrier.stateBefore = RHIResourceState::ShaderResource;
                flBarrier.stateAfter  = RHIResourceState::UnorderedAccess;
                cmdList->ResourceBarrier(&flBarrier, 1);
            }
        }

        // ── UAV barrier between spawn and update ───────────────────────
        // Flush aliveCount, freeListHead, and write-set attribute writes
        // so the update shader sees the newly spawned particles.
        EmitUAVBarrier(cmdList, gpuBuf.aliveCount);

        // ── Update dispatch ────────────────────────────────────────────
        ParticleUpdateConstants updateCBData{};

        updateCBData.gravity[0]  = emitter->gravity.x;
        updateCBData.gravity[1]  = emitter->gravity.y;
        updateCBData.gravity[2]  = emitter->gravity.z;
        updateCBData.dt          = emitterDt;
        updateCBData.damping     = 1.0f - emitter->damping;

        // Process ALL particle slots so the shader handles particles at
        // arbitrary free-list-assigned indices.
        updateCBData.aliveCount  = gpuBuf.capacity;
        updateCBData.frameNumber = frameIndex;
        updateCBData.maxParticles = gpuBuf.capacity;

        // In-place update: read AND write the SAME buffer (WriteSet = sets[1]).
        // The spawn already wrote to sets[1]. The update reads the spawned data
        // from the same buffer and writes updated positions back. Each thread
        // accesses only its own index, so there's no read/write race.
        // SRV reads of UAV-state buffers work on NVIDIA via implicit promotion.
        updateCBData.positionsSrvIndex  = m_device->GetBufferSRVIndex(writeSet.positions);
        updateCBData.velocitiesSrvIndex = m_device->GetBufferSRVIndex(writeSet.velocities);
        updateCBData.agesSrvIndex       = m_device->GetBufferSRVIndex(writeSet.ages);
        updateCBData.lifespansSrvIndex  = m_device->GetBufferSRVIndex(writeSet.lifespans);
        updateCBData.sizesSrvIndex      = m_device->GetBufferSRVIndex(writeSet.sizes);
        updateCBData.colorsSrvIndex     = m_device->GetBufferSRVIndex(writeSet.colors);
        updateCBData.rotationsSrvIndex       = m_device->GetBufferSRVIndex(writeSet.rotations);
        updateCBData.subImageIndicesSrvIndex = m_device->GetBufferSRVIndex(writeSet.subImageIndices);

        updateCBData.positionsUavIndex  = m_device->GetBufferUAVIndex(writeSet.positions);
        updateCBData.velocitiesUavIndex = m_device->GetBufferUAVIndex(writeSet.velocities);
        updateCBData.agesUavIndex       = m_device->GetBufferUAVIndex(writeSet.ages);
        updateCBData.lifespansUavIndex  = m_device->GetBufferUAVIndex(writeSet.lifespans);
        updateCBData.sizesUavIndex      = m_device->GetBufferUAVIndex(writeSet.sizes);
        updateCBData.colorsUavIndex     = m_device->GetBufferUAVIndex(writeSet.colors);
        updateCBData.rotationsUavIndex       = m_device->GetBufferUAVIndex(writeSet.rotations);
        updateCBData.subImageIndicesUavIndex = m_device->GetBufferUAVIndex(writeSet.subImageIndices);

        // Shared buffers (not double-buffered).
        updateCBData.freeListUavIndex       = m_device->GetBufferUAVIndex(gpuBuf.freeList);
        updateCBData.freeListHeadUavIndex   = m_device->GetBufferUAVIndex(gpuBuf.freeListHead);
        updateCBData.aliveCountNewUavIndex  = m_device->GetBufferUAVIndex(gpuBuf.aliveCount);

        // Dynamic params: use dedicated dummy buffer.
        updateCBData.readDynParamsSrv  = m_device->GetBufferSRVIndex(m_vfxDummyDynParams);
        updateCBData.writeDynParamsUav = m_device->GetBufferUAVIndex(m_vfxDummyDynParams);

        // ── Simulation forces from emitter config ───────────────────
        updateCBData.curlStrength          = emitter->curlStrength;
        updateCBData.curlFrequency         = emitter->curlFrequency;
        updateCBData.curlSpeed             = emitter->curlSpeed;
        updateCBData.totalTime             = m_totalTime;

        updateCBData.vortexAxisOriginX     = emitter->vortexAxisOriginX;
        updateCBData.vortexAxisOriginY     = emitter->vortexAxisOriginY;
        updateCBData.vortexAxisOriginZ     = emitter->vortexAxisOriginZ;
        // Normalize vortex axis direction on CPU (shader expects unit vector)
        {
            float vdx = emitter->vortexAxisDirX;
            float vdy = emitter->vortexAxisDirY;
            float vdz = emitter->vortexAxisDirZ;
            float vlen = std::sqrt(vdx * vdx + vdy * vdy + vdz * vdz);
            if (vlen > 1e-6f) { vdx /= vlen; vdy /= vlen; vdz /= vlen; }
            else { vdx = 0.0f; vdy = 0.0f; vdz = 1.0f; }
            updateCBData.vortexAxisDirX = vdx;
            updateCBData.vortexAxisDirY = vdy;
            updateCBData.vortexAxisDirZ = vdz;
        }
        updateCBData.vortexTangentialStrength = emitter->vortexTangentialStrength;
        updateCBData.vortexRadialStrength   = emitter->vortexRadialStrength;
        updateCBData.vortexMaxRadius        = emitter->vortexMaxRadius;

        updateCBData.orbitEnabled           = emitter->orbitEnabled;
        updateCBData.orbitRadius             = emitter->orbitRadius;
        updateCBData.orbitSpeed              = emitter->orbitSpeed;
        updateCBData.orbitAxisX              = emitter->orbitAxisX;
        updateCBData.orbitAxisY              = emitter->orbitAxisY;
        updateCBData.orbitAxisZ              = emitter->orbitAxisZ;

        updateCBData.groundCollision        = emitter->groundCollision;
        updateCBData.restitution            = emitter->restitution;
        updateCBData.collisionResponse      = emitter->collisionResponse;
        updateCBData.collisionFriction      = emitter->collisionFriction;

        updateCBData.dragCoefficient        = emitter->dragCoefficient;
        // Combine explicit acceleration + simple wind (wind = direction * strength as acceleration).
        updateCBData.accelX                 = emitter->accelX + emitter->windDirX * emitter->windStrength;
        updateCBData.accelY                 = emitter->accelY + emitter->windDirY * emitter->windStrength;
        updateCBData.accelZ                 = emitter->accelZ + emitter->windDirZ * emitter->windStrength;

        updateCBData.rotationRate           = emitter->rotationRate;
        updateCBData.rotationOverLifeStart  = emitter->rotationOverLifeStart;
        updateCBData.rotationOverLifeEnd    = emitter->rotationOverLifeEnd;

        // Flipbook total frames for SubImageIndex computation in update shader.
        updateCBData.flipbookTotalFrames    = emitter->flipbookTotalFrames;

        // Kill volumes (buffer-based, default disabled with count=0).
        updateCBData.killVolumeSrvIndex     = m_device->GetBufferSRVIndex(m_vfxKillVolumeBuffer);
        updateCBData.activeKillVolumeCount  = 0; // No active kill volumes by default

        // Death spawn event buffers (disabled by default — deathSpawnEnabled=0 in CB defaults).
        updateCBData.deathEventBufferUavIndex = m_device->GetBufferUAVIndex(m_vfxDeathEventBuffer);
        updateCBData.deathEventCountUavIndex  = m_device->GetBufferUAVIndex(m_vfxDeathEventCountBuffer);

        const CBAllocation updateAlloc = m_cbRing->Allocate(sizeof(updateCBData));
        if (!updateAlloc.cpuPtr) {
            UZ_LOG(VFX, Warning,
                   "DispatchVFXSimulation: CB ring full, skipping emitter (update)");
            continue;
        }
        std::memcpy(updateAlloc.cpuPtr, &updateCBData, sizeof(updateCBData));

        cmdList->SetPipeline(updatePSO);
        cmdList->SetRootCBV(1, updateAlloc.gpuAddr);
        const uint32_t updateGroups = (gpuBuf.capacity + kUpdateGroupSize - 1) / kUpdateGroupSize;
        cmdList->Dispatch(updateGroups, 1, 1);

        // ── UAV barrier between update and indirect args ───────────────
        EmitUAVBarrier(cmdList, gpuBuf.aliveCount);

        // ── Indirect args dispatch ─────────────────────────────────────
        // The indirect args CS reads aliveCount (SRV) and writes
        // indirectDrawArgs / indirectDispatchArgs (UAV).
        // Root param slot 0 = root constants (register(b0)).
        ParticleIndirectArgsConstants indirectCBData{};
        indirectCBData.aliveCountSrvIndex  = m_device->GetBufferSRVIndex(gpuBuf.aliveCount);
        indirectCBData.indirectArgsUavIndex = m_device->GetBufferUAVIndex(gpuBuf.indirectDrawArgs);

        // Indirect args uses SetRootConstants on slot 0 (matching the demo).
        uint32_t rootData[4] = {};
        std::memcpy(rootData, &indirectCBData, sizeof(indirectCBData));

        cmdList->SetPipeline(indirectArgsPSO);
        cmdList->SetRootConstants(0, rootData, 4);
        cmdList->Dispatch(1, 1, 1);

        // ── UAV -> IndirectArgument barrier ─────────────────────────────
        TransitionIndirectArgs(cmdList, gpuBuf);

        // NOTE: No UAV→SRV barrier here. The standalone demo does NOT transition
        // write-set to SRV after compute. DX12 implicit state decay handles the
        // Common→UAV→Common lifecycle. The billboard reads via ResourceDescriptorHeap
        // (bindless) which works regardless of tracked state on NVIDIA.

        // Mark buffer as having completed a full simulation cycle.
        // The render graph import in RegisterParticlePasses will use
        // steady-state resource states from this point forward.
        gpuBuf.hasCompletedCycle = true;
    }

    if (m_gpuProfileEnabled) {
        m_device->GpuProfilerEndTimestamp(cmdList, "VFX_InlineSimulation");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// DispatchVFXSort — bitonic depth sort for emitters that need back-to-front
//                   rendering. Runs inline after DispatchVFXSimulation.
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::DispatchVFXSort(IRHICommandList* cmdList) {
    UZ_PROFILE_FUNCTION();

    VFXSystem* activeVFX = m_vfxSystemOverride ? m_vfxSystemOverride : m_vfxSystem.get();
    if (!activeVFX || !activeVFX->IsInitialized()) return;

    const auto& emitters = activeVFX->GetActiveEmitters();
    if (emitters.empty()) return;

    const auto sortPSO = m_pipelines->GetParticleSortCS();
    if (!sortPSO.IsValid()) return;

    if (m_gpuProfileEnabled) {
        m_device->GpuProfilerBeginTimestamp(cmdList, "VFX_InlineSort");
    }

    // Camera vectors for ViewDepth keys. Captured once per frame; the sort
    // dispatch reads them as float[3].
    const Vec3 camPos = m_cameraManager->GetActiveCamera().GetPosition();
    const Vec3 camFwd = m_cameraManager->GetActiveCamera().GetForward();
    const float camPosArr[3] = { camPos.x, camPos.y, camPos.z };
    const float camFwdArr[3] = { camFwd.x, camFwd.y, camFwd.z };

    constexpr uint32_t kBlendModeAlphaBlend = 0u;

    for (auto* emitter : emitters) {
        if (!emitter || !emitter->initialized) continue;
        if (emitter->cullSuppress) continue;
        if (emitter->needsInitialUpload) continue;

        auto& gpuBuf = emitter->buffer;
        if (!gpuBuf.IsValid() || !gpuBuf.sort.IsValid()) continue;
        if (emitter->cpuAliveCount == 0) continue;

        // ── Resolve sort mode + key source for this emitter ─────────────
        // Sprite emitters (no MeshRenderModule, or one we don't recognise)
        // auto-sort when blendMode == AlphaBlend and skip otherwise.
        // Mesh emitters consult MeshRenderModule::sortMode.
        bool        needsSort = false;
        GpuSortMode gpuSortMode = GpuSortMode::ViewDepth;
        uint32_t    customSortKeySrv = 0xFFFFFFFFu;

        const auto* renderModule = emitter->moduleStack.renderModule.get();
        const bool isMesh = renderModule != nullptr &&
                            std::strcmp(renderModule->GetName(), "mesh_render") == 0;

        if (isMesh) {
            const auto* meshModule = static_cast<const MeshRenderModule*>(renderModule);
            const auto& meshConfig = meshModule->GetConfig();
            switch (meshConfig.sortMode) {
            case MeshSortMode::None:
                needsSort = false;
                break;
            case MeshSortMode::ViewDepth:
                needsSort = true;
                gpuSortMode = GpuSortMode::ViewDepth;
                break;
            case MeshSortMode::CustomAttribute: {
                // Resolve `customSortAttribute` -> Float user-attribute slot
                // -> structured buffer SRV. CustomAttribute requires a Float
                // (Vec4 is rejected because the sort_cs reads scalar floats).
                // If anything fails, downgrade to None and warn once per
                // emitter so the authoring bug is visible without spamming.
                needsSort = true;
                gpuSortMode = GpuSortMode::CustomAttribute;

                const UserAttributeDef* attr = nullptr;
                if (!meshConfig.customSortAttribute.empty()) {
                    attr = emitter->FindUserAttribute(meshConfig.customSortAttribute);
                }

                bool resolved = false;
                if (attr != nullptr &&
                    attr->type == UserAttributeType::Float &&
                    attr->slotIndex < kMaxUserFloatAttrs) {
                    const auto& userFloatBuf =
                        gpuBuf.WriteSet().userFloat[attr->slotIndex];
                    if (userFloatBuf.IsValid()) {
                        customSortKeySrv = m_device->GetBufferSRVIndex(userFloatBuf);
                        resolved = (customSortKeySrv != UINT32_MAX);
                    }
                }

                if (!resolved) {
                    if (!emitter->customSortWarned) {
                        UZ_LOG(VFX, Warning,
                               "Emitter sort_mode=CustomAttribute references "
                               "user attribute '{}' that is not declared as a "
                               "Float channel or has no GPU buffer; sort "
                               "downgraded to None for this emitter",
                               meshConfig.customSortAttribute);
                        emitter->customSortWarned = true;
                    }
                    needsSort = false;
                }
                break;
            }
            }
        } else {
            // Sprite path: sort iff alpha-blended.
            needsSort = (emitter->blendMode == kBlendModeAlphaBlend);
            gpuSortMode = GpuSortMode::ViewDepth;
        }

        if (!needsSort) continue;

        // ── Resolve bindless SRV indices for the sort dispatch ──────────
        const auto& readSet = gpuBuf.WriteSet();
        const uint32_t positionsSrv = m_device->GetBufferSRVIndex(readSet.positions);
        const uint32_t agesSrv      = m_device->GetBufferSRVIndex(readSet.ages);
        const uint32_t lifespansSrv = m_device->GetBufferSRVIndex(readSet.lifespans);

        if (positionsSrv == UINT32_MAX || agesSrv == UINT32_MAX ||
            lifespansSrv == UINT32_MAX) {
            UZ_LOG(VFX, Warning,
                   "DispatchVFXSort: emitter has invalid SRV indices "
                   "(pos={}, ages={}, lifespans={}); skipping",
                   positionsSrv, agesSrv, lifespansSrv);
            continue;
        }

        // ── Run the sort over `capacity` slots so alive detection covers
        // every potentially-alive particle (free-list scatter pattern).
        GPUParticleSort::DispatchParams sp{};
        sp.sortMode              = gpuSortMode;
        sp.keyGenCount           = gpuBuf.capacity;
        sp.cameraPos             = camPosArr;
        sp.cameraForward         = camFwdArr;
        sp.positionsSrvIndex     = positionsSrv;
        sp.agesSrvIndex          = agesSrv;
        sp.lifespansSrvIndex     = lifespansSrv;
        sp.customSortKeySrvIndex = customSortKeySrv;

        gpuBuf.sort.DispatchSort(m_device, cmdList, sortPSO, *m_cbRing, sp);
    }

    if (m_gpuProfileEnabled) {
        m_device->GpuProfilerEndTimestamp(cmdList, "VFX_InlineSort");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GatherAndUploadVfxEmissiveSamples — feeds emissive particles into DDGI.
// ─────────────────────────────────────────────────────────────────────────────
//
// Phase 16 lumen-gi-contribution. Walks the active emitter list, produces a
// VfxEmissiveSample per emitter that has both alive particles AND non-zero
// emissive intensity, then forwards the result to `m_vfxDDGIEmissiveFeed`
// for top-N selection + GPU upload.
//
// Two CVars gate the gather:
//   * `r.VFX.DDGIInjection`           — master switch (default 1).
//   * `r.VFX.DDGIInjection.MaxEmitters` — hard cap on the number of
//     emitters considered, clamped to the feed's `kMaxSamples`.
//
// The DDGI probe-update CS reading the produced buffer is wired in
// `phase16_vfx-lumen-gi-hardware-validation`. Until that lands, the feed
// still uploads the buffer every frame so the hardware-side wiring only
// has to add the SRV bind on the probe-update CS side.

void SceneRenderer::GatherAndUploadVfxEmissiveSamples()
{
    UZ_PROFILE_FUNCTION();

    if (!m_vfxDDGIEmissiveFeed.IsInitialized()) return;

    auto& cvars = CVarRegistry::Get();
    const bool enabled = cvars.GetBool("r.VFX.DDGIInjection", true);
    if (!enabled) {
        m_vfxDDGIEmissiveFeed.Reset();
        return;
    }

    VFXSystem* activeVFX = m_vfxSystemOverride ? m_vfxSystemOverride : m_vfxSystem.get();
    if (!activeVFX || !activeVFX->IsInitialized()) {
        m_vfxDDGIEmissiveFeed.Reset();
        return;
    }

    const auto& emitters = activeVFX->GetActiveEmitters();
    if (emitters.empty()) {
        m_vfxDDGIEmissiveFeed.Reset();
        return;
    }

    const uint32_t requestedCap = static_cast<uint32_t>(std::max(0,
        cvars.GetInt("r.VFX.DDGIInjection.MaxEmitters",
                     static_cast<int>(VfxDDGIEmissiveFeed::kMaxSamples))));
    const uint32_t emitterCap = std::min(requestedCap,
                                          VfxDDGIEmissiveFeed::kMaxSamples);

    // Build a CPU-side staging vector of candidate samples. The feed's
    // UploadFrame already runs top-N selection, so we can over-produce
    // here and let it sort.
    VfxEmissiveSample staging[VfxDDGIEmissiveFeed::kMaxSamples];
    uint32_t produced = 0;

    for (auto* emitter : emitters) {
        if (produced >= emitterCap) break;
        if (!emitter || !emitter->initialized) continue;

        const float emissive = emitter->emissiveIntensity;
        if (emissive <= 0.0f)            continue;
        if (emitter->cpuAliveCount == 0) continue;

        // Build a virtual point-light approximation via the shared
        // helper so the math stays in lockstep with the unit tests.
        const Vec3 colorRgb {
            emitter->initialColor.x,
            emitter->initialColor.y,
            emitter->initialColor.z
        };
        staging[produced] = VfxDDGIBuildSampleForEmitter(
            emitter->worldPosition,
            colorRgb,
            emissive,
            emitter->cpuAliveCount,
            emitter->maxParticles,
            emitter->spawnRadius);
        ++produced;
    }

    m_vfxDDGIEmissiveFeed.UploadFrame(staging, produced);
}

} // namespace uz
