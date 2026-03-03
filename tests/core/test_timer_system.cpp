// test_timer_system.cpp -- 55+ tests for uz::TimerSystem (timer_system.h/.cpp)
//
// Covers: schedule_after, schedule_every, schedule_repeat, cancel, pause,
//         resume, state, remaining, fire_count, clear, active_count,
//         total_count, tick, edge cases, callback self-modification.
//
// No main() here -- linked with test_main.cpp.

#include <gtest/gtest.h>
#include "engine/core/timer_system.h"

#include <cstdint>
#include <vector>

using namespace uz;

// ============================================================================
// schedule_after
// ============================================================================

TEST(ScheduleAfter, BeforeDelay_NotFired) {
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_after(1.0f, [&] { ++fired; });

    ts.tick(0.5f); // Half the delay

    EXPECT_EQ(fired, 0);
}

TEST(ScheduleAfter, ExactDelay_Fires) {
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_after(1.0f, [&] { ++fired; });

    ts.tick(1.0f);

    EXPECT_EQ(fired, 1);
}

TEST(ScheduleAfter, AfterDelay_FiresOnceOnly) {
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_after(1.0f, [&] { ++fired; });

    ts.tick(1.0f); // Fires
    ts.tick(1.0f); // Should not fire again (timer removed)

    EXPECT_EQ(fired, 1);
}

TEST(ScheduleAfter, LargeDelta_StillFiresOnce) {
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_after(0.5f, [&] { ++fired; });

    ts.tick(10.0f); // Way past the delay

    EXPECT_EQ(fired, 1);
}

TEST(ScheduleAfter, StateFinishedAfterFire) {
    TimerSystem ts;
    auto h = ts.schedule_after(1.0f, [] {});

    ts.tick(1.0f);

    // Timer is removed after tick, so state returns Cancelled for missing handles
    EXPECT_EQ(ts.state(h), TimerState::Cancelled);
}

TEST(ScheduleAfter, ReturnsValidHandle) {
    TimerSystem ts;
    auto h = ts.schedule_after(1.0f, [] {});

    EXPECT_NE(h, kTimerHandleInvalid);
}

TEST(ScheduleAfter, ZeroDelay_FiresNextTick) {
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_after(0.0f, [&] { ++fired; });

    // Any positive tick should fire a timer with remaining=0
    ts.tick(0.001f);

    EXPECT_EQ(fired, 1);
}

TEST(ScheduleAfter, MultiplePartialTicks_FiresWhenAccumulated) {
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_after(1.0f, [&] { ++fired; });

    ts.tick(0.3f);
    EXPECT_EQ(fired, 0);
    ts.tick(0.3f);
    EXPECT_EQ(fired, 0);
    ts.tick(0.3f);
    EXPECT_EQ(fired, 0);
    ts.tick(0.3f); // Total 1.2s >= 1.0s
    EXPECT_EQ(fired, 1);
}

// ============================================================================
// schedule_every
// ============================================================================

TEST(ScheduleEvery, FiresRepeatedly) {
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_every(0.5f, [&] { ++fired; });

    ts.tick(0.5f);
    EXPECT_EQ(fired, 1);
    ts.tick(0.5f);
    EXPECT_EQ(fired, 2);
    ts.tick(0.5f);
    EXPECT_EQ(fired, 3);
}

TEST(ScheduleEvery, CountsMultipleFires) {
    TimerSystem ts;
    auto h = ts.schedule_every(1.0f, [] {});

    ts.tick(1.0f);
    EXPECT_EQ(ts.fire_count(h), 1u);
    ts.tick(1.0f);
    EXPECT_EQ(ts.fire_count(h), 2u);
}

TEST(ScheduleEvery, MultipleTicksAccumulate) {
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_every(1.0f, [&] { ++fired; });

    ts.tick(0.4f);
    ts.tick(0.4f);
    ts.tick(0.4f); // Total 1.2s -- should fire once
    EXPECT_EQ(fired, 1);
}

