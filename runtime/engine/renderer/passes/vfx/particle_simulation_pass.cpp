/// VFX Particle Simulation Pass -- Implementation
///
/// Registers and executes a compute pass that runs the full particle simulation
/// pipeline (spawn, update, indirect args) for every active emitter in the scene.

#include "particle_simulation_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"
#include "engine/shader/shader_compiler.h"
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

    // Use per-emitter composed PSO if available, otherwise fall back to hardcoded.
    const auto spawnPSO = emitter.composedSpawnPSO.IsValid()
        ? emitter.composedSpawnPSO : desc.spawnPipeline;
    cmd->SetPipeline(spawnPSO);

    // Bind per-dispatch CB at slot 1 (b1) via root CBV using GPU virtual address.
    cmd->SetRootCBV(kSlotPerDispatch, emitter.spawnCB.gpuAddr);

    // In bindless mode (SM6.6), SRV/UAV indices are passed as root constants
    // in b0 — no descriptor tables needed. The VFX shaders read buffer indices
    // from the spawn/update constant buffer, not from descriptor table bindings.

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
    // Use per-emitter composed PSO if available, otherwise fall back to hardcoded.
    const auto updatePSO = emitter.composedUpdatePSO.IsValid()
        ? emitter.composedUpdatePSO : desc.updatePipeline;
    cmd->SetPipeline(updatePSO);

    // Bind per-dispatch CB at slot 1 (b1) via root CBV using GPU virtual address.
    cmd->SetRootCBV(kSlotPerDispatch, emitter.updateCB.gpuAddr);

    // Bindless: SRV/UAV indices passed via CB, not descriptor tables.

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

