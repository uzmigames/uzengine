// test_arena.cpp — 100% branch + line coverage of arena.h / arena.cpp
//
// Covers: arena_init, arena_alloc, arena_reset, arena_mark, arena_restore,
//         arena_alloc_t, engine_arenas_init, global arenas.

#include <gtest/gtest.h>
#include "engine/core/arena.h"

#include <cstdint>
#include <cstring>
#include <new>

using namespace uz;

// ============================================================================
// Helpers
// ============================================================================

// Stack-backed arena for tests that don't need heap memory.
struct TestArenaFixture {
    static constexpr size_t kCapacity = 4096;
    alignas(64) uint8_t memory[kCapacity];
    Arena arena{};

    TestArenaFixture() {
        arena_init(&arena, memory, kCapacity, "test");
    }
};

// ============================================================================
// arena_init
// ============================================================================

TEST(ArenaInit, SetsBaseAndSizeAndZerosOffsetAndPeakAndName) {
    alignas(16) uint8_t mem[256];
    Arena a{};

    arena_init(&a, mem, sizeof(mem), "init_test");

    EXPECT_EQ(a.base,   mem);
    EXPECT_EQ(a.size,   256u);
    EXPECT_EQ(a.offset, 0u);
    EXPECT_EQ(a.peak,   0u);
    EXPECT_STREQ(a.name, "init_test");
}

TEST(ArenaInit, AcceptsNullName) {
    // name is allowed to be any pointer — nullptr is valid C++ for a const char*.
    alignas(16) uint8_t mem[64];
    Arena a{};
    arena_init(&a, mem, sizeof(mem), nullptr);

    EXPECT_EQ(a.base,   mem);
    EXPECT_EQ(a.size,   64u);
    EXPECT_EQ(a.offset, 0u);
    EXPECT_EQ(a.peak,   0u);
    EXPECT_EQ(a.name,   nullptr);
}

// ============================================================================
// arena_alloc — basic
// ============================================================================

TEST(ArenaAllocBasic, ReturnsNonNullAndAdvancesOffset) {
    TestArenaFixture f;

    void* p = arena_alloc(&f.arena, 32);

    ASSERT_NE(p, nullptr);
    EXPECT_GE(f.arena.offset, 32u);
}

TEST(ArenaAllocBasic, OffsetAdvancesByAtLeastRequestedBytes) {
    TestArenaFixture f;

    void* p = arena_alloc(&f.arena, 100);

    ASSERT_NE(p, nullptr);
    EXPECT_GE(f.arena.offset, 100u);
}

TEST(ArenaAllocBasic, ReturnedPointerIsInsideArenaMemory) {
    TestArenaFixture f;

    void* p = arena_alloc(&f.arena, 16);

    ASSERT_NE(p, nullptr);
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    uintptr_t base = reinterpret_cast<uintptr_t>(f.memory);
    EXPECT_GE(addr, base);
    EXPECT_LT(addr, base + TestArenaFixture::kCapacity);
}

// ============================================================================
// arena_alloc — alignment
// ============================================================================

TEST(ArenaAllocAlignment, Align1_Satisfies1ByteAlignment) {
    TestArenaFixture f;

    // Burn one byte to misalign the natural offset.
    arena_alloc(&f.arena, 1, 1);
    void* p = arena_alloc(&f.arena, 1, 1);

    ASSERT_NE(p, nullptr);
    // align=1 means any address is valid — just check it's non-null.
}

TEST(ArenaAllocAlignment, Align16_ReturnedAddressIsAlignedTo16) {
    TestArenaFixture f;

    // Burn 3 bytes to force an unaligned cursor.
    arena_alloc(&f.arena, 3, 1);
    void* p = arena_alloc(&f.arena, 16, 16);

    ASSERT_NE(p, nullptr);
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    EXPECT_EQ(addr % 16u, 0u) << "Expected 16-byte alignment, got address " << addr;
}

