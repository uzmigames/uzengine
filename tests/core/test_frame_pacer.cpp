// test_frame_pacer.cpp -- Tests for uz::FramePacer (frame_pacer.h/.cpp)
//
// Uses an injectable time source to make all tests deterministic.
// No main() here -- linked with test_main.cpp.

#include <gtest/gtest.h>
#include "engine/core/frame_pacer.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace uz;

// ============================================================================
// Mock time source -- deterministic, controllable time.
// ============================================================================

namespace {

// Global mock time that the mock time source reads from.
// Tests set this before calling FramePacer methods.
double g_mock_time = 0.0;

double mock_time_source() {
    return g_mock_time;
}

// Utility: advance mock time and call a function, useful for simulating
// spin-wait progression.
struct AutoAdvancingTimeSource {
    double start;
    double increment;
    mutable uint32_t call_count = 0;

    static AutoAdvancingTimeSource* s_instance;

    static double time_fn() {
        auto* self = s_instance;
        double t = self->start + self->increment * static_cast<double>(self->call_count);
        ++self->call_count;
        return t;
    }
};

AutoAdvancingTimeSource* AutoAdvancingTimeSource::s_instance = nullptr;

} // anonymous namespace

// ============================================================================
// Construction
// ============================================================================

TEST(FramePacerConstruct, DefaultConstruction_HasDefaultValues) {
    // Default constructor uses platform time source -- just verify it doesn't crash.
    FramePacer fp;
    EXPECT_EQ(fp.vsync_mode(), VSyncMode::On);
    EXPECT_FLOAT_EQ(fp.target_fps(), 0.0f);
    EXPECT_EQ(fp.frame_count(), 0u);
    EXPECT_FLOAT_EQ(fp.last_frame_ms(), 0.0f);
}

TEST(FramePacerConstruct, InjectableTimeSource_Works) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);
    EXPECT_EQ(fp.vsync_mode(), VSyncMode::On);
}

// ============================================================================
// VSyncMode
// ============================================================================

TEST(FramePacerVSync, SetOff) {
    FramePacer fp(mock_time_source);
    fp.set_vsync_mode(VSyncMode::Off);
    EXPECT_EQ(fp.vsync_mode(), VSyncMode::Off);
}

TEST(FramePacerVSync, SetOn) {
    FramePacer fp(mock_time_source);
    fp.set_vsync_mode(VSyncMode::On);
    EXPECT_EQ(fp.vsync_mode(), VSyncMode::On);
}

TEST(FramePacerVSync, SetAdaptive) {
    FramePacer fp(mock_time_source);
    fp.set_vsync_mode(VSyncMode::Adaptive);
    EXPECT_EQ(fp.vsync_mode(), VSyncMode::Adaptive);
}

TEST(FramePacerVSync, SetMailBox) {
    FramePacer fp(mock_time_source);
    fp.set_vsync_mode(VSyncMode::MailBox);
    EXPECT_EQ(fp.vsync_mode(), VSyncMode::MailBox);
}

TEST(FramePacerVSync, DefaultIsOn) {
    FramePacer fp(mock_time_source);
    EXPECT_EQ(fp.vsync_mode(), VSyncMode::On);
}

// ============================================================================
// Target FPS
// ============================================================================

TEST(FramePacerTargetFps, SetZero_Unlimited) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(0.0f);
    EXPECT_FLOAT_EQ(fp.target_fps(), 0.0f);
}

TEST(FramePacerTargetFps, SetNegative_TreatedAsUnlimited) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(-60.0f);
    EXPECT_FLOAT_EQ(fp.target_fps(), 0.0f);
}

TEST(FramePacerTargetFps, SetValid_60fps) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(60.0f);
    EXPECT_FLOAT_EQ(fp.target_fps(), 60.0f);
}

TEST(FramePacerTargetFps, SetValid_144fps) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(144.0f);
    EXPECT_FLOAT_EQ(fp.target_fps(), 144.0f);
}

TEST(FramePacerTargetFps, ClampToMin) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(0.1f);
    EXPECT_FLOAT_EQ(fp.target_fps(), FramePacer::kMinTargetFps);
}

