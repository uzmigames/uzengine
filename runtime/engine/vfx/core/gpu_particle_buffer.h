#pragma once

/// GPU Particle Buffer -- SoA ping-pong double-buffered storage.
///
/// Adapted from UE5 Niagara's FNiagaraDataBuffer pattern. Per-particle
/// attributes are stored in Structure-of-Arrays layout across two attribute
/// sets. Each frame, compute shaders read from the "read" set and write to
/// the "write" set; after simulation, Swap() toggles them.
///
/// Shared bookkeeping buffers (alive count, free list, indirect args) are
/// NOT double-buffered -- they are updated in-place by GPU atomics.
///
/// # SoA Layout (per attribute set)
/// - positions:  float3 per particle (12 bytes)
/// - velocities: float3 per particle (12 bytes)
/// - ages:       float  per particle (4 bytes)
/// - lifespans:  float  per particle (4 bytes)
/// - sizes:      float  per particle (4 bytes)
/// - colors:     float4 per particle (16 bytes, RGBA)
/// - rotations:  float  per particle (4 bytes, 2D angle in radians)
/// Total: 56 bytes per particle per set, 112 bytes for both sets.
///
/// # Ping-Pong
/// `readIndex` and `writeIndex` alternate between 0 and 1 each frame.
/// The compute shader reads from `sets[readIndex]` and writes to
/// `sets[writeIndex]`. After dispatch, `Swap()` flips both indices via XOR.
///
/// # Memory Budget
/// Total per particle: ~120 bytes (112 for attributes + 8 for shared buffers).
/// Example: 50K particles ~ 6 MB GPU memory.

#include "gpu_particle_sort.h"

#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;

// ---------------------------------------------------------------------------
// ParticleAttributeSet -- one half of the ping-pong pair
// ---------------------------------------------------------------------------

/// Per-slot SoA buffers for one side of the ping-pong pair.
///
/// Each slot contains all the structured buffers needed to represent
/// the full particle state. The compute shader binds one slot as SRV
/// (read) and the other as UAV (write).
///
/// # Validity
/// `IsValid()` returns true only when ALL seven attribute buffers have been
/// successfully allocated. A partially-allocated set is considered invalid.
struct ParticleAttributeSet {
    /// Per-particle positions (float3, 12 bytes stride).
    RHIBufferHandle positions = RHIBufferHandle::Invalid();

    /// Per-particle velocities (float3, 12 bytes stride).
    RHIBufferHandle velocities = RHIBufferHandle::Invalid();

    /// Per-particle current age in seconds (float, 4 bytes stride).
    RHIBufferHandle ages = RHIBufferHandle::Invalid();

    /// Per-particle maximum lifespan in seconds (float, 4 bytes stride).
    RHIBufferHandle lifespans = RHIBufferHandle::Invalid();

    /// Per-particle size (float, 4 bytes stride).
    RHIBufferHandle sizes = RHIBufferHandle::Invalid();

    /// Per-particle color (float4 RGBA, 16 bytes stride).
    RHIBufferHandle colors = RHIBufferHandle::Invalid();

    /// Per-particle 2D rotation angle in radians (float, 4 bytes stride).
    RHIBufferHandle rotations = RHIBufferHandle::Invalid();

    /// Returns true if all attribute buffers have been allocated.
    [[nodiscard]] bool IsValid() const noexcept;
};

// ---------------------------------------------------------------------------
// GPUParticleBuffer -- main double-buffered particle storage
// ---------------------------------------------------------------------------

