#pragma once

/// UzEngine RingBuffer — Lock-free SPSC circular queue.
///
/// # Design
/// - **SPSC**: Single-Producer Single-Consumer (one reader, one writer)
/// - **Lock-free**: Uses atomic operations only (std::memory_order optimizations)
/// - **Power-of-2 capacity**: Index masking instead of modulo
/// - **Intrusive**: Elements stored contiguously (no heap allocation)
///
/// # Memory Ordering
/// - Acquire/Release semantics for thread synchronization
/// - Relaxed loads where possible for performance
/// - False-sharing prevention via cache line alignment (64-byte padding)
///
/// # Use Case
/// Ring buffers excel for:
/// - Audio frame streaming (producer: audio decoder, consumer: output)
/// - Command queues (producer: main thread, consumer: GPU thread)
/// - Log message buffering
///
/// # Example
/// ```cpp
/// RingBuffer<Message, 4096> queue;
/// // Producer thread:
/// Message msg{...};
/// queue.TryPush(msg);
///
/// // Consumer thread:
/// Message out;
/// while (queue.TryPop(out)) { process(out); }
/// ```

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <atomic>
#include <new>
#include <type_traits>

namespace uz {

/// Fixed-capacity lock-free circular queue for single producer/consumer.
///
/// # Template Parameters
/// * `T` - Element type
/// * `Capacity` - Must be a power of 2 (e.g., 256, 512, 1024, 4096)
///
/// # Thread Safety
/// - Thread-safe IFF: one thread calls Puush/Size/Full, another calls Pop/Empty
/// - Multiple producers or consumers require external synchronization
/// - No locks: uses atomic operations only
template <typename T, size_t Capacity>
class RingBuffer {
    static_assert(Capacity > 0, "Capacity must be > 0");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

public:
    /// Initializes an empty ring buffer.
    RingBuffer() = default;

    /// Destroys all remaining items in the buffer.
    ~RingBuffer() { Clear(); }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    bool TryPush(const T& item) {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t next = (head + 1) & kMask;
        if (next == m_tail.load(std::memory_order_acquire)) return false; // full
        new (&m_storage[head]) T(item);
        m_head.store(next, std::memory_order_release);
        return true;
    }

    bool TryPush(T&& item) {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t next = (head + 1) & kMask;
        if (next == m_tail.load(std::memory_order_acquire)) return false;
        new (&m_storage[head]) T(static_cast<T&&>(item));
        m_head.store(next, std::memory_order_release);
        return true;
    }

    bool TryPop(T& out) {
        size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) return false; // empty
        out = static_cast<T&&>(reinterpret_cast<T&>(m_storage[tail]));
        reinterpret_cast<T&>(m_storage[tail]).~T();
        m_tail.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    size_t Size() const {
        size_t head = m_head.load(std::memory_order_acquire);
        size_t tail = m_tail.load(std::memory_order_acquire);
        return (head - tail) & kMask;
    }

    bool Empty() const { return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire); }
    bool Full() const { return Size() == Capacity - 1; }
    static constexpr size_t GetCapacity() { return Capacity - 1; } // usable capacity

    void Clear() {
        T tmp;
        while (TryPop(tmp)) {}
    }

private:
    static constexpr size_t kMask = Capacity - 1;
    alignas(64) std::atomic<size_t> m_head{0};
    alignas(64) std::atomic<size_t> m_tail{0};
    typename std::aligned_storage<sizeof(T), alignof(T)>::type m_storage[Capacity];
};

} // namespace uz