TEST(ScheduleEvery, StateRemainsActive) {
    TimerSystem ts;
    auto h = ts.schedule_every(1.0f, [] {});

    ts.tick(1.0f);
    ts.tick(1.0f);

    EXPECT_EQ(ts.state(h), TimerState::Active);
}

TEST(ScheduleEvery, FireTwiceInOneTick_LargeDelta) {
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_every(1.0f, [&] { ++fired; });

    ts.tick(2.5f); // Should fire twice (at 1.0 and 2.0), remaining=0.5

    EXPECT_EQ(fired, 2);
}

TEST(ScheduleEvery, FireManyInOneTick) {
    // A timer with interval 0.125 ticked with delta 1.0 should fire at least
    // floor(1.0/0.125)=8 times. Due to <= 0 threshold semantics it may fire
    // one extra time when remaining lands on exactly 0.0, so accept 8 or 9.
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_every(0.125f, [&] { ++fired; }); // 1/8

    ts.tick(1.0f);

    EXPECT_GE(fired, 8);
    EXPECT_LE(fired, 9);
}

// ============================================================================
// schedule_repeat
// ============================================================================

TEST(ScheduleRepeat, FiresExactCount) {
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_repeat(0.5f, 3, [&] { ++fired; });

    ts.tick(0.5f);
    EXPECT_EQ(fired, 1);
    ts.tick(0.5f);
    EXPECT_EQ(fired, 2);
    ts.tick(0.5f);
    EXPECT_EQ(fired, 3);
    ts.tick(0.5f);
    EXPECT_EQ(fired, 3); // Should NOT fire a 4th time
}

TEST(ScheduleRepeat, StateFinishedAfterCount) {
    TimerSystem ts;
    auto h = ts.schedule_repeat(0.5f, 2, [] {});

    ts.tick(0.5f);
    EXPECT_EQ(ts.state(h), TimerState::Active); // Fired 1 of 2

    ts.tick(0.5f);
    // After tick processes, timer is finished and removed
    EXPECT_EQ(ts.state(h), TimerState::Cancelled); // Removed = Cancelled for lookup
}

TEST(ScheduleRepeat, Count1_SameAsScheduleAfter) {
    TimerSystem ts;
    int fired_repeat = 0;
    int fired_after = 0;

    (void)ts.schedule_repeat(1.0f, 1, [&] { ++fired_repeat; });
    (void)ts.schedule_after(1.0f, [&] { ++fired_after; });

    ts.tick(1.0f);
    EXPECT_EQ(fired_repeat, 1);
    EXPECT_EQ(fired_after, 1);

    ts.tick(1.0f);
    EXPECT_EQ(fired_repeat, 1); // No more fires
    EXPECT_EQ(fired_after, 1);
}

TEST(ScheduleRepeat, LargeDelta_FiresUpToMaxCount) {
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_repeat(0.5f, 3, [&] { ++fired; });

    ts.tick(100.0f); // Way past 3 fires

    EXPECT_EQ(fired, 3);
}

// ============================================================================
// cancel
// ============================================================================

TEST(Cancel, BeforeFire_CallbackNotCalled) {
    TimerSystem ts;
    int fired = 0;
    auto h = ts.schedule_after(1.0f, [&] { ++fired; });

    ts.cancel(h);
    ts.tick(2.0f);

    EXPECT_EQ(fired, 0);
}

TEST(Cancel, InvalidHandle_NoOp) {
    TimerSystem ts;
    // Should not crash or throw
    ts.cancel(kTimerHandleInvalid);
    ts.cancel(9999);
}

TEST(Cancel, AlreadyFinished_NoOp) {
    TimerSystem ts;
    int fired = 0;
    auto h = ts.schedule_after(0.5f, [&] { ++fired; });

    ts.tick(1.0f); // Fire and remove
    ts.cancel(h);  // Timer is gone -- no-op

    EXPECT_EQ(fired, 1);
}