TEST(FramePacerTargetFps, ClampToMax) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(5000.0f);
    EXPECT_FLOAT_EQ(fp.target_fps(), FramePacer::kMaxTargetFps);
}

TEST(FramePacerTargetFps, ExactMin) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(FramePacer::kMinTargetFps);
    EXPECT_FLOAT_EQ(fp.target_fps(), FramePacer::kMinTargetFps);
}

TEST(FramePacerTargetFps, ExactMax) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(FramePacer::kMaxTargetFps);
    EXPECT_FLOAT_EQ(fp.target_fps(), FramePacer::kMaxTargetFps);
}

TEST(FramePacerTargetFps, SetThenClear) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(60.0f);
    EXPECT_FLOAT_EQ(fp.target_fps(), 60.0f);
    fp.set_target_fps(0.0f);
    EXPECT_FLOAT_EQ(fp.target_fps(), 0.0f);
}

// ============================================================================
// Limiter active logic
// ============================================================================

TEST(FramePacerLimiter, NoTargetFps_InactiveRegardlessOfMode) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(0.0f);

    fp.set_vsync_mode(VSyncMode::Off);
    EXPECT_FALSE(fp.is_limiter_active());

    fp.set_vsync_mode(VSyncMode::On);
    EXPECT_FALSE(fp.is_limiter_active());

    fp.set_vsync_mode(VSyncMode::Adaptive);
    EXPECT_FALSE(fp.is_limiter_active());

    fp.set_vsync_mode(VSyncMode::MailBox);
    EXPECT_FALSE(fp.is_limiter_active());
}

TEST(FramePacerLimiter, WithTargetFps_ActiveInOffMode) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(60.0f);
    fp.set_vsync_mode(VSyncMode::Off);
    EXPECT_TRUE(fp.is_limiter_active());
}

TEST(FramePacerLimiter, WithTargetFps_InactiveInOnMode) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(60.0f);
    fp.set_vsync_mode(VSyncMode::On);
    EXPECT_FALSE(fp.is_limiter_active());
}

TEST(FramePacerLimiter, WithTargetFps_ActiveInAdaptiveMode) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(60.0f);
    fp.set_vsync_mode(VSyncMode::Adaptive);
    EXPECT_TRUE(fp.is_limiter_active());
}

TEST(FramePacerLimiter, WithTargetFps_InactiveInMailBoxMode) {
    FramePacer fp(mock_time_source);
    fp.set_target_fps(60.0f);
    fp.set_vsync_mode(VSyncMode::MailBox);
    EXPECT_FALSE(fp.is_limiter_active());
}

// ============================================================================
// Frame lifecycle -- basic timing
// ============================================================================

TEST(FramePacerFrame, BeginEnd_RecordsFrameTime) {
    g_mock_time = 1.0;
    FramePacer fp(mock_time_source);

    fp.begin_frame();
    g_mock_time = 1.016; // ~16ms frame
    fp.end_frame();

    EXPECT_EQ(fp.frame_count(), 1u);
    EXPECT_NEAR(fp.last_frame_ms(), 16.0f, 0.01f);
}

TEST(FramePacerFrame, MultipleFrames_RecordAll) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    for (int i = 0; i < 10; ++i) {
        g_mock_time = static_cast<double>(i) * 0.016;
        fp.begin_frame();
        g_mock_time += 0.016;
        fp.end_frame();
    }

    EXPECT_EQ(fp.frame_count(), 10u);
}

TEST(FramePacerFrame, EndWithoutBegin_NoRecord) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    fp.end_frame(); // No begin_frame
    EXPECT_EQ(fp.frame_count(), 0u);
}

TEST(FramePacerFrame, DoubleBegin_OverwritesStart) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    fp.begin_frame();
    g_mock_time = 0.010;
    fp.begin_frame(); // Overwrites the start time
    g_mock_time = 0.020;
    fp.end_frame();

    // Frame time should be from the second begin_frame (10ms, not 20ms)
    EXPECT_NEAR(fp.last_frame_ms(), 10.0f, 0.01f);
}

// ============================================================================
// Frame limiter behavior with mock time
// ============================================================================

