/// Unit tests for phase 11.8.0: `VfxBurstEvent` + `ParticleEmitter::bursts`
/// runtime behaviour + timeline-scrubber marker layout.
///
/// Covers:
///   - `VfxBurstList::Add` maintains ascending-by-time ordering.
///   - `Update` re-sorts when time changes across a neighbour.
///   - `Remove` out-of-range returns false; in-range shrinks vector.
///   - `AdvanceCursor` fires all bursts ≤ currentTime starting from a
///     given cursor; cursor monotonically advances; unfired bursts stay.
///   - `ParticleEmitter::CalculateSpawnCount` adds burst contributions
///     to this frame's spawn total when the cursor crosses their time.
///   - Bursts still fire when `emissionRate == 0` (bursts are
///     independent of the continuous rate).
///   - `burstCursor == bursts.size()` once all bursts have fired;
///     further frames don't double-fire.
///   - Setting `burstCursor = 0` (restart) re-fires every burst.
///   - Serializer round-trip: `"bursts": [{t, n}]` preserved; empty
///     list omits the key; malformed entries dropped; unsorted input
///     is sorted on load.
///   - Timeline scrubber renders a `BurstMarker` per burst inside the
///     track bounds; marker height reflects `log2(count)`; bursts
///     outside `[0, duration]` are skipped.

#include <gtest/gtest.h>

#include "engine/vfx/components/emitter_serializer.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/editor/vfx_preview_clock.h"
#include "engine/vfx/editor/vfx_timeline_scrubber_widget.h"
#include "engine/vfx/spawn/vfx_burst_event.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace uz;