TEST(Cancel, StateIsCancelled) {
    TimerSystem ts;
    auto h = ts.schedule_after(1.0f, [] {});

    ts.cancel(h);

    EXPECT_EQ(ts.state(h), TimerState::Cancelled);
}

TEST(Cancel, CancelledTimerRemovedAfterTick) {
    TimerSystem ts;
    auto h = ts.schedule_after(1.0f, [] {});

    ts.cancel(h);
    ts.tick(0.0f); // Tick to trigger cleanup

    EXPECT_EQ(ts.total_count(), 0u);
}

// ============================================================================
// pause / resume
// ============================================================================

TEST(PauseResume, Pause_TimerNotAdvancing) {
    TimerSystem ts;
    int fired = 0;
    auto h = ts.schedule_after(1.0f, [&] { ++fired; });

    ts.pause(h);
    ts.tick(5.0f); // Even 5 seconds -- timer is paused

    EXPECT_EQ(fired, 0);
    EXPECT_EQ(ts.state(h), TimerState::Paused);
}

TEST(PauseResume, Pause_ThenResume_ContinuesFromWhere) {
    TimerSystem ts;
    int fired = 0;
    auto h = ts.schedule_after(1.0f, [&] { ++fired; });

    ts.tick(0.6f); // remaining = 0.4
    ts.pause(h);
    ts.tick(5.0f); // Paused -- remaining stays 0.4
    ts.resume(h);
    ts.tick(0.3f); // remaining = 0.1 -- not yet
    EXPECT_EQ(fired, 0);
    ts.tick(0.2f); // remaining = -0.1 -- fires

    EXPECT_EQ(fired, 1);
}

TEST(PauseResume, Pause_InvalidHandle_NoOp) {
    TimerSystem ts;
    ts.pause(kTimerHandleInvalid);
    ts.pause(42);
    // No crash
}

TEST(PauseResume, Resume_ActiveTimer_NoOp) {
    TimerSystem ts;
    auto h = ts.schedule_after(1.0f, [] {});

    // Already active -- resume should be a no-op
    ts.resume(h);

    EXPECT_EQ(ts.state(h), TimerState::Active);
}

TEST(PauseResume, Resume_InvalidHandle_NoOp) {
    TimerSystem ts;
    ts.resume(kTimerHandleInvalid);
    ts.resume(42);
    // No crash
}

TEST(PauseResume, PausedTimerCountsAsActive) {
    TimerSystem ts;
    (void)ts.schedule_after(1.0f, [] {});
    auto h2 = ts.schedule_after(2.0f, [] {});

    ts.pause(h2);

    // Both timers count toward active_count (Active + Paused)
    EXPECT_EQ(ts.active_count(), 2u);
}

// ============================================================================
// state query
// ============================================================================

TEST(StateQuery, ActiveTimer_Active) {
    TimerSystem ts;
    auto h = ts.schedule_after(1.0f, [] {});

    EXPECT_EQ(ts.state(h), TimerState::Active);
}

TEST(StateQuery, InvalidHandle_Cancelled) {
    TimerSystem ts;

    EXPECT_EQ(ts.state(kTimerHandleInvalid), TimerState::Cancelled);
    EXPECT_EQ(ts.state(9999), TimerState::Cancelled);
}

// ============================================================================
// remaining query
// ============================================================================

TEST(RemainingQuery, ValidTimer_CorrectValue) {
    TimerSystem ts;
    auto h = ts.schedule_after(2.0f, [] {});

    ts.tick(0.5f);

    EXPECT_FLOAT_EQ(ts.remaining(h), 1.5f);
}

TEST(RemainingQuery, InvalidHandle_Zero) {
    TimerSystem ts;

    EXPECT_FLOAT_EQ(ts.remaining(kTimerHandleInvalid), 0.0f);
    EXPECT_FLOAT_EQ(ts.remaining(9999), 0.0f);
}

