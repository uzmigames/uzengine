/// Unit tests for phase 9.8.0 viewer side — snapshot viewer widget.
///
/// @ref engine/vfx/editor/vfx_snapshot_viewer_widget.h

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_snapshot_viewer_widget.h"

#include <cstring>

using namespace uz;

namespace {

/// Builds a tiny snapshot with 4 particles and 2 channels for tests.
VfxSnapshot MakeTestSnapshot() {
    VfxSnapshot snap;
    snap.header.emitterName = "test";
    snap.header.frameIndex  = 0;
    snap.header.capacity    = 4;
    snap.header.aliveCount  = 4;

    // Channel 1: positions (Float3), 4 elements.
    VfxSnapshotChannel pos;
    pos.name = "positions";
    pos.elementType = VfxSnapshotElementType::Float3;
    pos.elementCount = 4;
    pos.data.resize(4 * 12);
    float positions[4][3] = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 2.0f, 3.0f},
        {-1.0f, 0.5f, 1.0f},
        {2.0f, 4.0f, 0.0f},
    };
    std::memcpy(pos.data.data(), positions, sizeof(positions));
    snap.channels.push_back(std::move(pos));

    // Channel 2: ages (Float), 4 elements.
    VfxSnapshotChannel ages;
    ages.name = "ages";
    ages.elementType = VfxSnapshotElementType::Float;
    ages.elementCount = 4;
    ages.data.resize(4 * 4);
    float ageVals[4] = {0.0f, 0.25f, 0.5f, 1.0f};
    std::memcpy(ages.data.data(), ageVals, sizeof(ageVals));
    snap.channels.push_back(std::move(ages));

    return snap;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Default state
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnapshotViewer, StartsEmpty) {
    VfxSnapshotViewerWidget w;
    EXPECT_FALSE(w.HasSnapshot());
    EXPECT_EQ(w.AliveCount(), 0u);
    EXPECT_EQ(w.SelectedChannelIndex(), UINT32_MAX);
    EXPECT_EQ(w.ChannelNames().size(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// SetSnapshot
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnapshotViewer, SetSnapshotPopulatesState) {
    VfxSnapshotViewerWidget w;
    w.SetSnapshot(MakeTestSnapshot());
    EXPECT_TRUE(w.HasSnapshot());
    EXPECT_EQ(w.AliveCount(), 4u);
    ASSERT_EQ(w.ChannelNames().size(), 2u);
    EXPECT_EQ(w.ChannelNames()[0], "positions");
    EXPECT_EQ(w.ChannelNames()[1], "ages");
    EXPECT_EQ(w.SelectedChannelIndex(), 0u); // default selection
}

TEST(VfxSnapshotViewer, ClearResetsState) {
    VfxSnapshotViewerWidget w;
    w.SetSnapshot(MakeTestSnapshot());
    w.Clear();
    EXPECT_FALSE(w.HasSnapshot());
    EXPECT_EQ(w.AliveCount(), 0u);
    EXPECT_EQ(w.SelectedChannelIndex(), UINT32_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
// Channel selection + stats
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnapshotViewer, SelectChannelPositions) {
    VfxSnapshotViewerWidget w;
    w.SetSnapshot(MakeTestSnapshot());
    w.SelectChannel(0);
    EXPECT_EQ(w.SelectedChannelIndex(), 0u);

    const auto& stats = w.SelectedChannelStats();
    EXPECT_EQ(stats.componentCount, 3u);
    EXPECT_EQ(stats.sampleCount, 4u);
    // Position X: min=-1, max=2
    EXPECT_FLOAT_EQ(stats.minValue[0], -1.0f);
    EXPECT_FLOAT_EQ(stats.maxValue[0],  2.0f);
    // Position Y: min=0, max=4
    EXPECT_FLOAT_EQ(stats.minValue[1],  0.0f);
    EXPECT_FLOAT_EQ(stats.maxValue[1],  4.0f);
}

TEST(VfxSnapshotViewer, SelectChannelAges) {
    VfxSnapshotViewerWidget w;
    w.SetSnapshot(MakeTestSnapshot());
    w.SelectChannel(1);

    const auto& stats = w.SelectedChannelStats();
    EXPECT_EQ(stats.componentCount, 1u);
    EXPECT_EQ(stats.sampleCount, 4u);
    EXPECT_FLOAT_EQ(stats.minValue[0], 0.0f);
    EXPECT_FLOAT_EQ(stats.maxValue[0], 1.0f);
    // Average of 0, 0.25, 0.5, 1.0 = 0.4375
    EXPECT_NEAR(stats.avgValue[0], 0.4375f, 1e-5f);
}

TEST(VfxSnapshotViewer, SelectChannelInvalidNoOp) {
    VfxSnapshotViewerWidget w;
    w.SetSnapshot(MakeTestSnapshot());
    w.SelectChannel(99); // out of range
    EXPECT_EQ(w.SelectedChannelIndex(), 0u); // unchanged
}

// ─────────────────────────────────────────────────────────────────────────────
// Particle cursor
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnapshotViewer, ParticleCursorClamped) {
    VfxSnapshotViewerWidget w;
    w.SetSnapshot(MakeTestSnapshot());
    w.SetParticleCursor(999);
    EXPECT_EQ(w.ParticleCursor(), 3u); // clamped to aliveCount-1

    w.SetParticleCursor(2);
    EXPECT_EQ(w.ParticleCursor(), 2u);
}

TEST(VfxSnapshotViewer, ParticleCursorWithoutSnapshot) {
    VfxSnapshotViewerWidget w;
    w.SetParticleCursor(5);
    EXPECT_EQ(w.ParticleCursor(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxSnapshotViewer, TumbleUpdatesYaw) {
    VfxSnapshotViewerWidget w;
    w.Tumble(1.0f, 0.0f);
    EXPECT_FLOAT_EQ(w.orbitYaw, 1.0f);
}

TEST(VfxSnapshotViewer, TumbleClampsPitch) {
    VfxSnapshotViewerWidget w;
    w.Tumble(0.0f, 10.0f); // way beyond limit
    EXPECT_LE(w.orbitPitch, 1.5534f);
    w.Tumble(0.0f, -20.0f);
    EXPECT_GE(w.orbitPitch, -1.5534f);
}

TEST(VfxSnapshotViewer, DollyUpdatesDistance) {
    VfxSnapshotViewerWidget w;
    w.orbitDistance = 10.0f;
    w.Dolly(3.0f);
    EXPECT_FLOAT_EQ(w.orbitDistance, 7.0f);
}

TEST(VfxSnapshotViewer, DollyClampsToMinimum) {
    VfxSnapshotViewerWidget w;
    w.orbitDistance = 1.0f;
    w.Dolly(100.0f); // overshoot
    EXPECT_FLOAT_EQ(w.orbitDistance, 0.1f); // floor
}