/// GPU-resident particle storage with ping-pong double buffering.
///
/// Each frame, compute shaders read from the "read" attribute set and write
/// to the "write" set. After simulation, call Swap() to toggle them.
///
/// Shared buffers (alive count, free list, indirect args) are written by GPU
/// atomics and consumed by indirect draw/dispatch -- they are NOT double-buffered.
///
/// # Lifetime
/// Call Create() to allocate all GPU buffers, Destroy() to release them.
/// The buffer is not usable until Create() succeeds (IsValid() returns false).
///
/// # Thread Safety
/// Not thread-safe. Intended for single-threaded render thread use.
///
/// # Example
/// ```cpp
/// GPUParticleBuffer buffer;
/// buffer.Create(device, 50000);
/// // ... each frame:
/// auto& read = buffer.ReadSet();   // bind as SRV
/// auto& write = buffer.WriteSet(); // bind as UAV
/// // ... dispatch compute ...
/// buffer.Swap();
/// // ... on shutdown:
/// buffer.Destroy(device);
/// ```
struct GPUParticleBuffer {
    /// The two ping-pong attribute sets.
    ParticleAttributeSet sets[2] = {};

    /// Index of the slot currently used as the read source (0 or 1).
    uint32_t readIndex = 0;

    /// Index of the slot currently used as the write target (0 or 1).
    uint32_t writeIndex = 1;

    /// Maximum capacity (number of particles this buffer can hold).
    uint32_t capacity = 0;

    // -- Shared buffers (not double-buffered) ---------------------------------

    /// GPU atomic counter: number of alive particles (single uint32).
    /// The compute shader increments this atomically; the CPU reads it back
    /// once per frame for budget tracking.
    RHIBufferHandle aliveCount = RHIBufferHandle::Invalid();

    /// GPU free list: recycled particle indices stored as a stack.
    /// Size: capacity * sizeof(uint32_t).
    RHIBufferHandle freeList = RHIBufferHandle::Invalid();

    /// GPU atomic counter: current top-of-stack index into freeList (single uint32).
    RHIBufferHandle freeListHead = RHIBufferHandle::Invalid();

    /// Maps ParticleID -> buffer index. 0xFFFFFFFF = dead/invalid.
    /// Size: capacity * sizeof(uint32_t).
    RHIBufferHandle idToIndex = RHIBufferHandle::Invalid();

    /// Indirect draw arguments (written by GPU compute, consumed by DrawIndexedIndirect).
    /// Layout: RHIDrawIndexedIndirectArgs (20 bytes).
    RHIBufferHandle indirectDrawArgs = RHIBufferHandle::Invalid();

    /// Indirect dispatch arguments (written by GPU compute, consumed by DispatchIndirect).
    /// Layout: RHIDispatchIndirectArgs (12 bytes).
    RHIBufferHandle indirectDispatchArgs = RHIBufferHandle::Invalid();

    // -- Mesh instance buffers (optional, for MeshRenderModule emitters) -----

    /// Per-particle instance transform buffer (float4x4 per alive particle).
    /// Written by particle_mesh_instance_cs.hlsl as UAV, consumed by the mesh
    /// render pass as SRV. Only allocated when `hasMeshBuffers` is true.
    RHIBufferHandle meshInstanceTransforms = RHIBufferHandle::Invalid();

    /// Atomic counter for the number of alive mesh instances written this frame.
    /// Single uint32 element, incremented by InterlockedAdd in the instance CS.
    /// Only allocated when `hasMeshBuffers` is true.
    RHIBufferHandle meshInstanceCount = RHIBufferHandle::Invalid();

    /// Indirect draw arguments for DrawIndexedInstancedIndirect (20 bytes).
    /// Written by the instance CS, consumed by the mesh render pass.
    /// Only allocated when `hasMeshBuffers` is true.
    RHIBufferHandle meshIndirectDrawArgs = RHIBufferHandle::Invalid();

    /// Whether the mesh instance buffers have been allocated.
    /// True only for emitters using MeshRenderModule.
    bool hasMeshBuffers = false;

    // -- Sort buffers (for depth-ordered transparent rendering) ---------------

    /// Bitonic merge sort manager for back-to-front particle ordering.
    /// Owns sortKeys and sortIndices structured buffers. Created alongside the
    /// main particle buffers in Create() and destroyed in Destroy().
    ///
    /// @ref Batcher 1968 "Sorting Networks and Their Applications" — bitonic merge sort
    GPUParticleSort sort;