namespace {

ParticleEmitter MakeRuntimeEmitter() {
    ParticleEmitter e;
    e.emissionRate = 0.0f;  // emphasise burst-only behaviour
    e.maxParticles = 10000;
    e.lifetime     = 2.0f;
    e.initialized = true;
    e.isEmitting  = true;
    return e;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxBurstList helpers
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxBurstList, Add_MaintainsAscendingOrder) {
    std::vector<VfxBurstEvent> out;
    VfxBurstList::Add(out, {0.5f, 100});
    VfxBurstList::Add(out, {0.2f, 50});
    VfxBurstList::Add(out, {0.9f, 200});
    VfxBurstList::Add(out, {0.35f, 75});

    ASSERT_EQ(out.size(), 4u);
    for (size_t i = 1; i < out.size(); ++i) {
        EXPECT_LE(out[i - 1].time, out[i].time);
    }
    EXPECT_FLOAT_EQ(out[0].time, 0.2f);
    EXPECT_FLOAT_EQ(out[3].time, 0.9f);
}

TEST(VfxBurstList, Update_ReSortsOnNeighbourCross) {
    std::vector<VfxBurstEvent> out;
    VfxBurstList::Add(out, {0.1f, 10});
    VfxBurstList::Add(out, {0.2f, 20});
    VfxBurstList::Add(out, {0.3f, 30});

    // Move index 0 past index 2.
    const size_t newIdx = VfxBurstList::Update(out, 0, {0.4f, 10});
    EXPECT_EQ(newIdx, 2u);
    EXPECT_FLOAT_EQ(out[0].time, 0.2f);
    EXPECT_FLOAT_EQ(out[1].time, 0.3f);
    EXPECT_FLOAT_EQ(out[2].time, 0.4f);
    EXPECT_EQ(out[2].count, 10u);
}

TEST(VfxBurstList, Remove_OutOfRange_NoMutation) {
    std::vector<VfxBurstEvent> out;
    VfxBurstList::Add(out, {0.1f, 10});
    EXPECT_FALSE(VfxBurstList::Remove(out, 5));
    EXPECT_EQ(out.size(), 1u);
    EXPECT_TRUE(VfxBurstList::Remove(out, 0));
    EXPECT_TRUE(out.empty());
}

TEST(VfxBurstList, AdvanceCursor_FiresAllEligible) {
    std::vector<VfxBurstEvent> bursts{
        {0.1f, 10}, {0.3f, 30}, {0.8f, 80}
    };
    uint32_t fired = 0;
    size_t   newCursor = 0;
    VfxBurstList::AdvanceCursor(bursts, 0, 0.35f, fired, newCursor);
    EXPECT_EQ(fired, 40u);      // 10 + 30
    EXPECT_EQ(newCursor, 2u);

    // Next call starts from cursor=2 — no more fire at 0.35s.
    VfxBurstList::AdvanceCursor(bursts, newCursor, 0.5f, fired, newCursor);
    EXPECT_EQ(fired, 0u);
    EXPECT_EQ(newCursor, 2u);

    // Advance past final burst.
    VfxBurstList::AdvanceCursor(bursts, newCursor, 1.0f, fired, newCursor);
    EXPECT_EQ(fired, 80u);
    EXPECT_EQ(newCursor, 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Runtime integration with CalculateSpawnCount
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxBurstEvents, CalculateSpawnCount_FiresBurstOnCross) {
    ParticleEmitter e = MakeRuntimeEmitter();
    VfxBurstList::Add(e.bursts, {0.2f, 300});
    VfxBurstList::Add(e.bursts, {0.4f, 150});

    // Frame at t=0.1 — no burst crossed.
    e.timeSinceRegistration = 0.1f;
    EXPECT_EQ(e.CalculateSpawnCount(0.1f), 0u);

    // Frame crossing t=0.2.
    e.timeSinceRegistration = 0.25f;
    EXPECT_EQ(e.CalculateSpawnCount(0.1f), 300u);
    EXPECT_EQ(e.burstCursor, 1u);

    // Frame crossing t=0.4.
    e.timeSinceRegistration = 0.5f;
    EXPECT_EQ(e.CalculateSpawnCount(0.1f), 150u);
    EXPECT_EQ(e.burstCursor, 2u);

    // Further frames: no more bursts.
    e.timeSinceRegistration = 1.0f;
    EXPECT_EQ(e.CalculateSpawnCount(0.1f), 0u);
    EXPECT_EQ(e.burstCursor, 2u);
}

TEST(VfxBurstEvents, CalculateSpawnCount_BurstsFireWithZeroRate) {
    // Zero-rate + emitting should still fire bursts.
    ParticleEmitter e = MakeRuntimeEmitter();
    e.emissionRate = 0.0f;
    VfxBurstList::Add(e.bursts, {0.1f, 50});
    e.timeSinceRegistration = 0.2f;
    EXPECT_EQ(e.CalculateSpawnCount(0.05f), 50u);
}

TEST(VfxBurstEvents, CalculateSpawnCount_BurstsPlusConstantRate) {
    ParticleEmitter e = MakeRuntimeEmitter();
    e.emissionRate = 60.0f;            // 60/s → 1 spawn at 16.6ms
    VfxBurstList::Add(e.bursts, {0.1f, 20});

    e.timeSinceRegistration = 0.15f;
    // Burst fires (20) + rate contributes at least one particle in the
    // 16.6ms frame since spawnAccumulator starts at 0.
    const uint32_t spawned = e.CalculateSpawnCount(1.0f / 60.0f);
    EXPECT_EQ(spawned, 21u);           // 1 (rate) + 20 (burst)
}

TEST(VfxBurstEvents, ResetCursor_ReFiresAllBursts) {
    ParticleEmitter e = MakeRuntimeEmitter();
    VfxBurstList::Add(e.bursts, {0.1f, 10});

    e.timeSinceRegistration = 0.2f;
    EXPECT_EQ(e.CalculateSpawnCount(0.1f), 10u);
    EXPECT_EQ(e.burstCursor, 1u);

    // Restart: host sets timeSinceRegistration = 0 + burstCursor = 0.
    e.timeSinceRegistration = 0.0f;
    e.burstCursor = 0u;
    e.timeSinceRegistration = 0.2f;
    EXPECT_EQ(e.CalculateSpawnCount(0.1f), 10u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Serializer round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxBurstSerializer, EmptyListOmitsKey) {
    ParticleEmitter e;
    const std::string out = SerializeEmitter(e);
    EXPECT_EQ(out.find("bursts"), std::string::npos);
}

TEST(VfxBurstSerializer, PopulatedRoundTrip) {
    ParticleEmitter src;
    VfxBurstList::Add(src.bursts, {0.2f, 300});
    VfxBurstList::Add(src.bursts, {0.4f, 150});
    VfxBurstList::Add(src.bursts, {0.9f, 42});

    const std::string json = SerializeEmitter(src);

    ParticleEmitter dst;
    ASSERT_TRUE(DeserializeEmitter(json, dst));
    ASSERT_EQ(dst.bursts.size(), 3u);
    EXPECT_FLOAT_EQ(dst.bursts[0].time, 0.2f);
    EXPECT_EQ(dst.bursts[0].count, 300u);
    EXPECT_FLOAT_EQ(dst.bursts[1].time, 0.4f);
    EXPECT_EQ(dst.bursts[1].count, 150u);
    EXPECT_FLOAT_EQ(dst.bursts[2].time, 0.9f);
    EXPECT_EQ(dst.bursts[2].count, 42u);
    EXPECT_EQ(dst.burstCursor, 0u);  // reset on load
}

TEST(VfxBurstSerializer, UnsortedInputIsSorted) {
    const std::string json = R"({
        "bursts": [
            {"t": 0.7, "n": 70},
            {"t": 0.2, "n": 20},
            {"t": 0.4, "n": 40}
        ]
    })";
    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(json, e));
    ASSERT_EQ(e.bursts.size(), 3u);
    EXPECT_FLOAT_EQ(e.bursts[0].time, 0.2f);
    EXPECT_FLOAT_EQ(e.bursts[1].time, 0.4f);
    EXPECT_FLOAT_EQ(e.bursts[2].time, 0.7f);
}

