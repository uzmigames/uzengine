/// VFX Particle Simulation Pass -- Render graph compute pass for GPU particle sim.
///
/// Registers a compute pass in the render graph that drives the full particle
/// simulation pipeline for every active emitter in the scene. For each emitter
/// the pass dispatches three compute shaders in sequence:
///
/// 1. **Spawn CS** -- emits `spawnCount` new particles into the write attribute set.
/// 2. **Update CS** -- steps physics, aging, and module evaluation for all alive particles.
/// 3. **Indirect Args CS** -- fills the indirect draw/dispatch argument buffers
///    from the alive count so subsequent rendering can use `DrawIndexedIndirect`.
///
/// UAV barriers are inserted between each stage to prevent read-after-write hazards
/// on the particle attribute buffers and bookkeeping counters.
///
/// # Render Graph Integration
/// The pass is registered as a single compute pass with no explicit render graph
/// resource tracking (all buffers are external persistent resources owned by
/// `GPUParticleBuffer`). Internal UAV barriers are emitted explicitly inside the
/// execution callback.
///
/// # Pipeline Root Signature Layout
/// Matches the global engine root signature:
/// - Slot 1 (b1): Root CBV for per-dispatch constants.
/// - Slot 4 (u0..u3): UAV descriptor table for output buffers.
/// - Slot 5 (t0..tN): SRV descriptor table for read-only input buffers.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread only.

#pragma once

#include "cb_ring_allocator.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

struct GPUParticleBuffer;
struct EmitterModuleStack;
class ModuleCompositor;

} // namespace uz

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ---------------------------------------------------------------------------
// ParticleSimConstants -- CPU-side mirror of particle sim constant buffers
// ---------------------------------------------------------------------------

/// CPU-side mirror of the spawn shader's constant buffer (register b1).
///
/// Contains the number of particles to spawn and the emitter's current
/// random seed. Must be byte-identical to the HLSL cbuffer layout.
///
/// Total size: 16 bytes (256-byte aligned by CBRingAllocator).
struct ParticleSpawnConstants {
    uint32_t spawnCount  = 0;  ///< Number of new particles to emit this frame.
    uint32_t randomSeed  = 0;  ///< Per-frame random seed for spawn position/velocity jitter.
    uint32_t capacity    = 0;  ///< Buffer capacity (max particles).
    uint32_t _pad0       = 0;  ///< Padding to 16-byte boundary.
};

/// CPU-side mirror of the update shader's constant buffer (register b1).
///
/// Contains the delta time for integration and the current alive count.
/// Must be byte-identical to the HLSL cbuffer layout.
///
/// Total size: 16 bytes (256-byte aligned by CBRingAllocator).
struct ParticleUpdateConstants {
    float    deltaTime   = 0.0f; ///< Frame delta time in seconds for physics integration.
    uint32_t aliveCount  = 0;    ///< Number of alive particles to process.
    uint32_t capacity    = 0;    ///< Buffer capacity (max particles).
    uint32_t _pad0       = 0;    ///< Padding to 16-byte boundary.
};

/// CPU-side mirror of the indirect args shader's constant buffer (register b1).
///
/// The indirect args shader reads the alive count from GPU memory and writes
/// draw/dispatch argument structs. This CB provides supplementary parameters.
///
/// Total size: 16 bytes (256-byte aligned by CBRingAllocator).
struct ParticleIndirectArgsConstants {
    uint32_t verticesPerParticle = 6; ///< Vertices per particle (6 for a billboard quad: 2 triangles).
    uint32_t threadGroupSize     = 64; ///< Thread group size for dispatch indirect args.
    uint32_t _pad0               = 0;  ///< Padding to 16-byte boundary.
    uint32_t _pad1               = 0;  ///< Padding to 16-byte boundary.
};

// ---------------------------------------------------------------------------
// EmitterSimDispatch -- per-emitter data for the simulation pass
// ---------------------------------------------------------------------------

/// Per-emitter dispatch data for the particle simulation pass.
///
/// Contains the GPU buffer pointers, pre-filled constant buffer allocations,
/// and descriptor heap indices needed to dispatch all three compute shaders
/// (spawn, update, indirect args) for a single emitter.
///
/// # Ownership
/// Does NOT own any of the pointed-to resources. The caller must ensure
/// `buffer` remains valid until `RgBuilder::Execute()` completes.
struct EmitterSimDispatch {
    /// The emitter's GPU particle storage (ping-pong attribute sets + bookkeeping).
    GPUParticleBuffer* buffer = nullptr;

    /// Pre-filled CB allocation for the spawn shader constants.
    CBAllocation spawnCB = {};

    /// Pre-filled CB allocation for the update shader constants.
    CBAllocation updateCB = {};

    /// Pre-filled CB allocation for the indirect args shader constants.
    CBAllocation indirectCB = {};

    /// Per-emitter composed spawn PSO (from module compositor). When valid,
    /// overrides desc.spawnPipeline for this emitter.
    RHIPipelineHandle composedSpawnPSO;

    /// Per-emitter composed update PSO (from module compositor). When valid,
    /// overrides desc.updatePipeline for this emitter.
    RHIPipelineHandle composedUpdatePSO;

    /// Number of particles to spawn this frame. May be 0 (skip spawn dispatch).
    uint32_t spawnCount = 0;

    /// Number of currently alive particles (from CPU readback of previous frame).
    uint32_t aliveCount = 0;

