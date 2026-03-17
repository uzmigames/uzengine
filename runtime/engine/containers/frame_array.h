#pragma once

/// UzEngine FrameArray — Scoped linear allocator for per-frame temporary objects.
///
/// # Design
/// - **Linear allocation**: Push elements sequentially (no free)
/// - **Zero-copy reset**: Call Clear() once per frame, reuse storage
/// - **Stack-like**: LIFO push/pop is efficient, random access via operator[]
/// - **No fragmentation**: All allocations are contiguous
///
/// # Lifetime
/// All elements live until Clear() or destruction. Ideal for:
/// - Command buffers (allocate commands, execute, clear)
/// - Render pass temps (frustum culling, light lists, etc.)
/// - String building (format strings, then use result)
///
/// # Performance
/// - Allocation: O(1) — just increment counter
/// - Clear: O(n) — destructs all elements, resets counter
/// - No malloc/free per frame (zero allocator pressure)
///
/// # Example
/// ```cpp
/// FrameArray<RenderCommand, 10000> commands;
/// for (Entity e : scene.entities) {
///     commands.Emplace(e.mesh, e.transform);
/// }
/// ExecuteCommands(commands);
/// commands.Clear();  // Ready for next frame
/// ```

#include "engine_assert.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace uz {

/// Fixed-capacity linear allocator for transient frame data.
///
/// # Template Parameters
/// * `T` - Element type
/// * `Capacity` - Maximum elements (stack-allocated storage)
///
/// # Constraints
/// - No free: only Clear() resets
/// - No resize: adding past Capacity asserts
/// - Not thread-safe: single-threaded use or external sync
template <typename T, size_t Capacity> class FrameArray {
    static_assert(Capacity > 0, "Capacity must be > 0");

  public:
    /// Initializes empty linear allocator.
    FrameArray() = default;

    /// Destructs all remaining elements.
    ~FrameArray() { Clear(); }

    FrameArray(const FrameArray&) = delete;
    FrameArray& operator=(const FrameArray&) = delete;

    /// Copies an item into the next available slot.
    ///
    /// # Arguments
    /// * `item` - Element to copy-construct into the array.
    ///
    /// # Returns
    /// Reference to the newly inserted element.
    ///
    /// # Panics
    /// Asserts if the array is full (Count == Capacity).
    T& Push(const T& item) {
        UZ_CHECK(m_count < Capacity);
        T* ptr = new (&Storage(m_count)) T(item);
        ++m_count;
        return *ptr;
    }

    /// Moves an item into the next available slot.
    ///
    /// # Arguments
    /// * `item` - Element to move-construct into the array.
    ///
    /// # Returns
    /// Reference to the newly inserted element.
    ///
    /// # Panics
    /// Asserts if the array is full.
    T& Push(T&& item) {
        UZ_CHECK(m_count < Capacity);
        T* ptr = new (&Storage(m_count)) T(static_cast<T&&>(item));
        ++m_count;
        return *ptr;
    }

    /// Constructs an element in-place at the next available slot.
    ///
    /// # Arguments
    /// * `args` - Forwarded to T's constructor.
    ///
    /// # Returns
    /// Reference to the newly constructed element.
    ///
    /// # Panics
    /// Asserts if the array is full.
    template <typename... Args> T& Emplace(Args&&... args) {
        UZ_CHECK(m_count < Capacity);
        T* ptr = new (&Storage(m_count)) T(static_cast<Args&&>(args)...);
        ++m_count;
        return *ptr;
    }

    /// Destructs all elements and resets count to zero. Call once per frame.
    void Clear() {
        for (size_t i = 0; i < m_count; ++i) {
            At(i).~T();
        }
        m_count = 0;
    }

    /// Mutable element access by index.
    ///
    /// # Panics
    /// Asserts if `i >= Count()`.
    T& operator[](size_t i) {
        UZ_CHECK(i < m_count);
        return At(i);
    }
    /// Const element access by index.
    ///
    /// # Panics
    /// Asserts if `i >= Count()`.
    const T& operator[](size_t i) const {
        UZ_CHECK(i < m_count);
        return At(i);
    }

    /// Pointer to contiguous element storage, or nullptr if empty.
    T* Data() { return m_count > 0 ? &At(0) : nullptr; }
    /// Const pointer to contiguous element storage.
    const T* Data() const { return m_count > 0 ? &At(0) : nullptr; }

    /// Number of elements currently in the array.
    size_t Count() const { return m_count; }
    /// Maximum number of elements (compile-time constant).
    size_t GetCapacity() const { return Capacity; }
    /// True if the array contains no elements.
    bool IsEmpty() const { return m_count == 0; }
    /// True if Count == Capacity (no more room).
    bool IsFull() const { return m_count == Capacity; }

    /// Iterator to the first element (for range-based for loops).
    T* begin() { return Data(); }
    /// Iterator past the last element.
    T* end() { return Data() + m_count; }
    /// Const iterator to the first element.
    const T* begin() const { return Data(); }
    /// Const iterator past the last element.
    const T* end() const { return Data() + m_count; }

  private:
    T& At(size_t i) { return reinterpret_cast<T&>(m_storage[i]); }
    const T& At(size_t i) const { return reinterpret_cast<const T&>(m_storage[i]); }

    char& Storage(size_t i) { return reinterpret_cast<char&>(m_storage[i]); }

    typename std::aligned_storage<sizeof(T), alignof(T)>::type m_storage[Capacity];
    size_t m_count = 0;
};

} // namespace uz