    /// Whether this buffer has been through at least one full sim+render cycle.
    /// Used by the render graph import to determine initial resource states.
    /// On frame 0, all buffers are in UAV (from Create). After the first
    /// complete cycle, the write set is left in SRV (from render) and the
    /// read set is left in UAV (from sim).
    bool hasCompletedCycle = false;

    // -- Accessors ------------------------------------------------------------

    /// Returns the attribute set currently used as the read source (SRV).
    [[nodiscard]] const ParticleAttributeSet& ReadSet() const noexcept {
        return sets[readIndex];
    }

    /// Returns the attribute set currently used as the write target (UAV).
    [[nodiscard]] ParticleAttributeSet& WriteSet() noexcept {
        return sets[writeIndex];
    }

    /// Returns the attribute set currently used as the write target (UAV, const).
    [[nodiscard]] const ParticleAttributeSet& WriteSet() const noexcept {
        return sets[writeIndex];
    }

    /// Swaps read and write sets after simulation completes.
    ///
    /// Uses XOR to toggle both indices simultaneously. Call once per frame
    /// after the simulation compute shader has finished.
    void Swap() noexcept { readIndex ^= 1; writeIndex ^= 1; }

    /// Returns true if the buffer has been successfully allocated.
    ///
    /// Checks that capacity is non-zero and the first attribute set is valid.
    [[nodiscard]] bool IsValid() const noexcept {
        return capacity > 0 && sets[0].IsValid();
    }

    // -- Lifecycle ------------------------------------------------------------

    /// Allocates all GPU buffers for the given particle capacity.
    ///
    /// Creates both ping-pong attribute sets and all shared bookkeeping buffers.
    /// All buffers use StructuredBuffer | UnorderedAccess usage flags.
    ///
    /// # Parameters
    /// - `device`: RHI device for buffer creation. Must not be null.
    /// - `particleCapacity`: Maximum number of particles. Must be > 0.
    ///
    /// # Returns
    /// `true` on success, `false` if any buffer allocation fails.
    /// On failure, all partially allocated buffers are cleaned up.
    ///
    /// # Side Effects
    /// - Logs VFX::Info on success with capacity and estimated memory.
    /// - Logs VFX::Error on failure.
    ///
    /// # Preconditions
    /// - `device` must not be null (fatal assert in debug).
    /// - `particleCapacity` must be > 0 (fatal assert in debug).
    /// - Buffer must not already be allocated (fatal assert). Call Destroy() first.
    [[nodiscard]] bool Create(IRHIDevice* device, uint32_t particleCapacity);

    /// Allocates the optional mesh instance buffers for MeshRenderModule emitters.
    ///
    /// Creates three additional GPU buffers: instance transforms (float4x4 per
    /// particle), instance count (single uint32 atomic counter), and mesh indirect
    /// draw arguments (20 bytes DrawIndexedInstancedIndirect).
    ///
    /// Must be called AFTER a successful `Create()` — the base buffer must already
    /// be allocated so `capacity` is known.
    ///
    /// # Parameters
    /// - `device`: RHI device for buffer creation. Must not be null.
    ///
    /// # Returns
    /// `true` on success, `false` if any allocation fails (partial allocs cleaned up).
    ///
    /// # Preconditions
    /// - `IsValid()` must be true (base buffers allocated).
    /// - `hasMeshBuffers` must be false (not already allocated).
    [[nodiscard]] bool CreateMeshBuffers(IRHIDevice* device);

    /// Releases the optional mesh instance buffers and resets handles to invalid.
    ///
    /// No-op if `hasMeshBuffers` is false or `device` is null.
    ///
    /// # Parameters
    /// - `device`: RHI device for deallocation. May be null (no-op).
    void DestroyMeshBuffers(IRHIDevice* device);

    /// Releases all GPU buffers and resets state.
    ///
    /// All handles become invalid after this call. Safe to call on an
    /// already-destroyed or never-created buffer (no-op if device is null).
    ///
    /// # Parameters
    /// - `device`: RHI device for deallocation. May be null (no-op).
    void Destroy(IRHIDevice* device);
};

}  // namespace uz
