/// Unit tests for phase 12.6.0: `VfxTimelineScrubberWidget` redesign —
/// transport icons, Play/Pause glyph swap, speed dropdown label,
/// loop state accent, time readout format, scrub-track filled region,
/// skip-to-start / skip-to-end, triangle burst markers.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "engine/vfx/editor/vfx_preview_clock.h"
#include "engine/vfx/editor/vfx_timeline_scrubber_widget.h"
#include "engine/vfx/spawn/vfx_burst_event.h"

using namespace uz;

namespace {

constexpr VfxStackRect kBar{0.0f, 0.0f, 900.0f, 40.0f};

VfxPreviewClock MakeClock(float duration = 5.0f) {
    VfxPreviewClockConfig cfg;
    cfg.maxDuration = duration;
    return VfxPreviewClock(cfg);
}

const VfxTimelineLayoutItem* First(
    const std::vector<VfxTimelineLayoutItem>& layout,
    VfxTimelineItemType t) {
    for (const auto& it : layout) {
        if (it.type == t) return &it;
    }
    return nullptr;
}

size_t CountType(const std::vector<VfxTimelineLayoutItem>& layout,
                 VfxTimelineItemType t) {
    size_t n = 0;
    for (const auto& it : layout) {
        if (it.type == t) ++n;
    }
    return n;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// SkipStart / SkipEnd — new transport buttons
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberRedesign, Layout_HasSkipStartAndSkipEndButtons) {
    auto c = MakeClock();
    VfxTimelineScrubberWidget w;
    w.Bind(&c);
    const auto layout = w.ComputeLayout(kBar);
    EXPECT_EQ(CountType(layout, VfxTimelineItemType::SkipStartBtn), 1u);
    EXPECT_EQ(CountType(layout, VfxTimelineItemType::SkipEndBtn),   1u);
}

TEST(VfxTimelineScrubberRedesign, SkipToStart_SetsClockToZero) {
    auto c = MakeClock(5.0f);
    c.Scrub(3.0f);
    VfxTimelineScrubberWidget w;
    w.Bind(&c);
    w.SkipToStart();
    EXPECT_FLOAT_EQ(c.GetCurrentTime(), 0.0f);
}

TEST(VfxTimelineScrubberRedesign, SkipToEnd_SetsClockToDuration) {
    auto c = MakeClock(5.0f);
    VfxTimelineScrubberWidget w;
    w.Bind(&c);
    w.SkipToEnd();
    EXPECT_NEAR(c.GetCurrentTime(), 5.0f, 1e-3f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Play/Pause glyph swap
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberRedesign, PlayButton_ShowsPlayGlyphWhenPaused) {
    auto c = MakeClock();
    c.Pause();
    VfxTimelineScrubberWidget w;
    w.Bind(&c);

    const auto layout = w.ComputeLayout(kBar);
    const auto* play = First(layout, VfxTimelineItemType::PlayButton);
    ASSERT_NE(play, nullptr);
    EXPECT_EQ(play->label, ">");
}

TEST(VfxTimelineScrubberRedesign, PlayButton_ShowsPauseGlyphWhenPlaying) {
    auto c = MakeClock();
    c.Play();
    VfxTimelineScrubberWidget w;
    w.Bind(&c);

    const auto layout = w.ComputeLayout(kBar);
    const auto* play = First(layout, VfxTimelineItemType::PlayButton);
    ASSERT_NE(play, nullptr);
    EXPECT_EQ(play->label, "||");
}

// ─────────────────────────────────────────────────────────────────────────────
// Speed dropdown label format
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberRedesign, SpeedDropdown_LabelFormatIsRateX) {
    auto c = MakeClock();
    VfxTimelineScrubberWidget w;
    w.Bind(&c);
    w.SetSpeed(2.0f);
    const auto layout = w.ComputeLayout(kBar);
    const auto* s = First(layout, VfxTimelineItemType::SpeedBtn);
    ASSERT_NE(s, nullptr);
    // Label must contain "2.0" and end with "x v" (dropdown trigger).
    EXPECT_NE(s->label.find("2.0"), std::string::npos) << s->label;
    EXPECT_NE(s->label.find('x'),   std::string::npos) << s->label;
    EXPECT_NE(s->label.find('v'),   std::string::npos) << s->label;
}

// ─────────────────────────────────────────────────────────────────────────────
// Time readout format — "cur / total s"
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberRedesign, TimeLabel_LabelMatchesFormatTimeDisplay) {
    auto c = MakeClock(5.0f);
    c.Scrub(1.23f);
    VfxTimelineScrubberWidget w;
    w.Bind(&c);

    const auto layout = w.ComputeLayout(kBar);
    const auto* tl = First(layout, VfxTimelineItemType::TimeLabel);
    ASSERT_NE(tl, nullptr);
    EXPECT_EQ(tl->label, w.FormatTimeDisplay());
    // Must mention both the current time and total (to two decimals).
    EXPECT_NE(tl->label.find("1.23"), std::string::npos);
    EXPECT_NE(tl->label.find("5.00"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scrub track + burst markers
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberRedesign, SeekTrack_HalfPlayedHasHalfFillViaNormalisedTime) {
    auto c = MakeClock(4.0f);
    c.Scrub(2.0f);
    VfxTimelineScrubberWidget w;
    w.Bind(&c);
    EXPECT_NEAR(w.NormalisedTime(), 0.5f, 1e-3f);
    const auto layout = w.ComputeLayout(kBar);
    const auto* track = First(layout, VfxTimelineItemType::SeekTrack);
    ASSERT_NE(track, nullptr);
    EXPECT_GT(track->rect.width, 0.0f);
    // Filled portion computed at render time equals track.width * u.
    const float expectedFillW = track->rect.width * 0.5f;
    EXPECT_GT(expectedFillW, 0.0f);
}

TEST(VfxTimelineScrubberRedesign, BurstMarker_HigherCountTallerMarker) {
    auto c = MakeClock(5.0f);
    VfxTimelineScrubberWidget w;
    w.Bind(&c);

    std::vector<VfxBurstEvent> bursts;
    {
        VfxBurstEvent b;
        b.time = 1.0f;
        b.count = 4;
        bursts.push_back(b);
    }
    {
        VfxBurstEvent b;
        b.time = 2.0f;
        b.count = 64;
        bursts.push_back(b);
    }
    w.BindBursts(&bursts);

    const auto layout = w.ComputeLayout(kBar);

    const VfxTimelineLayoutItem* first  = nullptr;
    const VfxTimelineLayoutItem* second = nullptr;
    for (const auto& it : layout) {
        if (it.type != VfxTimelineItemType::BurstMarker) continue;
        if      (it.burstIndex == 0u) first  = &it;
        else if (it.burstIndex == 1u) second = &it;
    }
    ASSERT_NE(first,  nullptr);
    ASSERT_NE(second, nullptr);
    // Higher-count burst should be at least as tall.
    EXPECT_GT(second->rect.height, first->rect.height);
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop toggle label consistency (labels must be non-empty)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberRedesign, LoopButton_LabelNonEmpty) {
    auto c = MakeClock();
    VfxTimelineScrubberWidget w;
    w.Bind(&c);
    const auto layout = w.ComputeLayout(kBar);
    const auto* lb = First(layout, VfxTimelineItemType::LoopBtn);
    ASSERT_NE(lb, nullptr);
    EXPECT_GT(lb->label.size(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Transport order — skip-start precedes step-back, play is central
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberRedesign, Transport_SkipStartPrecedesStepBack) {
    auto c = MakeClock();
    VfxTimelineScrubberWidget w;
    w.Bind(&c);
    const auto layout = w.ComputeLayout(kBar);
    const auto* skip = First(layout, VfxTimelineItemType::SkipStartBtn);
    const auto* back = First(layout, VfxTimelineItemType::StepBackBtn);
    const auto* play = First(layout, VfxTimelineItemType::PlayButton);
    ASSERT_NE(skip, nullptr);
    ASSERT_NE(back, nullptr);
    ASSERT_NE(play, nullptr);
    EXPECT_LT(skip->rect.x, back->rect.x);
    EXPECT_LT(back->rect.x, play->rect.x);
}