    // -- Descriptor heap indices for SRV/UAV binding (SM5 path) ---------------

    /// SRV index for the read attribute set (positions, velocities, etc.).
    uint32_t readSetSrvIndex = UINT32_MAX;

    /// UAV index for the write attribute set.
    uint32_t writeSetUavIndex = UINT32_MAX;

    /// UAV index for the alive count buffer.
    uint32_t aliveCountUavIndex = UINT32_MAX;

    /// UAV index for the free list buffer.
    uint32_t freeListUavIndex = UINT32_MAX;

    /// UAV index for the indirect draw args buffer.
    uint32_t indirectDrawArgsUavIndex = UINT32_MAX;

    /// UAV index for the indirect dispatch args buffer.
    uint32_t indirectDispatchArgsUavIndex = UINT32_MAX;

    // -- Render graph resource IDs (for automatic barrier tracking) -----------

    /// RgResourceIds for the 7 write-set attribute buffers.
    /// Invalid (default) if not imported into the render graph.
    rg::RgResourceId rgWritePositions;
    rg::RgResourceId rgWriteVelocities;
    rg::RgResourceId rgWriteAges;
    rg::RgResourceId rgWriteLifespans;
    rg::RgResourceId rgWriteSizes;
    rg::RgResourceId rgWriteColors;
    rg::RgResourceId rgWriteRotations;

    /// RgResourceIds for shared bookkeeping buffers.
    rg::RgResourceId rgAliveCount;
    rg::RgResourceId rgFreeList;
    rg::RgResourceId rgFreeListHead;
    rg::RgResourceId rgIdToIndex;
    rg::RgResourceId rgIndirectDrawArgs;
    rg::RgResourceId rgIndirectDispatchArgs;
};

// ---------------------------------------------------------------------------
// ParticleSimulationPassDesc -- full pass configuration
// ---------------------------------------------------------------------------

/// Configuration for the GPU particle simulation render graph pass.
///
/// All fields must be set before calling `AddParticleSimulationPass`. The pass
/// does not take ownership of any pointed-to objects -- the caller must ensure
/// they remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct ParticleSimulationPassDesc {
    // -- Pipeline handles for the 3 compute shaders ----------------------------

    /// Compute PSO for the particle spawn shader.
    RHIPipelineHandle spawnPipeline;

    /// Compute PSO for the particle update shader.
    RHIPipelineHandle updatePipeline;

    /// Compute PSO for the indirect args generation shader.
    RHIPipelineHandle indirectArgsPipeline;

    // -- Per-emitter dispatch data --------------------------------------------

    /// Array of active emitters to simulate this frame.
    /// Empty means the pass is a no-op (but still registered for graph consistency).
    std::vector<EmitterSimDispatch> emitters;

    /// Small upload-heap buffer containing a single uint32 = 0.
    /// Used to zero-clear aliveCount each frame via CopyBuffer.
    RHIBufferHandle zeroBuffer;
};

// ---------------------------------------------------------------------------
// AddParticleSimulationPass
// ---------------------------------------------------------------------------

/// Registers the GPU particle simulation compute pass in the render graph.
///
/// For each emitter in `desc.emitters`, the pass executes:
/// 1. Bind spawn pipeline, set spawn CB, dispatch ceil(spawnCount / 64) groups.
/// 2. UAV barrier on all written buffers (attribute set, alive count, free list).
/// 3. Bind update pipeline, set update CB, dispatch ceil(aliveCount / 64) groups.
/// 4. UAV barrier on all written buffers.
/// 5. Bind indirect args pipeline, set indirect CB, dispatch 1 group.
/// 6. UAV barrier on indirect args buffers.
///
/// Emitters with `spawnCount == 0` skip the spawn dispatch but still run
/// update and indirect args.
///
/// # Arguments
/// * `builder` -- Render graph builder.
/// * `desc` -- Pass configuration (pipelines, per-emitter dispatch data).
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation.
///
/// # Panics
/// Asserts if any pipeline handle is invalid or if any emitter's `buffer` is null.
void AddParticleSimulationPass(rg::RgBuilder& builder,
                               const ParticleSimulationPassDesc& desc);

// ---------------------------------------------------------------------------
// ComposeEmitterShaders -- module composition integration point
// ---------------------------------------------------------------------------

/// Composes HLSL shaders for an emitter's module stack if dirty.
///
/// Checks `stack.dirty` and, if true, invokes the `ModuleCompositor` to
/// regenerate `composedUpdateHLSL` and `composedSpawnHLSL` from the current
/// module lists. Clears the dirty flag and updates the cache key on success.
///
/// If the module stack is empty (no modules), this is a no-op -- the emitter
/// falls back to the hardcoded shaders from 3.60.0.
///
/// # Parameters
/// * `stack` -- The emitter's module stack (modified in place).
/// * `compositor` -- The module compositor used to generate HLSL.
///
/// # Returns
/// `true` if composition succeeded or was not needed (stack clean or empty).
/// `false` if composition failed (e.g., all modules returned empty shader code).
///
/// # Side Effects
/// - Updates `stack.composedUpdateHLSL`, `stack.composedSpawnHLSL`,
///   `stack.composedCacheKey`, and clears `stack.dirty` on success.
/// - Logs VFX/Info when shaders are recomposed.
[[nodiscard]] bool ComposeEmitterShaders(EmitterModuleStack& stack,
                                         const ModuleCompositor& compositor);

} // namespace uz
