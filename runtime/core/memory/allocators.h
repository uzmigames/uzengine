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

class LinearAllocator {
  public:
    LinearAllocator(const char* name, size_t sizeBytes)
        : m_name(name), m_size(sizeBytes), m_offset(0) {
        m_buffer = static_cast<uint8_t*>(std::malloc(sizeBytes));
        assert(m_buffer && "LinearAllocator: allocation failed");
    }

    ~LinearAllocator() { std::free(m_buffer); }

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    void* Alloc(size_t size, size_t alignment = alignof(std::max_align_t)) {
        size_t aligned = AlignUp(m_offset, alignment);
        if (aligned + size > m_size)
            return nullptr; // out of memory
        void* ptr = m_buffer + aligned;
        m_offset = aligned + size;
        return ptr;
    }

    template <typename T, typename... Args> T* New(Args&&... args) {
        void* mem = Alloc(sizeof(T), alignof(T));
        if (!mem)
            return nullptr;
        return new (mem) T(static_cast<Args&&>(args)...);
    }

    void Reset() { m_offset = 0; }

    size_t Used() const { return m_offset; }
    size_t Remaining() const { return m_size - m_offset; }
    size_t GetSize() const { return m_size; }
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

template <typename T> class PoolAllocator {
  public:
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

    ~PoolAllocator() { std::free(m_buffer); }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    template <typename... Args> T* Alloc(Args&&... args) {
        if (!m_freeHead)
            return nullptr;
        void* block = m_freeHead;
        m_freeHead = *static_cast<void**>(block);
        ++m_count;
        return new (block) T(static_cast<Args&&>(args)...);
    }

    void Free(T* ptr) {
        assert(ptr != nullptr);
        assert(Contains(ptr));
        ptr->~T();
        *reinterpret_cast<void**>(ptr) = m_freeHead;
        m_freeHead = ptr;
        --m_count;
    }

    bool Contains(const T* ptr) const {
        auto* p = reinterpret_cast<const uint8_t*>(ptr);
        return p >= m_buffer && p < m_buffer + m_blockSize * m_capacity;
    }

    size_t Count() const { return m_count; }
    size_t GetCapacity() const { return m_capacity; }
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

class FrameAllocator {
  public:
    FrameAllocator(const char* name, size_t sizeBytes) : m_alloc(name, sizeBytes) {}

    void* Alloc(size_t size, size_t alignment = alignof(std::max_align_t)) {
        return m_alloc.Alloc(size, alignment);
    }

    template <typename T, typename... Args> T* New(Args&&... args) {
        return m_alloc.New<T>(static_cast<Args&&>(args)...);
    }

    void BeginFrame() { m_alloc.Reset(); }

    size_t Used() const { return m_alloc.Used(); }
    size_t Remaining() const { return m_alloc.Remaining(); }
    const char* Name() const { return m_alloc.Name(); }

  private:
    LinearAllocator m_alloc;
};

} // namespace uz
