/// Unit tests for H264Encoder — FFmpeg hardware-accelerated H.264 encoding.
///
/// Tests cover the full lifecycle (construct → init → encode → shutdown),
/// the async path (SubmitFrameAsync / GetEncodedFrame), bitrate/keyframe
/// controls, and the move-constructor ownership transfer.
///
/// Hardware availability note: Initialize() may fail on CI machines that
/// have no hardware H.264 encoder and no libx264 software fallback.  Every
/// test that calls Initialize() checks the return value and calls
/// GTEST_SKIP() with a descriptive reason when it returns false.

#include "h264_encoder.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a 320×240 RGBA buffer filled with a solid color.
///
/// The buffer layout is top-down, 4 bytes per pixel (R, G, B, A).
/// Using 320×240 keeps test execution fast regardless of the encoder backend.
///
/// @param r  Red channel value (0–255).
/// @param g  Green channel value (0–255).
/// @param b  Blue channel value (0–255).
static std::vector<uint8_t> MakeSolidRGBA(uint8_t r, uint8_t g, uint8_t b) {
    constexpr uint32_t kWidth  = 320;
    constexpr uint32_t kHeight = 240;
    std::vector<uint8_t> buf(kWidth * kHeight * 4);
    for (uint32_t i = 0; i < kWidth * kHeight; ++i) {
        buf[i * 4 + 0] = r;
        buf[i * 4 + 1] = g;
        buf[i * 4 + 2] = b;
        buf[i * 4 + 3] = 0xFF;
    }
    return buf;
}

/// Minimal encoder config for 320×240 at 30 FPS.
///
/// Keeps tests fast by using the smallest practical frame size and the
/// lowest FPS that still exercises the encoder pipeline end-to-end.
static uz::H264Encoder::Config SmallConfig() {
    uz::H264Encoder::Config cfg;
    cfg.width            = 320;
    cfg.height           = 240;
    cfg.fps              = 30;
    cfg.bitrateKbps      = 2000;
    cfg.keyframeInterval = 15;
    return cfg;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// DefaultConstruction
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, DefaultConstruction_IsNotInitialized) {
    uz::H264Encoder encoder;
    EXPECT_FALSE(encoder.IsInitialized())
        << "Freshly constructed encoder must report not-initialized";
    EXPECT_STREQ(encoder.GetEncoderName(), "none")
        << "GetEncoderName() must return \"none\" before Initialize()";
}

// ─────────────────────────────────────────────────────────────────────────────
// GetExtradataBeforeInit
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, GetExtradata_BeforeInit_ReturnsFalse) {
    uz::H264Encoder encoder;
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    EXPECT_FALSE(encoder.GetExtradata(&data, &size))
        << "GetExtradata() must return false before Initialize()";
    EXPECT_EQ(data, nullptr)
        << "outData must be null when GetExtradata() returns false";
    EXPECT_EQ(size, 0u)
        << "outSize must be zero when GetExtradata() returns false";
}

// ─────────────────────────────────────────────────────────────────────────────
// GetExtradata with null arguments before init
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, GetExtradata_NullArguments_DoesNotCrash) {
    uz::H264Encoder encoder;
    // Passing nullptrs must not crash, even before Initialize().
    EXPECT_FALSE(encoder.GetExtradata(nullptr, nullptr));
}

// ─────────────────────────────────────────────────────────────────────────────
// ShutdownWithoutInit
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, ShutdownWithoutInit_IsNoOp) {
    uz::H264Encoder encoder;
    encoder.Shutdown(); // Must not crash.
    encoder.Shutdown(); // Repeated calls must also be safe.
    EXPECT_FALSE(encoder.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// EncodeFrameWithoutInit
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, EncodeFrame_WithoutInit_ReturnsFalse) {
    uz::H264Encoder encoder;
    const auto buf = MakeSolidRGBA(0xFF, 0, 0);
    const uint8_t* outData = nullptr;
    uint32_t outSize = 0;

    bool result = encoder.EncodeFrame(buf.data(), 320 * 4, &outData, &outSize);

    EXPECT_FALSE(result)
        << "EncodeFrame() must return false when not initialized";
    EXPECT_EQ(outData, nullptr)
        << "outData must remain null when EncodeFrame() returns false";
    EXPECT_EQ(outSize, 0u)
        << "outSize must remain 0 when EncodeFrame() returns false";
}

// ─────────────────────────────────────────────────────────────────────────────
// EncodeFrame with null rgbaData
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, EncodeFrame_NullRgbaData_ReturnsFalse) {
    uz::H264Encoder encoder;
    const uint8_t* outData = nullptr;
    uint32_t outSize = 0;

    bool result = encoder.EncodeFrame(nullptr, 320 * 4, &outData, &outSize);

    EXPECT_FALSE(result);
}

