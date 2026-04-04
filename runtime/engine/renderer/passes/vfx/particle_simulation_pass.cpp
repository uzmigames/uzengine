/// VFX Particle Simulation Pass -- Implementation
///
/// Registers and executes a compute pass that runs the full particle simulation
/// pipeline (spawn, update, indirect args) for every active emitter in the scene.

#include "particle_simulation_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"
#include "engine/vfx/core/gpu_particle_buffer.h"
#include "engine/vfx/modules/emitter_module_stack.h"
#include "engine/vfx/modules/module_compositor.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Thread group size matching [numthreads(256, 1, 1)] in particle compute shaders.
static constexpr uint32_t kParticleThreadGroupSize = 256;

/// Root parameter slot for per-dispatch constant buffer (b1).
static constexpr uint32_t kSlotPerDispatch = 1;

/// Root parameter slot for indirect args constant buffer (b0).
/// The indirect args CS uses register(b0), unlike spawn/update which use b1.
static constexpr uint32_t kSlotIndirectArgsCB = 0;

/// Root parameter slot for UAV descriptor table (u0..u3).
static constexpr uint32_t kSlotUAVTable = 4;

/// Root parameter slot for SRV descriptor table (t0..tN).
static constexpr uint32_t kSlotSRVTable = 5;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Calculates the number of thread groups needed to cover `count` items
/// with `groupSize` threads per group.
///
/// # Arguments
/// * `count` -- Total number of work items to dispatch.
/// * `groupSize` -- Threads per group (must be > 0).
///
/// # Returns
/// ceil(count / groupSize), minimum 1 if count > 0, 0 if count == 0.
static uint32_t DivRoundUp(uint32_t count, uint32_t groupSize)
{
    if (count == 0) return 0;
    return (count + groupSize - 1) / groupSize;
}

/// Emits UAV barriers on the core writable buffers of an emitter.
///
/// Transitions the write attribute set buffers, alive count, and free list
/// from UAV to UAV (same-state UAV barrier) to ensure writes from one
/// dispatch are visible to the next.
///
/// # Arguments
/// * `cmd` -- Command list to record barriers into.
/// * `buffer` -- The emitter's GPU particle buffer.
static void EmitSimulationUAVBarriers(IRHICommandList* cmd, const GPUParticleBuffer& buffer)
{
    const auto& writeSet = buffer.WriteSet();

    RHIBarrierDesc barriers[9] = {};
    uint32_t count = 0;

    // Write attribute set buffers -- all in UAV state
    auto addUAVBarrier = [&](RHIBufferHandle buf) {
        if (buf.IsValid()) {
            barriers[count].buffer      = buf;
            barriers[count].stateBefore = RHIResourceState::UnorderedAccess;
            barriers[count].stateAfter  = RHIResourceState::UnorderedAccess;
            ++count;
        }
    };

    addUAVBarrier(writeSet.positions);
    addUAVBarrier(writeSet.velocities);
    addUAVBarrier(writeSet.ages);
    addUAVBarrier(writeSet.lifespans);
    addUAVBarrier(writeSet.sizes);
    addUAVBarrier(writeSet.colors);
    addUAVBarrier(writeSet.rotations);

    // Shared bookkeeping buffers
    addUAVBarrier(buffer.aliveCount);
    addUAVBarrier(buffer.freeList);

    if (count > 0) {
        cmd->ResourceBarrier(barriers, count);
    }
}

