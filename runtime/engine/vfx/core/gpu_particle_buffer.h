#pragma once

/// GPU Particle Buffer -- SoA ping-pong double-buffered storage.
///
/// @ref SoA (Structure of Arrays) GPU particle buffer pattern — per-particle
/// attributes stored across two attribute sets. Each frame, compute shaders
/// read from the "read" set and write to the "write" set; after simulation,
/// Swap() toggles them.
///
/// Shared bookkeeping buffers (alive count, free list, indirect args) are
/// NOT double-buffered -- they are updated in-place by GPU atomics.
///
/// # SoA Layout (per attribute set)
/// - positions:  float4 per particle (16 bytes, w unused/padding)
/// - velocities: float4 per particle (16 bytes, w unused/padding)
/// - ages:       float  per particle (4 bytes)
/// - lifespans:  float  per particle (4 bytes)
/// - sizes:      float  per particle (4 bytes)
/// - colors:     float4 per particle (16 bytes, RGBA)
/// - rotations:  float  per particle (4 bytes, 2D angle in radians)
/// Total: 64 bytes per particle per set, 128 bytes for both sets.
///
/// # Ping-Pong
/// `readIndex` and `writeIndex` alternate between 0 and 1 each frame.
/// The compute shader reads from `sets[readIndex]` and writes to
/// `sets[writeIndex]`. After dispatch, `Swap()` flips both indices via XOR.
///
/// # Memory Budget
/// Total per particle: ~128 bytes (128 for attributes + shared buffers overhead).
/// Example: 50K particles ~ 6.25 MB GPU memory.

#include "gpu_particle_sort.h"

#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHICommandList;
class IRHIDevice;

// ---------------------------------------------------------------------------
// ParticleDeathEvent -- GPU death event written by the update CS
// ---------------------------------------------------------------------------

/// Represents a particle death event for decal spawning.
///
/// Written by the particle update compute shader when a particle's age exceeds
/// its lifespan and death decals are enabled. The CPU reads these events back
/// (with 1-frame latency) and injects them into the DecalManager.
///
/// Layout matches the HLSL `ParticleDeathEvent` struct exactly (48 bytes,
/// 16-byte aligned). Must be kept in sync with `particle_update_cs.hlsl`.
///
/// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR" -- deferred decal
///      projection for particle death marks
struct ParticleDeathEvent {
    float position[3] = {};  ///< World-space death position (12 bytes).
    float size         = 0.0f; ///< Particle size at death (4 bytes).
    float color[4]     = {};  ///< Particle RGBA color at death (16 bytes).
    float normal[3]    = {};  ///< Estimated surface normal at death (12 bytes).
    float _pad         = 0.0f; ///< Padding to 48 bytes (16-byte alignment).
};

