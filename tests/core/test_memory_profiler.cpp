// test_memory_profiler.cpp — tests for Arena profiling instrumentation and MemoryProfiler.
//
// Covers: UZ_MEMORY_PROFILING counters on Arena, MemoryProfiler register/unregister,
//         get_snapshot, detect_leaks, arena_count, edge cases.

#include <gtest/gtest.h>
#include "engine/core/memory_profiler.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

using namespace uz;

// ============================================================================
// Helpers
// ============================================================================

struct ProfilerArenaFixture {
    static constexpr size_t kCapacity = 4096;
    alignas(64) uint8_t memory[kCapacity];
    Arena arena{};

    ProfilerArenaFixture() {
        arena_init(&arena, memory, kCapacity, "test_profiler");
    }
};

// ============================================================================
// Arena Instrumentation (UZ_MEMORY_PROFILING)
// ============================================================================

#ifdef UZ_MEMORY_PROFILING

TEST(ArenaInstrumentation, InitZerosProfilingCounters) {
    ProfilerArenaFixture f;

    EXPECT_EQ(f.arena.total_alloc_count, 0u);
    EXPECT_EQ(f.arena.reset_count, 0u);
}

TEST(ArenaInstrumentation, AllocIncrementsTotalAllocCount) {
    ProfilerArenaFixture f;

    arena_alloc(&f.arena, 32, 1);
    EXPECT_EQ(f.arena.total_alloc_count, 1u);

    arena_alloc(&f.arena, 64, 1);
    EXPECT_EQ(f.arena.total_alloc_count, 2u);

    arena_alloc(&f.arena, 16, 1);
    EXPECT_EQ(f.arena.total_alloc_count, 3u);
}

TEST(ArenaInstrumentation, ResetIncrementsResetCount) {
    ProfilerArenaFixture f;

    arena_alloc(&f.arena, 32, 1);
    arena_reset(&f.arena);
    EXPECT_EQ(f.arena.reset_count, 1u);

    arena_alloc(&f.arena, 64, 1);
    arena_reset(&f.arena);
    EXPECT_EQ(f.arena.reset_count, 2u);
}

TEST(ArenaInstrumentation, AllocCountSurvivesReset) {
    // total_alloc_count is cumulative — reset does not clear it.
    ProfilerArenaFixture f;

    arena_alloc(&f.arena, 32, 1);
    arena_alloc(&f.arena, 64, 1);
    EXPECT_EQ(f.arena.total_alloc_count, 2u);

    arena_reset(&f.arena);
    EXPECT_EQ(f.arena.total_alloc_count, 2u);

    arena_alloc(&f.arena, 16, 1);
    EXPECT_EQ(f.arena.total_alloc_count, 3u);
}

TEST(ArenaInstrumentation, ResetCountSurvivesAllocs) {
    ProfilerArenaFixture f;

    arena_reset(&f.arena);
    arena_alloc(&f.arena, 32, 1);
    arena_alloc(&f.arena, 64, 1);
    arena_reset(&f.arena);

    EXPECT_EQ(f.arena.reset_count, 2u);
}

TEST(ArenaInstrumentation, PeakStillTrackedWithProfiling) {
    // Ensure the existing peak tracking is not broken by profiling additions.
    ProfilerArenaFixture f;

    arena_alloc(&f.arena, 200, 1);
    arena_alloc(&f.arena, 100, 1);
    const size_t peak_before = f.arena.peak;

    arena_reset(&f.arena);

    EXPECT_GE(f.arena.peak, 300u);
    EXPECT_EQ(f.arena.peak, peak_before);
}

TEST(ArenaInstrumentation, ReInitResetsCounters) {
    // Re-initializing an arena should zero the profiling counters.
    ProfilerArenaFixture f;

    arena_alloc(&f.arena, 32, 1);
    arena_reset(&f.arena);
    ASSERT_GT(f.arena.total_alloc_count, 0u);
    ASSERT_GT(f.arena.reset_count, 0u);

    arena_init(&f.arena, f.memory, ProfilerArenaFixture::kCapacity, "reinit");

    EXPECT_EQ(f.arena.total_alloc_count, 0u);
    EXPECT_EQ(f.arena.reset_count, 0u);
}

#endif // UZ_MEMORY_PROFILING

// ============================================================================
// MemoryProfiler — empty state
// ============================================================================

