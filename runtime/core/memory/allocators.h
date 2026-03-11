#pragma once

/// UzEngine Allocators — LinearAllocator, PoolAllocator, FrameAllocator.
/// All allocators are named and tracked for memory budget debugging.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <type_traits>

namespace uz {

// ── LinearAllocator — bump pointer, reset to start ──────────────────────────

/// Bump-pointer allocator that allocates sequentially from a contiguous buffer.
///
/// Allocations are O(1) — just increment the offset. Individual frees are NOT supported;
/// call `Reset()` to reclaim all memory at once. Ideal for per-frame or per-scope temporaries.
///
/// # Thread Safety
/// NOT thread-safe. Use one per thread or protect with external synchronization.
///
/// # Examples
/// ```cpp
/// LinearAllocator alloc("frame_temps", 1024 * 1024); // 1 MB
/// auto* mesh = alloc.New<MeshData>(vertices, indices);
/// alloc.Reset(); // reclaim everything
/// ```
class LinearAllocator {
  public:
    /// Creates a linear allocator with the given name and byte capacity.
    ///
    /// # Arguments
    /// * `name` - Debug name for memory tracking (must outlive this allocator).
    /// * `sizeBytes` - Total buffer size in bytes.
    ///
    /// # Panics
    /// Asserts if malloc fails.
    LinearAllocator(const char* name, size_t sizeBytes)
        : m_name(name), m_size(sizeBytes), m_offset(0) {
        m_buffer = static_cast<uint8_t*>(std::malloc(sizeBytes));
        assert(m_buffer && "LinearAllocator: allocation failed");
    }

    /// Frees the backing buffer.
    ~LinearAllocator() { std::free(m_buffer); }

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    /// Allocates `size` bytes with the given alignment from the buffer.
    ///
    /// # Arguments
    /// * `size` - Number of bytes to allocate.
    /// * `alignment` - Required alignment (default: `max_align_t`).
    ///
    /// # Returns
    /// Pointer to allocated memory, or nullptr if insufficient space remains.
    void* Alloc(size_t size, size_t alignment = alignof(std::max_align_t)) {
        size_t aligned = AlignUp(m_offset, alignment);
        if (aligned + size > m_size)
            return nullptr; // out of memory
        void* ptr = m_buffer + aligned;
        m_offset = aligned + size;
        return ptr;
    }

    /// Allocates and constructs a T in-place.
    ///
    /// # Returns
    /// Pointer to constructed T, or nullptr if insufficient space.
    template <typename T, typename... Args> T* New(Args&&... args) {
        void* mem = Alloc(sizeof(T), alignof(T));
        if (!mem)
            return nullptr;
        return new (mem) T(static_cast<Args&&>(args)...);
    }

    /// Resets the allocator to empty. Does NOT call destructors on allocated objects.
    void Reset() { m_offset = 0; }

    /// Number of bytes currently allocated.
    size_t Used() const { return m_offset; }
    /// Number of bytes remaining before the allocator is full.
    size_t Remaining() const { return m_size - m_offset; }
    /// Total buffer size in bytes.
    size_t GetSize() const { return m_size; }
    /// Debug name of this allocator.
    const char* Name() const { return m_name; }

  private:
    static size_t AlignUp(size_t offset, size_t alignment) {
        return (offset + alignment - 1) & ~(alignment - 1);
    }

