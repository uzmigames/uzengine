/// Unit tests for FrameReadback — triple-buffered GPU-to-CPU frame capture.
///
/// FrameReadback wraps 3 RHI readback buffers + 3 fences.  Full round-trip
/// tests (CaptureFrame → SignalFence → MapRead) require a real GPU device and
/// a running swapchain, so those are covered by the C-API integration tests in
/// test_runtime_api.cpp.  This file focuses on the object lifecycle (construct,
/// Init, Shutdown, dimension queries) which are fully testable without GPU
/// hardware by probing with null/invalid arguments and by verifying the
/// "pipeline filling" logic through IsReadReady().
///
/// Tests that call Init() with a real IRHIDevice require a viewport — see the
/// RuntimeViewportTest fixture in test_runtime_api.cpp for that pattern.

#include "frame_readback.h"

#include <gtest/gtest.h>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Constructs a FrameReadback and verifies the invariants of the
/// freshly-constructed (not-yet-initialized) state that can be checked
/// without any GPU device.
static void AssertDefaultState(const uz::FrameReadback& rb) {
    EXPECT_FALSE(rb.IsInitialized())
        << "FrameReadback must report not-initialized before Init()";
    EXPECT_EQ(rb.GetWidth(), 0u)
        << "GetWidth() must return 0 before Init()";
    EXPECT_EQ(rb.GetHeight(), 0u)
        << "GetHeight() must return 0 before Init()";
    EXPECT_FALSE(rb.IsReadReady())
        << "IsReadReady() must return false before Init()";
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// DefaultConstruction
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, DefaultConstruction_IsNotInitialized) {
    uz::FrameReadback rb;
    AssertDefaultState(rb);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShutdownWithoutInit
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, ShutdownWithoutInit_IsNoOp) {
    uz::FrameReadback rb;
    rb.Shutdown(); // First call — not initialized, must be a no-op.
    rb.Shutdown(); // Repeated call — still a no-op.
    AssertDefaultState(rb);
}

// ─────────────────────────────────────────────────────────────────────────────
// IsReadReady returns false before Init
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, IsReadReady_BeforeInit_ReturnsFalse) {
    uz::FrameReadback rb;
    EXPECT_FALSE(rb.IsReadReady())
        << "IsReadReady() must return false when not initialized";
}

// ─────────────────────────────────────────────────────────────────────────────
// MapRead returns false before Init
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, MapRead_BeforeInit_ReturnsFalse) {
    uz::FrameReadback rb;
    const uint8_t* data   = nullptr;
    uint32_t stride = 0;
    EXPECT_FALSE(rb.MapRead(&data, &stride))
        << "MapRead() must return false when not initialized";
    EXPECT_EQ(data,   nullptr) << "outData must remain null when MapRead() fails";
    EXPECT_EQ(stride, 0u)      << "outStride must remain 0 when MapRead() fails";
}

// ─────────────────────────────────────────────────────────────────────────────
// MapRead with null arguments
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, MapRead_NullArguments_ReturnsFalse) {
    uz::FrameReadback rb;
    // Null outData/outStride — must return false without crashing.
    EXPECT_FALSE(rb.MapRead(nullptr, nullptr));
}

// ─────────────────────────────────────────────────────────────────────────────
// UnmapRead before Init — no-op
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, UnmapRead_BeforeInit_DoesNotCrash) {
    uz::FrameReadback rb;
    rb.UnmapRead(); // Must be a no-op.
    SUCCEED() << "UnmapRead() before Init() did not crash";
}

// ─────────────────────────────────────────────────────────────────────────────
// AdvanceRead before Init — no-op
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, AdvanceRead_BeforeInit_DoesNotCrash) {
    uz::FrameReadback rb;
    rb.AdvanceRead(); // Must be a no-op.
    EXPECT_FALSE(rb.IsInitialized())
        << "IsInitialized() must remain false after AdvanceRead() on an uninit object";
}

// ─────────────────────────────────────────────────────────────────────────────
// SignalFence before Init — no-op
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, SignalFence_BeforeInit_DoesNotCrash) {
    uz::FrameReadback rb;
    rb.SignalFence(); // Must be a no-op (impl_ is null).
    SUCCEED() << "SignalFence() before Init() did not crash";
}

// ─────────────────────────────────────────────────────────────────────────────
// CaptureFrame before Init — no-op
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, CaptureFrame_BeforeInit_DoesNotCrash) {
    uz::FrameReadback rb;
    // Passing null cmdList and swapChain to an uninit FrameReadback must be
    // a safe no-op (both the impl_ null-check and the arg null-checks protect
    // against this).
    rb.CaptureFrame(nullptr, nullptr);
    SUCCEED() << "CaptureFrame(null,null) before Init() did not crash";
}

// ─────────────────────────────────────────────────────────────────────────────
// GetWidth / GetHeight before Init
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, GetWidth_BeforeInit_ReturnsZero) {
    uz::FrameReadback rb;
    EXPECT_EQ(rb.GetWidth(), 0u)
        << "GetWidth() must return 0 before Init()";
}