TEST(MemoryProfilerEmpty, ArenaCountIsZero) {
    MemoryProfiler profiler;
    EXPECT_EQ(profiler.arena_count(), 0u);
}

TEST(MemoryProfilerEmpty, SnapshotIsEmpty) {
    MemoryProfiler profiler;
    auto snap = profiler.get_snapshot();
    EXPECT_TRUE(snap.arenas.empty());
}

TEST(MemoryProfilerEmpty, DetectLeaksReturnsEmpty) {
    MemoryProfiler profiler;
    auto leaks = profiler.detect_leaks();
    EXPECT_TRUE(leaks.empty());
}

// ============================================================================
// MemoryProfiler — register / unregister
// ============================================================================

TEST(MemoryProfilerRegister, RegisterIncreasesCount) {
    ProfilerArenaFixture f;
    MemoryProfiler profiler;

    profiler.register_arena(&f.arena, "a", ArenaCategory::CPU_FRAME);
    EXPECT_EQ(profiler.arena_count(), 1u);
}

TEST(MemoryProfilerRegister, RegisterMultiple) {
    alignas(64) uint8_t mem1[1024];
    alignas(64) uint8_t mem2[1024];
    alignas(64) uint8_t mem3[1024];
    Arena a1{}, a2{}, a3{};
    arena_init(&a1, mem1, sizeof(mem1), "a1");
    arena_init(&a2, mem2, sizeof(mem2), "a2");
    arena_init(&a3, mem3, sizeof(mem3), "a3");

    MemoryProfiler profiler;
    profiler.register_arena(&a1, "a1", ArenaCategory::CPU_FRAME);
    profiler.register_arena(&a2, "a2", ArenaCategory::CPU_LEVEL);
    profiler.register_arena(&a3, "a3", ArenaCategory::CPU_PERMANENT);

    EXPECT_EQ(profiler.arena_count(), 3u);
}

TEST(MemoryProfilerRegister, DuplicateRegistrationIsNoOp) {
    ProfilerArenaFixture f;
    MemoryProfiler profiler;

    profiler.register_arena(&f.arena, "dup", ArenaCategory::CPU_FRAME);
    profiler.register_arena(&f.arena, "dup", ArenaCategory::CPU_FRAME);

    EXPECT_EQ(profiler.arena_count(), 1u);
}

TEST(MemoryProfilerUnregister, UnregisterDecreasesCount) {
    ProfilerArenaFixture f;
    MemoryProfiler profiler;

    profiler.register_arena(&f.arena, "a", ArenaCategory::CPU_FRAME);
    profiler.unregister_arena(&f.arena);

    EXPECT_EQ(profiler.arena_count(), 0u);
}

TEST(MemoryProfilerUnregister, UnregisterNonRegisteredIsNoOp) {
    ProfilerArenaFixture f;
    MemoryProfiler profiler;

    // Should not crash or change count.
    profiler.unregister_arena(&f.arena);
    EXPECT_EQ(profiler.arena_count(), 0u);
}

TEST(MemoryProfilerUnregister, UnregisterOneOfMany) {
    alignas(64) uint8_t mem1[512];
    alignas(64) uint8_t mem2[512];
    alignas(64) uint8_t mem3[512];
    Arena a1{}, a2{}, a3{};
    arena_init(&a1, mem1, sizeof(mem1), "a1");
    arena_init(&a2, mem2, sizeof(mem2), "a2");
    arena_init(&a3, mem3, sizeof(mem3), "a3");

    MemoryProfiler profiler;
    profiler.register_arena(&a1, "a1", ArenaCategory::CPU_FRAME);
    profiler.register_arena(&a2, "a2", ArenaCategory::CPU_LEVEL);
    profiler.register_arena(&a3, "a3", ArenaCategory::CPU_PERMANENT);

    profiler.unregister_arena(&a2);

    EXPECT_EQ(profiler.arena_count(), 2u);

    // Snapshot should not contain a2.
    auto snap = profiler.get_snapshot();
    EXPECT_EQ(snap.arenas.size(), 2u);
    for (const auto& s : snap.arenas) {
        EXPECT_STRNE(s.name, "a2");
    }
}

// ============================================================================
// MemoryProfiler — get_snapshot
// ============================================================================