/// Emits UAV barriers on the indirect argument buffers of an emitter.
///
/// Ensures the indirect args written by the indirect args CS are visible
/// to subsequent DrawIndexedIndirect / DispatchIndirect calls.
///
/// # Arguments
/// * `cmd` -- Command list to record barriers into.
/// * `buffer` -- The emitter's GPU particle buffer.
static void EmitIndirectArgsBarriers(IRHICommandList* cmd, const GPUParticleBuffer& buffer)
{
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

/// Transitions the write attribute set from ShaderResource to UnorderedAccess.
///
/// After the render pass reads the attribute set as SRV, the ping-pong swap
/// makes it the new write set. The simulation CS needs UAV access, so this
/// barrier must run before any compute dispatch touches the write set.
///
/// On the very first frame, buffers start in UAV state (from Create()), so
/// the SRV->UAV transition is technically a no-op validated state. D3D12
/// allows transitioning from Common/UAV to UAV without error.
///
/// # Arguments
/// * `cmd` -- Command list to record barriers into.
/// * `buffer` -- The emitter's GPU particle buffer.
static void TransitionWriteSetToUAV(IRHICommandList* cmd, const GPUParticleBuffer& buffer)
{
    const auto& writeSet = buffer.WriteSet();

    RHIBarrierDesc barriers[9] = {};
    uint32_t count = 0;

    auto addBarrier = [&](RHIBufferHandle buf) {
        if (buf.IsValid()) {
            barriers[count].buffer      = buf;
            barriers[count].stateBefore = RHIResourceState::ShaderResource;
            barriers[count].stateAfter  = RHIResourceState::UnorderedAccess;
            ++count;
        }
    };

    // Write attribute set buffers (were SRV after previous frame's render)
    addBarrier(writeSet.positions);
    addBarrier(writeSet.velocities);
    addBarrier(writeSet.ages);
    addBarrier(writeSet.lifespans);
    addBarrier(writeSet.sizes);
    addBarrier(writeSet.colors);
    addBarrier(writeSet.rotations);

    // Shared bookkeeping: aliveCount was also transitioned to SRV by render pass
    addBarrier(buffer.aliveCount);

    // freeList and freeListHead stay in UAV (render pass doesn't touch them)

    if (count > 0) {
        cmd->ResourceBarrier(barriers, count);
    }
}

/// Transitions the indirect args buffers from IndirectArgument back to UAV
/// for the current frame's simulation to write.
///
/// After the previous frame's simulation, EmitIndirectArgsBarriers left these
/// in IndirectArgument state for the render pass's DrawIndexedIndirect call.
/// The current frame's indirect args CS needs UAV access to overwrite them.
///
/// # Arguments
/// * `cmd` -- Command list to record barriers into.
/// * `buffer` -- The emitter's GPU particle buffer.
static void TransitionIndirectArgsToUAV(IRHICommandList* cmd, const GPUParticleBuffer& buffer)
{
    RHIBarrierDesc barriers[2] = {};
    uint32_t count = 0;

    if (buffer.indirectDrawArgs.IsValid()) {
        barriers[count].buffer      = buffer.indirectDrawArgs;
        barriers[count].stateBefore = RHIResourceState::IndirectArgument;
        barriers[count].stateAfter  = RHIResourceState::UnorderedAccess;
        ++count;
    }

    if (buffer.indirectDispatchArgs.IsValid()) {
        barriers[count].buffer      = buffer.indirectDispatchArgs;
        barriers[count].stateBefore = RHIResourceState::IndirectArgument;
        barriers[count].stateAfter  = RHIResourceState::UnorderedAccess;
        ++count;
    }

    if (count > 0) {
        cmd->ResourceBarrier(barriers, count);
    }
}

/// Dispatches the spawn compute shader for a single emitter.
///
/// Binds the spawn pipeline, sets the spawn constant buffer, binds SRV/UAV
/// descriptor tables, and dispatches ceil(spawnCount / 64) thread groups.
///
/// # Arguments
/// * `cmd` -- Command list to record into.
/// * `desc` -- Pass-level descriptor (holds pipeline handles).
/// * `emitter` -- Per-emitter dispatch data.
static void DispatchSpawn(IRHICommandList* cmd,
                          const ParticleSimulationPassDesc& desc,
                          const EmitterSimDispatch& emitter)
{
    if (emitter.spawnCount == 0) return;

    cmd->SetPipeline(desc.spawnPipeline);

    cmd->SetRootCBV(kSlotPerDispatch,
                    emitter.spawnCB.bufferHandle,
                    emitter.spawnCB.offsetBytes,
                    reinterpret_cast<const uint8_t*>(emitter.spawnCB.cpuPtr),
                    emitter.spawnCB.sizeBytes);

    // SM6.6 bindless: SRV/UAV accessed via ResourceDescriptorHeap[] using
    // indices passed through the constant buffer. No descriptor table needed.
    // SM5 fallback: bind SRV table at slot 5 (only exists in SM5 root sig).
    // UAV table at slot 4 exists in both root signatures.
    // Note: kSlotSRVTable (5) is NOT valid for SM6.6 root sig — skip it.

    const uint32_t groups = DivRoundUp(emitter.spawnCount, kParticleThreadGroupSize);
    cmd->Dispatch(groups, 1, 1);
}

/// Dispatches the update compute shader for a single emitter.
///
/// Binds the update pipeline, sets the update constant buffer, binds SRV/UAV
/// descriptor tables, and dispatches ceil(aliveCount / 64) thread groups.
/// If aliveCount is 0, dispatches 1 group as a minimum (handles edge case
/// where spawn just added particles).
///
/// # Arguments
/// * `cmd` -- Command list to record into.
/// * `desc` -- Pass-level descriptor (holds pipeline handles).
/// * `emitter` -- Per-emitter dispatch data.
static void DispatchUpdate(IRHICommandList* cmd,
                           const ParticleSimulationPassDesc& desc,
                           const EmitterSimDispatch& emitter)
{
    cmd->SetPipeline(desc.updatePipeline);

    cmd->SetRootCBV(kSlotPerDispatch,
                    emitter.updateCB.bufferHandle,
                    emitter.updateCB.offsetBytes,
                    reinterpret_cast<const uint8_t*>(emitter.updateCB.cpuPtr),
                    emitter.updateCB.sizeBytes);

    // SM6.6 bindless: descriptor indices are in the CB, not descriptor tables.

    // Dispatch at least 1 group so newly spawned particles get updated.
    const uint32_t totalCount = emitter.aliveCount + emitter.spawnCount;
    const uint32_t groups = (totalCount > 0)
        ? DivRoundUp(totalCount, kParticleThreadGroupSize)
        : 1;
    cmd->Dispatch(groups, 1, 1);
}

/// Dispatches the indirect args compute shader for a single emitter.
///
/// Reads the alive count from the GPU buffer and writes DrawIndexedIndirect
/// and DispatchIndirect argument structs. Always dispatches exactly 1 group.
///
/// # Arguments
/// * `cmd` -- Command list to record into.
/// * `desc` -- Pass-level descriptor (holds pipeline handles).
/// * `emitter` -- Per-emitter dispatch data.
static void DispatchIndirectArgs(IRHICommandList* cmd,
                                 const ParticleSimulationPassDesc& desc,
                                 const EmitterSimDispatch& emitter)
{
    cmd->SetPipeline(desc.indirectArgsPipeline);

    // Indirect args CS uses register(b0) = root param 0 = 32-bit root constants.
    // Pass the 4 uint32 values (aliveCountSrvIndex, indirectArgsUavIndex, pad, pad)
    // directly as root constants instead of a CBV.
    cmd->SetRootConstants(kSlotIndirectArgsCB,
                          reinterpret_cast<const uint32_t*>(emitter.indirectCB.cpuPtr),
                          4);

    // SM6.6 bindless: descriptor indices are in the CB, not descriptor tables.

    // Single group -- the shader reads alive count and writes 2 arg structs.
    cmd->Dispatch(1, 1, 1);
}

// ---------------------------------------------------------------------------
// ComposeEmitterShaders -- module composition integration
// ---------------------------------------------------------------------------

bool ComposeEmitterShaders(EmitterModuleStack& stack,
                           const ModuleCompositor& compositor)
{
    // No modules means fallback to hardcoded shaders -- not an error.
    if (!stack.HasModules()) {
        return true;
    }

    // Already up to date -- skip composition.
    if (!stack.dirty) {
        return true;
    }

    UZ_PROFILE_SCOPE("VFX::ComposeEmitterShaders");

    // Compose update shader if there are update modules.
    if (!stack.updateModules.empty()) {
        const auto updatePtrs = stack.GetUpdateModulePtrs();
        auto composed = compositor.ComposeUpdateShader(updatePtrs);
        stack.composedUpdateHLSL = std::move(composed.hlslSource);
        stack.composedCacheKey = composed.cacheKey;
    } else {
        stack.composedUpdateHLSL.clear();
    }

    // Compose spawn shader if there are spawn modules.
    if (!stack.spawnModules.empty()) {
        const auto spawnPtrs = stack.GetSpawnModulePtrs();
        auto composed = compositor.ComposeSpawnShader(spawnPtrs);
        stack.composedSpawnHLSL = std::move(composed.hlslSource);
    } else {
        stack.composedSpawnHLSL.clear();
    }

    stack.dirty = false;

    UZ_LOG(VFX, Info,
           "ComposeEmitterShaders: recomposed shaders -- "
           "update={} bytes, spawn={} bytes, cacheKey={:#x}",
           stack.composedUpdateHLSL.size(),
           stack.composedSpawnHLSL.size(),
           stack.composedCacheKey);

    // TODO(3.61.0): Compile composed HLSL via DXC runtime compilation when available.
    // Once DXC is integrated, the composed HLSL should be compiled into a PSO here
    // and stored in the EmitterModuleStack (or a PipelineLibrary cache keyed by
    // composedCacheKey). Until then, the pass falls through to the hardcoded
    // pipelines in ParticleSimulationPassDesc.

    return true;
}

// ---------------------------------------------------------------------------
// AddParticleSimulationPass
// ---------------------------------------------------------------------------

void AddParticleSimulationPass(rg::RgBuilder& builder,
                               const ParticleSimulationPassDesc& desc)
{
    UZ_CHECKF(desc.spawnPipeline.IsValid(),
              "ParticleSimulationPass: spawn pipeline handle is invalid");
    UZ_CHECKF(desc.updatePipeline.IsValid(),
              "ParticleSimulationPass: update pipeline handle is invalid");
    UZ_CHECKF(desc.indirectArgsPipeline.IsValid(),
              "ParticleSimulationPass: indirect args pipeline handle is invalid");

    for (size_t i = 0; i < desc.emitters.size(); ++i) {
        UZ_CHECKF(desc.emitters[i].buffer != nullptr,
                  "ParticleSimulationPass: emitter[{}] has null GPUParticleBuffer", i);
        UZ_CHECKF(desc.emitters[i].buffer->IsValid(),
                  "ParticleSimulationPass: emitter[{}] GPUParticleBuffer is not allocated", i);
    }

    builder.AddPass("VFX_ParticleSimulation", [desc](rg::RgPassBuilder& pb) {
        // Declare render graph resource dependencies for automatic barrier
        // tracking. The graph handles frame-boundary transitions (e.g.,
        // SRV->UAV for the write set). Intra-pass UAV barriers between
        // spawn/update/indirect stages remain manual.
        for (const auto& emitter : desc.emitters) {
            if (emitter.rgWritePositions.IsValid()) {
                pb.Write(emitter.rgWritePositions,  RHIResourceState::UnorderedAccess);
                pb.Write(emitter.rgWriteVelocities, RHIResourceState::UnorderedAccess);
                pb.Write(emitter.rgWriteAges,       RHIResourceState::UnorderedAccess);
                pb.Write(emitter.rgWriteLifespans,  RHIResourceState::UnorderedAccess);
                pb.Write(emitter.rgWriteSizes,      RHIResourceState::UnorderedAccess);
                pb.Write(emitter.rgWriteColors,     RHIResourceState::UnorderedAccess);
                pb.Write(emitter.rgWriteRotations,  RHIResourceState::UnorderedAccess);
            }
            if (emitter.rgAliveCount.IsValid()) {
                pb.Write(emitter.rgAliveCount,           RHIResourceState::UnorderedAccess);
                pb.Write(emitter.rgFreeList,             RHIResourceState::UnorderedAccess);
                pb.Write(emitter.rgFreeListHead,         RHIResourceState::UnorderedAccess);
                pb.Write(emitter.rgIdToIndex,            RHIResourceState::UnorderedAccess);
                pb.Write(emitter.rgIndirectDrawArgs,     RHIResourceState::UnorderedAccess);
                pb.Write(emitter.rgIndirectDispatchArgs, RHIResourceState::UnorderedAccess);
            }
        }

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("VFX::ParticleSimulation");

            if (desc.emitters.empty()) return;

            for (size_t i = 0; i < desc.emitters.size(); ++i) {
                const auto& emitter = desc.emitters[i];
                const auto& gpuBuf  = *emitter.buffer;

                // ── Stage 1: Spawn ──────────────────────────────────────
                DispatchSpawn(cmd, desc, emitter);

                if (emitter.spawnCount > 0) {
                    EmitSimulationUAVBarriers(cmd, gpuBuf);
                }

                // ── Stage 2: Update ─────────────────────────────────────
                DispatchUpdate(cmd, desc, emitter);
                EmitSimulationUAVBarriers(cmd, gpuBuf);

                // ── Stage 3: Indirect args ──────────────────────────────
                DispatchIndirectArgs(cmd, desc, emitter);
                EmitIndirectArgsBarriers(cmd, gpuBuf);
            }

            // Log first execution for diagnostics.
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "ParticleSimulationPass: first execution -- {} emitters",
                       desc.emitters.size());
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
