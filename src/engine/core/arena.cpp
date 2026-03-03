// Arena Allocator — implementation + global arena backing memory.
//
// The three global arenas (frame, level, permanent) use static arrays so they
// are available before any dynamic initialization runs. Call engine_arenas_init()
// once at startup to wire them up.

#include "arena.h"

#include <cassert>
#include <cstring>

namespace uz {

// ============================================================================
// Arena functions
// ============================================================================

void arena_init(Arena* arena, void* memory, size_t size, const char* name) {
    assert(arena != nullptr);
    assert(memory != nullptr);
    assert(size > 0);

    arena->base   = static_cast<uint8_t*>(memory);
    arena->size   = size;
    arena->offset = 0;
    arena->peak   = 0;
    arena->name   = name;
}

void* arena_alloc(Arena* arena, size_t n, size_t align) {
    assert(arena != nullptr);
    assert(n > 0);
    // Alignment must be a power of two.
    assert(align > 0 && (align & (align - 1)) == 0);

    // Round up current offset to satisfy alignment requirement.
    const uintptr_t current = reinterpret_cast<uintptr_t>(arena->base + arena->offset);
    const uintptr_t aligned = (current + align - 1) & ~(align - 1);
    const size_t    padding = static_cast<size_t>(aligned - current);
    const size_t    total   = padding + n;

    // Out-of-memory check — assert in debug, return nullptr in release.
    if (arena->offset + total > arena->size) {
        assert(false && "Arena out of memory");
        return nullptr;
    }

    arena->offset += total;

    // Track peak usage for profiling.
    if (arena->offset > arena->peak) {
        arena->peak = arena->offset;
    }

    return reinterpret_cast<void*>(aligned);
}

void arena_reset(Arena* arena) {
    assert(arena != nullptr);
    arena->offset = 0;
}

ArenaMark arena_mark(const Arena* arena) {
    assert(arena != nullptr);
    return ArenaMark{arena->offset};
}

void arena_restore(Arena* arena, ArenaMark mark) {
    assert(arena != nullptr);
    // Cannot restore to a position beyond the current offset.
    assert(mark.offset <= arena->offset);
    arena->offset = mark.offset;
}

ArenaStats arena_get_stats(const Arena* arena) {
    assert(arena != nullptr);
    return ArenaStats{
        .used     = arena->offset,
        .peak     = arena->peak,
        .capacity = arena->size,
        .name     = arena->name,
    };
}

// ============================================================================
// Global arena backing memory (static arrays — no dynamic init)
// ============================================================================

static uint8_t s_frame_mem[64  * 1024 * 1024];   // 64 MB
static uint8_t s_level_mem[256 * 1024 * 1024];   // 256 MB
static uint8_t s_perm_mem [64  * 1024 * 1024];   // 64 MB

Arena g_frame_arena;
Arena g_level_arena;
Arena g_permanent_arena;

void engine_arenas_init() {
    arena_init(&g_frame_arena,     s_frame_mem, sizeof(s_frame_mem),  "frame");
    arena_init(&g_level_arena,     s_level_mem, sizeof(s_level_mem),  "level");
    arena_init(&g_permanent_arena, s_perm_mem,  sizeof(s_perm_mem),   "permanent");
}

} // namespace uz