TEST(FrameReadback, GetHeight_BeforeInit_ReturnsZero) {
    uz::FrameReadback rb;
    EXPECT_EQ(rb.GetHeight(), 0u)
        << "GetHeight() must return 0 before Init()";
}

// ─────────────────────────────────────────────────────────────────────────────
// Init with null device — must fail gracefully
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, Init_NullDevice_ReturnsFalse) {
    uz::FrameReadback rb;
    bool ok = rb.Init(nullptr, 1280, 720);
    EXPECT_FALSE(ok)
        << "Init() must return false when device is null";
    EXPECT_FALSE(rb.IsInitialized())
        << "IsInitialized() must remain false after Init() with null device";
    EXPECT_EQ(rb.GetWidth(), 0u)
        << "GetWidth() must remain 0 after failed Init()";
    EXPECT_EQ(rb.GetHeight(), 0u)
        << "GetHeight() must remain 0 after failed Init()";
}

// ─────────────────────────────────────────────────────────────────────────────
// Init with zero width — must fail gracefully
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, Init_ZeroWidth_ReturnsFalse) {
    uz::FrameReadback rb;
    // IRHIDevice* non-null but width == 0.  The implementation rejects
    // this in the guard at the top of Init().
    // We use a non-null sentinel (reinterpret_cast) — Init checks the device
    // pointer value before calling any methods on it, so this is safe.
    auto* fakeDevice = reinterpret_cast<uz::IRHIDevice*>(static_cast<uintptr_t>(1));
    bool ok = rb.Init(fakeDevice, 0, 720);
    EXPECT_FALSE(ok)
        << "Init() must return false when width is 0";
    EXPECT_FALSE(rb.IsInitialized())
        << "IsInitialized() must remain false after Init() with zero width";
}

// ─────────────────────────────────────────────────────────────────────────────
// Init with zero height — must fail gracefully
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, Init_ZeroHeight_ReturnsFalse) {
    uz::FrameReadback rb;
    auto* fakeDevice = reinterpret_cast<uz::IRHIDevice*>(static_cast<uintptr_t>(1));
    bool ok = rb.Init(fakeDevice, 1280, 0);
    EXPECT_FALSE(ok)
        << "Init() must return false when height is 0";
    EXPECT_FALSE(rb.IsInitialized())
        << "IsInitialized() must remain false after Init() with zero height";
}

// ─────────────────────────────────────────────────────────────────────────────
// IsReadReady pipeline fill — requires exactly kBufferCount captures
//
// This test cannot run without a real GPU because it requires a valid swapchain
// and command list.  We assert the simpler rule: without ANY captures,
// IsReadReady() must always return false.
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, IsReadReady_WithoutCaptures_ReturnsFalse) {
    // The implementation checks `captureCount < kBufferCount` (3) before
    // checking any fence.  Since we never call CaptureFrame(), captureCount
    // stays at 0 and IsReadReady() must return false.
    uz::FrameReadback rb;
    EXPECT_FALSE(rb.IsReadReady())
        << "IsReadReady() must return false when no captures have been submitted";
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor cleans up — no leak when initialized
//
// We cannot call Init() here without a real GPU device.  This test simply
// verifies that a default-constructed FrameReadback destructs cleanly.
// The GPU-path destructor correctness is tested via RuntimeViewportTest.
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, Destructor_DefaultState_NoCrash) {
    {
        uz::FrameReadback rb;
        // Destructor is called here.
    }
    SUCCEED() << "FrameReadback destructor on default-constructed object did not crash";
}

// ─────────────────────────────────────────────────────────────────────────────
// Non-copyable and non-movable — compile-time check (verified by inspection)
//
// The header declares all four copy/move special members as `= delete`.
// This test documents that intent and fails at compile time if the delete
// is accidentally removed.  Uncomment the marked lines to verify the
// delete constraints produce a compile error.
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, NotCopyableNotMovable_StaticAssertion) {
    static_assert(!std::is_copy_constructible_v<uz::FrameReadback>,
                  "FrameReadback must not be copy-constructible");
    static_assert(!std::is_copy_assignable_v<uz::FrameReadback>,
                  "FrameReadback must not be copy-assignable");
    static_assert(!std::is_move_constructible_v<uz::FrameReadback>,
                  "FrameReadback must not be move-constructible");
    static_assert(!std::is_move_assignable_v<uz::FrameReadback>,
                  "FrameReadback must not be move-assignable");
    SUCCEED() << "FrameReadback static copy/move assertions passed";
}

// ─────────────────────────────────────────────────────────────────────────────
// AdvanceRead modulo wrapping — no crash after many advances
//
// Without calling Init() first, every advance is a no-op.  This test
// verifies there is no integer overflow or undefined behavior from many
// repeated no-op calls.
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrameReadback, AdvanceRead_ManyNoOpCalls_DoesNotCrash) {
    uz::FrameReadback rb;
    for (int i = 0; i < 1000; ++i) {
        rb.AdvanceRead();
    }
    EXPECT_FALSE(rb.IsInitialized())
        << "IsInitialized() must remain false after 1000 no-op AdvanceRead() calls";
}
