#pragma once

/// GPU Particle Sort — Bitonic merge sort manager for depth-ordered particles.
///
/// Manages sort key and index buffers per emitter, and dispatches the two-phase
/// bitonic sort compute shader. The sorted index buffer is consumed by
/// DrawIndexedIndirect in the billboard and mesh render passes for correct
/// back-to-front alpha blending.
///
/// # Algorithm
/// Phase 0 (local sort): Generates sort keys from particle positions and
///   performs a complete bitonic sort within each thread group using shared memory.
/// Phase 1 (global merge): Iterates over (blockSize, subBlockSize) pairs with
///   increasing block sizes, dispatching one pass per pair to merge sorted blocks
///   across thread group boundaries.
///
/// # Buffer Layout
/// Two RWStructuredBuffer<uint> of paddedCount elements (power-of-two >= capacity):
///   - sortKeys:    per-element sort key (distance-based, sign-bit flipped)
///   - sortIndices: per-element original particle index
///
/// After sorting, sortIndices[0..aliveCount-1] gives back-to-front draw order.
///
/// @ref Batcher 1968 "Sorting Networks and Their Applications" — bitonic merge sort
/// @ref Peters & Schulz-Hildebrandt 2023 "Fast GPU-based sorting"

#include "engine/rhi/rhi_types.h"

#include <cstdint>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class CBRingAllocator;

// ── Constants ───────────────────────────────────────────────────────────────

/// Thread group size matching the compute shader's [numthreads(256, 1, 1)].
/// Must stay in sync with SORT_GROUP_SIZE in particle_sort_cs.hlsl.
static constexpr uint32_t kSortGroupSize = 256;

// ── VFXSortConstants — CPU-side mirror of the shader cbuffer ────────────────

/// CPU-side mirror of the VFXSortConstants cbuffer in particle_sort_cs.hlsl.
///
/// Layout matches the shader's HLSL packing (16-byte aligned rows, 80 bytes
/// total). Fields are ordered identically to the cbuffer declaration so that
/// a straight memcpy produces the correct GPU layout.
///
/// @ref particle_sort_cs.hlsl cbuffer VFXSortConstants : register(b0)
struct VFXSortConstants {
    // ── Row 0 (bytes 0-15): particle control ────────────────────────────
    uint32_t particleCount;       ///< Total alive particles to sort.
    uint32_t sortPhase;           ///< 0 = local sort + keygen, 1 = global merge.
    uint32_t paddedCount;         ///< particleCount rounded up to next power of two.
    uint32_t _pad0;

    // ── Row 1 (bytes 16-31): camera position ────────────────────────────
    float    cameraPosX;          ///< World-space camera position X.
    float    cameraPosY;          ///< World-space camera position Y.
    float    cameraPosZ;          ///< World-space camera position Z.
    float    _pad1;

    // ── Row 2 (bytes 32-47): camera forward ─────────────────────────────
    float    cameraForwardX;      ///< World-space camera forward direction X.
    float    cameraForwardY;      ///< World-space camera forward direction Y.
    float    cameraForwardZ;      ///< World-space camera forward direction Z.
    float    _pad2;

    // ── Row 3 (bytes 48-63): bindless descriptor indices ────────────────
    uint32_t positionsSrvIndex;   ///< SRV index for particle positions buffer.
    uint32_t sortKeysUavIndex;    ///< UAV index for sort keys buffer.
    uint32_t sortIndicesUavIndex; ///< UAV index for sorted indices buffer.
    uint32_t _pad3;

    // ── Row 4 (bytes 64-79): global merge parameters ────────────────────
    uint32_t bitonicBlockSize;    ///< Current merge block size (power of two).
    uint32_t bitonicSubBlockSize; ///< Current sub-block size within merge block.
    uint32_t _pad4;
    uint32_t _pad5;
};

static_assert(sizeof(VFXSortConstants) == 80,
              "VFXSortConstants must be exactly 80 bytes to match shader cbuffer layout");

// ── GPUParticleSort ─────────────────────────────────────────────────────────

/// GPU particle sort manager.
///
/// Manages sort key and index buffers for a single emitter, and dispatches
/// the bitonic sort compute shader. Owns two structured buffers sized to
/// the next power of two of the emitter's particle capacity.
///
/// # Lifetime
/// Call Create() once with the emitter's particle capacity. Call Destroy()
/// on shutdown. DispatchSort() is called each frame for emitters that need
/// depth-sorted rendering.
///
/// # Thread Safety
/// Not thread-safe. Intended for single-threaded render thread use.
///
/// @ref Batcher 1968 "Sorting Networks and Their Applications" — bitonic merge sort
struct GPUParticleSort {
    /// RWStructuredBuffer<uint> — per-element sort keys.
    RHIBufferHandle sortKeys = RHIBufferHandle::Invalid();

