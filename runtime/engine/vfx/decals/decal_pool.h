#pragma once

/// DecalPool -- Pre-allocated object pool for DecalComponent instances.
///
/// Eliminates per-frame heap allocations by pre-allocating all DecalComponents
/// in a single contiguous vector at construction time. Decals are acquired and
/// returned via O(1) free-list operations.
///
/// # Design
/// - **Contiguous backing store**: Single `std::vector<DecalComponent>` for
///   cache-friendly iteration and zero fragmentation.
/// - **Index-based free list**: `std::vector<uint32_t>` tracks available slot
///   indices. Acquire pops from the back (O(1)), return pushes (O(1)).
/// - **Active pointer list**: `std::vector<DecalComponent*>` for efficient
///   iteration by rendering and tick systems.
/// - **No allocation after construction**: All vectors are reserved up front.
///
/// # Ownership
/// The pool owns all DecalComponent memory. Pointers returned by `AcquireDecal()`
/// remain valid until the pool is destroyed. Callers must not delete or free them.
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the main thread.
///
/// # Example
/// ```cpp
/// DecalPool pool(500);
/// DecalComponent* decal = pool.AcquireDecal();
/// if (decal) {
///     decal->position = impactPoint;
///     decal->materialPath = "materials/decals/bullet_hole.uzmat";
///     decal->lifetime = 15.0f;
///     decal->isActive = true;
/// }
/// // When done:
/// pool.ReturnDecal(decal);
/// ```

#include "engine/vfx/decals/decal_component.h"

#include <cstdint>
#include <vector>

namespace uz {

/// Pre-allocated pool of DecalComponent instances.
///
/// Manages a fixed-capacity pool where decals are acquired and returned
/// without dynamic memory allocation. The pool tracks active and available
/// decals separately, allowing O(1) acquire/return and efficient iteration
/// over only the active set.
///
/// # Capacity
/// The pool size is fixed at construction. If all slots are in use,
/// `AcquireDecal()` returns `nullptr` and logs a warning. The caller
/// should handle this gracefully (e.g., skip spawning the decal or
/// evict the oldest one).
class DecalPool {
  public:
    /// Constructs a decal pool with the given capacity.
    ///
    /// Pre-allocates `poolSize` DecalComponent instances in contiguous memory.
    /// All start in the free list (available for acquisition).
    ///
    /// # Arguments
    /// * `poolSize` -- Maximum number of decals. Must be > 0. Default: 1000.
    ///
    /// # Side Effects
    /// Logs VFX/Info with the pool size at creation.
    explicit DecalPool(uint32_t poolSize = 1000);

    ~DecalPool() = default;

    /// Non-copyable (owns contiguous backing store).
    DecalPool(const DecalPool&) = delete;
    DecalPool& operator=(const DecalPool&) = delete;

    /// Movable (transfers ownership of backing store).
    DecalPool(DecalPool&& other) noexcept;
    DecalPool& operator=(DecalPool&& other) noexcept;

    /// Acquires a decal from the pool, reset to default state.
    ///
    /// Pops an index from the free list, resets the corresponding
    /// DecalComponent to defaults, adds it to the active list, and
    /// returns a pointer into the backing store.
    ///
    /// # Returns
    /// Pointer to a default-initialized DecalComponent, or `nullptr`
    /// if the pool is exhausted.
    ///
    /// # Side Effects
    /// - Logs VFX/Warning on pool exhaustion (at most once per second
    ///   to avoid log spam).
    [[nodiscard]] DecalComponent* AcquireDecal();

    /// Returns a decal to the available pool.
    ///
    /// Validates that the pointer belongs to this pool's backing store.
    /// Resets the component to defaults, removes it from the active list,
    /// and pushes its index back onto the free list.
    ///
    /// # Arguments
    /// * `decal` -- Pointer previously returned by `AcquireDecal()`.
    ///              Must not be null. Must belong to this pool.
    ///
    /// # Side Effects
    /// - Logs VFX/Error if the pointer is null or out of range.
    /// - Logs VFX/Warning if the decal is not in the active list
    ///   (double-return).
    void ReturnDecal(DecalComponent* decal);

    /// Returns all active decals to the available pool.
    ///
    /// Resets every component to defaults and rebuilds the free list
    /// to contain all indices. The active list is cleared.
    ///
    /// # Side Effects
    /// Logs VFX/Info with the number of decals returned.
    void Clear();

    /// Number of decals currently acquired (in use).
    [[nodiscard]] uint32_t GetActiveCount() const noexcept;

    /// Number of decals available for acquisition.
    [[nodiscard]] uint32_t GetAvailableCount() const noexcept;

    /// Total pool capacity (fixed at construction).
    [[nodiscard]] uint32_t GetPoolSize() const noexcept;

    /// Read-only access to the list of currently active decal pointers.
    ///
    /// The returned reference is valid until the next mutating call
    /// (AcquireDecal, ReturnDecal, Clear). Iteration order is unspecified.
    ///
    /// # Returns
    /// Const reference to the internal active-decal pointer vector.
    [[nodiscard]] const std::vector<DecalComponent*>& GetActiveDecals() const noexcept;

  private:
    /// Contiguous backing store. All DecalComponent instances live here.
    /// Pointers into this vector remain stable because the vector is never
    /// resized after construction.
    std::vector<DecalComponent> backingStore_;

    /// Indices of available (free) slots in `backingStore_`.
    /// AcquireDecal() pops from the back; ReturnDecal() pushes.
    std::vector<uint32_t> freeList_;

    /// Pointers to currently active (in-use) decals in `backingStore_`.
    /// Used by rendering/tick systems to iterate only over live decals.
    std::vector<DecalComponent*> activeDecals_;

    /// Total capacity (size of backingStore_).
    uint32_t poolSize_ = 0;
};

} // namespace uz