static_assert(sizeof(ParticleDeathEvent) == 48,
              "ParticleDeathEvent must be exactly 48 bytes for GPU struct parity");

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
    /// Per-particle positions (float4, 16 bytes stride; w component unused).
    RHIBufferHandle positions = RHIBufferHandle::Invalid();

    /// Per-particle velocities (float4, 16 bytes stride; w component unused).
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

    /// Per-particle sub-image (flipbook) index for SubUV animation (float, 4 bytes stride).
    /// Computed by the update CS as `(age / lifespan) * (totalFrames - 1)`.
    /// The integer part selects the current frame; the fractional part is the blend
    /// factor to the next frame. Initialized to 0.0 by the spawn CS.
    /// @ref UE5 Niagara SubImageIndex module
    RHIBufferHandle subImageIndices = RHIBufferHandle::Invalid();

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

    // -- Death event buffers (for decal spawning on particle death) -----------

    /// Ring buffer for particle death events written by the update CS.
    /// CPU reads and clears this each frame (1-frame latency allowed).
    /// Stride = sizeof(ParticleDeathEvent) = 48 bytes, count = capacity.
    /// Only allocated when `hasDeathEventBuffers` is true.
    RHIBufferHandle deathEventBuffer = RHIBufferHandle::Invalid();

    /// Atomic counter for the number of death events written this frame.
    /// Single uint32 element, incremented by InterlockedAdd in the update CS.
    /// Reset to 0 via ClearUAV each frame before the update dispatch.
    /// Only allocated when `hasDeathEventBuffers` is true.
    RHIBufferHandle deathEventCount = RHIBufferHandle::Invalid();

    /// Whether the death event buffers have been allocated.
    /// True only for emitters with `emitDeathDecals` enabled.
    bool hasDeathEventBuffers = false;

    // -- Sort buffers (for depth-ordered transparent rendering) ---------------

    /// Bitonic merge sort manager for back-to-front particle ordering.
    /// Owns sortKeys and sortIndices structured buffers. Created alongside the
    /// main particle buffers in Create() and destroyed in Destroy().
    ///
    /// @ref Batcher 1968 "Sorting Networks and Their Applications" — bitonic merge sort
    GPUParticleSort sort;

    /// Staging buffers pending destruction after UploadInitialState + WaitIdle.
    std::vector<RHIBufferHandle> pendingStagingBuffers;

    /// Whether this buffer has been through at least one full sim+render cycle.
    /// Used by the render graph import to determine initial resource states.
    /// On frame 0, all buffers are in UAV (from Create). After the first
    /// complete cycle, the write set is left in SRV (from render) and the
    /// read set is left in UAV (from sim).
    bool hasCompletedCycle = false;

    /// Whether the GPU buffers (freeList, freeListHead, aliveCount) still need
    /// their initial data uploaded. Set to true after Create(), cleared to false
    /// after InitializeData() successfully copies staging data to the GPU buffers.
    bool needsInitialization = false;

    // -- Staging buffers for one-time initialization upload --------------------

    /// Upload-heap staging buffer for the free list: [0, 1, 2, ..., capacity-1].
    /// Kept alive until DestroyStaging() is called after the GPU copy completes.
    RHIBufferHandle freeListStaging = RHIBufferHandle::Invalid();

    /// Upload-heap staging buffer for the free list head counter (single uint32).
    RHIBufferHandle freeListHeadStaging = RHIBufferHandle::Invalid();

    /// Upload-heap staging buffer for the alive count counter (single uint32).
    RHIBufferHandle aliveCountStaging = RHIBufferHandle::Invalid();

    /// Upload-heap staging buffer for zeroing lifespans/ages.
    RHIBufferHandle zeroStagingBuffer = RHIBufferHandle::Invalid();

    /// Frame index when InitializeData ran. Used to defer staging buffer
    /// destruction by kFramesInFlight frames to ensure the GPU has finished
    /// executing the CopyBuffer commands before the staging memory is freed.
    uint32_t initFrameIndex = UINT32_MAX;

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

    /// Allocates the optional death event buffers for decal spawning.
    ///
    /// Creates two additional GPU buffers: death event structured buffer
    /// (ParticleDeathEvent per particle) and death event count (single uint32
    /// atomic counter).
    ///
    /// Must be called AFTER a successful `Create()` -- the base buffer must
    /// already be allocated so `capacity` is known.
    ///
    /// # Parameters
    /// - `device`: RHI device for buffer creation. Must not be null.
    ///
    /// # Returns
    /// `true` on success, `false` if any allocation fails.
    ///
    /// # Preconditions
    /// - `IsValid()` must be true (base buffers allocated).
    /// - `hasDeathEventBuffers` must be false (not already allocated).
    [[nodiscard]] bool CreateDeathEventBuffers(IRHIDevice* device);

    /// Releases the optional death event buffers and resets handles to invalid.
    ///
    /// No-op if `hasDeathEventBuffers` is false or `device` is null.
    void DestroyDeathEventBuffers(IRHIDevice* device);

    /// Releases all GPU buffers and resets state.
    ///
    /// All handles become invalid after this call. Safe to call on an
    /// already-destroyed or never-created buffer (no-op if device is null).
    ///
    /// # Parameters
    /// - `device`: RHI device for deallocation. May be null (no-op).
    void Destroy(IRHIDevice* device);

    /// Uploads initial data to the GPU bookkeeping buffers via staging copies.
    ///
    /// Creates Upload-heap staging buffers, fills them with initial data, and
    /// issues CopyBuffer commands to transfer data to the GPU-resident buffers:
    /// - freeList:     [0, 1, 2, ..., capacity-1] — all slots initially free.
    /// - freeListHead: capacity — stack pointer starts at the top.
    /// - aliveCount:   0 — no particles alive yet.
    ///
    /// After the copies, UAV barriers are emitted so compute shaders see the
    /// initialized data. Staging buffers are kept alive for GPU copy completion;
    /// call DestroyStaging() after the command list has been submitted and the
    /// GPU has finished the copy (typically next frame or after a fence).
    ///
    /// # Parameters
    /// - `device`: RHI device for staging buffer creation. Must not be null.
    /// - `cmd`: Command list to record copy and barrier commands. Must not be null.
    ///
    /// # Returns
    /// `true` on success, `false` if staging buffer creation or mapping fails.
    ///
    /// # Preconditions
    /// - `IsValid()` must be true (base buffers already allocated via Create).
    /// - `needsInitialization` must be true.
    [[nodiscard]] bool InitializeData(IRHIDevice* device, IRHICommandList* cmd);

    /// Releases the staging buffers used by InitializeData().
    ///
    /// Call after the GPU has finished executing the copy commands (e.g., after
    /// a fence wait or at the start of the next frame). Safe to call multiple
    /// times or if staging buffers were never created (no-op).
    ///
    /// # Parameters
    /// - `device`: RHI device for deallocation. May be null (no-op).
    void DestroyStaging(IRHIDevice* device);

    /// Initializes the free list, alive count, and attribute buffers on the GPU.
    ///
    /// Alternative to InitializeData() — uses CopySrc staging buffers and stores
    /// them in pendingStagingBuffers for deferred destruction after WaitIdle.
    /// Must be called ONCE after Create() and BEFORE any compute dispatch.
    /// Uploads: freeList[i] = i, freeListHead = capacity, aliveCount = 0.
    ///
    /// # Parameters
    /// - `device`: RHI device for staging buffer creation.
    /// - `cmd`: Command list in recording state for copy commands.
    void UploadInitialState(IRHIDevice* device, IRHICommandList* cmd);

    /// Transitions ALL buffers (both attribute sets + shared) from Common to UAV.
    ///
    /// DX12 Default heap buffers start in Common state after CreateBuffer().
    /// The render graph imports them as UAV — this function ensures the actual
    /// state matches the import assumption. Must be called once after Create()
    /// on a command list that is executed and waited on before the first frame.
    void TransitionAllToUAV(IRHICommandList* cmd);
};

}  // namespace uz
