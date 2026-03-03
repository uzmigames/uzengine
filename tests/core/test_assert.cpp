// test_assert.cpp — comprehensive tests for engine/core/assert.h
//
// Covers: ASSERT, ASSERT_MSG, VERIFY, ENSURE macros in non-shipping mode.
// ASSERT/ASSERT_MSG/VERIFY call abort() on failure — tested via EXPECT_DEATH.
// ENSURE logs to stderr but does NOT abort — tested via EXPECT_EXIT (no death).
// Dangling-else safety: all macros must be safe in unbraced if/else.

#include <gtest/gtest.h>
#include "engine/core/assert.h"

// ============================================================================
// ASSERT — truthy expressions must not abort
// ============================================================================

TEST(AssertMacro, TrueExpression_DoesNotAbort) {
    // ASSERT(true) should be a no-op at runtime.
    ASSERT(true);
    ASSERT(1 == 1);
    ASSERT(42 > 0);
}

TEST(AssertMacro, NonZeroInteger_DoesNotAbort) {
    int value = 7;
    ASSERT(value);
    ASSERT(value != 0);
}

// ============================================================================
// ASSERT — falsy expressions must abort (EXPECT_DEATH)
// ============================================================================

TEST(AssertMacro, FalseExpression_Aborts) {
    EXPECT_DEATH(ASSERT(false), "ASSERTION FAILED");
}

TEST(AssertMacro, ZeroExpression_Aborts) {
    EXPECT_DEATH(ASSERT(0 == 1), "ASSERTION FAILED");
}

TEST(AssertMacro, NullptrExpression_Aborts) {
    void* p = nullptr;
    EXPECT_DEATH(ASSERT(p != nullptr), "ASSERTION FAILED");
}

TEST(AssertMacro, DeathMessageContainsExpression) {
    // The stringified expression should appear in stderr output.
    EXPECT_DEATH(ASSERT(false), "false");
}

TEST(AssertMacro, DeathMessageContainsFileInfo) {
    // __FILE__ embeds the file path in the message.
    EXPECT_DEATH(ASSERT(false), "test_assert.cpp");
}

// ============================================================================
// ASSERT_MSG — with custom message
// ============================================================================

TEST(AssertMsgMacro, TrueExpression_DoesNotAbort) {
    ASSERT_MSG(true, "this should not fire");
    ASSERT_MSG(1 + 1 == 2, "math is broken");
}

TEST(AssertMsgMacro, FalseExpression_AbortsWithMessage) {
    EXPECT_DEATH(ASSERT_MSG(false, "test message"), "ASSERTION FAILED");
}

TEST(AssertMsgMacro, DeathMessageContainsCustomText) {
    EXPECT_DEATH(ASSERT_MSG(false, "custom failure text"), "custom failure text");
}

TEST(AssertMsgMacro, DeathMessageContainsExpression) {
    EXPECT_DEATH(ASSERT_MSG(1 == 2, "bad math"), "1 == 2");
}

TEST(AssertMsgMacro, DeathMessageContainsFileInfo) {
    EXPECT_DEATH(ASSERT_MSG(false, "msg"), "test_assert.cpp");
}

// ============================================================================
// VERIFY — in non-shipping mode, VERIFY is equivalent to ASSERT
// ============================================================================

TEST(VerifyMacro, TrueExpression_DoesNotAbort) {
    VERIFY(true);
    VERIFY(100 > 50);
}

TEST(VerifyMacro, FalseExpression_AbortsInDebug) {
    // In non-shipping builds, VERIFY(false) == ASSERT(false) -> abort.
    EXPECT_DEATH(VERIFY(false), "ASSERTION FAILED");
}

TEST(VerifyMacro, ExpressionSideEffectsArePreserved) {
    // VERIFY must always evaluate the expression (even in shipping).
    int counter = 0;
    VERIFY(++counter);
    EXPECT_EQ(counter, 1);
}

// ============================================================================
// ENSURE — logs to stderr but does NOT abort
// ============================================================================

TEST(EnsureMacro, TrueExpression_DoesNotLogOrAbort) {
    // ENSURE(true) is silent — no output, no abort.
    ENSURE(true);
    ENSURE(42 > 0);
}

TEST(EnsureMacro, FalseExpression_DoesNotAbort) {
    // ENSURE(false) logs to stderr but must NOT call abort().
    // We capture stderr to verify the log message.
    testing::internal::CaptureStderr();
    ENSURE(false);
    const std::string output = testing::internal::GetCapturedStderr();
    EXPECT_NE(output.find("ENSURE failed"), std::string::npos)
        << "Expected 'ENSURE failed' in stderr, got: " << output;
}

TEST(EnsureMacro, FalseExpression_LogsContainsExpression) {
    testing::internal::CaptureStderr();
    ENSURE(1 == 2);
    const std::string output = testing::internal::GetCapturedStderr();
    EXPECT_NE(output.find("1 == 2"), std::string::npos)
        << "Expected stringified expression '1 == 2' in stderr, got: " << output;
}