TEST(RemainingQuery, PausedTimer_RetainsRemaining) {
    TimerSystem ts;
    auto h = ts.schedule_after(2.0f, [] {});

    ts.tick(0.5f);
    ts.pause(h);
    ts.tick(5.0f); // Paused -- remaining unchanged

    EXPECT_FLOAT_EQ(ts.remaining(h), 1.5f);
}

// ============================================================================
// fire_count query
// ============================================================================

TEST(FireCountQuery, AfterFire_Correct) {
    TimerSystem ts;
    auto h = ts.schedule_every(1.0f, [] {});

    ts.tick(3.5f); // Fires at 1.0, 2.0, 3.0

    EXPECT_EQ(ts.fire_count(h), 3u);
}

TEST(FireCountQuery, InvalidHandle_Zero) {
    TimerSystem ts;

    EXPECT_EQ(ts.fire_count(kTimerHandleInvalid), 0u);
    EXPECT_EQ(ts.fire_count(9999), 0u);
}

// ============================================================================
// active_count / total_count
// ============================================================================

TEST(ActiveCount, Initial_Zero) {
    TimerSystem ts;

    EXPECT_EQ(ts.active_count(), 0u);
}

TEST(ActiveCount, AfterAdd_Increments) {
    TimerSystem ts;
    (void)ts.schedule_after(1.0f, [] {});

    EXPECT_EQ(ts.active_count(), 1u);

    (void)ts.schedule_every(1.0f, [] {});

    EXPECT_EQ(ts.active_count(), 2u);
}

TEST(ActiveCount, AfterFinish_Decrements) {
    TimerSystem ts;
    (void)ts.schedule_after(0.5f, [] {});
    (void)ts.schedule_every(1.0f, [] {});

    EXPECT_EQ(ts.active_count(), 2u);

    ts.tick(0.5f); // First timer fires and is removed

    EXPECT_EQ(ts.active_count(), 1u);
}

TEST(TotalCount, IncludesAll) {
    TimerSystem ts;
    (void)ts.schedule_after(1.0f, [] {});
    auto h2 = ts.schedule_after(2.0f, [] {});
    (void)ts.schedule_every(1.0f, [] {});

    ts.pause(h2);

    EXPECT_EQ(ts.total_count(), 3u);
}

TEST(TotalCount, DecreasesAfterRemoval) {
    TimerSystem ts;
    (void)ts.schedule_after(0.5f, [] {});
    (void)ts.schedule_after(1.5f, [] {});

    ts.tick(1.0f); // First timer fires and is removed

    EXPECT_EQ(ts.total_count(), 1u);
}

// ============================================================================
// clear
// ============================================================================

TEST(Clear, RemovesAll) {
    TimerSystem ts;
    (void)ts.schedule_after(1.0f, [] {});
    (void)ts.schedule_every(1.0f, [] {});
    (void)ts.schedule_repeat(1.0f, 5, [] {});

    ts.clear();

    EXPECT_EQ(ts.total_count(), 0u);
}

TEST(Clear, ActiveCountZero) {
    TimerSystem ts;
    (void)ts.schedule_after(1.0f, [] {});
    (void)ts.schedule_every(1.0f, [] {});

    ts.clear();

    EXPECT_EQ(ts.active_count(), 0u);
}

TEST(Clear, NewTimersAfterClearWork) {
    TimerSystem ts;
    (void)ts.schedule_after(1.0f, [] {});
    ts.clear();

    int fired = 0;
    (void)ts.schedule_after(0.5f, [&] { ++fired; });
    ts.tick(0.5f);

    EXPECT_EQ(fired, 1);
}

// ============================================================================
// Multiple timers
// ============================================================================

TEST(MultipleTimers, IndependentCountdowns) {
    TimerSystem ts;
    int a = 0, b = 0;

    (void)ts.schedule_after(1.0f, [&] { ++a; });
    (void)ts.schedule_after(2.0f, [&] { ++b; });

    ts.tick(1.0f);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 0);

    ts.tick(1.0f);
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

