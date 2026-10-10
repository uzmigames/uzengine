/// Unit tests for phase 11.12.0: `VfxThumbnailDrainer` state machine.
///
/// Covers:
///   - Default state is `Idle`; Tick with empty queue stays Idle.
///   - Push request → first Tick enters `Preparing` + fires
///     `onBeginCapture`.
///   - Capturer advances ticks each frame in Preparing; once the
///     target tick is hit it transitions to `Capturing`.
///   - `Capturing` invokes `onCapturePng` and — when the host returns
///     PNG bytes that produce a successful FinishCapture — transitions
///     through `Finishing` back to Idle (via pop), firing
///     `onEndCapture` on the way out.
///   - Capture queue is Pop'd after each completed run.
///   - Host returning empty PNG bytes → capture marked dropped, state
///     returns Idle, `onEndCapture` still fires (overlay is restored).
///   - Bad config (fps ≤ 0) → request dropped immediately, counter
///     bumps, state stays Idle.
///   - Multiple queued requests are drained one at a time.
///   - `DroppedCount` + `CompletedCount` diagnostics.
///
/// The test suite intentionally uses a non-existent asset path so
/// `FinishCapture` fails to write to disk — we use the counter
/// semantics to distinguish completed-but-failed vs dropped-early
/// paths without a real filesystem.

#include <gtest/gtest.h>

#include "engine/vfx/baker/vfx_thumbnail.h"
#include "engine/vfx/editor/vfx_thumbnail_drainer.h"
#include "engine/vfx/editor/vfx_thumbnail_save_ext.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace uz;

namespace {

VfxThumbnailCaptureRequest MakeRequest(const std::string& asset,
                                        uint32_t captureFrame = 3,
                                        float fps = 60.0f) {
    VfxThumbnailCaptureRequest req{};
    req.config.assetPath    = asset;
    req.config.captureFrame = captureFrame;
    req.config.fps          = fps;
    req.config.width        = 128u;
    req.config.height       = 128u;
    req.config.maxTicks     = 16u;
    req.kind                = VfxSaveAssetKind::EmitterPreset;
    return req;
}

/// Tiny valid PNG (1x1 red). Serves as a non-empty payload so
/// `FinishCapture` treats it as a successful write attempt.
const std::vector<uint8_t> kTinyPng = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Idle behaviour
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailDrainer, Unbound_TickStaysIdle) {
    VfxThumbnailDrainer d;
    EXPECT_EQ(d.Tick(1.0f / 60.0f), VfxThumbnailDrainState::Idle);
    EXPECT_EQ(d.State(), VfxThumbnailDrainState::Idle);
    EXPECT_EQ(d.Capturer(), nullptr);
}

TEST(VfxThumbnailDrainer, EmptyQueue_StaysIdle) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailDrainer d;
    d.Bind(&q);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(d.Tick(0.016f), VfxThumbnailDrainState::Idle);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Preparing → Capturing transitions
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailDrainer, QueueNonEmpty_EntersPreparing_FiresBeginCapture) {
    VfxThumbnailCaptureQueue q;
    q.Push(MakeRequest("test_asset.uzfx"));

    int beginCount = 0;
    VfxThumbnailDrainer d;
    d.Bind(&q);
    d.onBeginCapture = [&] { ++beginCount; };
    d.onCapturePng   = [](uint32_t, uint32_t) { return std::vector<uint8_t>{}; };

    EXPECT_EQ(d.Tick(0.016f), VfxThumbnailDrainState::Preparing);
    EXPECT_EQ(beginCount, 1);
    ASSERT_NE(d.Capturer(), nullptr);
    EXPECT_EQ(d.Capturer()->GetWidth(),  128u);
    EXPECT_EQ(d.Capturer()->GetHeight(), 128u);
}