TEST(FramePacerLimiterBehavior, LimiterOff_NoDelay) {
    // With no target FPS, end_frame should return immediately.
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);
    fp.set_vsync_mode(VSyncMode::Off);
    fp.set_target_fps(0.0f);

    fp.begin_frame();
    g_mock_time = 0.005; // 5ms frame
    fp.end_frame();

    // Frame time should be approximately 5ms (no added wait).
    EXPECT_NEAR(fp.last_frame_ms(), 5.0f, 0.01f);
}

TEST(FramePacerLimiterBehavior, LimiterActive_FrameAlreadyOverTarget_NoDelay) {
    // If the frame already took longer than the target, no additional wait.
    AutoAdvancingTimeSource ts;
    ts.start = 0.0;
    ts.increment = 0.001; // 1ms per call
    ts.call_count = 0;
    AutoAdvancingTimeSource::s_instance = &ts;

    FramePacer fp(AutoAdvancingTimeSource::time_fn);
    fp.set_vsync_mode(VSyncMode::Off);
    fp.set_target_fps(60.0f); // Target: 16.67ms

    // begin_frame reads time (call 0 -> 0.0s)
    fp.begin_frame();

    // Simulate frame work by advancing time
    ts.start = 0.020; // 20ms already elapsed at next read
    ts.call_count = 0;

    // end_frame reads time, sees we're over target, records frame time
    fp.end_frame();

    // Frame time should be >= 20ms since we were already over budget
    EXPECT_GE(fp.last_frame_ms(), 20.0f);
}

TEST(FramePacerLimiterBehavior, LimiterActive_WaitsForTarget) {
    // Use auto-advancing time to simulate the spin-wait loop.
    AutoAdvancingTimeSource ts;
    ts.start = 0.0;
    ts.increment = 0.001; // 1ms per call
    ts.call_count = 0;
    AutoAdvancingTimeSource::s_instance = &ts;

    FramePacer fp(AutoAdvancingTimeSource::time_fn);
    fp.set_vsync_mode(VSyncMode::Off);
    fp.set_target_fps(60.0f); // Target: ~16.67ms

    // begin_frame: call_count=0, time=0.0
    fp.begin_frame();

    // After begin, call_count=1. The next calls in end_frame will advance time.
    // The spin-wait loop will iterate until time >= target (0 + 1/60 = 0.01667s).
    // With 1ms increments, it will take about 17 calls to reach the target.
    fp.end_frame();

    // The frame time should be approximately the target (16.67ms, or close).
    // Due to mock time granularity (1ms steps), it might be 17ms.
    EXPECT_GE(fp.last_frame_ms(), 16.0f);
}

TEST(FramePacerLimiterBehavior, VsyncOn_NoLimiterEvenWithTargetFps) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);
    fp.set_vsync_mode(VSyncMode::On);
    fp.set_target_fps(30.0f); // Set target but VSync On should ignore it

    fp.begin_frame();
    g_mock_time = 0.005; // 5ms frame -- under 30fps target (33ms)
    fp.end_frame();

    // Should NOT wait -- vsync On doesn't use the software limiter
    EXPECT_NEAR(fp.last_frame_ms(), 5.0f, 0.01f);
}

TEST(FramePacerLimiterBehavior, MailBox_NoLimiterEvenWithTargetFps) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);
    fp.set_vsync_mode(VSyncMode::MailBox);
    fp.set_target_fps(30.0f);

    fp.begin_frame();
    g_mock_time = 0.005;
    fp.end_frame();

    EXPECT_NEAR(fp.last_frame_ms(), 5.0f, 0.01f);
}

TEST(FramePacerLimiterBehavior, Adaptive_LimiterApplies) {
    AutoAdvancingTimeSource ts;
    ts.start = 0.0;
    ts.increment = 0.001;
    ts.call_count = 0;
    AutoAdvancingTimeSource::s_instance = &ts;

    FramePacer fp(AutoAdvancingTimeSource::time_fn);
    fp.set_vsync_mode(VSyncMode::Adaptive);
    fp.set_target_fps(60.0f);

    fp.begin_frame();
    fp.end_frame();

    // Should have waited to hit ~16.67ms
    EXPECT_GE(fp.last_frame_ms(), 16.0f);
}