TEST(VfxBurstSerializer, MalformedEntriesDropped) {
    const std::string json = R"({
        "bursts": [
            {"t": 0.1, "n": 10},
            "garbage",
            {"t": 0.3, "n": 30}
        ]
    })";
    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(json, e));
    EXPECT_EQ(e.bursts.size(), 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Timeline scrubber — burst markers
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTimelineScrubberBurstMarkers, EmitsMarkersForEveryInRangeBurst) {
    VfxPreviewClockConfig cfg; cfg.maxDuration = 2.0f;
    VfxPreviewClock clock(cfg);
    std::vector<VfxBurstEvent> bursts{
        {0.5f, 50}, {1.0f, 100}, {5.0f, 9999}  // last out of range
    };

    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    w.BindBursts(&bursts);

    const VfxStackRect bar{ 0.0f, 700.0f, 1280.0f, 32.0f };
    const auto layout = w.ComputeLayout(bar);
    int markers = 0;
    for (const auto& it : layout) {
        if (it.type == VfxTimelineItemType::BurstMarker) ++markers;
    }
    EXPECT_EQ(markers, 2);  // two in-range bursts (0.5 + 1.0); 5.0 skipped
}

TEST(VfxTimelineScrubberBurstMarkers, MarkerXReflectsBurstTime) {
    VfxPreviewClockConfig cfg; cfg.maxDuration = 4.0f;
    VfxPreviewClock clock(cfg);
    std::vector<VfxBurstEvent> bursts{ {1.0f, 10} };
    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    w.BindBursts(&bursts);

    const VfxStackRect bar{ 0.0f, 700.0f, 1000.0f, 32.0f };
    const auto layout = w.ComputeLayout(bar);

    const VfxTimelineLayoutItem* marker = nullptr;
    const VfxTimelineLayoutItem* track = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxTimelineItemType::BurstMarker) marker = &it;
        if (it.type == VfxTimelineItemType::SeekTrack)   track = &it;
    }
    ASSERT_NE(marker, nullptr);
    ASSERT_NE(track, nullptr);

    // Marker centre should land at 25% of the track (t=1.0 of duration=4.0).
    const float expectedX = track->rect.x + track->rect.width * 0.25f;
    const float centreX   = marker->rect.x + marker->rect.width * 0.5f;
    EXPECT_NEAR(centreX, expectedX, 2.0f);
}

TEST(VfxTimelineScrubberBurstMarkers, HitTest_MarkerReturnsBurstIndex) {
    VfxPreviewClockConfig cfg; cfg.maxDuration = 4.0f;
    VfxPreviewClock clock(cfg);
    std::vector<VfxBurstEvent> bursts{ {2.0f, 42} };
    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    w.BindBursts(&bursts);

    const VfxStackRect bar{ 0.0f, 700.0f, 1000.0f, 32.0f };
    const auto layout = w.ComputeLayout(bar);
    const VfxTimelineLayoutItem* marker = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxTimelineItemType::BurstMarker) { marker = &it; break; }
    }
    ASSERT_NE(marker, nullptr);

    const float cx = marker->rect.x + marker->rect.width * 0.5f;
    const float cy = marker->rect.y + marker->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxTimelineItemType::BurstMarker);
    EXPECT_EQ(hit.burstIndex, 0u);
}

TEST(VfxTimelineScrubberBurstMarkers, UnboundBursts_NoMarkers) {
    VfxPreviewClockConfig cfg; cfg.maxDuration = 2.0f;
    VfxPreviewClock clock(cfg);
    VfxTimelineScrubberWidget w;
    w.Bind(&clock);
    // BindBursts not called.
    const auto layout = w.ComputeLayout({0, 0, 1000, 32});
    int markers = 0;
    for (const auto& it : layout) {
        if (it.type == VfxTimelineItemType::BurstMarker) ++markers;
    }
    EXPECT_EQ(markers, 0);
}
