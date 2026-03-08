// MemoryProfiler — implementation.

#include "memory_profiler.h"

#include <algorithm>
#include <cassert>

namespace uz {

void MemoryProfiler::register_arena(Arena* arena, const char* name, ArenaCategory cat) {
    assert(arena != nullptr);

    // Reject duplicate registration of the same arena pointer.
    for (const auto& e : entries_) {
        if (e.arena == arena) {
            return;
        }
    }

    entries_.push_back(Entry{arena, name, cat});
}

void MemoryProfiler::unregister_arena(Arena* arena) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [arena](const Entry& e) { return e.arena == arena; });

    if (it != entries_.end()) {
        // Swap-and-pop for O(1) removal (order doesn't matter).
        *it = entries_.back();
        entries_.pop_back();
    }
}

MemorySnapshot MemoryProfiler::get_snapshot() const {
    MemorySnapshot snap;
    snap.arenas.reserve(entries_.size());

    for (const auto& e : entries_) {
        MemArenaStats stats{};
        stats.name              = e.name;
        stats.category          = e.category;
        stats.current_used_bytes = e.arena->offset;
        stats.peak_used_bytes   = e.arena->peak;
        stats.capacity_bytes    = e.arena->size;

#ifdef UZ_MEMORY_PROFILING
        stats.total_alloc_count = e.arena->total_alloc_count;
        stats.reset_count       = e.arena->reset_count;
#else
        stats.total_alloc_count = 0;
        stats.reset_count       = 0;
#endif

        snap.arenas.push_back(stats);
    }

    return snap;
}

std::vector<std::string> MemoryProfiler::detect_leaks() const {
    std::vector<std::string> leaks;

    for (const auto& e : entries_) {
        if (e.category != ArenaCategory::CPU_LEVEL) {
            continue;
        }

        // A CPU_LEVEL arena with offset > 0 still has live allocations —
        // this is a potential leak if the level has been unloaded.
        if (e.arena->offset > 0) {
            leaks.emplace_back(e.name ? e.name : "(unnamed)");
        }
    }

    return leaks;
}

size_t MemoryProfiler::arena_count() const {
    return entries_.size();
}

} // namespace uz