// ============================================================================
// Statistics -- empty
// ============================================================================

TEST(FramePacerStats, Empty_AllZeros) {
    FramePacer fp(mock_time_source);
    const auto s = fp.stats();

    EXPECT_FLOAT_EQ(s.avg_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.min_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.max_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.p50_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.p90_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.p95_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.p99_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.fps, 0.0f);
    EXPECT_EQ(s.count, 0u);
}

// ============================================================================
// Statistics -- single frame
// ============================================================================

TEST(FramePacerStats, SingleFrame_AllFieldsMatch) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    fp.begin_frame();
    g_mock_time = 0.016;
    fp.end_frame();

    const auto s = fp.stats();
    EXPECT_EQ(s.count, 1u);
    EXPECT_NEAR(s.avg_ms, 16.0f, 0.01f);
    EXPECT_NEAR(s.min_ms, 16.0f, 0.01f);
    EXPECT_NEAR(s.max_ms, 16.0f, 0.01f);
    EXPECT_NEAR(s.p50_ms, 16.0f, 0.01f);
    EXPECT_NEAR(s.p90_ms, 16.0f, 0.01f);
    EXPECT_NEAR(s.p95_ms, 16.0f, 0.01f);
    EXPECT_NEAR(s.p99_ms, 16.0f, 0.01f);
    EXPECT_NEAR(s.fps, 62.5f, 0.1f); // 1000 / 16 = 62.5
}

// ============================================================================
// Statistics -- multiple frames with known values
// ============================================================================

TEST(FramePacerStats, MultipleFrames_CorrectAvgMinMax) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    // Record 5 frames with known times: 10, 20, 30, 40, 50 ms
    const float frame_times[] = { 0.010f, 0.020f, 0.030f, 0.040f, 0.050f };
    double t = 0.0;
    for (float ft : frame_times) {
        g_mock_time = t;
        fp.begin_frame();
        t += static_cast<double>(ft);
        g_mock_time = t;
        fp.end_frame();
    }

    const auto s = fp.stats();
    EXPECT_EQ(s.count, 5u);
    EXPECT_NEAR(s.avg_ms, 30.0f, 0.1f);  // (10+20+30+40+50)/5
    EXPECT_NEAR(s.min_ms, 10.0f, 0.1f);
    EXPECT_NEAR(s.max_ms, 50.0f, 0.1f);
    EXPECT_NEAR(s.p50_ms, 30.0f, 0.1f);  // Median of [10,20,30,40,50]
}

// ============================================================================
// Statistics -- percentiles
// ============================================================================

TEST(FramePacerStats, Percentiles_100Frames) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    // Record 100 frames with times 1ms, 2ms, ..., 100ms
    double t = 0.0;
    for (int i = 1; i <= 100; ++i) {
        g_mock_time = t;
        fp.begin_frame();
        t += static_cast<double>(i) * 0.001; // i ms
        g_mock_time = t;
        fp.end_frame();
    }

    const auto s = fp.stats();
    EXPECT_EQ(s.count, 100u);
    EXPECT_NEAR(s.avg_ms, 50.5f, 0.5f);
    EXPECT_NEAR(s.min_ms, 1.0f, 0.1f);
    EXPECT_NEAR(s.max_ms, 100.0f, 0.1f);
    EXPECT_NEAR(s.p50_ms, 50.5f, 1.0f);
    EXPECT_NEAR(s.p90_ms, 90.1f, 1.5f);
    EXPECT_NEAR(s.p95_ms, 95.06f, 1.5f);
    EXPECT_NEAR(s.p99_ms, 99.02f, 1.5f);
}

// ============================================================================
// Statistics -- FPS calculation
// ============================================================================

TEST(FramePacerStats, Fps_FromAverage) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    // 10 frames of exactly 16.667ms = 60 FPS
    double t = 0.0;
    for (int i = 0; i < 10; ++i) {
        g_mock_time = t;
        fp.begin_frame();
        t += 1.0 / 60.0;
        g_mock_time = t;
        fp.end_frame();
    }

    const auto s = fp.stats();
    EXPECT_NEAR(s.fps, 60.0f, 0.5f);
}

