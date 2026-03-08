#pragma once

// ============================================================================
// profiler_ring_buffer.h -- Lock-free ring buffer for per-zone profiling data.
//
// Stores ProfileZoneRecord entries produced by ProfilerCollector::BeginZone/
// EndZone. The ring buffer is fixed-size (no heap allocations) and uses atomic
// operations for the write cursor, allowing concurrent producers from multiple
// threads without locks.
//
// Reading (FlushFrame) is expected to happen on a single consumer thread
// (the IPC/main thread). Concurrent reads are NOT supported.
// ============================================================================

#include <atomic>
#include <cstdint>

namespace uz {

/// A single completed zone timing record captured during a frame.
struct ProfileZoneRecord {
    const char* name       = nullptr;  // Zone name (string literal, NOT owned)
    uint32_t    thread_id  = 0;        // OS thread ID of the recording thread
    int64_t     start_us   = 0;        // Start timestamp in microseconds (relative)
    int64_t     end_us     = 0;        // End timestamp in microseconds (relative)
    uint32_t    call_count = 1;        // Always 1 per record (aggregated by consumer)
    uint32_t    frame_id   = 0;        // Frame number when this zone was recorded
};

/// Fixed-capacity lock-free ring buffer for ProfileZoneRecord entries.
///
/// Multiple threads may call push() concurrently (lock-free via atomic CAS).
/// drain() must be called from a single thread and returns the number of
/// records copied into the output buffer.
class ProfileRingBuffer {
public:
    static constexpr uint32_t kDefaultCapacity = 4096;

    ProfileRingBuffer() noexcept = default;

    /// Push a record into the ring buffer. Returns true if successful.
    /// When the buffer is full, the oldest entry is overwritten (ring semantics).
    /// Thread-safe: multiple threads may call push() concurrently.
    bool push(const ProfileZoneRecord& record) noexcept {
        const uint32_t slot = write_cursor_.fetch_add(1, std::memory_order_relaxed)
                              % kDefaultCapacity;
        records_[slot] = record;

        // Track how many total writes have occurred for drain().
        total_writes_.fetch_add(1, std::memory_order_release);
        return true;
    }

    /// Drain all records written since the last drain() call into |out|.
    /// Returns the number of records copied (at most |max_count|).
    /// NOT thread-safe with other drain() calls -- single consumer only.
    uint32_t drain(ProfileZoneRecord* out, uint32_t max_count) noexcept {
        const uint32_t total = total_writes_.load(std::memory_order_acquire);
        const uint32_t available = total - last_drain_total_;

        if (available == 0 || max_count == 0) return 0;

        // If more records were written than the ring capacity, we can only
        // read the most recent kDefaultCapacity entries (older ones were
        // overwritten).
        const uint32_t readable = (available > kDefaultCapacity)
                                  ? kDefaultCapacity : available;
        const uint32_t to_copy = (readable < max_count) ? readable : max_count;

        // Read from oldest to newest. The write cursor tells us where the
        // newest entry is; work backwards from there.
        const uint32_t write_pos = write_cursor_.load(std::memory_order_acquire);
        const uint32_t start = (write_pos - readable) % kDefaultCapacity;

        for (uint32_t i = 0; i < to_copy; ++i) {
            const uint32_t slot = (start + i) % kDefaultCapacity;
            out[i] = records_[slot];
        }

        last_drain_total_ = total;
        return to_copy;
    }

    /// Reset the buffer, discarding all records. NOT thread-safe with push().
    void clear() noexcept {
        write_cursor_.store(0, std::memory_order_relaxed);
        total_writes_.store(0, std::memory_order_relaxed);
        last_drain_total_ = 0;
    }

    /// Number of records written since the last drain (or since creation).
    [[nodiscard]] uint32_t pending_count() const noexcept {
        return total_writes_.load(std::memory_order_acquire) - last_drain_total_;
    }

    /// Total capacity of the ring buffer.
    [[nodiscard]] static constexpr uint32_t capacity() noexcept {
        return kDefaultCapacity;
    }

private:
    ProfileZoneRecord           records_[kDefaultCapacity] = {};
    std::atomic<uint32_t>       write_cursor_{0};
    std::atomic<uint32_t>       total_writes_{0};
    uint32_t                    last_drain_total_ = 0;
};

// ============================================================================
// ProfilerCollector -- Frame-oriented zone collector built on the ring buffer.
//
// Usage:
//   ProfilerCollector::begin_zone("shadow_pass");
//   // ... do work ...
//   ProfilerCollector::end_zone("shadow_pass");
//
//   // At frame boundary:
//   auto span = ProfilerCollector::flush_frame(frame_id);
// ============================================================================

class ProfilerCollector {
public:
    static constexpr int kMaxFrames       = 8;
    static constexpr int kMaxRecordsTotal = 4096;

    /// Begin a named zone. Records the start timestamp.
    /// Thread-safe.
    static void begin_zone(const char* name) noexcept;

    /// End a named zone. Finds the matching begin and pushes a complete
    /// record into the ring buffer.
    /// Thread-safe.
    static void end_zone(const char* name) noexcept;

    /// Mark a frame boundary. Drains the ring buffer into the frame history.
    /// Returns the number of records captured for this frame.
    /// Single-threaded (call from main/IPC thread).
    static uint32_t flush_frame(uint32_t frame_id) noexcept;

    /// Get records for the last N frames (1..kMaxFrames).
    /// Returns the number of records copied into |out|.
    /// Single-threaded.
    static uint32_t get_frame_data(ProfileZoneRecord* out, uint32_t max_count,
                                   int num_frames = 1) noexcept;

    /// Check/set whether the collector is recording.
    static void set_enabled(bool enabled) noexcept;
    [[nodiscard]] static bool is_enabled() noexcept;

    /// Reset all state (for testing).
    static void reset() noexcept;

    /// Get the underlying ring buffer (for testing/inspection).
    static ProfileRingBuffer& ring_buffer() noexcept;
};

} // namespace uz