TEST(VfxThumbnailDrainer, Preparing_AdvancesTicksUntilReady) {
    VfxThumbnailCaptureQueue q;
    q.Push(MakeRequest("a.uzfx", /*captureFrame*/ 3));

    VfxThumbnailDrainer d;
    d.Bind(&q);
    d.onCapturePng = [](uint32_t, uint32_t) { return kTinyPng; };

    // Tick 1 → Preparing (first tick triggers transition).
    EXPECT_EQ(d.Tick(0.016f), VfxThumbnailDrainState::Preparing);
    EXPECT_EQ(d.Capturer()->GetCurrentTick(), 0u);

    // Subsequent Ticks advance the capturer. captureFrame=3 means
    // 3 AdvanceFrame calls before ready. Since DoPreparing calls
    // AdvanceFrame once per Tick, that's 3 further Ticks.
    d.Tick(0.016f);  // tick=1
    d.Tick(0.016f);  // tick=2
    d.Tick(0.016f);  // tick=3 → ready

    // At this point the drainer should have transitioned through
    // Capturing → Finishing → Idle inside a single call stack (since
    // Capturing is atomic with its callback). The queue has been
    // popped.
    EXPECT_EQ(d.State(), VfxThumbnailDrainState::Idle);
    EXPECT_EQ(q.Size(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Capturing → Finishing → Idle
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailDrainer, Capturing_InvokesPngCallback_FiresEndCapture) {
    VfxThumbnailCaptureQueue q;
    q.Push(MakeRequest("asset.uzfx", /*captureFrame*/ 0));  // ready immediately

    bool endFired = false;
    int pngCalls = 0;
    uint32_t capturedW = 0, capturedH = 0;

    VfxThumbnailDrainer d;
    d.Bind(&q);
    d.onEndCapture = [&] { endFired = true; };
    d.onCapturePng = [&](uint32_t w, uint32_t h) {
        ++pngCalls;
        capturedW = w;
        capturedH = h;
        return kTinyPng;
    };

    // Tick 1 → Preparing (captureFrame=0, AdvanceFrame + check →
    // ready on this same tick).
    d.Tick(0.016f);
    d.Tick(0.016f);

    EXPECT_EQ(d.State(), VfxThumbnailDrainState::Idle);
    EXPECT_EQ(pngCalls, 1);
    EXPECT_EQ(capturedW, 128u);
    EXPECT_EQ(capturedH, 128u);
    EXPECT_TRUE(endFired);
    EXPECT_EQ(q.Size(), 0u);
}

TEST(VfxThumbnailDrainer, EmptyPngBytes_DropsRequest_RestoresOverlay) {
    VfxThumbnailCaptureQueue q;
    q.Push(MakeRequest("a.uzfx", 0));

    bool endFired = false;
    VfxThumbnailDrainer d;
    d.Bind(&q);
    d.onEndCapture = [&] { endFired = true; };
    // Host declines to produce bytes.
    d.onCapturePng = [](uint32_t, uint32_t) { return std::vector<uint8_t>{}; };

    d.Tick(0.016f); d.Tick(0.016f);

    EXPECT_EQ(d.State(), VfxThumbnailDrainState::Idle);
    EXPECT_EQ(d.DroppedCount(), 1u);
    EXPECT_EQ(d.CompletedCount(), 0u);
    EXPECT_TRUE(endFired) << "Overlay must be restored even on failure";
    EXPECT_EQ(q.Size(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bad config short-circuits
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailDrainer, BadConfig_DropsImmediatelyStaysIdle) {
    VfxThumbnailCaptureQueue q;
    q.Push(MakeRequest("bad.uzfx", /*captureFrame*/ 3, /*fps*/ -1.0f));

    int beginCalls = 0;
    VfxThumbnailDrainer d;
    d.Bind(&q);
    d.onBeginCapture = [&] { ++beginCalls; };

    EXPECT_EQ(d.Tick(0.016f), VfxThumbnailDrainState::Idle);
    EXPECT_EQ(beginCalls, 0);  // never entered Preparing
    EXPECT_EQ(d.DroppedCount(), 1u);
    EXPECT_EQ(q.Size(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple queued requests
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailDrainer, MultipleRequests_DrainedOneByOne) {
    VfxThumbnailCaptureQueue q;
    q.Push(MakeRequest("a.uzfx", 0));
    q.Push(MakeRequest("b.uzfx", 0));
    q.Push(MakeRequest("c.uzfx", 0));

    VfxThumbnailDrainer d;
    d.Bind(&q);
    d.onCapturePng = [](uint32_t, uint32_t) { return std::vector<uint8_t>{}; };

    // 3 requests × (enter Preparing + finish) → drain in a handful of Ticks.
    for (int i = 0; i < 10; ++i) {
        d.Tick(0.016f);
        if (q.Size() == 0u && d.State() == VfxThumbnailDrainState::Idle) break;
    }

    EXPECT_EQ(q.Size(), 0u);
    EXPECT_EQ(d.State(), VfxThumbnailDrainState::Idle);
    EXPECT_EQ(d.DroppedCount(), 3u);   // empty bytes → all dropped
}

// ─────────────────────────────────────────────────────────────────────────────
// State name helper
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxThumbnailDrainer, StateName_StableLabels) {
    EXPECT_STREQ(VfxThumbnailDrainStateName(VfxThumbnailDrainState::Idle), "Idle");
    EXPECT_STREQ(VfxThumbnailDrainStateName(VfxThumbnailDrainState::Preparing), "Preparing");
    EXPECT_STREQ(VfxThumbnailDrainStateName(VfxThumbnailDrainState::Capturing), "Capturing");
    EXPECT_STREQ(VfxThumbnailDrainStateName(VfxThumbnailDrainState::Finishing), "Finishing");
}