    /// RWStructuredBuffer<uint> — per-element sorted particle indices.
    RHIBufferHandle sortIndices = RHIBufferHandle::Invalid();

    /// Original (non-padded) particle capacity.
    uint32_t capacity = 0;

    /// Power-of-two padded element count (>= capacity).
    uint32_t paddedCount = 0;

    // ── Lifecycle ───────────────────────────────────────────────────────

    /// Allocates sort key and index buffers for the given particle capacity.
    ///
    /// Both buffers are sized to NextPowerOfTwo(particleCapacity) elements,
    /// as the bitonic sort algorithm requires a power-of-two array size.
    ///
    /// # Parameters
    /// - `device`: RHI device for buffer creation. Must not be null.
    /// - `particleCapacity`: Maximum number of particles. Must be > 0.
    ///
    /// # Returns
    /// `true` on success, `false` if any buffer allocation fails.
    /// On failure, partially allocated buffers are cleaned up.
    ///
    /// # Preconditions
    /// - `device` must not be null (fatal assert in debug).
    /// - `particleCapacity` must be > 0 (fatal assert in debug).
    /// - Must not already be allocated (fatal assert). Call Destroy() first.
    [[nodiscard]] bool Create(IRHIDevice* device, uint32_t particleCapacity);

    /// Releases sort buffers and resets state.
    ///
    /// Safe to call on an already-destroyed or never-created instance (no-op
    /// if device is null).
    ///
    /// # Parameters
    /// - `device`: RHI device for deallocation. May be null (no-op).
    void Destroy(IRHIDevice* device);

    /// Returns true if sort buffers have been successfully allocated.
    [[nodiscard]] bool IsValid() const noexcept;

    /// Returns the padded (power-of-two) element count.
    [[nodiscard]] uint32_t GetPaddedCount() const noexcept { return paddedCount; }

    // ── Dispatch ────────────────────────────────────────────────────────

    /// Fills VFXSortConstants and dispatches both sort phases.
    ///
    /// Phase 0: local sort + key generation — one dispatch of
    ///   ceil(paddedCount / SORT_GROUP_SIZE) groups.
    ///
    /// Phase 1: global merge passes — for each (blockSize, subBlockSize) pair
    ///   where blockSize ranges from SORT_GROUP_SIZE*2 to paddedCount (doubling),
    ///   and subBlockSize ranges from blockSize/2 down to 1 (halving), dispatches
    ///   ceil(paddedCount / SORT_GROUP_SIZE) groups with a UAV barrier between
    ///   each dispatch.
    ///
    /// The root parameter slot for the sort constant buffer is b0.
    ///
    /// # Parameters
    /// - `device`: RHI device for querying UAV descriptor indices.
    /// - `cmd`: Command list to record compute dispatches into.
    /// - `pipeline`: Compiled ParticleSortCS pipeline handle.
    /// - `cbRing`: CB ring allocator for per-dispatch constant uploads.
    /// - `aliveCount`: Number of alive particles to sort. If 0, returns immediately.
    /// - `cameraPos`: Camera world-space position as float[3].
    /// - `cameraForward`: Camera world-space forward direction as float[3] (normalized).
    /// - `positionsSrvIndex`: Bindless SRV descriptor index for the positions buffer.
    void DispatchSort(IRHIDevice* device,
                      IRHICommandList* cmd,
                      RHIPipelineHandle pipeline,
                      CBRingAllocator& cbRing,
                      uint32_t aliveCount,
                      const float* cameraPos,
                      const float* cameraForward,
                      uint32_t positionsSrvIndex);
};

// ── Utility ─────────────────────────────────────────────────────────────────

/// Rounds up to the next power of two. Returns 1 for input 0.
///
/// Uses bit manipulation to fill all bits below the highest set bit,
/// then adds one. Handles the edge case where v is already a power of two
/// by decrementing first.
///
/// # Examples
/// - NextPowerOfTwo(1) == 1
/// - NextPowerOfTwo(255) == 256
/// - NextPowerOfTwo(256) == 256
/// - NextPowerOfTwo(50000) == 65536
[[nodiscard]] constexpr uint32_t NextPowerOfTwo(uint32_t v) noexcept
{
    if (v == 0) return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

} // namespace uz
