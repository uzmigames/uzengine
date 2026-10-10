/// Unit tests for phase 14.13.0: status-bar notification wiring.
///
/// The overlay's `PushStatus` + `TickStatus` forward directly to
/// the bound status bar. Since the overlay needs a GPU device for
/// a full `Init`, these tests exercise the contract by calling the
/// status-bar widget directly with the same sequence the overlay
/// emits internally — verifying the queue semantics the wiring
/// relies on.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_status_bar_widget.h"

using namespace uz;

TEST(VfxStatusBarWiring, PushThenTickPreservesOneEntryForThreeSeconds) {
    VfxStatusBarWidget bar;
    bar.PushNotification("Saved", VfxStatusLevel::Info);
    EXPECT_EQ(bar.QueueSize(), 1u);

    // Half a second — entry still present.
    bar.Tick(0.5f);
    EXPECT_EQ(bar.QueueSize(), 1u);

    // After 3.01 s total — dismissed.
    bar.Tick(2.51f);
    EXPECT_EQ(bar.QueueSize(), 0u);
}

TEST(VfxStatusBarWiring, MultiplePushesQueueFifo) {
    VfxStatusBarWidget bar;
    bar.PushNotification("A", VfxStatusLevel::Info);
    bar.PushNotification("B", VfxStatusLevel::Warning);
    bar.PushNotification("C", VfxStatusLevel::Error);

    ASSERT_NE(bar.CurrentNotification(), nullptr);
    EXPECT_EQ(bar.CurrentNotification()->message, "A");
    EXPECT_EQ(bar.QueueSize(), 3u);
}

TEST(VfxStatusBarWiring, TickAdvancesToNextAfterDismiss) {
    VfxStatusBarWidget bar;
    bar.PushNotification("A", VfxStatusLevel::Info);
    bar.PushNotification("B", VfxStatusLevel::Info);

    bar.Tick(3.01f);  // dismisses "A"
    ASSERT_NE(bar.CurrentNotification(), nullptr);
    EXPECT_EQ(bar.CurrentNotification()->message, "B");
}

TEST(VfxStatusBarWiring, ToolbarActionsUseInfoLevel) {
    VfxStatusBarWidget bar;
    // Simulate overlay wiring — every toolbar button pushes Info.
    bar.PushNotification("Saved",              VfxStatusLevel::Info);
    bar.PushNotification("Restarted",          VfxStatusLevel::Info);
    bar.PushNotification("Playing",            VfxStatusLevel::Info);
    bar.PushNotification("Thumbnail captured", VfxStatusLevel::Info);

    EXPECT_EQ(bar.QueueSize(), 4u);
    // First in queue should be "Saved" (FIFO).
    ASSERT_NE(bar.CurrentNotification(), nullptr);
    EXPECT_EQ(bar.CurrentNotification()->message, "Saved");
    EXPECT_EQ(bar.CurrentNotification()->level, VfxStatusLevel::Info);
}

TEST(VfxStatusBarWiring, WarningOnMissingEmitterCapture) {
    VfxStatusBarWidget bar;
    // Toolbar wiring pushes Warning when the Thumb button cannot
    // capture (no bound emitter).
    bar.PushNotification("No emitter to capture",
                          VfxStatusLevel::Warning);
    ASSERT_NE(bar.CurrentNotification(), nullptr);
    EXPECT_EQ(bar.CurrentNotification()->level, VfxStatusLevel::Warning);
}

TEST(VfxStatusBarWiring, AcceleratorCommandPushesLabel) {
    VfxStatusBarWidget bar;
    // Accelerator-dispatched command pushes the item's label
    // verbatim — e.g. `Alt+F A` fires "Save As".
    bar.PushNotification("Save As", VfxStatusLevel::Info);
    ASSERT_NE(bar.CurrentNotification(), nullptr);
    EXPECT_EQ(bar.CurrentNotification()->message, "Save As");
}