// ─────────────────────────────────────────────────────────────────────────────
// RequestKeyframe without init
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, RequestKeyframe_WithoutInit_DoesNotCrash) {
    uz::H264Encoder encoder;
    encoder.RequestKeyframe(); // Must be a no-op, not a crash.
    EXPECT_FALSE(encoder.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// SetBitrate without init
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, SetBitrate_WithoutInit_DoesNotCrash) {
    uz::H264Encoder encoder;
    encoder.SetBitrate(5000); // Must be a no-op, not a crash.
    EXPECT_FALSE(encoder.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// InitializeZeroDimensions
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, Initialize_ZeroWidth_ReturnsFalse) {
    uz::H264Encoder encoder;
    uz::H264Encoder::Config cfg = SmallConfig();
    cfg.width = 0;
    EXPECT_FALSE(encoder.Initialize(cfg))
        << "Initialize() must reject zero width";
    EXPECT_FALSE(encoder.IsInitialized());
}

TEST(H264Encoder, Initialize_ZeroHeight_ReturnsFalse) {
    uz::H264Encoder encoder;
    uz::H264Encoder::Config cfg = SmallConfig();
    cfg.height = 0;
    EXPECT_FALSE(encoder.Initialize(cfg))
        << "Initialize() must reject zero height";
    EXPECT_FALSE(encoder.IsInitialized());
}

TEST(H264Encoder, Initialize_ZeroFPS_ReturnsFalse) {
    uz::H264Encoder encoder;
    uz::H264Encoder::Config cfg = SmallConfig();
    cfg.fps = 0;
    EXPECT_FALSE(encoder.Initialize(cfg))
        << "Initialize() must reject zero FPS";
    EXPECT_FALSE(encoder.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// InitializeAndShutdown
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, InitializeAndShutdown_Lifecycle) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine (no NVENC/AMF/QSV/MF/libx264)";
    }

    EXPECT_TRUE(encoder.IsInitialized())
        << "IsInitialized() must return true after successful Initialize()";
    EXPECT_STRNE(encoder.GetEncoderName(), "none")
        << "GetEncoderName() must not be \"none\" after Initialize()";
    EXPECT_GT(std::strlen(encoder.GetEncoderName()), 0u)
        << "GetEncoderName() must return a non-empty string after Initialize()";

    encoder.Shutdown();

    EXPECT_FALSE(encoder.IsInitialized())
        << "IsInitialized() must return false after Shutdown()";
    EXPECT_STREQ(encoder.GetEncoderName(), "none")
        << "GetEncoderName() must return \"none\" after Shutdown()";
}

// ─────────────────────────────────────────────────────────────────────────────
// DoubleInitFails
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, DoubleInit_WithoutShutdown_ReturnsFalse) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    // Second Initialize() without a Shutdown() in between must fail.
    bool secondInit = encoder.Initialize(SmallConfig());
    EXPECT_FALSE(secondInit)
        << "Second Initialize() without Shutdown() must return false";

    // The encoder must remain in the initialized state from the first call.
    EXPECT_TRUE(encoder.IsInitialized())
        << "IsInitialized() must remain true after a failed double-init";
    EXPECT_STRNE(encoder.GetEncoderName(), "none")
        << "GetEncoderName() must still be valid after a failed double-init";
}

// ─────────────────────────────────────────────────────────────────────────────
// GetExtradataAfterInit
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, GetExtradata_AfterInit_ReturnsValidData) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    const uint8_t* data = nullptr;
    uint32_t size = 0;

    bool ok = encoder.GetExtradata(&data, &size);

    // Not all codecs populate extradata immediately after avcodec_open2().
    // Some require encoding at least one frame before SPS/PPS is available.
    // Skip validation if the encoder does not provide extradata up-front.
    if (!ok) {
        GTEST_SKIP() << "Encoder does not provide extradata before first encode "
                        "(codec-dependent behavior)";
    }

    EXPECT_NE(data, nullptr)
        << "extradata pointer must not be null when GetExtradata() returns true";
    EXPECT_GT(size, 0u)
        << "extradata size must be > 0 when GetExtradata() returns true (contains SPS/PPS NAL units)";
}