TEST(MultipleTimers, OneFinishes_OthersStillActive) {
    TimerSystem ts;
    int one_shot = 0, repeating = 0;

    (void)ts.schedule_after(0.5f, [&] { ++one_shot; });
    auto h_rep = ts.schedule_every(1.0f, [&] { ++repeating; });

    ts.tick(0.5f);
    EXPECT_EQ(one_shot, 1);
    EXPECT_EQ(repeating, 0);
    EXPECT_EQ(ts.state(h_rep), TimerState::Active);

    ts.tick(0.5f);
    EXPECT_EQ(repeating, 1);
    EXPECT_EQ(ts.state(h_rep), TimerState::Active);
}

TEST(MultipleTimers, DifferentIntervals) {
    TimerSystem ts;
    int fast = 0, slow = 0;

    (void)ts.schedule_every(0.25f, [&] { ++fast; });
    (void)ts.schedule_every(1.0f, [&] { ++slow; });

    ts.tick(1.0f);

    EXPECT_EQ(fast, 4);
    EXPECT_EQ(slow, 1);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(EdgeCases, Tick_ZeroDelta_NothingFires) {
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_after(1.0f, [&] { ++fired; });

    ts.tick(0.0f);

    EXPECT_EQ(fired, 0);
}

TEST(EdgeCases, Tick_NegativeDelta_TreatedAsZero) {
    TimerSystem ts;
    int fired = 0;
    auto h = ts.schedule_after(1.0f, [&] { ++fired; });

    ts.tick(-5.0f); // Negative -- clamped to 0

    EXPECT_EQ(fired, 0);
    EXPECT_FLOAT_EQ(ts.remaining(h), 1.0f);
}

TEST(EdgeCases, Callback_CanCancelOwnTimer) {
    TimerSystem ts;
    int fired = 0;
    TimerHandle h = kTimerHandleInvalid;

    h = ts.schedule_every(1.0f, [&] {
        ++fired;
        ts.cancel(h); // Cancel self inside callback
    });

    ts.tick(1.0f); // Should fire once then be cancelled
    ts.tick(1.0f); // Should not fire again

    EXPECT_EQ(fired, 1);
}

TEST(EdgeCases, Callback_CanScheduleNewTimer) {
    TimerSystem ts;
    int parent = 0, child = 0;

    (void)ts.schedule_after(0.5f, [&] {
        ++parent;
        // Schedule inside callback -- the new timer is appended beyond the
        // current iteration bound, so it won't be processed this tick.
        (void)ts.schedule_after(0.5f, [&] { ++child; });
    });

    ts.tick(0.5f); // Parent fires, schedules child
    EXPECT_EQ(parent, 1);
    EXPECT_EQ(child, 0);

    ts.tick(0.5f); // Child fires
    EXPECT_EQ(child, 1);
}

TEST(EdgeCases, VerySmallInterval_ManyFires) {
    // 0.0625 = 1/16. Due to <= 0 threshold semantics the timer may fire 16 or 17
    // times when remaining lands exactly on 0.0 after the 16th accumulation.
    TimerSystem ts;
    int fired = 0;
    (void)ts.schedule_every(0.0625f, [&] { ++fired; });

    ts.tick(1.0f);

    EXPECT_GE(fired, 16);
    EXPECT_LE(fired, 17);
}

TEST(EdgeCases, HandleMonotonicallyIncreases) {
    TimerSystem ts;
    auto h1 = ts.schedule_after(1.0f, [] {});
    auto h2 = ts.schedule_after(1.0f, [] {});
    auto h3 = ts.schedule_after(1.0f, [] {});

    EXPECT_LT(h1, h2);
    EXPECT_LT(h2, h3);
}

TEST(EdgeCases, CancelPausedTimer) {
    TimerSystem ts;
    int fired = 0;
    auto h = ts.schedule_after(1.0f, [&] { ++fired; });

    ts.pause(h);
    ts.cancel(h);
    ts.tick(2.0f);

    EXPECT_EQ(fired, 0);
    // Timer is removed after tick, state returns Cancelled for missing handles
    EXPECT_EQ(ts.state(h), TimerState::Cancelled);
}

TEST(EdgeCases, ResumeFinishedTimer_NoOp) {
    TimerSystem ts;
    auto h = ts.schedule_after(0.5f, [] {});

    ts.tick(1.0f); // Fire and remove
    ts.resume(h);  // Timer gone -- no-op

    // Should not crash. State is Cancelled (not found).
    EXPECT_EQ(ts.state(h), TimerState::Cancelled);
}

TEST(EdgeCases, PauseFinishedTimer_NoOp) {
    TimerSystem ts;
    auto h = ts.schedule_after(0.5f, [] {});

    ts.tick(1.0f); // Fire and remove
    ts.pause(h);   // Timer gone -- no-op

    EXPECT_EQ(ts.state(h), TimerState::Cancelled);
}

TEST(EdgeCases, ScheduleAfterFire_RemainingResets) {
    // A repeating timer should have remaining reset based on accumulation
    TimerSystem ts;
    auto h = ts.schedule_every(1.0f, [] {});

    ts.tick(1.5f); // Fires at 1.0, remaining should be 0.5

    EXPECT_NEAR(ts.remaining(h), 0.5f, 1e-5f);
}

TEST(EdgeCases, EmptyTick_NoEffect) {
    // Ticking with no timers should be harmless
    TimerSystem ts;
    ts.tick(1.0f);
    ts.tick(0.0f);
    ts.tick(-1.0f);

    EXPECT_EQ(ts.active_count(), 0u);
    EXPECT_EQ(ts.total_count(), 0u);
}

TEST(EdgeCases, Callback_CanPauseOwnTimer) {
    TimerSystem ts;
    int fired = 0;
    TimerHandle h = kTimerHandleInvalid;

    h = ts.schedule_every(1.0f, [&] {
        ++fired;
        ts.pause(h); // Pause self inside callback
    });

    ts.tick(3.0f); // Would fire 3 times, but pauses after first

    EXPECT_EQ(fired, 1);
    EXPECT_EQ(ts.state(h), TimerState::Paused);
}

TEST(EdgeCases, MaxTimersCanAdd) {
    TimerSystem ts;

    // Add up to kMaxTimers -- should not assert
    for (uint32_t i = 0; i < TimerSystem::kMaxTimers; ++i) {
        auto h = ts.schedule_after(100.0f, [] {});
        EXPECT_NE(h, kTimerHandleInvalid);
    }

    EXPECT_EQ(ts.active_count(), TimerSystem::kMaxTimers);
}

TEST(EdgeCases, RemainingOfCancelledTimer_Zero) {
    TimerSystem ts;
    auto h = ts.schedule_after(1.0f, [] {});

    ts.cancel(h);

    EXPECT_FLOAT_EQ(ts.remaining(h), 0.0f);
}

TEST(EdgeCases, FireCountOfScheduleRepeat_TracksCorrectly) {
    TimerSystem ts;
    auto h = ts.schedule_repeat(0.1f, 5, [] {});

    ts.tick(0.35f); // Fires at 0.1, 0.2, 0.3 => 3 fires

    EXPECT_EQ(ts.fire_count(h), 3u);
    EXPECT_EQ(ts.state(h), TimerState::Active);
}

TEST(EdgeCases, Callback_CanCancelOtherTimer) {
    TimerSystem ts;
    int fired_a = 0, fired_b = 0;
    TimerHandle h_b = kTimerHandleInvalid;

    (void)ts.schedule_after(0.5f, [&] {
        ++fired_a;
        ts.cancel(h_b); // Cancel the other timer
    });

    h_b = ts.schedule_after(1.0f, [&] { ++fired_b; });

    ts.tick(0.5f); // Timer A fires, cancels B
    ts.tick(1.0f); // Timer B should not fire

    EXPECT_EQ(fired_a, 1);
    EXPECT_EQ(fired_b, 0);
}
