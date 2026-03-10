#pragma once

/// UzEngine FreeList — Intrusive O(1) allocation/deallocation from a fixed-size pool.
///
/// # Design
/// - **Fixed capacity** (compile-time template): no dynamic allocator calls
/// - **Intrusive**: element storage is contiguous (cache-friendly)
/// - **O(1) allocate/free**: maintains free chain linked list
/// - **No fragmentation**: reuses any freed slot immediately
/// - **Stack-allocated**: entire pool is on the stack (Capacity must fit)
///
/// # Use Cases
/// - Entity pools (Scene holds entity FreeList<Entity, 1000>)
/// - Handle allocation (HandleMap uses FreeList internally)
/// - Frame-local pools (allocate many objects, free all at frame end)
///
/// # Example
/// ```cpp
/// FreeList<Particle, 10000> particles;
/// Particle* p = particles.Alloc(pos, velocity);
/// particles.Free(p);
/// ```

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <new>
#include <type_traits>

namespace uz {

/// Fixed-size intrusive free list container.
///
/// # Template Parameters
/// * `T` - Element type to allocate
/// * `Capacity` - Maximum number of elements (stack-allocated)
///
/// # Thread Safety
/// NOT thread-safe: single-threaded use only.
/// For multi-threaded access, use external synchronization (mutex).
template <typename T, size_t Capacity>
class FreeList {
    static_assert(Capacity > 0, "Capacity must be > 0");

public:
    /// Initializes empty free list with Capacity slots available.
    ///
    /// # Behavior
    /// - Builds a linked list of free slots
    /// - m_freeHead points to first free slot
    /// - No allocations occur (all stack-allocated)
    FreeList() {
        // Build free chain: each slot points to next free slot
        for (size_t i = 0; i < Capacity - 1; ++i) {
            m_next[i] = static_cast<uint32_t>(i + 1);
        }
        m_next[Capacity - 1] = kInvalid;
        m_freeHead = 0;
        m_count = 0;
    }

    /// Destroys all live objects in the list.
    ///
    /// # Behavior
    /// - Calls destructor on each alive element
    /// - Safe if some elements were already freed
    ~FreeList() {
        // Destroy all live objects
        for (size_t i = 0; i < Capacity; ++i) {
            if (m_alive[i]) {
                reinterpret_cast<T&>(m_storage[i]).~T();
            }
        }
    }

    FreeList(const FreeList&) = delete;
    FreeList& operator=(const FreeList&) = delete;

    /// Allocates a new element, constructing with provided arguments.
    ///
    /// # Arguments
    /// * `args` - Forwarded to T's constructor
    ///
    /// # Returns
    /// Pointer to newly allocated T, or nullptr if list is full.
    ///
    /// # Complexity
    /// O(1): constant-time allocation from free chain.
    ///
    /// # Example
    /// ```cpp
    /// Entity* e = list.Alloc(position, velocity);
    /// if (!e) { /* out of space */ }
    /// ```
    template <typename... Args>
    T* Alloc(Args&&... args) {
        if (m_freeHead == kInvalid) return nullptr; // full
        uint32_t idx = m_freeHead;
        m_freeHead = m_next[idx];
        T* ptr = new (&m_storage[idx]) T(static_cast<Args&&>(args)...);
        m_alive[idx] = true;
        ++m_count;
        return ptr;
    }

    /// Frees a previously allocated element.
    ///
    /// # Arguments
    /// * `ptr` - Pointer returned by Alloc() — must be valid
    ///
    /// # Behavior
    /// - Calls destructor on element
    /// - Marks slot as free
    /// - Adds slot back to free chain
    ///
    /// # Complexity
    /// O(1): constant-time deallocation.
    ///
    /// # Safety
    /// Undefined behavior if:
    /// - `ptr` is nullptr
    /// - `ptr` was already freed
    /// - `ptr` points to a different allocation
    ///
    /// Use asserts to catch these errors in development.
    void Free(T* ptr) {
        assert(ptr != nullptr);
        size_t idx = IndexOf(ptr);
        assert(idx < Capacity && m_alive[idx]);
        ptr->~T();
        m_alive[idx] = false;
        m_next[idx] = m_freeHead;
        m_freeHead = static_cast<uint32_t>(idx);
        --m_count;
    }

    /// Gets the index of a pointer within the pool.
    ///
    /// # Arguments
    /// * `ptr` - Pointer to an element (must be in this pool)
    ///
    /// # Returns
    /// Index [0, Capacity) for the element.
    /// Undefined if ptr doesn't point into this pool.
    ///
    /// # Use Case
    /// Convert pointer-based handles to index-based handles.
    size_t IndexOf(const T* ptr) const {
        auto* base = reinterpret_cast<const char*>(&m_storage[0]);
        auto* p = reinterpret_cast<const char*>(ptr);
        size_t offset = static_cast<size_t>(p - base);
        return offset / sizeof(typename std::aligned_storage<sizeof(T), alignof(T)>::type);
    }

    /// Number of currently allocated elements.
    size_t Count() const { return m_count; }

    /// Maximum capacity (fixed).
    size_t GetCapacity() const { return Capacity; }

    /// True if no free slots remain.
    bool IsFull() const { return m_count == Capacity; }

    /// True if no elements are allocated.
    bool IsEmpty() const { return m_count == 0; }

    /// True if slot at index is occupied.
    bool IsAlive(size_t idx) const { return idx < Capacity && m_alive[idx]; }

    /// Gets mutable pointer to element at index.
    ///
    /// # Safety
    /// Asserts that index is valid and alive.
    T* At(size_t idx) {
        assert(idx < Capacity && m_alive[idx]);
        return &reinterpret_cast<T&>(m_storage[idx]);
    }

    /// Gets const pointer to element at index.
    const T* At(size_t idx) const {
        assert(idx < Capacity && m_alive[idx]);
        return &reinterpret_cast<const T&>(m_storage[idx]);
    }

private:
    static constexpr uint32_t kInvalid = 0xFFFFFFFF;

    typename std::aligned_storage<sizeof(T), alignof(T)>::type m_storage[Capacity];
    uint32_t m_next[Capacity];
    bool m_alive[Capacity] = {};
    uint32_t m_freeHead = 0;
    size_t m_count = 0;
};

} // namespace uz