// ============================================================================
// Ring buffer wrapping
// ============================================================================

TEST(FramePacerRingBuffer, WrapAround_OldFramesOverwritten) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    // Fill the ring buffer completely, then add more.
    const uint32_t total = FramePacer::kHistorySize + 10;
    double t = 0.0;
    for (uint32_t i = 0; i < total; ++i) {
        g_mock_time = t;
        fp.begin_frame();
        t += 0.010; // 10ms per frame
        g_mock_time = t;
        fp.end_frame();
    }

    // frame_count tracks total, not capped at ring size.
    EXPECT_EQ(fp.frame_count(), total);

    // Stats should report kHistorySize frames (the last 256).
    const auto s = fp.stats();
    EXPECT_EQ(s.count, FramePacer::kHistorySize);
    EXPECT_NEAR(s.avg_ms, 10.0f, 0.1f);
}

TEST(FramePacerRingBuffer, LastFrameMs_AfterWrap) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    // Fill ring buffer + 1 extra frame with a different time.
    double t = 0.0;
    for (uint32_t i = 0; i < FramePacer::kHistorySize; ++i) {
        g_mock_time = t;
        fp.begin_frame();
        t += 0.010;
        g_mock_time = t;
        fp.end_frame();
    }

    // One more frame with 20ms.
    g_mock_time = t;
    fp.begin_frame();
    t += 0.020;
    g_mock_time = t;
    fp.end_frame();

    EXPECT_NEAR(fp.last_frame_ms(), 20.0f, 0.01f);
}

// ============================================================================
// Reset stats
// ============================================================================

TEST(FramePacerResetStats, ClearsEverything) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    fp.begin_frame();
    g_mock_time = 0.016;
    fp.end_frame();

    EXPECT_EQ(fp.frame_count(), 1u);
    EXPECT_GT(fp.last_frame_ms(), 0.0f);

    fp.reset_stats();

    EXPECT_EQ(fp.frame_count(), 0u);
    EXPECT_FLOAT_EQ(fp.last_frame_ms(), 0.0f);

    const auto s = fp.stats();
    EXPECT_EQ(s.count, 0u);
    EXPECT_FLOAT_EQ(s.avg_ms, 0.0f);
}

TEST(FramePacerResetStats, CanRecordAfterReset) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    fp.begin_frame();
    g_mock_time = 0.010;
    fp.end_frame();

    fp.reset_stats();

    g_mock_time = 0.020;
    fp.begin_frame();
    g_mock_time = 0.030;
    fp.end_frame();

    EXPECT_EQ(fp.frame_count(), 1u);
    EXPECT_NEAR(fp.last_frame_ms(), 10.0f, 0.01f);
}

// ============================================================================
// Constants
// ============================================================================

TEST(FramePacerConstants, HistorySize_IsPowerOf2) {
    EXPECT_EQ(FramePacer::kHistorySize & (FramePacer::kHistorySize - 1), 0u);
}

TEST(FramePacerConstants, HistorySize_Is256) {
    EXPECT_EQ(FramePacer::kHistorySize, 256u);
}

TEST(FramePacerConstants, MaxTargetFps_Is1000) {
    EXPECT_FLOAT_EQ(FramePacer::kMaxTargetFps, 1000.0f);
}

TEST(FramePacerConstants, MinTargetFps_Is1) {
    EXPECT_FLOAT_EQ(FramePacer::kMinTargetFps, 1.0f);
}

TEST(FramePacerConstants, SpinWaitThreshold_Is2ms) {
    EXPECT_DOUBLE_EQ(FramePacer::kSpinWaitThresholdSec, 0.002);
}

// ============================================================================
// VSyncMode enum values
// ============================================================================

TEST(VSyncModeEnum, Values) {
    EXPECT_EQ(static_cast<uint8_t>(VSyncMode::Off), 0);
    EXPECT_EQ(static_cast<uint8_t>(VSyncMode::On), 1);
    EXPECT_EQ(static_cast<uint8_t>(VSyncMode::Adaptive), 2);
    EXPECT_EQ(static_cast<uint8_t>(VSyncMode::MailBox), 3);
}