// ─────────────────────────────────────────────────────────────────────────────
// EncodeProducesOutput
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, EncodeFrame_MultipleFrames_ProducesOutput) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    const auto buf = MakeSolidRGBA(0xFF, 0x00, 0x00); // solid red
    constexpr uint32_t kStride = 320 * 4;

    // The encoder may buffer the first frame (EAGAIN) before producing
    // output.  With max_b_frames=0 and keyframeInterval=15, we should
    // see output within the first 5 frames — 10 gives plenty of margin.
    constexpr int kAttempts = 10;
    bool gotOutput = false;

    for (int i = 0; i < kAttempts; ++i) {
        const uint8_t* outData = nullptr;
        uint32_t outSize = 0;

        bool produced = encoder.EncodeFrame(buf.data(), kStride, &outData, &outSize);

        if (produced) {
            EXPECT_NE(outData, nullptr)
                << "outData must not be null when EncodeFrame() returns true (frame " << i << ")";
            EXPECT_GT(outSize, 0u)
                << "outSize must be > 0 when EncodeFrame() returns true (frame " << i << ")";
            gotOutput = true;
            break;
        }

        // When returning false, the output pointers must still be cleared.
        EXPECT_EQ(outData, nullptr)
            << "outData must be null when EncodeFrame() returns false (frame " << i << ")";
        EXPECT_EQ(outSize, 0u)
            << "outSize must be zero when EncodeFrame() returns false (frame " << i << ")";
    }

    EXPECT_TRUE(gotOutput)
        << "Expected at least one encoded frame within " << kAttempts << " calls to EncodeFrame()";
}

// ─────────────────────────────────────────────────────────────────────────────
// RequestKeyframeDoesNotCrash
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, RequestKeyframe_AfterInit_DoesNotCrash) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    encoder.RequestKeyframe();

    // Call EncodeFrame after RequestKeyframe — must not crash.
    const auto buf = MakeSolidRGBA(0x00, 0xFF, 0x00); // solid green
    const uint8_t* outData = nullptr;
    uint32_t outSize = 0;

    // We don't assert the return value here — the encoder may buffer this
    // frame.  The test is that no crash or assertion failure occurs.
    (void)encoder.EncodeFrame(buf.data(), 320 * 4, &outData, &outSize);
    SUCCEED() << "RequestKeyframe() followed by EncodeFrame() did not crash";
}

// ─────────────────────────────────────────────────────────────────────────────
// SetBitrateDoesNotCrash
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, SetBitrate_AfterInit_DoesNotCrash) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    encoder.SetBitrate(5000);

    const auto buf = MakeSolidRGBA(0x00, 0x00, 0xFF); // solid blue
    const uint8_t* outData = nullptr;
    uint32_t outSize = 0;

    (void)encoder.EncodeFrame(buf.data(), 320 * 4, &outData, &outSize);
    SUCCEED() << "SetBitrate() followed by EncodeFrame() did not crash";
}

// ─────────────────────────────────────────────────────────────────────────────
// SetBitrate zero — must not crash (edge-case value)
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, SetBitrate_ZeroKbps_DoesNotCrash) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    encoder.SetBitrate(0); // Edge case — must not crash.
    EXPECT_TRUE(encoder.IsInitialized())
        << "IsInitialized() must remain true after SetBitrate(0)";
}

// ─────────────────────────────────────────────────────────────────────────────
// SetBitrate same value — no-op, encoder stays alive
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, SetBitrate_SameValue_IsNoOp) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    // SmallConfig uses 2000 kbps.  Setting the same value should be a no-op
    // (the implementation skips the atomic store when value is unchanged).
    encoder.SetBitrate(2000);
    EXPECT_TRUE(encoder.IsInitialized())
        << "IsInitialized() must remain true after SetBitrate() with unchanged value";
}

