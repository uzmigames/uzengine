#pragma once

// Generational Handle — typed opaque index for cross-lifetime references.
//
// A Handle<Tag> is a 32-bit value split into a 24-bit slot index and an 8-bit
// generation counter. The Tag type parameter exists solely to create distinct
// handle types that cannot be accidentally mixed (EntityHandle vs TextureHandle).
//
// Handles are always validated through a HandlePool, which increments the
// generation on each remove(). A stale handle (one whose slot was recycled)
// will fail the generation check in get()/is_valid().
//
// Index 0 with generation 0 is reserved as the "null" handle sentinel.

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace uz {

// ============================================================================
// Handle<Tag> — generational index
// ============================================================================

template<typename Tag>
struct Handle {
    uint32_t index      : 24;   // Slot index in the pool (0 = null)
    uint32_t generation : 8;    // Generation counter (wraps at 255)

    /// Returns the null handle sentinel ({0, 0}).
    [[nodiscard]] static constexpr Handle null() noexcept { return {0, 0}; }

    /// True if this handle is the null sentinel (index == 0).
    [[nodiscard]] constexpr bool is_null() const noexcept { return index == 0; }

    [[nodiscard]] constexpr bool operator==(const Handle& o) const noexcept {
        return index == o.index && generation == o.generation;
    }

    [[nodiscard]] constexpr bool operator!=(const Handle& o) const noexcept {
        return !(*this == o);
    }

    /// Explicit bool conversion — true if not null.
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return !is_null(); }
};

// ============================================================================
// Type aliases — distinct handle types for each engine subsystem
// ============================================================================

using EntityHandle      = Handle<struct EntityTag>;
using TextureHandle     = Handle<struct TextureTag>;
using AudioSourceHandle = Handle<struct AudioSourceTag>;
using MeshHandle        = Handle<struct MeshTag>;

// ============================================================================
// HandlePool<T, Tag> — manages objects addressed by Handle<Tag>
// ============================================================================

/// HandlePool<T, Tag> — manages objects addressed by Handle<Tag>.
/// T must be default-constructible and move-assignable.
template<typename T, typename Tag>
struct HandlePool {
    struct Slot {
        T       value;
        uint8_t generation = 0;
        bool    occupied   = false;
    };

    std::vector<Slot>     slots;
    std::vector<uint32_t> free_list;

    HandlePool() {
        // Reserve slot 0 as the null sentinel — it is never allocated.
        slots.emplace_back();
    }

    /// Insert a value into the pool, returning a handle to it.
    /// Reuses a free slot if available, otherwise grows the pool.
    [[nodiscard]] Handle<Tag> insert(T&& value) {
        uint32_t idx = 0;

        if (!free_list.empty()) {
            idx = free_list.back();
            free_list.pop_back();
        } else {
            // Pool capacity is capped at 2^24 - 1 (16'777'215) usable slots.
            assert(slots.size() < 0x00FF'FFFFu && "HandlePool: index space exhausted");
            idx = static_cast<uint32_t>(slots.size());
            slots.emplace_back();
        }

        Slot& slot     = slots[idx];
        slot.value     = std::move(value);
        slot.occupied  = true;
        // Generation was already bumped on the last remove(); keep it.

        Handle<Tag> h;
        h.index      = idx;
        h.generation = slot.generation;
        return h;
    }

    /// Returns a mutable pointer to the value, or nullptr if the handle is
    /// stale or null.
    [[nodiscard]] T* get(Handle<Tag> h) {
        if (h.is_null()) return nullptr;
        if (static_cast<size_t>(h.index) >= slots.size()) return nullptr;

        Slot& slot = slots[h.index];
        if (!slot.occupied || slot.generation != h.generation) return nullptr;
        return &slot.value;
    }

    /// Returns a const pointer to the value, or nullptr if the handle is
    /// stale or null.
    [[nodiscard]] const T* get(Handle<Tag> h) const {
        if (h.is_null()) return nullptr;
        if (static_cast<size_t>(h.index) >= slots.size()) return nullptr;

        const Slot& slot = slots[h.index];
        if (!slot.occupied || slot.generation != h.generation) return nullptr;
        return &slot.value;
    }

    /// Remove the object at the given handle. The slot's generation is bumped
    /// so that any remaining copies of this handle become stale.
    void remove(Handle<Tag> h) {
        if (h.is_null()) return;
        assert(static_cast<size_t>(h.index) < slots.size());

        Slot& slot = slots[h.index];
        assert(slot.occupied && "HandlePool::remove on empty slot");
        assert(slot.generation == h.generation && "HandlePool::remove with stale handle");

        slot.occupied = false;
        // Bump generation so stale handles are detected. Wraps at 255 — this
        // is intentional; 256 remove+reinsert cycles on the same slot before
        // a stale handle can alias is acceptable for game workloads.
        slot.generation = static_cast<uint8_t>(slot.generation + 1);

        free_list.push_back(h.index);
    }

    /// Check whether a handle refers to a live object.
    [[nodiscard]] bool is_valid(Handle<Tag> h) const {
        if (h.is_null()) return false;
        if (static_cast<size_t>(h.index) >= slots.size()) return false;

        const Slot& slot = slots[h.index];
        return slot.occupied && slot.generation == h.generation;
    }
};

} // namespace uz