/// Compiles an HLSL source string into a compute PSO via DXC runtime compilation.
///
/// # Returns
/// Valid pipeline handle on success, invalid handle on failure (logged).
static RHIPipelineHandle CompileComposedCS(
    IRHIDevice* device, ShaderCompiler& compiler,
    const std::string& hlslSource, const char* debugName)
{
    if (hlslSource.empty()) return {};

    ShaderCompileDesc compileDesc;
    compileDesc.source = hlslSource;
    compileDesc.entryPoint = "main";
    compileDesc.sourceName = debugName;
    compileDesc.stage = ShaderStageCompile::Compute;
    compileDesc.model = ShaderModelCompile::SM6_6;
    compileDesc.output = ShaderOutputFormat::DXIL;

    auto result = compiler.Compile(compileDesc);
    if (!result.success) {
        UZ_LOG(VFX, Error,
               "CompileComposedCS: DXC compilation failed for '{}': {}",
               debugName, result.errors);
        return {};
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode = result.bytecode.data();
    shaderDesc.bytecodeSize = result.bytecode.size();
    shaderDesc.stage = RHIShaderStage::Compute;
    shaderDesc.entryPoint = "main";
    shaderDesc.debugName = debugName;

    auto shaderHandle = device->CreateShader(shaderDesc);
    if (!shaderHandle.IsValid()) {
        UZ_LOG(VFX, Error, "CompileComposedCS: CreateShader failed for '{}'", debugName);
        return {};
    }

    RHIComputePipelineDesc psoDesc{};
    psoDesc.computeShader = shaderHandle;
    psoDesc.debugName = debugName;

    auto pso = device->CreateComputePipeline(psoDesc);
    if (!pso.IsValid()) {
        UZ_LOG(VFX, Error, "CompileComposedCS: CreateComputePipeline failed for '{}'", debugName);
        return {};
    }

    UZ_LOG(VFX, Info, "CompileComposedCS: compiled '{}' ({} bytes DXIL)",
           debugName, result.bytecode.size());
    return pso;
}

bool ComposeEmitterShaders(EmitterModuleStack& stack,
                           const ModuleCompositor& compositor,
                           IRHIDevice* device,
                           ShaderCompiler& compiler)
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

    // Compile composed HLSL into compute PSOs via DXC runtime compilation.
    if (!stack.composedUpdateHLSL.empty()) {
        stack.composedUpdatePSO = CompileComposedCS(
            device, compiler, stack.composedUpdateHLSL,
            "VFX_ComposedUpdate");
    }

    if (!stack.composedSpawnHLSL.empty()) {
        stack.composedSpawnPSO = CompileComposedCS(
            device, compiler, stack.composedSpawnHLSL,
            "VFX_ComposedSpawn");
    }

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
                if (!emitter.buffer) continue;

                // Inter-pass barriers (UAV→SRV for render, UAV→IndirectArgument)
                // are handled by the render graph. Intra-pass barriers between
                // sequential dispatches on the same command list are manual.

                // Zero the alive count buffer before this frame's sim.
                // The update CS atomically increments this for each surviving
                // particle. Without the reset, alive count accumulates across
                // frames → indirect args uses stale count → particles stop spawning.
                //
                // UAV→CopyDst → copy zero → CopyDst→UAV + UAV barrier.
                if (emitter.buffer->aliveCount.IsValid() && desc.zeroBuffer.IsValid()) {
                    RHIBarrierDesc toCopy{};
                    toCopy.buffer = emitter.buffer->aliveCount;
                    toCopy.stateBefore = RHIResourceState::UnorderedAccess;
                    toCopy.stateAfter  = RHIResourceState::CopyDst;
                    cmd->ResourceBarrier(&toCopy, 1);

                    cmd->CopyBuffer(desc.zeroBuffer, 0,
                                    emitter.buffer->aliveCount, 0, sizeof(uint32_t));

                    RHIBarrierDesc toUav{};
                    toUav.buffer = emitter.buffer->aliveCount;
                    toUav.stateBefore = RHIResourceState::CopyDst;
                    toUav.stateAfter  = RHIResourceState::UnorderedAccess;
                    cmd->ResourceBarrier(&toUav, 1);
                }

                // freeList transitions UAV→SRV for spawn CS which reads it
                // via StructuredBuffer<uint> (SRV descriptor). The buffer is
                // in UAV from Create/UploadInitialState.
                {
                    RHIBarrierDesc barrier{};
                    barrier.buffer      = emitter.buffer->freeList;
                    barrier.stateBefore = RHIResourceState::UnorderedAccess;
                    barrier.stateAfter  = RHIResourceState::ShaderResource;
                    cmd->ResourceBarrier(&barrier, 1);
                }

                DispatchSpawn(cmd, desc, emitter);

                // freeList back to UAV for update CS (which may recycle dead
                // particles back to the free list via RWStructuredBuffer).
                {
                    RHIBarrierDesc barrier{};
                    barrier.buffer      = emitter.buffer->freeList;
                    barrier.stateBefore = RHIResourceState::ShaderResource;
                    barrier.stateAfter  = RHIResourceState::UnorderedAccess;
                    cmd->ResourceBarrier(&barrier, 1);
                }

                DispatchUpdate(cmd, desc, emitter);

                // aliveCount transitions from UAV (written by spawn+update via
                // atomics) to SRV (read by indirect args CS via bindless
                // StructuredBuffer<uint>). DX12 does NOT auto-insert UAV→SRV
                // barriers between dispatches on the same command list.
                {
                    RHIBarrierDesc barrier{};
                    barrier.buffer      = emitter.buffer->aliveCount;
                    barrier.stateBefore = RHIResourceState::UnorderedAccess;
                    barrier.stateAfter  = RHIResourceState::ShaderResource;
                    cmd->ResourceBarrier(&barrier, 1);
                }

                DispatchIndirectArgs(cmd, desc, emitter);

                // Transition aliveCount back to UAV so the render graph's
                // tracked exit state remains correct (the graph expects all
                // sim-pass buffers to end in UAV).
                {
                    RHIBarrierDesc barrier{};
                    barrier.buffer      = emitter.buffer->aliveCount;
                    barrier.stateBefore = RHIResourceState::ShaderResource;
                    barrier.stateAfter  = RHIResourceState::UnorderedAccess;
                    cmd->ResourceBarrier(&barrier, 1);
                }
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