// ============================================================================
// FrameStats default construction
// ============================================================================

TEST(FrameStatsDefault, AllZeros) {
    FrameStats s{};
    EXPECT_FLOAT_EQ(s.avg_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.min_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.max_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.p50_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.p90_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.p95_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.p99_ms, 0.0f);
    EXPECT_FLOAT_EQ(s.fps, 0.0f);
    EXPECT_EQ(s.count, 0u);
}

// ============================================================================
// Edge cases -- very small and very large frame times
// ============================================================================

TEST(FramePacerEdge, VerySmallFrameTime) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    fp.begin_frame();
    g_mock_time = 0.000001; // 1 microsecond
    fp.end_frame();

    EXPECT_NEAR(fp.last_frame_ms(), 0.001f, 0.001f);
}

TEST(FramePacerEdge, VeryLargeFrameTime) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    fp.begin_frame();
    g_mock_time = 10.0; // 10 seconds
    fp.end_frame();

    EXPECT_NEAR(fp.last_frame_ms(), 10000.0f, 1.0f);
}

TEST(FramePacerEdge, ZeroFrameTime) {
    g_mock_time = 1.0;
    FramePacer fp(mock_time_source);

    fp.begin_frame();
    // Time doesn't advance
    fp.end_frame();

    EXPECT_FLOAT_EQ(fp.last_frame_ms(), 0.0f);
}

// ============================================================================
// Mode switching mid-session
// ============================================================================

TEST(FramePacerModeSwitch, SwitchPreservesHistory) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    fp.begin_frame();
    g_mock_time = 0.016;
    fp.end_frame();

    EXPECT_EQ(fp.frame_count(), 1u);

    fp.set_vsync_mode(VSyncMode::Off);

    // History should still be intact.
    EXPECT_EQ(fp.frame_count(), 1u);
    EXPECT_NEAR(fp.last_frame_ms(), 16.0f, 0.01f);
}

TEST(FramePacerModeSwitch, SwitchTargetFpsPreservesHistory) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    fp.begin_frame();
    g_mock_time = 0.016;
    fp.end_frame();

    fp.set_target_fps(30.0f);

    EXPECT_EQ(fp.frame_count(), 1u);
    EXPECT_NEAR(fp.last_frame_ms(), 16.0f, 0.01f);
}

// ============================================================================
// Statistics with uniform frame times
// ============================================================================

TEST(FramePacerStats, UniformFrames_MinEqualsMax) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    double t = 0.0;
    for (int i = 0; i < 50; ++i) {
        g_mock_time = t;
        fp.begin_frame();
        t += 0.016;
        g_mock_time = t;
        fp.end_frame();
    }

    const auto s = fp.stats();
    EXPECT_EQ(s.count, 50u);
    // All frames are 16ms, so min == max == avg == all percentiles.
    EXPECT_NEAR(s.min_ms, 16.0f, 0.01f);
    EXPECT_NEAR(s.max_ms, 16.0f, 0.01f);
    EXPECT_NEAR(s.avg_ms, 16.0f, 0.01f);
    EXPECT_NEAR(s.p50_ms, 16.0f, 0.01f);
    EXPECT_NEAR(s.p99_ms, 16.0f, 0.01f);
}

// ============================================================================
// Statistics with two distinct frame times (bimodal)
// ============================================================================

TEST(FramePacerStats, BimodalFrames_PercentilesCorrect) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    double t = 0.0;
    // 90 frames of 10ms, 10 frames of 50ms
    for (int i = 0; i < 100; ++i) {
        g_mock_time = t;
        fp.begin_frame();
        const double ft = (i < 90) ? 0.010 : 0.050;
        t += ft;
        g_mock_time = t;
        fp.end_frame();
    }

    const auto s = fp.stats();
    EXPECT_EQ(s.count, 100u);
    EXPECT_NEAR(s.avg_ms, 14.0f, 0.5f); // (90*10 + 10*50)/100 = 14
    EXPECT_NEAR(s.min_ms, 10.0f, 0.1f);
    EXPECT_NEAR(s.max_ms, 50.0f, 0.1f);
    EXPECT_NEAR(s.p50_ms, 10.0f, 1.0f); // 50th percentile is still in the 10ms group
    EXPECT_NEAR(s.p90_ms, 10.0f, 5.0f); // 90th percentile is near the boundary
    // p99 should be close to 50ms
    EXPECT_GT(s.p99_ms, 30.0f);
}

