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

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
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
template <typename T, size_t Capacity> class RingBuffer {
    static_assert(Capacity > 0, "Capacity must be > 0");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

  public:
    /// Initializes an empty ring buffer.
    RingBuffer() = default;

    /// Destroys all remaining items in the buffer.
    ~RingBuffer() { Clear(); }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    /// Attempts to enqueue an item by copy. Returns false if the buffer is full.
    ///
    /// # Memory Ordering
    /// 1. Loads `m_head` with relaxed (only the producer writes it).
    /// 2. Loads `m_tail` with acquire to see the consumer's latest pop.
    /// 3. Stores `m_head` with release so the consumer sees the new element
    ///    before it sees the advanced head index.
    ///
    /// # Arguments
    /// * `item` - Element to copy into the next free slot.
    ///
    /// # Returns
    /// `true` if the item was enqueued, `false` if the buffer is full
    /// (next head position equals tail — one slot is always kept empty
    /// as a sentinel to distinguish full from empty).
    bool TryPush(const T& item) {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t next = (head + 1) & kMask;
        if (next == m_tail.load(std::memory_order_acquire))
            return false; // full
        new (&m_storage[head]) T(item);
        m_head.store(next, std::memory_order_release);
        return true;
    }

    /// Attempts to enqueue an item by move. Returns false if the buffer is full.
    ///
    /// Same semantics and memory ordering as the const-ref overload, but
    /// move-constructs the element into the slot to avoid a copy.
    ///
    /// # Arguments
    /// * `item` - Rvalue reference to the element to move into the buffer.
    ///
    /// # Returns
    /// `true` if the item was enqueued, `false` if the buffer is full.
    bool TryPush(T&& item) {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t next = (head + 1) & kMask;
        if (next == m_tail.load(std::memory_order_acquire))
            return false;
        new (&m_storage[head]) T(static_cast<T&&>(item));
        m_head.store(next, std::memory_order_release);
        return true;
    }

    /// Attempts to dequeue the oldest item into `out`. Returns false if empty.
    ///
    /// # Memory Ordering
    /// 1. Loads `m_tail` with relaxed (only the consumer writes it).
    /// 2. Loads `m_head` with acquire to see the producer's latest push.
    /// 3. Move-assigns the element out, destructs the slot copy.
    /// 4. Stores `m_tail` with release so the producer sees the freed slot.
    ///
    /// # Arguments
    /// * `out` - Receives the dequeued element via move-assignment.
    ///
    /// # Returns
    /// `true` if an item was dequeued, `false` if the buffer was empty.
    bool TryPop(T& out) {
        size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire))
            return false; // empty
        out = static_cast<T&&>(reinterpret_cast<T&>(m_storage[tail]));
        reinterpret_cast<T&>(m_storage[tail]).~T();
        m_tail.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    /// Current number of items in the buffer.
    size_t Size() const {
        size_t head = m_head.load(std::memory_order_acquire);
        size_t tail = m_tail.load(std::memory_order_acquire);
        return (head - tail) & kMask;
    }

    /// True if buffer contains no items.
    bool Empty() const {
        return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
    }
    /// True if buffer is at maximum usable capacity.
    bool Full() const { return Size() == Capacity - 1; }
    /// Usable capacity (Capacity - 1 due to sentinel slot).
    static constexpr size_t GetCapacity() { return Capacity - 1; }

    /// Drains all items from the buffer, destroying each element.
    void Clear() {
        T tmp;
        while (TryPop(tmp)) {
        }
    }

  private:
    /// Bitmask for power-of-2 wraparound: `index & kMask` replaces `index % Capacity`.
    static constexpr size_t kMask = Capacity - 1;

    /// Write cursor (next slot to write into). Only modified by the producer
    /// thread. Aligned to a 64-byte cache line to prevent false sharing with
    /// `m_tail`, which lives on a different cache line.
    alignas(64) std::atomic<size_t> m_head{0};

    /// Read cursor (next slot to read from). Only modified by the consumer
    /// thread. Cache-line-aligned to avoid false sharing with `m_head`.
    /// When `m_tail == m_head`, the buffer is empty.
    alignas(64) std::atomic<size_t> m_tail{0};

    /// Contiguous element storage. Slots between `m_tail` and `m_head` (modulo
    /// Capacity) contain live constructed objects; all other slots are
    /// uninitialized raw memory. One slot is always kept empty as a sentinel
    /// to distinguish full (head+1 == tail) from empty (head == tail), which
    /// is why usable capacity is `Capacity - 1`.
    typename std::aligned_storage<sizeof(T), alignof(T)>::type m_storage[Capacity];
};

} // namespace uz