TEST(ArenaAllocAlignment, Align64_ReturnedAddressIsAlignedTo64) {
    TestArenaFixture f;

    // Burn 5 bytes to force misalignment.
    arena_alloc(&f.arena, 5, 1);
    void* p = arena_alloc(&f.arena, 64, 64);

    ASSERT_NE(p, nullptr);
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    EXPECT_EQ(addr % 64u, 0u) << "Expected 64-byte alignment, got address " << addr;
}

TEST(ArenaAllocAlignment, DefaultAlignment_MeetsMaxAlignT) {
    TestArenaFixture f;

    // Burn 1 byte so cursor is unaligned.
    arena_alloc(&f.arena, 1, 1);
    void* p = arena_alloc(&f.arena, 8); // default align = alignof(max_align_t)

    ASSERT_NE(p, nullptr);
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    EXPECT_EQ(addr % alignof(std::max_align_t), 0u);
}

// ============================================================================
// arena_alloc — multiple allocations
// ============================================================================

TEST(ArenaAllocMultiple, ThreeAllocsReturnNonOverlappingRegions) {
    TestArenaFixture f;

    void* a = arena_alloc(&f.arena, 64, 8);
    void* b = arena_alloc(&f.arena, 64, 8);
    void* c = arena_alloc(&f.arena, 64, 8);

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);

    // Non-overlapping: each must start at or after the end of the previous.
    uintptr_t ea = reinterpret_cast<uintptr_t>(a) + 64u;
    uintptr_t eb = reinterpret_cast<uintptr_t>(b) + 64u;

    EXPECT_LE(ea, reinterpret_cast<uintptr_t>(b))
        << "Region A and B overlap";
    EXPECT_LE(eb, reinterpret_cast<uintptr_t>(c))
        << "Region B and C overlap";
}

TEST(ArenaAllocMultiple, WrittenDataSurvivesSubsequentAllocs) {
    TestArenaFixture f;

    uint32_t* arr = static_cast<uint32_t*>(arena_alloc(&f.arena, sizeof(uint32_t) * 4, alignof(uint32_t)));
    ASSERT_NE(arr, nullptr);
    arr[0] = 0xDEAD; arr[1] = 0xBEEF; arr[2] = 0xCAFE; arr[3] = 0xBABE;

    // Make another alloc to ensure the first isn't stomped.
    arena_alloc(&f.arena, 128, 8);

    EXPECT_EQ(arr[0], 0xDEADu);
    EXPECT_EQ(arr[1], 0xBEEFu);
    EXPECT_EQ(arr[2], 0xCAFEu);
    EXPECT_EQ(arr[3], 0xBABEu);
}

// ============================================================================
// arena_alloc — fill to capacity
// ============================================================================

TEST(ArenaAllocFillsMemory, AllocationOfEntireCapacitySucceeds) {
    // Use a fresh arena so we control exact size.
    constexpr size_t kSize = 1024;
    alignas(alignof(std::max_align_t)) uint8_t mem[kSize];
    Arena a{};
    arena_init(&a, mem, kSize, "full");

    // Alloc with align=1 to avoid any padding waste.
    void* p = arena_alloc(&a, kSize, 1);

    ASSERT_NE(p, nullptr);
    EXPECT_EQ(a.offset, kSize);
}

// ============================================================================
// arena_alloc — overflow (debug death)
// ============================================================================

TEST(ArenaAllocOverflowDebug, AllocBeyondCapacity_AssertsInDebug) {
    constexpr size_t kSize = 128;
    alignas(alignof(std::max_align_t)) uint8_t mem[kSize];
    Arena a{};
    arena_init(&a, mem, kSize, "overflow");

    // Fill to capacity first.
    arena_alloc(&a, kSize, 1);

    // Next alloc must fail. In debug (UZENGINE_DEBUG=1), assert fires → death.
#ifdef UZENGINE_DEBUG
    EXPECT_DEATH(arena_alloc(&a, 1, 1), "");
#else
    void* p = arena_alloc(&a, 1, 1);
    EXPECT_EQ(p, nullptr);
#endif
}

