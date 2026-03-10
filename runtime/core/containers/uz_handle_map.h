#pragma once

/// UzEngine HandleMap — Generational handle-based allocator.
///
/// # Design
/// - **Handles**: Typed references (Handle<Entity>) that include generation for safe dereferencing
/// - **Use-after-free prevention**: Incrementing generation invalidates old handles on reuse
/// - **O(1) access**: Index lookup into sparse array
/// - **Sparse storage**: Only alive objects occupy space (via FreeList pattern)
///
/// # Problem Solved
/// Pointers become invalid when memory reallocates (std::vector).
/// Indices alone don't detect reuse (allocate slot N, free, reallocate N).
/// Handles combine both: (index, generation) so Handle(N, gen=5) != Handle(N, gen=6).
///
/// # Inspiration
/// Pattern from Godot's RID_Owner and Unreal's FHandleTable.
///
/// # Example
/// ```cpp
/// HandleMap<Entity, 1000> entities;
/// Handle<Entity> h = entities.Create(name, pos);
/// Entity* e = entities.Get(h);  // safe: validates generation
/// entities.Destroy(h);
/// e = entities.Get(h);  // nullptr: handle stale
/// ```

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <new>
#include <type_traits>

namespace uz {

/// Generational handle — typed, safe reference to a pooled object.
///
/// # Structure
/// - `index`: Slot in the HandleMap (uint32_t, 32-bit)
/// - `generation`: Version counter for safe reuse (uint32_t, 32-bit)
///
/// # Validity
/// A handle is valid (IsValid()) only if generation != 0.
/// After Destroy(), the handle's generation is never reused in this session,
/// so the stale handle is guaranteed not to match new allocations.
///
/// # Comparison
/// Handles are equal only if both index AND generation match.
/// ```cpp
/// Handle<Entity> h1 = map.Create(...);  // {0, 1}
/// map.Destroy(h1);                      // generation becomes unreachable
/// Handle<Entity> h2 = map.Create(...);  // {0, 2}
/// assert(h1 != h2);  // true: same slot, different generation
/// ```
template <typename T>
struct Handle {
    uint32_t index = 0;          ///< Slot index in pool
    uint32_t generation = 0;     ///< Version counter (0 = invalid)

    /// Returns true if this handle was ever created (generation != 0).
    bool IsValid() const { return generation != 0; }

    /// Equality: both index and generation must match.
    bool operator==(Handle other) const { return index == other.index && generation == other.generation; }

    /// Inequality: either index or generation differs.
    bool operator!=(Handle other) const { return !(*this == other); }

    /// Creates an invalid handle (generation=0).
    /// Use to signal "not found" or initialization.
    static Handle Invalid() { return {0, 0}; }
};

// ── HandleMap — sparse generational array ────────────────────────────────────

template <typename T, size_t Capacity>
class HandleMap {
    static_assert(Capacity > 0, "Capacity must be > 0");

public:
    HandleMap() {
        for (size_t i = 0; i < Capacity - 1; ++i) {
            m_freeNext[i] = static_cast<uint32_t>(i + 1);
        }
        m_freeNext[Capacity - 1] = kInvalid;
        m_freeHead = 0;
        m_count = 0;
    }

    ~HandleMap() {
        for (size_t i = 0; i < Capacity; ++i) {
            if (m_alive[i]) {
                reinterpret_cast<T&>(m_storage[i]).~T();
            }
        }
    }

    HandleMap(const HandleMap&) = delete;
    HandleMap& operator=(const HandleMap&) = delete;

    template <typename... Args>
    Handle<T> Create(Args&&... args) {
        if (m_freeHead == kInvalid) return Handle<T>::Invalid();
        uint32_t idx = m_freeHead;
        m_freeHead = m_freeNext[idx];
        m_generation[idx]++;
        new (&m_storage[idx]) T(static_cast<Args&&>(args)...);
        m_alive[idx] = true;
        ++m_count;
        return {idx, m_generation[idx]};
    }

    void Destroy(Handle<T> h) {
        if (!IsValidHandle(h)) return;
        reinterpret_cast<T&>(m_storage[h.index]).~T();
        m_alive[h.index] = false;
        m_freeNext[h.index] = m_freeHead;
        m_freeHead = h.index;
        --m_count;
    }

    T* Get(Handle<T> h) {
        if (!IsValidHandle(h)) return nullptr;
        return &reinterpret_cast<T&>(m_storage[h.index]);
    }

    const T* Get(Handle<T> h) const {
        if (!IsValidHandle(h)) return nullptr;
        return &reinterpret_cast<const T&>(m_storage[h.index]);
    }

    bool IsValidHandle(Handle<T> h) const {
        return h.index < Capacity
            && m_alive[h.index]
            && m_generation[h.index] == h.generation;
    }

    size_t Count() const { return m_count; }
    size_t GetCapacity() const { return Capacity; }

    // Iterate over all alive entries
    template <typename Fn>
    void ForEach(Fn&& fn) {
        for (size_t i = 0; i < Capacity; ++i) {
            if (m_alive[i]) {
                fn(Handle<T>{static_cast<uint32_t>(i), m_generation[i]},
                   reinterpret_cast<T&>(m_storage[i]));
            }
        }
    }

private:
    static constexpr uint32_t kInvalid = 0xFFFFFFFF;

    typename std::aligned_storage<sizeof(T), alignof(T)>::type m_storage[Capacity];
    uint32_t m_generation[Capacity] = {};
    uint32_t m_freeNext[Capacity];
    bool m_alive[Capacity] = {};
    uint32_t m_freeHead = 0;
    size_t m_count = 0;
};

} // namespace uz
