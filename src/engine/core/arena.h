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

// Typed allocation + construction — allocates one T on the arena and calls its
// constructor with the forwarded arguments. Destructors are NOT called automatically;
// the caller must call them manually before arena reset if T has non-trivial dtor.
template<typename T, typename... Args>
T* arena_new(Arena* arena, Args&&... args) {
    void* mem = arena_alloc(arena, sizeof(T), alignof(T));
    if (!mem) return nullptr;
    return new (mem) T(static_cast<Args&&>(args)...);
}

// Reset the arena to its initial state. All prior allocations are invalidated.
void arena_reset(Arena* arena);

// Save the current allocation position.
[[nodiscard]] ArenaMark arena_mark(const Arena* arena);

// Restore the arena to a previously saved position. All allocations made after
// that mark are invalidated. `mark` must have been obtained from the same arena.
void arena_restore(Arena* arena, ArenaMark mark);

// ============================================================================
// ArenaScopeGuard — RAII restore-on-exit for temporary allocations
// ============================================================================

struct ArenaScopeGuard {
    Arena*    arena;
    ArenaMark mark;

    explicit ArenaScopeGuard(Arena* a) : arena(a), mark(arena_mark(a)) {}
    ~ArenaScopeGuard() { arena_restore(arena, mark); }

    ArenaScopeGuard(const ArenaScopeGuard&) = delete;
    ArenaScopeGuard& operator=(const ArenaScopeGuard&) = delete;
};

// ============================================================================
// SmallVec<T, N> — inline small vector with dynamic spillover
// ============================================================================
//
// Stores up to N elements inline (stack). Beyond N, spills to heap via
// std::vector. Designed for frame-local arrays where N is tuned to the
// typical case. T must be trivially copyable for inline storage.

template<typename T, size_t N>
struct SmallVec {
    T      inline_buf[N];
    T*     data_ptr = inline_buf;
    size_t count    = 0;
    size_t cap      = N;
    bool   spilled  = false;

    SmallVec() = default;

    ~SmallVec() {
        if (spilled) {
            delete[] data_ptr;
        }
    }

    // Non-copyable (move-only if needed, currently deleted for simplicity).
    SmallVec(const SmallVec&) = delete;
    SmallVec& operator=(const SmallVec&) = delete;

    void push_back(const T& val) {
        if (count >= cap) {
            grow();
        }
        data_ptr[count++] = val;
    }

    [[nodiscard]] T&       operator[](size_t i)       { assert(i < count); return data_ptr[i]; }
    [[nodiscard]] const T& operator[](size_t i) const { assert(i < count); return data_ptr[i]; }
    [[nodiscard]] size_t   size() const { return count; }
    [[nodiscard]] bool     empty() const { return count == 0; }
    [[nodiscard]] T*       data() { return data_ptr; }
    [[nodiscard]] const T* data() const { return data_ptr; }
    [[nodiscard]] T*       begin() { return data_ptr; }
    [[nodiscard]] T*       end()   { return data_ptr + count; }
    [[nodiscard]] const T* begin() const { return data_ptr; }
    [[nodiscard]] const T* end()   const { return data_ptr + count; }

    void clear() { count = 0; }

private:
    void grow() {
        size_t new_cap = cap * 2;
        T* new_buf = new T[new_cap];
        std::memcpy(new_buf, data_ptr, count * sizeof(T));
        if (spilled) delete[] data_ptr;
        data_ptr = new_buf;
        cap      = new_cap;
        spilled  = true;
    }
};

// ============================================================================
// Global arenas — backing memory defined in arena.cpp
// ============================================================================

extern Arena g_frame_arena;      // 64 MB  — reset every frame
extern Arena g_level_arena;      // 256 MB — reset on scene unload
extern Arena g_permanent_arena;  // 64 MB  — never reset

// Call once at engine startup to initialize all three global arenas.
void engine_arenas_init();

} // namespace uz