TEST(MemoryProfilerSnapshot, ReturnsCorrectStats) {
    ProfilerArenaFixture f;
    MemoryProfiler profiler;

    profiler.register_arena(&f.arena, "snap_test", ArenaCategory::CPU_FRAME);

    arena_alloc(&f.arena, 128, 1);
    arena_alloc(&f.arena, 64, 1);

    auto snap = profiler.get_snapshot();
    ASSERT_EQ(snap.arenas.size(), 1u);

    const auto& s = snap.arenas[0];
    EXPECT_STREQ(s.name, "snap_test");
    EXPECT_EQ(s.category, ArenaCategory::CPU_FRAME);
    EXPECT_GE(s.current_used_bytes, 192u);     // 128 + 64 (at minimum)
    EXPECT_GE(s.peak_used_bytes, 192u);
    EXPECT_EQ(s.capacity_bytes, ProfilerArenaFixture::kCapacity);

#ifdef UZ_MEMORY_PROFILING
    EXPECT_EQ(s.total_alloc_count, 2u);
    EXPECT_EQ(s.reset_count, 0u);
#else
    EXPECT_EQ(s.total_alloc_count, 0u);
    EXPECT_EQ(s.reset_count, 0u);
#endif
}

TEST(MemoryProfilerSnapshot, MultipleArenasAllPresent) {
    alignas(64) uint8_t mem1[1024];
    alignas(64) uint8_t mem2[2048];
    Arena a1{}, a2{};
    arena_init(&a1, mem1, sizeof(mem1), "small");
    arena_init(&a2, mem2, sizeof(mem2), "large");

    MemoryProfiler profiler;
    profiler.register_arena(&a1, "small", ArenaCategory::CPU_FRAME);
    profiler.register_arena(&a2, "large", ArenaCategory::CPU_LEVEL);

    arena_alloc(&a1, 100, 1);
    arena_alloc(&a2, 500, 1);

    auto snap = profiler.get_snapshot();
    ASSERT_EQ(snap.arenas.size(), 2u);

    // Find each arena in the snapshot (order not guaranteed due to swap-and-pop).
    bool found_small = false;
    bool found_large = false;
    for (const auto& s : snap.arenas) {
        if (std::string(s.name) == "small") {
            found_small = true;
            EXPECT_EQ(s.capacity_bytes, 1024u);
            EXPECT_GE(s.current_used_bytes, 100u);
        } else if (std::string(s.name) == "large") {
            found_large = true;
            EXPECT_EQ(s.capacity_bytes, 2048u);
            EXPECT_GE(s.current_used_bytes, 500u);
        }
    }
    EXPECT_TRUE(found_small);
    EXPECT_TRUE(found_large);
}

TEST(MemoryProfilerSnapshot, SnapshotReflectsResetState) {
    ProfilerArenaFixture f;
    MemoryProfiler profiler;
    profiler.register_arena(&f.arena, "reset_snap", ArenaCategory::CPU_FRAME);

    arena_alloc(&f.arena, 256, 1);
    arena_reset(&f.arena);

    auto snap = profiler.get_snapshot();
    ASSERT_EQ(snap.arenas.size(), 1u);

    // After reset, current_used should be 0, but peak should still reflect 256+.
    EXPECT_EQ(snap.arenas[0].current_used_bytes, 0u);
    EXPECT_GE(snap.arenas[0].peak_used_bytes, 256u);

#ifdef UZ_MEMORY_PROFILING
    EXPECT_EQ(snap.arenas[0].total_alloc_count, 1u);
    EXPECT_EQ(snap.arenas[0].reset_count, 1u);
#endif
}

// ============================================================================
// MemoryProfiler — detect_leaks
// ============================================================================

TEST(MemoryProfilerLeaks, CpuLevelWithAllocations_DetectsLeak) {
    ProfilerArenaFixture f;
    MemoryProfiler profiler;
    profiler.register_arena(&f.arena, "level_leaky", ArenaCategory::CPU_LEVEL);

    // Allocate but do NOT reset — simulates a level unload without cleanup.
    arena_alloc(&f.arena, 64, 1);

    auto leaks = profiler.detect_leaks();
    ASSERT_EQ(leaks.size(), 1u);
    EXPECT_EQ(leaks[0], "level_leaky");
}

TEST(MemoryProfilerLeaks, CpuLevelAfterReset_NoLeak) {
    ProfilerArenaFixture f;
    MemoryProfiler profiler;
    profiler.register_arena(&f.arena, "level_clean", ArenaCategory::CPU_LEVEL);

    arena_alloc(&f.arena, 128, 1);
    arena_reset(&f.arena);

    auto leaks = profiler.detect_leaks();
    EXPECT_TRUE(leaks.empty());
}