TEST(EnsureMacro, FalseExpression_LogsContainsFileInfo) {
    testing::internal::CaptureStderr();
    ENSURE(false);
    const std::string output = testing::internal::GetCapturedStderr();
    EXPECT_NE(output.find("test_assert.cpp"), std::string::npos)
        << "Expected file name in stderr, got: " << output;
}

TEST(EnsureMacro, ExpressionSideEffectsArePreserved) {
    int counter = 0;
    testing::internal::CaptureStderr();
    ENSURE(++counter == 2); // condition is false (counter becomes 1, 1 != 2)
    testing::internal::GetCapturedStderr(); // consume output
    EXPECT_EQ(counter, 1) << "ENSURE must evaluate the expression";
}

// ============================================================================
// Dangling-else safety — macros must be safe in unbraced if/else
// ============================================================================

TEST(AssertDanglingElse, MacroInUnbracedIf_CompilesCorrectly) {
    // This tests that the do-while(0) wrapping prevents dangling-else bugs.
    // If the macro were not wrapped, this would associate the else with
    // the inner if (inside the macro), not the outer if.
    bool condition = true;

    if (condition)
        ASSERT(true);
    else
        ASSERT(false); // should not be reached

    // If we got here, the dangling-else was handled correctly.
    SUCCEED();
}

TEST(AssertMsgDanglingElse, MacroInUnbracedIf_CompilesCorrectly) {
    bool condition = true;

    if (condition)
        ASSERT_MSG(true, "ok");
    else
        ASSERT_MSG(false, "should not fire");

    SUCCEED();
}

TEST(VerifyDanglingElse, MacroInUnbracedIf_CompilesCorrectly) {
    bool condition = true;

    if (condition)
        VERIFY(true);
    else
        VERIFY(false);

    SUCCEED();
}

TEST(EnsureDanglingElse, MacroInUnbracedIf_CompilesCorrectly) {
    bool condition = true;

    if (condition)
        ENSURE(true);
    else
        ENSURE(false);

    SUCCEED();
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(AssertMacro, ComplexExpression_ParenthesizedCorrectly) {
    // Macros must handle complex expressions with commas in templates etc.
    int a = 5, b = 10;
    ASSERT(a < b);
    ASSERT(a + b == 15);
}

TEST(AssertMacro, PointerExpression_NonNull_DoesNotAbort) {
    int x = 42;
    int* p = &x;
    ASSERT(p);
}

// ============================================================================
// ArenaStats — arena_get_stats() (added in this phase)
// ============================================================================

#include "engine/core/arena.h"

TEST(ArenaStats, ReturnsCorrectValuesAfterAllocation) {
    constexpr size_t kCap = 1024;
    alignas(64) uint8_t mem[kCap];
    uz::Arena arena{};
    uz::arena_init(&arena, mem, kCap, "stats_test");

    uz::arena_alloc(&arena, 200, 1);
    uz::arena_alloc(&arena, 100, 1);

    uz::ArenaStats stats = uz::arena_get_stats(&arena);
    EXPECT_EQ(stats.used, 300u);
    EXPECT_EQ(stats.peak, 300u);
    EXPECT_EQ(stats.capacity, kCap);
    EXPECT_STREQ(stats.name, "stats_test");
}

TEST(ArenaStats, PeakPersistsAfterReset) {
    constexpr size_t kCap = 1024;
    alignas(64) uint8_t mem[kCap];
    uz::Arena arena{};
    uz::arena_init(&arena, mem, kCap, "peak_test");

    uz::arena_alloc(&arena, 500, 1);
    uz::arena_reset(&arena);
    uz::arena_alloc(&arena, 100, 1);

    uz::ArenaStats stats = uz::arena_get_stats(&arena);
    EXPECT_EQ(stats.used, 100u);
    EXPECT_EQ(stats.peak, 500u);
    EXPECT_EQ(stats.capacity, kCap);
}

TEST(ArenaStats, EmptyArena_UsedAndPeakAreZero) {
    constexpr size_t kCap = 256;
    alignas(64) uint8_t mem[kCap];
    uz::Arena arena{};
    uz::arena_init(&arena, mem, kCap, "empty");

    uz::ArenaStats stats = uz::arena_get_stats(&arena);
    EXPECT_EQ(stats.used, 0u);
    EXPECT_EQ(stats.peak, 0u);
    EXPECT_EQ(stats.capacity, kCap);
    EXPECT_STREQ(stats.name, "empty");
}

TEST(ArenaStats, NullName_ReturnsNullptr) {
    constexpr size_t kCap = 128;
    alignas(64) uint8_t mem[kCap];
    uz::Arena arena{};
    uz::arena_init(&arena, mem, kCap, nullptr);

    uz::ArenaStats stats = uz::arena_get_stats(&arena);
    EXPECT_EQ(stats.name, nullptr);
}