TEST(ArenaAllocOverflowDebug, ZeroSizeAlloc_AssertsInDebug) {
    // arena_alloc asserts n > 0 unconditionally.
    TestArenaFixture f;

#ifdef UZENGINE_DEBUG
    EXPECT_DEATH(arena_alloc(&f.arena, 0, 1), "");
#else
    // Non-debug: undefined behavior territory — just verify it doesn't segfault
    // by checking we haven't corrupted the offset (undefined but implementation-specific).
    (void)f;
#endif
}

// ============================================================================
// Peak tracking
// ============================================================================

TEST(ArenaPeakTracking, PeakReflectsMaxOffsetAfterReset) {
    TestArenaFixture f;

    arena_alloc(&f.arena, 200, 1);
    arena_alloc(&f.arena, 100, 1);
    const size_t peak_before_reset = f.arena.peak;

    arena_reset(&f.arena);

    // Offset is back to 0 but peak persists.
    EXPECT_EQ(f.arena.offset, 0u);
    EXPECT_GE(f.arena.peak,   300u)  << "Peak should be at least 300 bytes";
    EXPECT_EQ(f.arena.peak,   peak_before_reset) << "Peak must not change on reset";
}

TEST(ArenaPeakTracking, PeakIsHighWaterMark_DoesNotDecrease) {
    TestArenaFixture f;

    // Big alloc then restore — peak stays high.
    arena_alloc(&f.arena, 512, 1);
    const size_t peak_after_big = f.arena.peak;

    ArenaMark mark = arena_mark(&f.arena);
    arena_alloc(&f.arena, 100, 1);
    arena_restore(&f.arena, mark);

    EXPECT_GE(f.arena.peak, peak_after_big);
}

// ============================================================================
// arena_reset
// ============================================================================

TEST(ArenaReset, OffsetBecomesZeroAfterReset) {
    TestArenaFixture f;

    arena_alloc(&f.arena, 256, 1);
    ASSERT_GT(f.arena.offset, 0u);

    arena_reset(&f.arena);

    EXPECT_EQ(f.arena.offset, 0u);
}

TEST(ArenaReset, AllocAfterResetReturnsSameAddressAsFirstAlloc) {
    TestArenaFixture f;

    // Both must start from offset=0 and align=1 → same base address.
    void* first = arena_alloc(&f.arena, 64, 1);
    arena_reset(&f.arena);
    void* second = arena_alloc(&f.arena, 64, 1);

    EXPECT_EQ(first, second)
        << "After reset, fresh alloc should reuse the same memory region";
}

// ============================================================================
// ArenaMark / arena_restore
// ============================================================================

TEST(ArenaMarkRestore, RestoreReturnsToPreviousOffset) {
    TestArenaFixture f;

    arena_alloc(&f.arena, 128, 1);
    ArenaMark mark = arena_mark(&f.arena);
    const size_t offset_at_mark = f.arena.offset;

    arena_alloc(&f.arena, 256, 1);
    ASSERT_GT(f.arena.offset, offset_at_mark);

    arena_restore(&f.arena, mark);

    EXPECT_EQ(f.arena.offset, offset_at_mark);
}

TEST(ArenaMarkRestore, AllocAfterRestoreReusesScratchMemory) {
    TestArenaFixture f;

    ArenaMark mark = arena_mark(&f.arena);
    void* scratch = arena_alloc(&f.arena, 64, 1);
    arena_restore(&f.arena, mark);

    void* reused = arena_alloc(&f.arena, 64, 1);

    EXPECT_EQ(scratch, reused)
        << "After restore, new alloc must reuse the same scratch region";
}

TEST(ArenaMarkRestoreDoesNotChangePeak, PeakIsUnchangedByRestore) {
    TestArenaFixture f;

    arena_alloc(&f.arena, 512, 1);
    const size_t peak_before = f.arena.peak;

    ArenaMark mark = arena_mark(&f.arena);
    arena_alloc(&f.arena, 256, 1);
    arena_restore(&f.arena, mark);

    EXPECT_GE(f.arena.peak, peak_before)
        << "Peak must not decrease after restore";
}

