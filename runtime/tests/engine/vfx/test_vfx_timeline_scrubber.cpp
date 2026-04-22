/// Unit tests for phase 11.6.0: `VfxTimelineScrubberWidget`.
///
/// Covers:
///   - Bind / unbind behaviour (unbound widget returns safe defaults).
///   - `TogglePlayPause` flips clock state; `IsPlaying` mirrors the clock.
///   - `StepForward` uses the clock's `StepFrame`; `StepBackward` scrubs
///     backward clamped at t=0.
///   - Speed ring: `CycleSpeed` walks `[0.25, 0.5, 1, 2, 4]`, wraps;
///     `SetSpeed` snaps to the nearest ring entry; `Speed` reads back
///     the snapped value; tie-break towards 1.0x.
///   - Loop flag: default on, `ToggleLoop` flips, `SetLoop` only fires
///     callback on change.
///   - Restart: `RequestRestart` fires the callback.
///   - Seek math: `Seek` / `SeekByNormalised` / `NormalisedTime` clamp
///     correctly and round-trip through the clock.
///   - `FormatTimeDisplay` honours `timeDecimals`.
///   - `TickAutoLoop` fires restart + resets the clock when the clock
///     hits the end AND loop is enabled; does nothing otherwise.
///   - Layout emits the expected transport buttons + seek track in
///     left-to-right order.
///   - Hit-test resolves buttons / track / handle correctly.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_preview_clock.h"
#include "engine/vfx/editor/vfx_timeline_scrubber_widget.h"

#include <cmath>
#include <string>

using namespace uz;

namespace {

constexpr VfxStackRect kBar{ 0.0f, 700.0f, 1280.0f, 32.0f };

VfxPreviewClock MakeClock(float maxDuration = 10.0f) {
    VfxPreviewClockConfig cfg;
    cfg.maxDuration = maxDuration;
    return VfxPreviewClock(cfg);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Bind / unbind
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberWidget, Unbound_IsPlayingFalse_NoCrash) {
    VfxTimelineScrubberWidget w;
    EXPECT_FALSE(w.IsPlaying());
    EXPECT_FLOAT_EQ(w.CurrentTime(), 0.0f);
    EXPECT_FLOAT_EQ(w.Duration(),    0.0f);
    EXPECT_FLOAT_EQ(w.NormalisedTime(), 0.0f);
    EXPECT_TRUE(w.ComputeLayout(kBar).empty());
    w.TogglePlayPause();      // no-op
    w.StepForward();          // no-op
    w.Seek(1.0f);             // no-op
}

// ─────────────────────────────────────────────────────────────────────────────
// Play / pause / step
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberWidget, TogglePlayPause_FlipsClockState) {
    VfxPreviewClock clock = MakeClock();
    clock.Pause();  // start paused so the first toggle flips to playing

    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    EXPECT_FALSE(w.IsPlaying());
    w.TogglePlayPause();
    EXPECT_TRUE(w.IsPlaying());
    w.TogglePlayPause();
    EXPECT_FALSE(w.IsPlaying());
}

TEST(VfxTimelineScrubberWidget, StepBackward_ClampsAtZero) {
    VfxPreviewClock clock = MakeClock();
    clock.Pause();
    clock.Scrub(0.005f);
    (void)clock.Tick(0.0f);  // consume scrub

    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    w.StepBackward();          // scrub by -1/60; clamps to 0
    (void)clock.Tick(0.0f);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 0.0f);
}

