#pragma once

// MemoryProfiler — tracks registered arenas and provides diagnostic snapshots.
//
// Not a singleton: instantiate wherever needed. Designed to be testable in isolation.
// Requires UZ_MEMORY_PROFILING to be defined for total_alloc_count / reset_count
// fields on Arena; without it, those counters report zero.
//
// Thread-safety: the profiler itself is NOT thread-safe. External synchronization
// is required if register/unregister/snapshot happen from different threads.

#include "engine/core/arena.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// Lifetime category for an arena — determines leak-detection behavior.
enum class ArenaCategory : uint8_t {
    CPU_FRAME,      // Reset every frame (e.g., g_frame_arena)
    CPU_LEVEL,      // Reset on scene unload — leak detection target
    CPU_PERMANENT,  // Never reset — excluded from leak detection
};

// Snapshot of a single arena's state at a point in time.
struct MemArenaStats {
    const char*   name;               // Non-owning pointer to arena's debug name
    ArenaCategory category;
    size_t        current_used_bytes;  // Arena::offset
    size_t        peak_used_bytes;     // Arena::peak
    uint64_t      total_alloc_count;   // Cumulative alloc calls (0 if !UZ_MEMORY_PROFILING)
    uint32_t      reset_count;         // Cumulative reset calls (0 if !UZ_MEMORY_PROFILING)
    size_t        capacity_bytes;      // Arena::size
};

// Collection of arena snapshots.
struct MemorySnapshot {
    std::vector<MemArenaStats> arenas;
};

// MemoryProfiler — arena registry + diagnostics.
class MemoryProfiler {
public:
    MemoryProfiler() = default;
    ~MemoryProfiler() = default;

    // Non-copyable, non-movable (registry holds raw pointers to arenas).
    MemoryProfiler(const MemoryProfiler&) = delete;
    MemoryProfiler& operator=(const MemoryProfiler&) = delete;

    // Register an arena for tracking. `name` is stored as-is (non-owning).
    // Duplicate registration of the same arena pointer is a no-op.
    void register_arena(Arena* arena, const char* name, ArenaCategory cat);

    // Remove an arena from tracking. No-op if the arena was not registered.
    void unregister_arena(Arena* arena);

    // Read current stats from all registered arenas.
    [[nodiscard]] MemorySnapshot get_snapshot() const;

    // Check CPU_LEVEL arenas for potential leaks.
    // Returns the names of arenas that have allocations (offset > 0).
    [[nodiscard]] std::vector<std::string> detect_leaks() const;

    // Number of currently registered arenas.
    [[nodiscard]] size_t arena_count() const;

private:
    struct Entry {
        Arena*        arena;
        const char*   name;
        ArenaCategory category;
    };

    std::vector<Entry> entries_;
};

} // namespace uz
