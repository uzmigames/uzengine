#pragma once

/// VFX Emitter Pool -- pre-allocated GPU particle buffer slot recycling.
///
/// Pre-allocates GPU particle buffer slots at fixed capacity tiers and recycles
/// them when emitters are destroyed, turning create/destroy into O(1) slot
/// assignment. Eliminates per-emitter GPU heap allocation overhead.
///
/// # Capacity Tiers
/// Pool slots are created at fixed sizes: 1K, 4K, 8K, 16K, 32K, 64K particles.
/// `AcquireSlot()` finds the smallest free slot with capacity >= requested.
/// Emitters requesting > 64K always fall back to dynamic allocation.
///
/// # Default Distribution (32 slots)
/// 8x1K, 8x4K, 8x8K, 4x16K, 2x32K, 2x64K = 32 slots total.
///
/// # Thread Safety
/// Not thread-safe. Intended for single-threaded main/render thread use.

#include "gpu_particle_buffer.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// PoolSlot -- a single reusable GPU particle buffer
// ─────────────────────────────────────────────────────────────────────────────

/// One pre-allocated slot in the emitter pool.
///
/// The buffer persists for the lifetime of the pool. When released, the slot
/// is reset (alive_count=0, free_list refilled) but GPU memory is NOT freed.
struct PoolSlot {
    GPUParticleBuffer buffer;   ///< The GPU particle buffer for this slot.
    uint32_t capacity = 0;     ///< Max particles this slot supports.
    bool inUse = false;        ///< True when acquired, false when in the free list.
};

// ─────────────────────────────────────────────────────────────────────────────
// PoolStats -- diagnostic counters
// ─────────────────────────────────────────────────────────────────────────────

/// Statistics for monitoring pool utilization.
struct PoolStats {
    uint32_t totalSlots = 0;          ///< Total pool size.
    uint32_t inUseSlots = 0;          ///< Currently acquired slots.
    uint32_t dynamicOverflowCount = 0;///< Requests that fell through to dynamic alloc.
    uint32_t peakInUse = 0;           ///< Highest simultaneous in-use count observed.
};

// ─────────────────────────────────────────────────────────────────────────────
// VFXEmitterPool -- O(1) GPU particle buffer recycling
// ─────────────────────────────────────────────────────────────────────────────

/// Pre-allocated pool of GPU particle buffer slots for O(1) emitter creation.
///
/// # Lifecycle
/// 1. `Create(device, poolSize, defaultCapacity)` -- allocate all slots.
/// 2. `AcquireSlot(requestedCapacity)` -- get a free slot (O(1) scan).
/// 3. `ReleaseSlot(buffer)` -- return the slot for reuse (O(1)).
/// 4. `Destroy(device)` -- deallocate all GPU memory on shutdown.
///
/// # Capacity Matching
/// `AcquireSlot()` finds the smallest free slot >= requested capacity.
/// If no slot is large enough, returns nullptr (caller falls back to
/// dynamic GPUParticleBuffer::Create).
class VFXEmitterPool {
public:
    VFXEmitterPool() = default;
    ~VFXEmitterPool() = default;

    // Non-copyable, movable.
    VFXEmitterPool(const VFXEmitterPool&) = delete;
    VFXEmitterPool& operator=(const VFXEmitterPool&) = delete;
    VFXEmitterPool(VFXEmitterPool&&) = default;
    VFXEmitterPool& operator=(VFXEmitterPool&&) = default;

    /// Allocates all pool slots with the default tier distribution.
    ///
    /// Default distribution (32 slots):
    /// - 8 slots at 1024 particles
    /// - 8 slots at 4096 particles
    /// - 8 slots at 8192 particles
    /// - 4 slots at 16384 particles
    /// - 2 slots at 32768 particles
    /// - 2 slots at 65536 particles
    ///
    /// # Parameters
    /// - `device`: RHI device for buffer creation. Must not be null.
    ///
    /// # Returns
    /// `true` if all slots were allocated successfully.
    [[nodiscard]] bool Create(IRHIDevice* device);

    /// Allocates pool slots with a custom tier list.
    ///
    /// # Parameters
    /// - `device`: RHI device for buffer creation. Must not be null.
    /// - `tiers`: List of (capacity, count) pairs defining the pool.
    ///
    /// # Returns
    /// `true` if all slots were allocated successfully.
    [[nodiscard]] bool CreateCustom(
        IRHIDevice* device,
        const std::vector<std::pair<uint32_t, uint32_t>>& tiers);

    /// Releases all GPU memory and resets the pool.
    void Destroy(IRHIDevice* device);

    /// Acquires a free slot with capacity >= `requestedCapacity`.
    ///
    /// Returns the smallest free slot that fits the request. If no free slot
    /// is large enough, returns nullptr and increments `dynamicOverflowCount`.
    ///
    /// # Parameters
    /// - `requestedCapacity`: Minimum particle capacity needed.
    ///
    /// # Returns
    /// Pointer to the acquired GPUParticleBuffer, or nullptr if no slot fits.
    /// The returned pointer is valid until `Destroy()` is called.
    [[nodiscard]] GPUParticleBuffer* AcquireSlot(uint32_t requestedCapacity);

    /// Returns a previously acquired slot to the free list.
    ///
    /// Resets the slot (alive_count=0, free_list refilled) but does NOT
    /// deallocate GPU memory. The buffer is ready for immediate reuse.
    ///
    /// # Parameters
    /// - `buffer`: Pointer previously returned by `AcquireSlot()`.
    ///   Must not be null. Must belong to this pool (asserted in debug).
    void ReleaseSlot(GPUParticleBuffer* buffer);

    /// Returns current pool utilization statistics.
    [[nodiscard]] PoolStats GetStats() const noexcept;

    /// Returns true if the pool has been created.
    [[nodiscard]] bool IsValid() const noexcept { return !slots_.empty(); }

private:
    std::vector<PoolSlot> slots_;
    std::vector<uint32_t> freeSlots_;   ///< Indices into slots_ for available slots.
    PoolStats stats_;
};

} // namespace uz
