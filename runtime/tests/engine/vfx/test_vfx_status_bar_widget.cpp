/// Unit tests for phase 14.6.0: `VfxStatusBarWidget`.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_editor_theme.h"
#include "engine/vfx/editor/vfx_status_bar_widget.h"

using namespace uz;

TEST(VfxStatusBar, FpsChipTintMatchesThreshold) {
    const auto& c = VfxThemeColors();

    EXPECT_EQ(VfxStatusBarWidget::FpsTint(60.0f).x, c.stateSuccess.x);
    EXPECT_EQ(VfxStatusBarWidget::FpsTint(55.0f).x, c.stateSuccess.x);
    EXPECT_EQ(VfxStatusBarWidget::FpsTint(42.0f).x, c.stateWarning.x);
    EXPECT_EQ(VfxStatusBarWidget::FpsTint(30.0f).x, c.stateWarning.x);
    EXPECT_EQ(VfxStatusBarWidget::FpsTint(29.9f).x, c.stateError.x);
    EXPECT_EQ(VfxStatusBarWidget::FpsTint(10.0f).x, c.stateError.x);
}

TEST(VfxStatusBar, DirtyFlagRoundTrips) {
    VfxStatusBarWidget w;
    EXPECT_FALSE(w.IsDirty());
    w.SetDirty(true);
    EXPECT_TRUE(w.IsDirty());
    w.SetDirty(false);
    EXPECT_FALSE(w.IsDirty());
}

TEST(VfxStatusBar, NotificationQueueFifo) {
    VfxStatusBarWidget w;
    w.PushNotification("a", VfxStatusLevel::Info);
    w.PushNotification("b", VfxStatusLevel::Warning);
    w.PushNotification("c", VfxStatusLevel::Error);

    EXPECT_EQ(w.QueueSize(), 3u);
    ASSERT_NE(w.CurrentNotification(), nullptr);
    EXPECT_EQ(w.CurrentNotification()->message, "a");
}

TEST(VfxStatusBar, NotificationCapsAtEight) {
    VfxStatusBarWidget w;
    for (int i = 0; i < 20; ++i) {
        w.PushNotification("msg", VfxStatusLevel::Info);
    }
    EXPECT_EQ(w.QueueSize(), 8u);
}

TEST(VfxStatusBar, NotificationAutoDismissesAfter3Seconds) {
    VfxStatusBarWidget w;
    w.PushNotification("temporary", VfxStatusLevel::Info);
    EXPECT_EQ(w.QueueSize(), 1u);
    w.Tick(3.01f);
    EXPECT_EQ(w.QueueSize(), 0u);
}

TEST(VfxStatusBar, NotificationTintByLevel) {
    VfxStatusBarWidget w;
    const auto& c = VfxThemeColors();
    EXPECT_EQ(w.NotificationTint(VfxStatusLevel::Info).x,    c.stateInfo.x);
    EXPECT_EQ(w.NotificationTint(VfxStatusLevel::Warning).x, c.stateWarning.x);
    EXPECT_EQ(w.NotificationTint(VfxStatusLevel::Error).x,   c.stateError.x);
}

TEST(VfxStatusBar, ParticleCountStateRoundTrips) {
    VfxStatusBarWidget w;
    w.SetParticleCount(1000u, 8192u);
    EXPECT_EQ(w.AliveCount(), 1000u);
    EXPECT_EQ(w.MaxCount(), 8192u);
}

TEST(VfxStatusBar, VramStateRoundTrips) {
    VfxStatusBarWidget w;
    w.SetVramUsed(450.5f, 8192.0f);
    EXPECT_FLOAT_EQ(w.VramUsedMb(),   450.5f);
    EXPECT_FLOAT_EQ(w.VramTotalMb(), 8192.0f);
}
