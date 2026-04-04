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

    static bool loggedOnce = false;
    if (!loggedOnce) {
        UZ_LOG(VFX, Info, "DispatchVFXSimulation: {} emitters, PSOs valid", emitters.size());
        loggedOnce = true;
    }

    for (auto* emitter : emitters) {
        if (!emitter || !emitter->initialized) continue;

        auto& gpuBuf = emitter->buffer;
        if (!gpuBuf.IsValid()) continue;

        // Ping-pong swap: after swap, WriteSet has the buffers that were ReadSet
        // last frame. Spawn writes to WriteSet, Update reads ReadSet → writes WriteSet.
        gpuBuf.Swap();

        // ── Deferred GPU buffer initialization ─────────────────────────
        // Clean up freeList/freeListHead staging from a previous init,
        // but KEEP aliveCountStaging for the per-frame reset below.
        if (!gpuBuf.needsInitialization && gpuBuf.freeListStaging.IsValid()) {
            if (gpuBuf.freeListStaging.IsValid())
                m_device->DestroyBuffer(gpuBuf.freeListStaging);
            gpuBuf.freeListStaging = RHIBufferHandle::Invalid();
            if (gpuBuf.freeListHeadStaging.IsValid())
                m_device->DestroyBuffer(gpuBuf.freeListHeadStaging);
            gpuBuf.freeListHeadStaging = RHIBufferHandle::Invalid();
            // Keep aliveCountStaging for per-frame reset!
        }

        // Upload initial data if this buffer has never been initialized.
        if (gpuBuf.needsInitialization) {
            if (!gpuBuf.InitializeData(m_device, cmdList)) {
                UZ_LOG(VFX, Error,
                       "DispatchVFXSimulation: failed to initialize GPU buffer for emitter");
                continue;
            }
            // Clear the merge's upload flag so RegisterParticlePasses doesn't skip this emitter.
            emitter->needsInitialUpload = false;
        }

        // ── Reset aliveCount to 0 each frame ──────────────────────────
        // The spawn and update shaders atomically increment aliveCount.
        // Without resetting to 0 each frame, the counter grows unbounded
        // and the free list eventually exhausts (particles stop spawning).
        // This matches the standalone demo's per-frame aliveCount reset.
        if (gpuBuf.aliveCountStaging.IsValid()) {
            // Transition aliveCount to CopyDst before the copy.
            // After InitializeData the buffer is in UAV state (CopyDst→UAV
            // barrier emitted by InitializeData). DX12 requires the
            // destination of CopyBuffer to be in CopyDst state.
            RHIBarrierDesc preCopy{};
            preCopy.buffer      = gpuBuf.aliveCount;
            preCopy.stateBefore = RHIResourceState::UnorderedAccess;
            preCopy.stateAfter  = RHIResourceState::CopyDst;
            cmdList->ResourceBarrier(&preCopy, 1);

            cmdList->CopyBuffer(gpuBuf.aliveCountStaging, 0,
                                gpuBuf.aliveCount, 0, sizeof(uint32_t));
            // CopyDst → UAV barrier so spawn CS can atomically increment.
            RHIBarrierDesc b{};
            b.buffer = gpuBuf.aliveCount;
            b.stateBefore = RHIResourceState::CopyDst;
            b.stateAfter  = RHIResourceState::UnorderedAccess;
            cmdList->ResourceBarrier(&b, 1);
        }

        // ── Spawn dispatch ─────────────────────────────────────────────
        const uint32_t spawnCount = emitter->CalculateSpawnCount(dt);

        static uint32_t spawnLogCount = 0;
        if (spawnLogCount < 10) {
            UZ_LOG(VFX, Info, "Frame {}: spawnCount={}, cpuAlive={}, dt={:.4f}",
                   frameIndex, spawnCount, emitter->cpuAliveCount, dt);
            ++spawnLogCount;
        }

        // cpuAliveCount is already updated inside CalculateSpawnCount().

        ParticleSpawnConstants spawnCBData{};

        // Emitter parameters.
        spawnCBData.emitterPos[0]     = 0.0f;
        spawnCBData.emitterPos[1]     = 0.0f;
        spawnCBData.emitterPos[2]     = 0.0f;
        spawnCBData.spawnRadius       = 200.0f; // 200cm random offset — wide spread
        spawnCBData.spawnVelocity[0]  = emitter->spawnVelocity.x;
        spawnCBData.spawnVelocity[1]  = emitter->spawnVelocity.y;
        spawnCBData.spawnVelocity[2]  = emitter->spawnVelocity.z;
        spawnCBData.velocityVariation = 100.0f; // ±100 cm/s random variation

        // Convert spread angle from degrees (emitter) to radians (shader).
        constexpr float kDegToRad = 3.14159265358979f / 180.0f;
        spawnCBData.spreadAngle       = emitter->spreadAngle * kDegToRad;

        // Lifetime range: base lifetime with variation applied.
        const float minLife = (emitter->lifetime - emitter->lifetimeVariation > 0.01f)
                            ? (emitter->lifetime - emitter->lifetimeVariation)
                            : 0.01f;
        spawnCBData.minLifetime       = minLife;
        spawnCBData.maxLifetime       = emitter->lifetime;
        spawnCBData.initialSize       = 20.0f; // 20 cm — visible but not overlapping

        spawnCBData.initialColor[0]   = 1.0f;
        spawnCBData.initialColor[1]   = 1.0f;
        spawnCBData.initialColor[2]   = 1.0f;
        spawnCBData.initialColor[3]   = 1.0f;

        spawnCBData.dt                = dt;
        spawnCBData.frameNumber       = frameIndex;
        spawnCBData.spawnCount        = spawnCount;
        spawnCBData.maxParticles      = gpuBuf.capacity;

        // Bindless descriptor indices for write-set attribute buffers.
        const auto& writeSet = gpuBuf.WriteSet();
        spawnCBData.freeListSrvIndex       = m_device->GetBufferSRVIndex(gpuBuf.freeList);
        spawnCBData.positionsUavIndex       = m_device->GetBufferUAVIndex(writeSet.positions);
        static bool logIdx = false;
        if (!logIdx) {
            UZ_LOG(VFX, Info, "Spawn posUAV={} | Update posSRV={} posUAV={} | readIdx={} writeIdx={}",
                   spawnCBData.positionsUavIndex,
                   m_device->GetBufferSRVIndex(gpuBuf.ReadSet().positions),
                   m_device->GetBufferUAVIndex(writeSet.positions),
                   gpuBuf.readIndex, gpuBuf.writeIndex);
            logIdx = true;
        }
        spawnCBData.velocitiesUavIndex      = m_device->GetBufferUAVIndex(writeSet.velocities);
        spawnCBData.agesUavIndex            = m_device->GetBufferUAVIndex(writeSet.ages);
        spawnCBData.lifespansUavIndex       = m_device->GetBufferUAVIndex(writeSet.lifespans);
        spawnCBData.sizesUavIndex           = m_device->GetBufferUAVIndex(writeSet.sizes);
        spawnCBData.colorsUavIndex          = m_device->GetBufferUAVIndex(writeSet.colors);
        spawnCBData.rotationsUavIndex       = m_device->GetBufferUAVIndex(writeSet.rotations);
        spawnCBData.freeListHeadUavIndex    = m_device->GetBufferUAVIndex(gpuBuf.freeListHead);
        spawnCBData.aliveCountUavIndex      = m_device->GetBufferUAVIndex(gpuBuf.aliveCount);

        // Spawn shape defaults (point emitter).
        spawnCBData.spawnShape        = 0;
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
            cmdList->SetPipeline(spawnPSO);
            cmdList->SetRootCBV(1, spawnAlloc.gpuAddr);
            const uint32_t spawnGroups = (spawnCount + kSpawnGroupSize - 1) / kSpawnGroupSize;
            cmdList->Dispatch(spawnGroups, 1, 1);
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

        // Ping-pong: READ from ReadSet (SRV), WRITE to WriteSet (UAV).
        // In-place (SRV+UAV on same buffer) is a DX12 data hazard.
        // The spawn writes to WriteSet, update reads ReadSet (previous frame's
        // WriteSet after swap) and writes to WriteSet.
        const auto& readSet = gpuBuf.ReadSet();
        updateCBData.positionsSrvIndex  = m_device->GetBufferSRVIndex(readSet.positions);
        updateCBData.velocitiesSrvIndex = m_device->GetBufferSRVIndex(readSet.velocities);
        updateCBData.agesSrvIndex       = m_device->GetBufferSRVIndex(readSet.ages);
        updateCBData.lifespansSrvIndex  = m_device->GetBufferSRVIndex(readSet.lifespans);
        updateCBData.sizesSrvIndex      = m_device->GetBufferSRVIndex(readSet.sizes);
        updateCBData.colorsSrvIndex     = m_device->GetBufferSRVIndex(readSet.colors);
        updateCBData.rotationsSrvIndex  = m_device->GetBufferSRVIndex(readSet.rotations);

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
        // CANNOT use positions/sizes — dynParamsOut write would overwrite those attributes!
        updateCBData.readDynParamsSrv  = m_device->GetBufferSRVIndex(m_vfxDummyDynParams);
        updateCBData.writeDynParamsUav = m_device->GetBufferUAVIndex(m_vfxDummyDynParams);

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