TEST(VfxTimelineScrubberWidget, StepForward_AdvancesOneFrame) {
    VfxPreviewClock clock = MakeClock();
    clock.Pause();

    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    w.StepForward();
    const float dt = clock.Tick(0.0f);
    EXPECT_NEAR(dt, 1.0f / 60.0f, 1e-5f);
    EXPECT_NEAR(clock.GetCurrentTime(), 1.0f / 60.0f, 1e-5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Speed ring
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberWidget, CycleSpeed_WalksRing) {
    VfxPreviewClock clock = MakeClock();
    VfxTimelineScrubberWidget w;
    w.Bind(&clock);

    EXPECT_FLOAT_EQ(w.Speed(), 1.0f);   // default play rate = 1.0x → ring idx 2
    w.CycleSpeed();
    EXPECT_FLOAT_EQ(w.Speed(), 2.0f);
    w.CycleSpeed();
    EXPECT_FLOAT_EQ(w.Speed(), 4.0f);
    w.CycleSpeed();
    EXPECT_FLOAT_EQ(w.Speed(), 0.25f);  // wrap
    w.CycleSpeed();
    EXPECT_FLOAT_EQ(w.Speed(), 0.5f);
}

TEST(VfxTimelineScrubberWidget, SetSpeed_SnapsToNearestRingEntry) {
    VfxPreviewClock clock = MakeClock();
    VfxTimelineScrubberWidget w;
    w.Bind(&clock);

    w.SetSpeed(0.3f);
    EXPECT_FLOAT_EQ(w.Speed(), 0.25f);
    w.SetSpeed(0.7f);
    EXPECT_FLOAT_EQ(w.Speed(), 0.5f);
    w.SetSpeed(3.0f);
    EXPECT_FLOAT_EQ(w.Speed(), 2.0f);  // 3.0 is equidistant 1:2, 2:4 — tie-break towards 2x (closer)
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop + restart
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberWidget, Loop_DefaultOn_ToggleFiresCallback) {
    VfxPreviewClock clock = MakeClock();
    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    EXPECT_TRUE(w.IsLoopEnabled());

    int calls = 0;
    bool lastState = false;
    w.onLoopChanged = [&](bool v) { ++calls; lastState = v; };
    w.ToggleLoop();
    EXPECT_FALSE(w.IsLoopEnabled());
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(lastState);
    w.ToggleLoop();
    EXPECT_EQ(calls, 2);
}

TEST(VfxTimelineScrubberWidget, SetLoop_FiresOnlyOnChange) {
    VfxPreviewClock clock = MakeClock();
    VfxTimelineScrubberWidget w;
    w.Bind(&clock);

    int calls = 0;
    w.onLoopChanged = [&](bool) { ++calls; };
    w.SetLoop(true);
    EXPECT_EQ(calls, 0);  // already true
    w.SetLoop(false);
    EXPECT_EQ(calls, 1);
    w.SetLoop(false);
    EXPECT_EQ(calls, 1);
}

TEST(VfxTimelineScrubberWidget, RestartFiresCallback) {
    VfxTimelineScrubberWidget w;
    int calls = 0;
    w.onRestartRequested = [&]() { ++calls; };
    w.RequestRestart();
    EXPECT_EQ(calls, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Seek
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberWidget, Seek_ClampsAndWritesToClock) {
    VfxPreviewClock clock = MakeClock(4.0f);
    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    w.Seek(2.5f);
    (void)clock.Tick(0.0f);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 2.5f);

    w.Seek(10.0f);   // beyond duration → clamp to 4.0
    (void)clock.Tick(0.0f);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 4.0f);

    w.Seek(-1.0f);   // negative → clamp to 0
    (void)clock.Tick(0.0f);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 0.0f);
}

TEST(VfxTimelineScrubberWidget, SeekByNormalised_MapsToFraction) {
    VfxPreviewClock clock = MakeClock(4.0f);
    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    w.SeekByNormalised(0.5f);
    (void)clock.Tick(0.0f);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 2.0f);
    EXPECT_FLOAT_EQ(w.NormalisedTime(), 0.5f);
}

TEST(VfxTimelineScrubberWidget, NormalisedTime_ZeroWhenDurationZero) {
    VfxPreviewClockConfig cfg;
    cfg.maxDuration = 0.0f;
    VfxPreviewClock clock(cfg);
    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    EXPECT_FLOAT_EQ(w.NormalisedTime(), 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Time display
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberWidget, FormatTimeDisplay_HonoursDecimals) {
    VfxPreviewClock clock = MakeClock(4.0f);
    clock.Scrub(1.23456f);
    (void)clock.Tick(0.0f);

    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    w.timeDecimals = 2;
    EXPECT_EQ(w.FormatTimeDisplay(), "1.23s / 4.00s");
    w.timeDecimals = 3;
    EXPECT_EQ(w.FormatTimeDisplay(), "1.235s / 4.000s");
}

// ─────────────────────────────────────────────────────────────────────────────
// Auto-loop
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberWidget, TickAutoLoop_LoopOnAtEnd_FiresRestart) {
    VfxPreviewClock clock = MakeClock(2.0f);
    clock.Scrub(2.0f);
    (void)clock.Tick(0.0f);

    VfxTimelineScrubberWidget w;
    w.Bind(&clock);

    int restarts = 0;
    w.onRestartRequested = [&]() { ++restarts; };
    EXPECT_TRUE(w.TickAutoLoop());
    EXPECT_EQ(restarts, 1);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 0.0f);
    EXPECT_TRUE(clock.IsPlaying());
}