// ============================================================================
// Frame count wrapping (history_count_ can grow beyond kHistorySize)
// ============================================================================

TEST(FramePacerFrameCount, TracksTotal_NotRingSize) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    double t = 0.0;
    for (uint32_t i = 0; i < 1000; ++i) {
        g_mock_time = t;
        fp.begin_frame();
        t += 0.010;
        g_mock_time = t;
        fp.end_frame();
    }

    EXPECT_EQ(fp.frame_count(), 1000u);

    // Stats only uses the last kHistorySize frames.
    const auto s = fp.stats();
    EXPECT_EQ(s.count, FramePacer::kHistorySize);
}

// ============================================================================
// Spin-wait with auto-advancing time source
// ============================================================================

TEST(FramePacerSpinWait, SpinWaitConverges) {
    // Verify the spin-wait loop terminates and converges near the target.
    AutoAdvancingTimeSource ts;
    ts.start = 0.0;
    ts.increment = 0.0001; // 0.1ms per call -- fine granularity
    ts.call_count = 0;
    AutoAdvancingTimeSource::s_instance = &ts;

    FramePacer fp(AutoAdvancingTimeSource::time_fn);
    fp.set_vsync_mode(VSyncMode::Off);
    fp.set_target_fps(100.0f); // Target: 10ms

    fp.begin_frame(); // call_count=0 -> time=0.0
    fp.end_frame();

    // Should be close to 10ms (with mock time, it will be 10ms or slightly over).
    EXPECT_GE(fp.last_frame_ms(), 9.5f);
    EXPECT_LE(fp.last_frame_ms(), 12.0f);
}

// ============================================================================
// Limiter when remaining time < spin threshold (no os_sleep, only spin)
// ============================================================================

TEST(FramePacerLimiterBehavior, RemainingLessThanSpinThreshold_OnlySpins) {
    AutoAdvancingTimeSource ts;
    ts.start = 0.0;
    ts.increment = 0.0001; // 0.1ms per call
    ts.call_count = 0;
    AutoAdvancingTimeSource::s_instance = &ts;

    FramePacer fp(AutoAdvancingTimeSource::time_fn);
    fp.set_vsync_mode(VSyncMode::Off);
    fp.set_target_fps(1000.0f); // Target: 1ms -- less than 2ms spin threshold

    fp.begin_frame();
    fp.end_frame();

    // Should still converge near 1ms.
    EXPECT_GE(fp.last_frame_ms(), 0.9f);
    EXPECT_LE(fp.last_frame_ms(), 3.0f);
}

// ============================================================================
// Statistics after reset then new frames
// ============================================================================

TEST(FramePacerStats, AfterReset_NewFramesOnly) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    // Record some fast frames.
    double t = 0.0;
    for (int i = 0; i < 5; ++i) {
        g_mock_time = t;
        fp.begin_frame();
        t += 0.005;
        g_mock_time = t;
        fp.end_frame();
    }

    fp.reset_stats();

    // Record some slow frames.
    for (int i = 0; i < 5; ++i) {
        g_mock_time = t;
        fp.begin_frame();
        t += 0.033;
        g_mock_time = t;
        fp.end_frame();
    }

    const auto s = fp.stats();
    EXPECT_EQ(s.count, 5u);
    EXPECT_NEAR(s.avg_ms, 33.0f, 0.5f);
}

// ============================================================================
// Two frames -- verify last_frame_ms returns the most recent
// ============================================================================

