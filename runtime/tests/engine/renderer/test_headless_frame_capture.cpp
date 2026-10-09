/// Unit tests for phase 10.9.0 — headless frame capture.
///
/// The full GPU path (RTV allocation, render callback, readback) requires a
/// live RHI device and is exercised by the thumbnail pipeline smoke test
/// when phase 9.7 lands. These tests cover the pre-Init contract + pixel
/// layout invariants that need to hold regardless of backend.
///
/// @ref engine/renderer/headless/headless_frame_capture.h

#include <gtest/gtest.h>

#include "engine/renderer/headless/headless_frame_capture.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Uninitialised-state contract
// ─────────────────────────────────────────────────────────────────────────────

TEST(HeadlessFrameCapture, DefaultIsUninitialised) {
    HeadlessFrameCapture cap;
    EXPECT_FALSE(cap.IsInitialized());
    EXPECT_EQ(cap.Width(),  0u);
    EXPECT_EQ(cap.Height(), 0u);
    EXPECT_EQ(cap.RowPitch(), 0u);
}

TEST(HeadlessFrameCapture, ReadPixelsUninitialisedReturnsEmpty) {
    HeadlessFrameCapture cap;
    EXPECT_TRUE(cap.ReadPixels().empty());
    EXPECT_TRUE(cap.ReadPNG().empty());
    EXPECT_FALSE(cap.SaveToPNG("out.png"));
}

TEST(HeadlessFrameCapture, InitWithNullDeviceRejected) {
    HeadlessFrameCapture cap;
    EXPECT_FALSE(cap.Init(nullptr, 128, 128));
    EXPECT_FALSE(cap.IsInitialized());
}

TEST(HeadlessFrameCapture, InitWithZeroSizeRejected) {
    HeadlessFrameCapture cap;
    // Non-null but dummy device pointer — Init should still fail the
    // zero-size check before hitting the device.
    auto* dummy = reinterpret_cast<IRHIDevice*>(intptr_t{1});
    EXPECT_FALSE(cap.Init(dummy, 0, 128));
    EXPECT_FALSE(cap.Init(dummy, 128, 0));
}

TEST(HeadlessFrameCapture, InitWithUnsupportedFormatRejected) {
    HeadlessFrameCapture cap;
    auto* dummy = reinterpret_cast<IRHIDevice*>(intptr_t{1});
    // R8Unorm is not in the supported format list (BytesPerPixel → 0).
    EXPECT_FALSE(cap.Init(dummy, 128, 128, RHIFormat::R8Unorm));
}

TEST(HeadlessFrameCapture, BeginEndOnUninitialisedAreSafe) {
    HeadlessFrameCapture cap;
    // Passing a null command list to an uninitialised capturer must be a
    // safe no-op — the capturer is expected to handle both cases without
    // touching its state.
    cap.BeginFrame(nullptr);
    cap.EndFrame(nullptr);
    EXPECT_FALSE(cap.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// Row pitch alignment
// ─────────────────────────────────────────────────────────────────────────────

/// Tests the math the capturer applies internally: row pitch is rounded
/// up to a 256-byte multiple (DX12 constraint). Mirror the AlignUp helper
/// here since it's private to the cpp — test matches the code contract.
namespace {
uint32_t AlignUp256(uint32_t v) { return (v + 255u) & ~255u; }
}

TEST(HeadlessFrameCaptureAlignment, RowPitchAlignedTo256) {
    // 128px × 4bpp = 512 → already 256-aligned → stays 512.
    EXPECT_EQ(AlignUp256(128u * 4u), 512u);
    // 130px × 4bpp = 520 → rounds up to 768.
    EXPECT_EQ(AlignUp256(130u * 4u), 768u);
    // 64px × 4bpp = 256 → already aligned.
    EXPECT_EQ(AlignUp256(64u * 4u), 256u);
    // 1px × 4bpp = 4 → rounds up to 256.
    EXPECT_EQ(AlignUp256(4u), 256u);
}

TEST(HeadlessFrameCaptureAlignment, SquareThumbnailSizeBudget) {
    // 128×128 RGBA8 thumbnail: rowPitch 512, total = 512 × 128 = 65 KiB.
    EXPECT_EQ(AlignUp256(128u * 4u) * 128u, 65536u);
    // 256×256 → 1 MiB.
    EXPECT_EQ(AlignUp256(256u * 4u) * 256u, 262144u);
}