    const char* m_name;
    uint8_t* m_buffer;
    size_t m_size;
    size_t m_offset;
};

// ── PoolAllocator — fixed-size block pool with free list ────────────────────

/// Pool allocator for fixed-size objects with O(1) alloc/free via intrusive free list.
///
/// All blocks are the same size (sizeof(T)). Freed blocks are recycled immediately.
/// Heap-allocated backing buffer, capacity set at construction.
///
/// # Thread Safety
/// NOT thread-safe. Use one per thread or protect with external synchronization.
///
/// # Examples
/// ```cpp
/// PoolAllocator<Particle> pool("particles", 10000);
/// Particle* p = pool.Alloc(pos, vel, lifetime);
/// pool.Free(p);
/// ```
template <typename T> class PoolAllocator {
  public:
    /// Creates a pool with the given name and element capacity.
    ///
    /// # Arguments
    /// * `name` - Debug name (must outlive this allocator).
    /// * `capacity` - Maximum number of T objects.
    ///
    /// # Panics
    /// Asserts if malloc fails.
    PoolAllocator(const char* name, size_t capacity)
        : m_name(name), m_capacity(capacity), m_count(0) {
        size_t blockSize = sizeof(T) > sizeof(void*) ? sizeof(T) : sizeof(void*);
        m_blockSize = blockSize;
        m_buffer = static_cast<uint8_t*>(std::malloc(blockSize * capacity));
        assert(m_buffer && "PoolAllocator: allocation failed");

        // Build free chain
        m_freeHead = m_buffer;
        for (size_t i = 0; i < capacity - 1; ++i) {
            void** slot = reinterpret_cast<void**>(m_buffer + i * blockSize);
            *slot = m_buffer + (i + 1) * blockSize;
        }
        void** lastSlot = reinterpret_cast<void**>(m_buffer + (capacity - 1) * blockSize);
        *lastSlot = nullptr;
    }

    /// Frees the backing buffer. Does NOT call destructors on live objects.
    ~PoolAllocator() { std::free(m_buffer); }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    /// Allocates and constructs a T in-place from the pool.
    ///
    /// # Returns
    /// Pointer to constructed T, or nullptr if pool is full.
    template <typename... Args> T* Alloc(Args&&... args) {
        if (!m_freeHead)
            return nullptr;
        void* block = m_freeHead;
        m_freeHead = *static_cast<void**>(block);
        ++m_count;
        return new (block) T(static_cast<Args&&>(args)...);
    }

    /// Destructs and returns a block to the free list.
    ///
    /// # Safety
    /// `ptr` must have been returned by `Alloc()` on this pool and not already freed.
    void Free(T* ptr) {
        assert(ptr != nullptr);
        assert(Contains(ptr));
        ptr->~T();
        *reinterpret_cast<void**>(ptr) = m_freeHead;
        m_freeHead = ptr;
        --m_count;
    }

    /// Returns true if the pointer falls within this pool's backing buffer.
    bool Contains(const T* ptr) const {
        auto* p = reinterpret_cast<const uint8_t*>(ptr);
        return p >= m_buffer && p < m_buffer + m_blockSize * m_capacity;
    }

    /// Number of currently allocated objects.
    size_t Count() const { return m_count; }
    /// Maximum number of objects this pool can hold.
    size_t GetCapacity() const { return m_capacity; }
    /// Debug name of this pool.
    const char* Name() const { return m_name; }

  private:
    const char* m_name;
    uint8_t* m_buffer;
    void* m_freeHead;
    size_t m_blockSize;
    size_t m_capacity;
    size_t m_count;
};

// ── FrameAllocator — linear allocator that auto-resets each frame ───────────

/// Per-frame linear allocator that resets at the start of each frame.
///
/// Thin wrapper over `LinearAllocator` that provides a `BeginFrame()` method
/// instead of raw `Reset()`, making the frame-scoped lifetime explicit.
///
/// # Thread Safety
/// NOT thread-safe. Typically one per thread (main thread, render thread).
///
/// # Examples
/// ```cpp
/// FrameAllocator alloc("render_frame", 2 * 1024 * 1024);
/// // Each frame:
/// alloc.BeginFrame();
/// auto* cmd = alloc.New<DrawCommand>(mesh, material);
/// // ... use cmd, no need to free — BeginFrame() reclaims all.
/// ```
class FrameAllocator {
  public:
    /// Creates a frame allocator with the given name and byte capacity.
    FrameAllocator(const char* name, size_t sizeBytes) : m_alloc(name, sizeBytes) {}

    /// Allocates `size` bytes with given alignment. Returns nullptr if full.
    void* Alloc(size_t size, size_t alignment = alignof(std::max_align_t)) {
        return m_alloc.Alloc(size, alignment);
    }

    /// Allocates and constructs a T in-place. Returns nullptr if full.
    template <typename T, typename... Args> T* New(Args&&... args) {
        return m_alloc.New<T>(static_cast<Args&&>(args)...);
    }

    /// Resets the allocator for a new frame. Call at the start of each frame.
    ///
    /// # Safety
    /// All pointers from previous frame become dangling after this call.
    void BeginFrame() { m_alloc.Reset(); }

    /// Number of bytes currently allocated this frame.
    size_t Used() const { return m_alloc.Used(); }
    /// Number of bytes remaining before the allocator is full.
    size_t Remaining() const { return m_alloc.Remaining(); }
    /// Debug name of this allocator.
    const char* Name() const { return m_alloc.Name(); }

  private:
    LinearAllocator m_alloc;
};

} // namespace uz