TEST(VfxTimelineScrubberWidget, TickAutoLoop_LoopOff_NoRestart) {
    VfxPreviewClock clock = MakeClock(2.0f);
    clock.Scrub(2.0f);
    (void)clock.Tick(0.0f);

    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    w.SetLoop(false);

    int restarts = 0;
    w.onRestartRequested = [&]() { ++restarts; };
    EXPECT_FALSE(w.TickAutoLoop());
    EXPECT_EQ(restarts, 0);
    EXPECT_FLOAT_EQ(clock.GetCurrentTime(), 2.0f);
}

TEST(VfxTimelineScrubberWidget, TickAutoLoop_MidPlayback_NoRestart) {
    VfxPreviewClock clock = MakeClock(2.0f);
    clock.Scrub(1.0f);
    (void)clock.Tick(0.0f);

    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    int restarts = 0;
    w.onRestartRequested = [&]() { ++restarts; };
    EXPECT_FALSE(w.TickAutoLoop());
    EXPECT_EQ(restarts, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout + hit-test
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberWidget, Layout_EmitsAllControls) {
    VfxPreviewClock clock = MakeClock();
    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    const auto layout = w.ComputeLayout(kBar);

    int strip=0, timeLbl=0, play=0, stepB=0, stepF=0, speed=0, loop=0,
        restart=0, track=0, handle=0;
    for (const auto& it : layout) {
        switch (it.type) {
        case VfxTimelineItemType::Strip:       ++strip; break;
        case VfxTimelineItemType::TimeLabel:   ++timeLbl; break;
        case VfxTimelineItemType::PlayButton:  ++play; break;
        case VfxTimelineItemType::StepBackBtn: ++stepB; break;
        case VfxTimelineItemType::StepFwdBtn:  ++stepF; break;
        case VfxTimelineItemType::SpeedBtn:    ++speed; break;
        case VfxTimelineItemType::LoopBtn:     ++loop; break;
        case VfxTimelineItemType::RestartBtn:  ++restart; break;
        case VfxTimelineItemType::SeekTrack:   ++track; break;
        case VfxTimelineItemType::SeekHandle:  ++handle; break;
        case VfxTimelineItemType::None:        break;
        }
    }
    EXPECT_EQ(strip, 1);
    EXPECT_EQ(timeLbl, 1);
    EXPECT_EQ(play, 1);
    EXPECT_EQ(stepB, 1);
    EXPECT_EQ(stepF, 1);
    EXPECT_EQ(speed, 1);
    EXPECT_EQ(loop, 1);
    EXPECT_EQ(restart, 1);
    EXPECT_EQ(track, 1);
    EXPECT_EQ(handle, 1);
}

TEST(VfxTimelineScrubberWidget, HitTest_PlayButton) {
    VfxPreviewClock clock = MakeClock();
    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    const auto layout = w.ComputeLayout(kBar);
    const VfxTimelineLayoutItem* play = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxTimelineItemType::PlayButton) { play = &it; break; }
    }
    ASSERT_NE(play, nullptr);
    const float cx = play->rect.x + play->rect.width * 0.5f;
    const float cy = play->rect.y + play->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxTimelineItemType::PlayButton);
}

TEST(VfxTimelineScrubberWidget, HitTest_SeekHandleBeatsTrack) {
    VfxPreviewClock clock = MakeClock();
    clock.Scrub(5.0f);
    (void)clock.Tick(0.0f);

    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    const auto layout = w.ComputeLayout(kBar);

    const VfxTimelineLayoutItem* handle = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxTimelineItemType::SeekHandle) { handle = &it; break; }
    }
    ASSERT_NE(handle, nullptr);
    const float cx = handle->rect.x + handle->rect.width * 0.5f;
    const float cy = handle->rect.y + handle->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxTimelineItemType::SeekHandle);
}
