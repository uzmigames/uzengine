#pragma once

// Arena Allocator — fast bump allocator for frame/level/permanent lifetimes.
//
// Arenas own no memory themselves; the caller provides a pre-allocated block via
// arena_init(). All allocations are pointer-bump with alignment padding. Reset
// is O(1) — just rewind the offset. ArenaMark/arena_restore allow temporary
// sub-allocations without a full reset.
//
// Thread-safety: arenas are NOT thread-safe. Each thread that needs scratch
// memory should have its own arena, or access must be externally synchronized.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace uz {

// ============================================================================
// Arena — linear bump allocator
// ============================================================================

struct Arena {
    uint8_t*    base;       // Start of memory block
    size_t      size;       // Total capacity in bytes
    size_t      offset;     // Current allocation cursor
    size_t      peak;       // High-water mark for profiling
    const char* name;       // Debug name (non-owning pointer)
};

// Saved allocation position for temporary sub-allocations.
struct ArenaMark {
    size_t offset;
};

// Read-only snapshot of an arena's current state, useful for debug/profiling UIs.
struct ArenaStats {
    size_t      used;       // Current bytes allocated (offset)
    size_t      peak;       // High-water mark since init
    size_t      capacity;   // Total capacity in bytes
    const char* name;       // Debug name (non-owning)
};

// Return a snapshot of the arena's current statistics.
[[nodiscard]] ArenaStats arena_get_stats(const Arena* arena);

// Initialize an arena with a pre-allocated memory block.
// `memory` must remain valid for the arena's entire lifetime.
void arena_init(Arena* arena, void* memory, size_t size, const char* name);

// Allocate `n` bytes with the given alignment (must be a power of 2).
// Returns nullptr if the arena cannot satisfy the request (release builds).
// Asserts in debug builds.
void* arena_alloc(Arena* arena, size_t n, size_t align = alignof(std::max_align_t));

// Typed allocation helper — allocates `count` objects of type T with proper alignment.
// Does NOT call constructors — the caller must placement-new if T is non-trivial.
template<typename T>
T* arena_alloc_t(Arena* arena, size_t count = 1) {
    return static_cast<T*>(arena_alloc(arena, sizeof(T) * count, alignof(T)));
}

// Reset the arena to its initial state. All prior allocations are invalidated.
void arena_reset(Arena* arena);

// Save the current allocation position.
[[nodiscard]] ArenaMark arena_mark(const Arena* arena);

// Restore the arena to a previously saved position. All allocations made after
// that mark are invalidated. `mark` must have been obtained from the same arena.
void arena_restore(Arena* arena, ArenaMark mark);

// ============================================================================
// Global arenas — backing memory defined in arena.cpp
// ============================================================================

extern Arena g_frame_arena;      // 64 MB  — reset every frame
extern Arena g_level_arena;      // 256 MB — reset on scene unload
extern Arena g_permanent_arena;  // 64 MB  — never reset

// Call once at engine startup to initialize all three global arenas.
void engine_arenas_init();

} // namespace uz