TEST(FramePacerLastFrame, ReturnsMostRecent) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    fp.begin_frame();
    g_mock_time = 0.010;
    fp.end_frame();

    fp.begin_frame();
    g_mock_time = 0.030;
    fp.end_frame();

    // Last frame: started at 0.010, ended at 0.030 = 20ms
    EXPECT_NEAR(fp.last_frame_ms(), 20.0f, 0.01f);
}

// ============================================================================
// Exactly fill the ring buffer -- no wrapping yet
// ============================================================================

TEST(FramePacerRingBuffer, ExactlyFull_StatsCorrect) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    double t = 0.0;
    for (uint32_t i = 0; i < FramePacer::kHistorySize; ++i) {
        g_mock_time = t;
        fp.begin_frame();
        t += 0.010;
        g_mock_time = t;
        fp.end_frame();
    }

    EXPECT_EQ(fp.frame_count(), FramePacer::kHistorySize);
    const auto s = fp.stats();
    EXPECT_EQ(s.count, FramePacer::kHistorySize);
    EXPECT_NEAR(s.avg_ms, 10.0f, 0.01f);
}

// ============================================================================
// Mixed frame times after ring wrap -- verify old data is overwritten
// ============================================================================

TEST(FramePacerRingBuffer, AfterWrap_OldDataGone) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    // Fill ring with 5ms frames.
    double t = 0.0;
    for (uint32_t i = 0; i < FramePacer::kHistorySize; ++i) {
        g_mock_time = t;
        fp.begin_frame();
        t += 0.005;
        g_mock_time = t;
        fp.end_frame();
    }

    // Overwrite entire ring with 20ms frames.
    for (uint32_t i = 0; i < FramePacer::kHistorySize; ++i) {
        g_mock_time = t;
        fp.begin_frame();
        t += 0.020;
        g_mock_time = t;
        fp.end_frame();
    }

    const auto s = fp.stats();
    EXPECT_EQ(s.count, FramePacer::kHistorySize);
    // All entries should now be 20ms. No trace of the 5ms frames.
    EXPECT_NEAR(s.avg_ms, 20.0f, 0.01f);
    EXPECT_NEAR(s.min_ms, 20.0f, 0.01f);
    EXPECT_NEAR(s.max_ms, 20.0f, 0.01f);
}

// ============================================================================
// Rapid mode changes don't corrupt state
// ============================================================================

TEST(FramePacerStress, RapidModeChanges) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    double t = 0.0;
    for (int i = 0; i < 20; ++i) {
        fp.set_vsync_mode(static_cast<VSyncMode>(i % 4));
        g_mock_time = t;
        fp.begin_frame();
        t += 0.016;
        g_mock_time = t;
        fp.end_frame();
    }

    EXPECT_EQ(fp.frame_count(), 20u);
    const auto s = fp.stats();
    EXPECT_NEAR(s.avg_ms, 16.0f, 0.1f);
}

// ============================================================================
// Rapid target FPS changes
// ============================================================================

TEST(FramePacerStress, RapidTargetFpsChanges) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);
    fp.set_vsync_mode(VSyncMode::On); // Limiter not active, so changes are instant

    for (int i = 0; i < 100; ++i) {
        fp.set_target_fps(static_cast<float>(i));
    }

    EXPECT_FLOAT_EQ(fp.target_fps(), 99.0f);
}

// ============================================================================
// Percentile interpolation with 2 frames
// ============================================================================

TEST(FramePacerStats, TwoFrames_PercentilesInterpolated) {
    g_mock_time = 0.0;
    FramePacer fp(mock_time_source);

    fp.begin_frame();
    g_mock_time = 0.010;
    fp.end_frame();

    g_mock_time = 0.010;
    fp.begin_frame();
    g_mock_time = 0.030;
    fp.end_frame();

    const auto s = fp.stats();
    EXPECT_EQ(s.count, 2u);
    EXPECT_NEAR(s.avg_ms, 15.0f, 0.1f);
    EXPECT_NEAR(s.min_ms, 10.0f, 0.01f);
    EXPECT_NEAR(s.max_ms, 20.0f, 0.01f);
    // p50 with 2 values: interpolated at rank 0.5
    EXPECT_NEAR(s.p50_ms, 15.0f, 0.1f);
}
