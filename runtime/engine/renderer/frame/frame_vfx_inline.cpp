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

    // Pipeline state objects for the three compute shaders.
    const auto spawnPSO        = m_pipelines->GetParticleSpawnCS();
    const auto updatePSO       = m_pipelines->GetParticleUpdateCS();
    const auto indirectArgsPSO = m_pipelines->GetParticleIndirectArgsCS();

    if (!spawnPSO.IsValid() || !updatePSO.IsValid() || !indirectArgsPSO.IsValid()) {
        return;
    }

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

        // ── Spawn dispatch ─────────────────────────────────────────────
        const uint32_t spawnCount = emitter->CalculateSpawnCount(dt);

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

        spawnCBData.dt                = dt;
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
        spawnCBData.freeListHeadUavIndex    = m_device->GetBufferUAVIndex(gpuBuf.freeListHead);
        spawnCBData.aliveCountUavIndex      = m_device->GetBufferUAVIndex(gpuBuf.aliveCount);

        // Spawn shape defaults (point emitter).
        spawnCBData.spawnShape        = emitter->spawnShape;
        spawnCBData.spawnOnSurface    = 0;

        // Dynamic params UAV -- use positions UAV as fallback to prevent
        // writes to descriptor 0 (which corrupts the descriptor ring).
        // Use dedicated dummy buffer — CANNOT use positions/sizes (spawn
        // writes dynamicParams[idx] which would overwrite that attribute).
        spawnCBData.dynamicParamsUavIndex = m_device->GetBufferUAVIndex(m_vfxDummyDynParams);

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
        updateCBData.dt          = dt;
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
        updateCBData.rotationsSrvIndex  = m_device->GetBufferSRVIndex(writeSet.rotations);

        updateCBData.positionsUavIndex  = m_device->GetBufferUAVIndex(writeSet.positions);
        updateCBData.velocitiesUavIndex = m_device->GetBufferUAVIndex(writeSet.velocities);
        updateCBData.agesUavIndex       = m_device->GetBufferUAVIndex(writeSet.ages);
        updateCBData.lifespansUavIndex  = m_device->GetBufferUAVIndex(writeSet.lifespans);
        updateCBData.sizesUavIndex      = m_device->GetBufferUAVIndex(writeSet.sizes);
        updateCBData.colorsUavIndex     = m_device->GetBufferUAVIndex(writeSet.colors);
        updateCBData.rotationsUavIndex  = m_device->GetBufferUAVIndex(writeSet.rotations);

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

        updateCBData.vortexAxisOriginX     = emitter->vortexAxisOriginX;
        updateCBData.vortexAxisOriginY     = emitter->vortexAxisOriginY;
        updateCBData.vortexAxisOriginZ     = emitter->vortexAxisOriginZ;
        updateCBData.vortexAxisDirX        = emitter->vortexAxisDirX;
        updateCBData.vortexAxisDirY        = emitter->vortexAxisDirY;
        updateCBData.vortexAxisDirZ        = emitter->vortexAxisDirZ;
        updateCBData.vortexTangentialStrength = emitter->vortexTangentialStrength;
        updateCBData.vortexRadialStrength   = emitter->vortexRadialStrength;
        updateCBData.vortexMaxRadius        = emitter->vortexMaxRadius;

        updateCBData.orbitEnabled           = emitter->orbitEnabled;
        updateCBData.orbitRadius             = emitter->orbitRadius;
        updateCBData.orbitSpeed              = emitter->orbitSpeed;

        updateCBData.groundCollision        = emitter->groundCollision;
        updateCBData.restitution            = emitter->restitution;
        updateCBData.collisionResponse      = emitter->collisionResponse;
        updateCBData.collisionFriction      = emitter->collisionFriction;

        updateCBData.dragCoefficient        = emitter->dragCoefficient;
        updateCBData.accelX                 = emitter->accelX;
        updateCBData.accelY                 = emitter->accelY;
        updateCBData.accelZ                 = emitter->accelZ;

        updateCBData.rotationRate           = emitter->rotationRate;
        updateCBData.rotationOverLifeStart  = emitter->rotationOverLifeStart;
        updateCBData.rotationOverLifeEnd    = emitter->rotationOverLifeEnd;

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

} // namespace uz
