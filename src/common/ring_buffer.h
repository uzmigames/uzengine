// ---------------------------------------------------------------------------
// ring_buffer.h -- Lock-free single-producer / single-consumer ring buffer.
//
// Used to pass FrameData from the render thread to the encode thread without
// blocking either side.  The render thread overwrites the oldest entry when
// the buffer is full, ensuring the encoder always gets recent frames.
// ---------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <array>

namespace uz {

/// Lock-free SPSC ring buffer of capacity N.
/// T must be default-constructible and move-assignable.
template <typename T, std::size_t N>
class RingBuffer {
    static_assert(N >= 2, "RingBuffer capacity must be at least 2");
    static_assert((N & (N - 1)) == 0, "RingBuffer capacity must be a power of 2");

public:
    RingBuffer() = default;

    // Non-copyable, non-movable -- contains atomic members.
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /// Push a value from the producer thread.
    /// If the buffer is full, the oldest unread entry is overwritten
    /// (drop-oldest policy for real-time frame delivery).
    void push(T value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t next = (head + 1) & kMask;

        if (next == tail) {
            // Buffer full: advance tail to overwrite oldest entry
            tail_.store((tail + 1) & kMask, std::memory_order_release);
        }

        slots_[head] = std::move(value);
        head_.store(next, std::memory_order_release);
    }

    /// Pop a value on the consumer thread.
    /// Returns std::nullopt if the buffer is empty.
    std::optional<T> pop() {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);

        if (tail == head) {
            return std::nullopt; // empty
        }

        T value = std::move(slots_[tail]);
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return value;
    }

    /// Returns true if the buffer contains no items.
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    /// Returns the number of items currently in the buffer.
    [[nodiscard]] std::size_t size() const noexcept {
        const std::size_t h = head_.load(std::memory_order_acquire);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & kMask;
    }

private:
    static constexpr std::size_t kMask = N - 1;

    std::array<T, N> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0}; // written by producer
    alignas(64) std::atomic<std::size_t> tail_{0}; // written by consumer
};

} // namespace uz