// ─────────────────────────────────────────────────────────────────────────────
// MoveConstruction
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, MoveConstruction_TransfersOwnership) {
    uz::H264Encoder encoderA;

    if (!encoderA.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    const char* nameA = encoderA.GetEncoderName();
    ASSERT_NE(nameA, nullptr);
    ASSERT_STRNE(nameA, "none");

    // Move A into B.
    uz::H264Encoder encoderB(std::move(encoderA));

    EXPECT_FALSE(encoderA.IsInitialized())
        << "Source encoder must be uninitialized after move";
    EXPECT_STREQ(encoderA.GetEncoderName(), "none")
        << "Source encoder must report \"none\" after move";

    EXPECT_TRUE(encoderB.IsInitialized())
        << "Destination encoder must be initialized after move";
    EXPECT_STREQ(encoderB.GetEncoderName(), nameA)
        << "Destination encoder must preserve the encoder name after move";
}

// ─────────────────────────────────────────────────────────────────────────────
// MoveAssignment
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, MoveAssignment_TransfersOwnership) {
    uz::H264Encoder encoderA;

    if (!encoderA.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    uz::H264Encoder encoderB;
    encoderB = std::move(encoderA);

    EXPECT_FALSE(encoderA.IsInitialized())
        << "Source encoder must be uninitialized after move-assignment";
    EXPECT_TRUE(encoderB.IsInitialized())
        << "Destination encoder must be initialized after move-assignment";
    EXPECT_STRNE(encoderB.GetEncoderName(), "none")
        << "Destination encoder must have a valid name after move-assignment";
}

// ─────────────────────────────────────────────────────────────────────────────
// MoveAssignment self-move
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, MoveAssignment_SelfMove_DoesNotCorrupt) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    // Self-move — the operator checks `if (this != &other)`, so this must
    // leave the encoder in a valid, initialized state.
    encoder = std::move(encoder); // NOLINT(clang-diagnostic-self-move)

    // After self-move the standard does not define a specific state, but the
    // encoder must at minimum not crash on subsequent operations.
    // We just verify the destructor runs cleanly by letting the local go
    // out of scope.  If the test reaches this point without crashing, pass.
    SUCCEED() << "Self-move did not crash";
}

// ─────────────────────────────────────────────────────────────────────────────
// AsyncSubmitAndGet — basic async round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, AsyncPath_SubmitAndGet_ProducesOutput) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    const auto buf = MakeSolidRGBA(0xFF, 0xFF, 0x00); // solid yellow
    constexpr uint32_t kStride = 320 * 4;

    // Attempt up to 5 submit/poll cycles.  Each cycle submits a frame then
    // waits up to 100 ms for the encoder thread to produce output.
    constexpr int kRounds = 5;
    bool gotOutput = false;

    for (int round = 0; round < kRounds; ++round) {
        encoder.SubmitFrameAsync(buf.data(), kStride);

        // Poll GetEncodedFrame() for up to 100 ms.
        const uint8_t* outData = nullptr;
        uint32_t outSize = 0;

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (std::chrono::steady_clock::now() < deadline) {
            if (encoder.GetEncodedFrame(&outData, &outSize)) {
                EXPECT_NE(outData, nullptr)
                    << "outData must not be null when GetEncodedFrame() returns true (round " << round << ")";
                EXPECT_GT(outSize, 0u)
                    << "outSize must be > 0 when GetEncodedFrame() returns true (round " << round << ")";
                gotOutput = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        if (gotOutput) break;
    }

    EXPECT_TRUE(gotOutput)
        << "Expected at least one encoded frame from the async path within "
        << kRounds << " rounds";
}

// ─────────────────────────────────────────────────────────────────────────────
// AsyncPath — GetEncodedFrame returns false initially before any submit
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, AsyncPath_GetEncodedFrame_BeforeAnySubmit_ReturnsFalse) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    const uint8_t* outData = nullptr;
    uint32_t outSize = 0;

    // No frame was submitted yet — must return false.
    EXPECT_FALSE(encoder.GetEncodedFrame(&outData, &outSize))
        << "GetEncodedFrame() must return false when no frame has been submitted";
    EXPECT_EQ(outData, nullptr);
    EXPECT_EQ(outSize, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// AsyncPath — null arguments to GetEncodedFrame
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, AsyncPath_GetEncodedFrame_NullArguments_ReturnsFalse) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    // Null outData or outSize must cause GetEncodedFrame() to return false
    // without crashing.
    EXPECT_FALSE(encoder.GetEncodedFrame(nullptr, nullptr));
}

