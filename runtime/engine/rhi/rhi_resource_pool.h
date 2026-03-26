#pragma once

/// Generic typed resource pool with generational handle validation.
///
/// Flat array pool for GPU resources. Allocates slots linearly from a
/// growable `std::vector`, reuses freed slots via a free list stack.
/// Each slot has a generation counter — handles with mismatched
/// generations are treated as invalid (use-after-free protection).
///
/// Shared by all RHI backends (DX12, DX11, Vulkan, Metal).
///
/// # Thread Safety
/// NOT thread-safe. Callers must serialize access.
///
/// # Type Requirements
/// `T` must have a `uint32_t generation` member field.

#include <cstdint>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool
// ─────────────────────────────────────────────────────────────────────────────

template <typename T>
/// Flat array pool for GPU resources with generational handle validation.
///
/// Allocates slots linearly from a growable `std::vector`, reuses freed slots
/// via a free list stack. Each slot has a generation counter — handles with
/// mismatched generations are treated as invalid (use-after-free protection).
///
/// # Thread Safety
/// NOT thread-safe. Callers must serialize access.
///
/// # Type Requirements
/// `T` must have a `uint32_t generation` member field.
class ResourcePool {
public:
    /// Allocates a new slot and returns its index. Sets `outGeneration` to the
    /// slot's new generation value. The caller must fill the slot's data fields.
    ///
    /// # Arguments
    /// * `outGeneration` — Set to the new generation of the allocated slot.
    ///
    /// # Returns
    /// The index of the allocated slot.
    uint32_t Allocate(uint32_t& outGeneration) {
        uint32_t index;
        if (!m_freeList.empty()) {
            index = m_freeList.back();
            m_freeList.pop_back();
        } else {
            index = static_cast<uint32_t>(m_slots.size());
            m_slots.emplace_back();
        }
        m_slots[index].generation++;
        outGeneration = m_slots[index].generation;
        return index;
    }

    /// Validates a handle and returns a pointer to the slot data.
    ///
    /// # Arguments
    /// * `index` — Slot index from the handle.
    /// * `generation` — Generation from the handle.
    ///
    /// # Returns
    /// Pointer to the slot if index is in range and generation matches, `nullptr` otherwise.
    T* Get(uint32_t index, uint32_t generation) {
        if (index >= m_slots.size()) return nullptr;
        auto& slot = m_slots[index];
        if (slot.generation != generation || generation == 0) return nullptr;
        return &slot;
    }

    /// Const overload of Get(). Returns nullptr on stale or out-of-range handle.
    const T* Get(uint32_t index, uint32_t generation) const {
        if (index >= m_slots.size()) return nullptr;
        auto& slot = m_slots[index];
        if (slot.generation != generation || generation == 0) return nullptr;
        return &slot;
    }

    /// Frees a slot for reuse. The generation stays bumped so old handles are invalidated.
    ///
    /// # Arguments
    /// * `index` — Slot index to free. Must have been previously allocated.
    void Free(uint32_t index) {
        if (index >= m_slots.size()) return;
        m_freeList.push_back(index);
    }

    /// Returns mutable reference to all slots (for cleanup on shutdown).
    std::vector<T>& Slots() { return m_slots; }
    /// Returns const reference to all slots.
    const std::vector<T>& Slots() const { return m_slots; }

private:
    std::vector<T>        m_slots;
    std::vector<uint32_t> m_freeList;
};

} // namespace uz