TEST(MemoryProfilerLeaks, CpuFrameWithAllocations_NotReportedAsLeak) {
    // CPU_FRAME arenas are not checked for leaks.
    ProfilerArenaFixture f;
    MemoryProfiler profiler;
    profiler.register_arena(&f.arena, "frame_busy", ArenaCategory::CPU_FRAME);

    arena_alloc(&f.arena, 64, 1);

    auto leaks = profiler.detect_leaks();
    EXPECT_TRUE(leaks.empty());
}

TEST(MemoryProfilerLeaks, CpuPermanentWithAllocations_NotReportedAsLeak) {
    // CPU_PERMANENT arenas are never expected to be reset — not checked.
    ProfilerArenaFixture f;
    MemoryProfiler profiler;
    profiler.register_arena(&f.arena, "perm_full", ArenaCategory::CPU_PERMANENT);

    arena_alloc(&f.arena, 64, 1);

    auto leaks = profiler.detect_leaks();
    EXPECT_TRUE(leaks.empty());
}

TEST(MemoryProfilerLeaks, MultipleLevelArenas_ReportsAllLeaky) {
    alignas(64) uint8_t mem1[1024];
    alignas(64) uint8_t mem2[1024];
    alignas(64) uint8_t mem3[1024];
    Arena a1{}, a2{}, a3{};
    arena_init(&a1, mem1, sizeof(mem1), "level_a");
    arena_init(&a2, mem2, sizeof(mem2), "level_b");
    arena_init(&a3, mem3, sizeof(mem3), "level_c");

    MemoryProfiler profiler;
    profiler.register_arena(&a1, "level_a", ArenaCategory::CPU_LEVEL);
    profiler.register_arena(&a2, "level_b", ArenaCategory::CPU_LEVEL);
    profiler.register_arena(&a3, "level_c", ArenaCategory::CPU_LEVEL);

    arena_alloc(&a1, 32, 1);
    // a2 left empty — no leak
    arena_alloc(&a3, 64, 1);

    auto leaks = profiler.detect_leaks();
    ASSERT_EQ(leaks.size(), 2u);

    // Sort for deterministic comparison.
    std::sort(leaks.begin(), leaks.end());
    EXPECT_EQ(leaks[0], "level_a");
    EXPECT_EQ(leaks[1], "level_c");
}

TEST(MemoryProfilerLeaks, UnnamedArena_ReportsAsUnnamed) {
    alignas(64) uint8_t mem[512];
    Arena a{};
    arena_init(&a, mem, sizeof(mem), nullptr);

    MemoryProfiler profiler;
    profiler.register_arena(&a, nullptr, ArenaCategory::CPU_LEVEL);

    arena_alloc(&a, 16, 1);

    auto leaks = profiler.detect_leaks();
    ASSERT_EQ(leaks.size(), 1u);
    EXPECT_EQ(leaks[0], "(unnamed)");
}

TEST(MemoryProfilerLeaks, CpuLevelNoAllocations_NoLeak) {
    // A CPU_LEVEL arena that was never allocated into is not a leak.
    ProfilerArenaFixture f;
    MemoryProfiler profiler;
    profiler.register_arena(&f.arena, "level_empty", ArenaCategory::CPU_LEVEL);

    auto leaks = profiler.detect_leaks();
    EXPECT_TRUE(leaks.empty());
}

// ============================================================================
// MemoryProfiler — unregister removes from snapshot and leak detection
// ============================================================================

TEST(MemoryProfilerUnregisterEffect, UnregisteredArenaNotInSnapshot) {
    ProfilerArenaFixture f;
    MemoryProfiler profiler;

    profiler.register_arena(&f.arena, "gone", ArenaCategory::CPU_FRAME);
    profiler.unregister_arena(&f.arena);

    auto snap = profiler.get_snapshot();
    EXPECT_TRUE(snap.arenas.empty());
}

TEST(MemoryProfilerUnregisterEffect, UnregisteredArenaNotInLeaks) {
    ProfilerArenaFixture f;
    MemoryProfiler profiler;

    profiler.register_arena(&f.arena, "gone_level", ArenaCategory::CPU_LEVEL);
    arena_alloc(&f.arena, 64, 1);
    profiler.unregister_arena(&f.arena);

    auto leaks = profiler.detect_leaks();
    EXPECT_TRUE(leaks.empty());
}