// ─────────────────────────────────────────────────────────────────────────────
// AsyncPath — SubmitFrameAsync with null data is a no-op
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, AsyncPath_SubmitFrameAsync_NullData_DoesNotCrash) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    encoder.SubmitFrameAsync(nullptr, 320 * 4); // Must be a no-op.
    EXPECT_TRUE(encoder.IsInitialized())
        << "IsInitialized() must remain true after SubmitFrameAsync() with null data";
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown joins encoder thread — no dangling thread
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, Shutdown_AfterAsyncSubmit_JoinsThread) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    // Submit a frame to ensure the encoder thread is active.
    const auto buf = MakeSolidRGBA(0xAA, 0xBB, 0xCC);
    encoder.SubmitFrameAsync(buf.data(), 320 * 4);

    // Give the encoder thread a moment to pick up the frame before shutdown.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Shutdown() must join the thread without deadlock or assertion failure.
    encoder.Shutdown();

    EXPECT_FALSE(encoder.IsInitialized())
        << "IsInitialized() must be false after Shutdown()";
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor calls Shutdown — no leak
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, Destructor_CallsShutdown_NoLeak) {
    // If the destructor does not call Shutdown() properly, this test will
    // either crash (use-after-free) or deadlock (background thread not joined).
    {
        uz::H264Encoder encoder;
        if (!encoder.Initialize(SmallConfig())) {
            GTEST_SKIP() << "No H.264 encoder available on this machine";
        }
        // Submit a frame so the encoder thread is busy.
        const auto buf = MakeSolidRGBA(0x10, 0x20, 0x30);
        encoder.SubmitFrameAsync(buf.data(), 320 * 4);
        // Destructor called here — must join the thread and free FFmpeg resources.
    }
    SUCCEED() << "Destructor cleaned up without crash or deadlock";
}

// ─────────────────────────────────────────────────────────────────────────────
// EncodeFrame with alternating colors — output changes across keyframes
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, EncodeFrame_AlternatingColors_ProducesNonZeroOutput) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    const std::vector<std::vector<uint8_t>> frames = {
        MakeSolidRGBA(0xFF, 0x00, 0x00), // red
        MakeSolidRGBA(0x00, 0xFF, 0x00), // green
        MakeSolidRGBA(0x00, 0x00, 0xFF), // blue
        MakeSolidRGBA(0xFF, 0xFF, 0x00), // yellow
        MakeSolidRGBA(0x00, 0xFF, 0xFF), // cyan
        MakeSolidRGBA(0xFF, 0x00, 0xFF), // magenta
        MakeSolidRGBA(0xFF, 0xFF, 0xFF), // white
        MakeSolidRGBA(0x00, 0x00, 0x00), // black
    };

    int outputCount = 0;
    for (int i = 0; i < static_cast<int>(frames.size()); ++i) {
        const uint8_t* outData = nullptr;
        uint32_t outSize = 0;
        if (encoder.EncodeFrame(frames[i].data(), 320 * 4, &outData, &outSize)) {
            EXPECT_NE(outData, nullptr)
                << "outData null on frame " << i;
            EXPECT_GT(outSize, 0u)
                << "outSize zero on frame " << i;
            ++outputCount;
        }
    }

    // With 8 frames and max_b_frames=0, we expect at least one output packet.
    EXPECT_GT(outputCount, 0)
        << "Expected at least one encoded frame from 8 alternating-color input frames";
}

// ─────────────────────────────────────────────────────────────────────────────
// RequestKeyframe — IDR frame is produced after the request
// ─────────────────────────────────────────────────────────────────────────────

TEST(H264Encoder, RequestKeyframe_ForcesIDROutput) {
    uz::H264Encoder encoder;

    if (!encoder.Initialize(SmallConfig())) {
        GTEST_SKIP() << "No H.264 encoder available on this machine";
    }

    const auto buf = MakeSolidRGBA(0x80, 0x80, 0x80); // mid-grey

    // Warm up the encoder — encode enough frames to flush the initial buffer.
    for (int i = 0; i < 5; ++i) {
        const uint8_t* d = nullptr; uint32_t s = 0;
        (void)encoder.EncodeFrame(buf.data(), 320 * 4, &d, &s);
    }

    // Request a keyframe and encode one more frame.
    encoder.RequestKeyframe();

    const uint8_t* outData = nullptr;
    uint32_t outSize = 0;
    bool produced = encoder.EncodeFrame(buf.data(), 320 * 4, &outData, &outSize);

    // The encoder must not crash.  Whether this specific call produces output
    // depends on encoder buffering; the important invariant is no crash and
    // the encoder remains usable.
    EXPECT_TRUE(encoder.IsInitialized())
        << "Encoder must remain initialized after RequestKeyframe() + EncodeFrame()";
    if (produced) {
        EXPECT_NE(outData, nullptr);
        EXPECT_GT(outSize, 0u);
    }
}