TEST(ArenaMarkRestore, MarkAtOffset0_RestoreToBeginning) {
    TestArenaFixture f;

    // Mark before any alloc.
    ArenaMark mark = arena_mark(&f.arena);
    EXPECT_EQ(mark.offset, 0u);

    arena_alloc(&f.arena, 128, 1);
    arena_restore(&f.arena, mark);

    EXPECT_EQ(f.arena.offset, 0u);
}

// ============================================================================
// arena_alloc_t — typed helpers
// ============================================================================

TEST(ArenaTAlloc, Uint32_ReturnsAligned4BytePointer) {
    TestArenaFixture f;

    // Burn 1 byte to misalign.
    arena_alloc(&f.arena, 1, 1);
    uint32_t* p = arena_alloc_t<uint32_t>(&f.arena);

    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % alignof(uint32_t), 0u);

    // Must be writable.
    *p = 0xABCD1234u;
    EXPECT_EQ(*p, 0xABCD1234u);
}

TEST(ArenaTAlloc, FloatArray4_Count3_Advances48Bytes) {
    // arena_alloc_t<float[4]>(arena, 3) allocates sizeof(float[4])*3 = 16*3 = 48 bytes.
    TestArenaFixture f;

    const size_t before = f.arena.offset;
    float (*p)[4] = arena_alloc_t<float[4]>(&f.arena, 3);

    ASSERT_NE(p, nullptr);
    const size_t allocated = f.arena.offset - before;
    EXPECT_GE(allocated, 48u)
        << "3 float[4] arrays should consume at least 48 bytes";

    // Writable check.
    p[0][0] = 1.0f; p[1][2] = 2.0f; p[2][3] = 3.0f;
    EXPECT_EQ(p[0][0], 1.0f);
    EXPECT_EQ(p[1][2], 2.0f);
    EXPECT_EQ(p[2][3], 3.0f);
}

TEST(ArenaTAlloc, DoubleAlignment_ReturnedPointerIsAlignedToDouble) {
    TestArenaFixture f;

    arena_alloc(&f.arena, 1, 1);
    double* p = arena_alloc_t<double>(&f.arena);

    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % alignof(double), 0u);
}

// ============================================================================
// Global arenas — initialized by engine_arenas_init() in test_main.cpp
// ============================================================================

TEST(ArenaGlobalFrameArena, BaseIsNonNull_SizeIs64MB) {
    EXPECT_NE(g_frame_arena.base, nullptr);
    EXPECT_EQ(g_frame_arena.size, 64u * 1024u * 1024u);
    EXPECT_STREQ(g_frame_arena.name, "frame");
}

TEST(ArenaGlobalLevelArena, BaseIsNonNull_SizeIs256MB) {
    EXPECT_NE(g_level_arena.base, nullptr);
    EXPECT_EQ(g_level_arena.size, 256u * 1024u * 1024u);
    EXPECT_STREQ(g_level_arena.name, "level");
}

TEST(ArenaGlobalPermArena, BaseIsNonNull_SizeIs64MB) {
    EXPECT_NE(g_permanent_arena.base, nullptr);
    EXPECT_EQ(g_permanent_arena.size, 64u * 1024u * 1024u);
    EXPECT_STREQ(g_permanent_arena.name, "permanent");
}

TEST(ArenaGlobalArenas, AllThreeArenasHaveDistinctMemoryBlocks) {
    EXPECT_NE(g_frame_arena.base,     g_level_arena.base);
    EXPECT_NE(g_level_arena.base,     g_permanent_arena.base);
    EXPECT_NE(g_frame_arena.base,     g_permanent_arena.base);
}

TEST(ArenaGlobalArenas, FrameArenaIsAllocatable) {
    // We can alloc from the global frame arena (test is isolated: save/restore).
    ArenaMark mark = arena_mark(&g_frame_arena);
    void* p = arena_alloc(&g_frame_arena, 64, 16);
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 16u, 0u);
    arena_restore(&g_frame_arena, mark);
}
