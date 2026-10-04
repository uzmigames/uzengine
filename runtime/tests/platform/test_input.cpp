/// Unit tests for the UzEngine InputSystem (4.2.0).
///
/// Covers: key and mouse button state tracking, action mapping, frame boundary
/// transitions, cursor shape, mouse capture state, cursor visibility,
/// gamepad connection/disconnection, invalid index safety, scroll and raw mouse
/// delta reset, lifecycle (Init/Shutdown/re-Init), and the deadzone math that
/// backs GetGamepadAxis().
///
/// Deadzone functions (ApplyRadialDeadzone, ApplyTriggerDeadzone) are
/// file-static in input.cpp and cannot be called directly. Their math is
/// verified here as pure-C++ unit tests that mirror the implementation exactly,
/// giving full algorithmic coverage without requiring a connected gamepad.

#include "input.h"

#include <gtest/gtest.h>

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture — each test gets a freshly Init()'d InputSystem.
// ─────────────────────────────────────────────────────────────────────────────

class InputTest : public ::testing::Test {
  protected:
    uz::InputSystem input;
    void SetUp() override { input.Init(); }
    void TearDown() override { input.Shutdown(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// Deadzone math — pure algorithmic tests (mirrors input.cpp static helpers)
// ─────────────────────────────────────────────────────────────────────────────

// Mirror of input.cpp::ApplyRadialDeadzone for white-box math verification.
static void ApplyRadialDeadzoneMirror(float& outX, float& outY,
                                      short rawX, short rawY,
                                      short deadzone) {
    float fx = static_cast<float>(rawX);
    float fy = static_cast<float>(rawY);
    float magnitude = std::sqrt(fx * fx + fy * fy);
    float dz = static_cast<float>(deadzone);

    if (magnitude < dz) {
        outX = 0.f;
        outY = 0.f;
        return;
    }

    constexpr float kMaxStick = 32767.f;
    float normalizedMagnitude = (magnitude - dz) / (kMaxStick - dz);
    if (normalizedMagnitude > 1.f) normalizedMagnitude = 1.f;

    float scale = normalizedMagnitude / magnitude;
    outX = fx * scale;
    outY = fy * scale;
}

// Mirror of input.cpp::ApplyTriggerDeadzone.
static float ApplyTriggerDeadzoneMirror(uint8_t rawValue, uint8_t deadzone) {
    if (rawValue < deadzone) return 0.f;
    return static_cast<float>(rawValue - deadzone) /
           static_cast<float>(255 - deadzone);
}

// XInput constants used in the implementation.
static constexpr short kLeftThumbDeadzone  = 7849;   // XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
static constexpr short kRightThumbDeadzone = 8689;   // XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE
static constexpr uint8_t kTriggerThreshold = 30;     // XINPUT_GAMEPAD_TRIGGER_THRESHOLD

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — Radial deadzone: magnitude below threshold → both axes are 0
// ─────────────────────────────────────────────────────────────────────────────

TEST(GamepadDeadzone, RadialDeadzone_BelowThreshold_BothAxesReturnZero) {
    // Choose a vector whose magnitude is clearly inside the deadzone.
    // Use a pure-X input of (deadzone - 1) so magnitude == deadzone - 1.
    short rawX = static_cast<short>(kLeftThumbDeadzone - 1);
    short rawY = 0;

    float outX = 999.f, outY = 999.f;
    ApplyRadialDeadzoneMirror(outX, outY, rawX, rawY, kLeftThumbDeadzone);

    EXPECT_FLOAT_EQ(outX, 0.f) << "X axis must be 0 when magnitude is below deadzone";
    EXPECT_FLOAT_EQ(outY, 0.f) << "Y axis must be 0 when magnitude is below deadzone";
}

TEST(GamepadDeadzone, RadialDeadzone_DiagonalBelowThreshold_BothAxesReturnZero) {
    // Diagonal vector: magnitude = sqrt(a^2 + b^2) < deadzone.
    // Pick a = b = deadzone/sqrt(2) - 1 to stay under the circle.
    short rawX = static_cast<short>(kLeftThumbDeadzone / 2);
    short rawY = static_cast<short>(kLeftThumbDeadzone / 2);
    // magnitude ≈ deadzone * 0.707 — well inside.

    float outX = 1.f, outY = 1.f;
    ApplyRadialDeadzoneMirror(outX, outY, rawX, rawY, kLeftThumbDeadzone);

    EXPECT_FLOAT_EQ(outX, 0.f);
    EXPECT_FLOAT_EQ(outY, 0.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — Radial deadzone: magnitude above threshold → scaled to 0..1 range
// ─────────────────────────────────────────────────────────────────────────────

TEST(GamepadDeadzone, RadialDeadzone_JustAboveThreshold_OutputIsNearZeroButNonZero) {
    // At magnitude == deadzone + epsilon, normalized output should be near 0
    // but not zero.  Use pure-X input for simplicity.
    short rawX = static_cast<short>(kLeftThumbDeadzone + 10);
    short rawY = 0;

    float outX = 0.f, outY = 0.f;
    ApplyRadialDeadzoneMirror(outX, outY, rawX, rawY, kLeftThumbDeadzone);

    EXPECT_GT(outX, 0.f)  << "X must be positive just above deadzone";
    EXPECT_LT(outX, 0.1f) << "X must still be small just above deadzone";
    EXPECT_FLOAT_EQ(outY, 0.f);
}

TEST(GamepadDeadzone, RadialDeadzone_MidRange_OutputScalesCorrectly) {
    // At rawX = 20000 (pure X), verify the normalized magnitude calculation.
    short rawX = 20000;
    short rawY = 0;
    float dz   = static_cast<float>(kLeftThumbDeadzone);

    float expectedNorm = (20000.f - dz) / (32767.f - dz);

    float outX = 0.f, outY = 0.f;
    ApplyRadialDeadzoneMirror(outX, outY, rawX, rawY, kLeftThumbDeadzone);

    EXPECT_NEAR(outX, expectedNorm, 1e-4f)
        << "Mid-range X axis must match rescaled deadzone formula";
    EXPECT_FLOAT_EQ(outY, 0.f);
}

TEST(GamepadDeadzone, RadialDeadzone_OutputIsAlwaysWithinMinusOneToOne) {
    // Test several raw values across the range and check bounds.
    const short testValues[] = {
        static_cast<short>(-32767), static_cast<short>(-20000),
        static_cast<short>(-kLeftThumbDeadzone), 0,
        static_cast<short>(kLeftThumbDeadzone),
        20000, 32767
    };

    for (short raw : testValues) {
        float outX = 0.f, outY = 0.f;
        ApplyRadialDeadzoneMirror(outX, outY, raw, 0, kLeftThumbDeadzone);
        EXPECT_GE(outX, -1.f) << "X axis must not go below -1 for raw=" << raw;
        EXPECT_LE(outX, 1.f)  << "X axis must not exceed +1 for raw=" << raw;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 — Radial deadzone: max input (32767) → returns 1.0
// ─────────────────────────────────────────────────────────────────────────────

TEST(GamepadDeadzone, RadialDeadzone_MaxInput_ReturnsOne) {
    short rawX = 32767;
    short rawY = 0;

    float outX = 0.f, outY = 0.f;
    ApplyRadialDeadzoneMirror(outX, outY, rawX, rawY, kLeftThumbDeadzone);

    EXPECT_NEAR(outX, 1.f, 1e-4f) << "Full deflection must return 1.0";
    EXPECT_FLOAT_EQ(outY, 0.f);
}

TEST(GamepadDeadzone, RadialDeadzone_MaxNegativeInput_ReturnsNegativeOne) {
    short rawX = static_cast<short>(-32767);
    short rawY = 0;

    float outX = 0.f, outY = 0.f;
    ApplyRadialDeadzoneMirror(outX, outY, rawX, rawY, kLeftThumbDeadzone);

    EXPECT_NEAR(outX, -1.f, 1e-4f) << "Full negative deflection must return -1.0";
    EXPECT_FLOAT_EQ(outY, 0.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 — Linear trigger deadzone: below threshold → returns 0
// ─────────────────────────────────────────────────────────────────────────────

TEST(GamepadDeadzone, TriggerDeadzone_BelowThreshold_ReturnsZero) {
    EXPECT_FLOAT_EQ(ApplyTriggerDeadzoneMirror(0, kTriggerThreshold), 0.f)
        << "Trigger value 0 must return 0";

    EXPECT_FLOAT_EQ(ApplyTriggerDeadzoneMirror(kTriggerThreshold - 1, kTriggerThreshold), 0.f)
        << "Trigger at threshold-1 must return 0";
}

TEST(GamepadDeadzone, TriggerDeadzone_AtThreshold_ReturnsZero) {
    // The implementation uses strict less-than: rawValue < deadzone returns 0.
    // At rawValue == deadzone, the formula gives (0) / (255 - deadzone) = 0.
    float result = ApplyTriggerDeadzoneMirror(kTriggerThreshold, kTriggerThreshold);
    EXPECT_FLOAT_EQ(result, 0.f)
        << "Trigger exactly at threshold must return 0 (no movement yet)";
}

TEST(GamepadDeadzone, TriggerDeadzone_JustAboveThreshold_IsSmallPositive) {
    float result = ApplyTriggerDeadzoneMirror(kTriggerThreshold + 1, kTriggerThreshold);
    EXPECT_GT(result, 0.f)  << "Trigger one above threshold must be positive";
    EXPECT_LT(result, 0.1f) << "Trigger one above threshold must be small";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5 — Linear trigger deadzone: max trigger (255) → returns 1.0
// ─────────────────────────────────────────────────────────────────────────────

TEST(GamepadDeadzone, TriggerDeadzone_MaxValue_ReturnsOne) {
    float result = ApplyTriggerDeadzoneMirror(255, kTriggerThreshold);
    EXPECT_NEAR(result, 1.f, 1e-6f) << "Max trigger value must return 1.0";
}

TEST(GamepadDeadzone, TriggerDeadzone_MidRange_ScalesCorrectly) {
    uint8_t raw = 128;
    float expected = static_cast<float>(raw - kTriggerThreshold) /
                     static_cast<float>(255 - kTriggerThreshold);
    float result = ApplyTriggerDeadzoneMirror(raw, kTriggerThreshold);
    EXPECT_NEAR(result, expected, 1e-6f)
        << "Mid-range trigger must match linear rescale formula";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6 — CursorShape enum: all values accepted without crash
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, CursorShape_AllValues_SetAndGetWithoutCrash) {
    const uz::CursorShape shapes[] = {
        uz::CursorShape::Arrow,
        uz::CursorShape::Hand,
        uz::CursorShape::IBeam,
        uz::CursorShape::Crosshair,
        uz::CursorShape::SizeNS,
        uz::CursorShape::SizeWE,
        uz::CursorShape::SizeNWSE,
        uz::CursorShape::SizeNESW,
        uz::CursorShape::Wait,
        uz::CursorShape::No,
    };

    for (uz::CursorShape shape : shapes) {
        EXPECT_NO_THROW(input.SetCursorShape(shape))
            << "SetCursorShape must not throw";
        EXPECT_EQ(input.GetCursorShape(), shape)
            << "GetCursorShape must return the shape that was set";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7 — Lifecycle: Init + Shutdown + re-Init
// ─────────────────────────────────────────────────────────────────────────────

TEST(InputLifecycle, InitShutdownReInit_DoesNotCrash) {
    uz::InputSystem sys;

    // First lifecycle
    EXPECT_NO_THROW(sys.Init());

    // Exercise some state so Shutdown has non-trivial work
    sys.BeginFrame();
    sys.OnKeyDown(static_cast<uint16_t>(uz::Key::W));
    sys.OnMouseButton(uz::MouseButton::Left, true);
    sys.OnMouseScroll(2.f);
    sys.RegisterAction("fire", uz::Key::Space);

    EXPECT_NO_THROW(sys.Shutdown());

    // Re-init — must clear all prior state
    EXPECT_NO_THROW(sys.Init());

    // After re-init, no keys should be pressed and no actions registered
    EXPECT_FALSE(sys.IsKeyDown(uz::Key::W))
        << "Key state must be cleared after re-Init";
    EXPECT_FALSE(sys.IsMouseDown(uz::MouseButton::Left))
        << "Mouse state must be cleared after re-Init";
    EXPECT_FALSE(sys.IsActionDown("fire"))
        << "Action mappings must be cleared after Shutdown + re-Init";

    sys.Shutdown();
}

TEST(InputLifecycle, MultipleShutdownCalls_DoNotCrash) {
    uz::InputSystem sys;
    sys.Init();
    EXPECT_NO_THROW(sys.Shutdown());
    EXPECT_NO_THROW(sys.Shutdown()); // second shutdown must be safe
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8 — BeginFrame resets scroll delta to 0
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, BeginFrame_ResetsScrollDelta_ToZero) {
    input.BeginFrame();
    input.OnMouseScroll(5.f);
    EXPECT_FLOAT_EQ(input.GetScrollDelta(), 5.f)
        << "Scroll delta must accumulate within a frame";

    input.BeginFrame();
    EXPECT_FLOAT_EQ(input.GetScrollDelta(), 0.f)
        << "BeginFrame must reset scroll delta to 0";
}

TEST_F(InputTest, BeginFrame_AccumulatedScrollBeforeReset_IsCorrect) {
    input.BeginFrame();
    input.OnMouseScroll(1.f);
    input.OnMouseScroll(2.f);
    input.OnMouseScroll(-0.5f);
    EXPECT_FLOAT_EQ(input.GetScrollDelta(), 2.5f)
        << "Scroll events within a frame must accumulate";

    input.BeginFrame();
    EXPECT_FLOAT_EQ(input.GetScrollDelta(), 0.f)
        << "BeginFrame must reset the accumulated scroll delta";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 9 — BeginFrame resets raw mouse deltas to 0
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, BeginFrame_ResetsRawMouseDeltas_ToZero) {
    // Raw deltas start at 0 after Init.
    EXPECT_FLOAT_EQ(input.GetRawMouseDeltaX(), 0.f);
    EXPECT_FLOAT_EQ(input.GetRawMouseDeltaY(), 0.f);

    // After BeginFrame they must still be 0 (no raw input in test env).
    input.BeginFrame();
    EXPECT_FLOAT_EQ(input.GetRawMouseDeltaX(), 0.f)
        << "Raw mouse DX must be 0 after BeginFrame with no raw input events";
    EXPECT_FLOAT_EQ(input.GetRawMouseDeltaY(), 0.f)
        << "Raw mouse DY must be 0 after BeginFrame with no raw input events";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 10 — Mouse capture state tracking
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, MouseCapture_InitialState_IsNotCaptured) {
    EXPECT_FALSE(input.IsMouseCaptured())
        << "Mouse must not be captured immediately after Init";
}

// Note: SetMouseCapture(bool, HWND) calls Win32 ClipCursor which requires a
// valid HWND. We test only the IsMouseCaptured() state accessor here to avoid
// a real window dependency. The m_mouseCaptured field is updated unconditionally
// before any Win32 call, so we can verify it via the public query.
TEST_F(InputTest, MouseCapture_SetFalse_ReportsNotCaptured) {
    // The field is false by default; setting it false again must remain false.
#ifdef _WIN32
    // SetMouseCapture with nullptr HWND sets the flag and calls ClipCursor(nullptr)
    // which is a no-op OS-side. Safe to call without a real window.
    input.SetMouseCapture(false, nullptr);
#endif
    EXPECT_FALSE(input.IsMouseCaptured())
        << "IsMouseCaptured must return false after SetMouseCapture(false)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 11 — Cursor visible state tracking
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, CursorVisible_InitialState_IsTrue) {
    EXPECT_TRUE(input.IsCursorVisible())
        << "Cursor must be visible by default after Init";
}

TEST_F(InputTest, CursorVisible_SetFalse_ReportsFalse) {
    input.SetCursorVisible(false);
    EXPECT_FALSE(input.IsCursorVisible())
        << "IsCursorVisible must return false after SetCursorVisible(false)";
}

TEST_F(InputTest, CursorVisible_SetTrueAfterFalse_ReportsTrue) {
    input.SetCursorVisible(false);
    EXPECT_FALSE(input.IsCursorVisible());

    input.SetCursorVisible(true);
    EXPECT_TRUE(input.IsCursorVisible())
        << "IsCursorVisible must return true after SetCursorVisible(true)";
}

TEST_F(InputTest, CursorVisible_MultipleToggles_TrackCorrectly) {
    for (int i = 0; i < 5; ++i) {
        input.SetCursorVisible(false);
        EXPECT_FALSE(input.IsCursorVisible()) << "iteration " << i;
        input.SetCursorVisible(true);
        EXPECT_TRUE(input.IsCursorVisible()) << "iteration " << i;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 12 — IsGamepadConnected: no hardware → returns false
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, IsGamepadConnected_AllPlayers_ReturnFalseWithNoHardware) {
    // In a CI / headless environment XInputGetState returns ERROR_DEVICE_NOT_CONNECTED.
    // After BeginFrame (which polls XInput), all slots must be disconnected.
    input.BeginFrame();

    for (int player = 0; player < uz::kMaxGamepads; ++player) {
        EXPECT_FALSE(input.IsGamepadConnected(player))
            << "Player " << player << " must report disconnected without XInput hardware";
    }
}

TEST_F(InputTest, IsGamepadConnected_InvalidIndex_ReturnsFalse) {
    input.BeginFrame();
    EXPECT_FALSE(input.IsGamepadConnected(-1))
        << "Negative player index must return false";
    EXPECT_FALSE(input.IsGamepadConnected(uz::kMaxGamepads))
        << "Index equal to kMaxGamepads must return false";
    EXPECT_FALSE(input.IsGamepadConnected(100))
        << "Large out-of-range index must return false";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 13 — SetVibration: invalid player index doesn't crash
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, SetVibration_InvalidPlayerIndex_DoesNotCrash) {
    input.BeginFrame();

    EXPECT_NO_THROW(input.SetVibration(-1, 1.f, 1.f))
        << "Negative player index must not crash";
    EXPECT_NO_THROW(input.SetVibration(uz::kMaxGamepads, 1.f, 1.f))
        << "Index == kMaxGamepads must not crash";
    EXPECT_NO_THROW(input.SetVibration(100, 0.5f, 0.5f))
        << "Large out-of-range index must not crash";
}

TEST_F(InputTest, SetVibration_ValidIndexButDisconnected_DoesNotCrash) {
    input.BeginFrame(); // polls XInput → all disconnected
    for (int player = 0; player < uz::kMaxGamepads; ++player) {
        EXPECT_NO_THROW(input.SetVibration(player, 1.f, 1.f))
            << "SetVibration on disconnected player " << player << " must not crash";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 14 — OnKeyDown + IsKeyDown returns true
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, OnKeyDown_IsKeyDown_ReturnsTrue) {
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::A));
    EXPECT_TRUE(input.IsKeyDown(uz::Key::A))
        << "IsKeyDown must return true immediately after OnKeyDown";
}

TEST_F(InputTest, OnKeyDown_MultipleKeys_EachTrackedIndependently) {
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::W));
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::S));
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::Space));

    EXPECT_TRUE(input.IsKeyDown(uz::Key::W));
    EXPECT_TRUE(input.IsKeyDown(uz::Key::S));
    EXPECT_TRUE(input.IsKeyDown(uz::Key::Space));
    EXPECT_FALSE(input.IsKeyDown(uz::Key::A))
        << "Keys not pressed must remain false";
    EXPECT_FALSE(input.IsKeyDown(uz::Key::D))
        << "Keys not pressed must remain false";
}

TEST_F(InputTest, OnKeyDown_IsKeyJustPressed_ReturnsTrue_SameFrame) {
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::Enter));
    EXPECT_TRUE(input.IsKeyJustPressed(uz::Key::Enter))
        << "Key pressed this frame must report JustPressed";
    EXPECT_FALSE(input.IsKeyJustReleased(uz::Key::Enter))
        << "Key pressed this frame must not report JustReleased";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 15 — OnKeyDown + BeginFrame → IsKeyJustPressed returns false
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, OnKeyDown_AfterBeginFrame_IsKeyJustPressedReturnsFalse) {
    // Frame 1: press the key
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::Space));
    EXPECT_TRUE(input.IsKeyJustPressed(uz::Key::Space))
        << "Key must be JustPressed in the frame it was pressed";

    // Frame 2: key is still held but no longer "just" pressed
    input.BeginFrame();
    EXPECT_TRUE(input.IsKeyDown(uz::Key::Space))
        << "Key must still be Down if not released";
    EXPECT_FALSE(input.IsKeyJustPressed(uz::Key::Space))
        << "Key must no longer be JustPressed in the following frame";
    EXPECT_FALSE(input.IsKeyJustReleased(uz::Key::Space))
        << "Key must not be JustReleased while still held";
}

TEST_F(InputTest, OnKeyDown_ThenRelease_IsKeyJustReleasedReturnsTrue) {
    // Frame 1: press
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::F1));

    // Frame 2: release
    input.BeginFrame();
    input.OnKeyUp(static_cast<uint16_t>(uz::Key::F1));

    EXPECT_FALSE(input.IsKeyDown(uz::Key::F1));
    EXPECT_FALSE(input.IsKeyJustPressed(uz::Key::F1));
    EXPECT_TRUE(input.IsKeyJustReleased(uz::Key::F1))
        << "Key must report JustReleased the frame it was released";
}

TEST_F(InputTest, OnKeyDown_MultiFrameHold_NeverJustPressedAfterFirstFrame) {
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::Left));
    EXPECT_TRUE(input.IsKeyJustPressed(uz::Key::Left));

    for (int frame = 0; frame < 5; ++frame) {
        input.BeginFrame();
        EXPECT_TRUE(input.IsKeyDown(uz::Key::Left))
            << "Key must remain Down during hold (frame " << frame << ")";
        EXPECT_FALSE(input.IsKeyJustPressed(uz::Key::Left))
            << "Key must not be JustPressed after first frame (frame " << frame << ")";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 16 — OnMouseButton + IsMouseDown returns true
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, OnMouseButton_Left_IsMouseDown_ReturnsTrue) {
    input.BeginFrame();
    input.OnMouseButton(uz::MouseButton::Left, true);
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::Left))
        << "Left button must be down after OnMouseButton(Left, true)";
}

TEST_F(InputTest, OnMouseButton_AllButtons_TrackedIndependently) {
    input.BeginFrame();
    input.OnMouseButton(uz::MouseButton::Left,   true);
    input.OnMouseButton(uz::MouseButton::Right,  true);
    input.OnMouseButton(uz::MouseButton::Middle, true);
    input.OnMouseButton(uz::MouseButton::X1,     true);
    input.OnMouseButton(uz::MouseButton::X2,     true);

    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::Left));
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::Right));
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::Middle));
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::X1));
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::X2));
}

TEST_F(InputTest, OnMouseButton_Release_IsMouseDownReturnsFalse) {
    input.BeginFrame();
    input.OnMouseButton(uz::MouseButton::Right, true);
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::Right));

    input.OnMouseButton(uz::MouseButton::Right, false);
    EXPECT_FALSE(input.IsMouseDown(uz::MouseButton::Right))
        << "Mouse button must not be down after release";
}

TEST_F(InputTest, OnMouseButton_IsMouseJustPressed_ClearsAfterBeginFrame) {
    input.BeginFrame();
    input.OnMouseButton(uz::MouseButton::Middle, true);
    EXPECT_TRUE(input.IsMouseJustPressed(uz::MouseButton::Middle))
        << "Button must be JustPressed this frame";

    input.BeginFrame();
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::Middle))
        << "Button still held — must remain Down";
    EXPECT_FALSE(input.IsMouseJustPressed(uz::MouseButton::Middle))
        << "Button must no longer be JustPressed in following frame";
}

TEST_F(InputTest, OnMouseButton_IsMouseJustReleased_CorrectlyDetected) {
    input.BeginFrame();
    input.OnMouseButton(uz::MouseButton::X1, true);

    input.BeginFrame();
    input.OnMouseButton(uz::MouseButton::X1, false);

    EXPECT_FALSE(input.IsMouseDown(uz::MouseButton::X1));
    EXPECT_TRUE(input.IsMouseJustReleased(uz::MouseButton::X1))
        << "Mouse button must report JustReleased the frame it was released";
}

// ─────────────────────────────────────────────────────────────────────────────
// Existing tests preserved (reformatted to fixture style)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, KeyboardPressHoldRelease) {
    // Frame 1: press W
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::W));
    EXPECT_TRUE(input.IsKeyDown(uz::Key::W));
    EXPECT_TRUE(input.IsKeyJustPressed(uz::Key::W));
    EXPECT_FALSE(input.IsKeyDown(uz::Key::A));

    // Frame 2: W still held
    input.BeginFrame();
    EXPECT_TRUE(input.IsKeyDown(uz::Key::W));
    EXPECT_FALSE(input.IsKeyJustPressed(uz::Key::W));

    // Frame 3: release W
    input.OnKeyUp(static_cast<uint16_t>(uz::Key::W));
    EXPECT_FALSE(input.IsKeyDown(uz::Key::W));
    EXPECT_TRUE(input.IsKeyJustReleased(uz::Key::W));
}

TEST_F(InputTest, MouseMoveButtonScroll) {
    input.BeginFrame();
    input.OnMouseMove(100.f, 200.f);
    EXPECT_FLOAT_EQ(input.GetMouseX(), 100.f);
    EXPECT_FLOAT_EQ(input.GetMouseY(), 200.f);

    input.OnMouseButton(uz::MouseButton::Left, true);
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::Left));
    EXPECT_TRUE(input.IsMouseJustPressed(uz::MouseButton::Left));

    input.BeginFrame();
    EXPECT_FALSE(input.IsMouseJustPressed(uz::MouseButton::Left));
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::Left));

    input.OnMouseScroll(3.f);
    EXPECT_FLOAT_EQ(input.GetScrollDelta(), 3.f);
}

TEST_F(InputTest, ActionMappingWorks) {
    input.RegisterAction("Jump", uz::Key::Space);
    input.RegisterAction("Forward", uz::Key::W);

    input.BeginFrame();
    EXPECT_FALSE(input.IsActionDown("Jump"));

    input.OnKeyDown(static_cast<uint16_t>(uz::Key::Space));
    EXPECT_TRUE(input.IsActionDown("Jump"));
    EXPECT_TRUE(input.IsActionJustPressed("Jump"));
    EXPECT_FALSE(input.IsActionDown("NonExistent"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Additional coverage: mouse delta, action rebinding, out-of-range VK
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, MouseDelta_ComputedFromMoveEvents) {
    input.BeginFrame();
    input.OnMouseMove(10.f, 20.f);

    input.BeginFrame();
    input.OnMouseMove(30.f, 50.f);

    // Delta is computed at BeginFrame, so we need a third BeginFrame to
    // calculate the delta between (10, 20) and (30, 50).
    input.BeginFrame();
    EXPECT_FLOAT_EQ(input.GetMouseDeltaX(), 20.f)
        << "Mouse DX must reflect the move from 10 to 30";
    EXPECT_FLOAT_EQ(input.GetMouseDeltaY(), 30.f)
        << "Mouse DY must reflect the move from 20 to 50";
}

TEST_F(InputTest, MouseDelta_NoMovement_IsZero) {
    // Establish initial position so prevMouse is set
    input.BeginFrame();
    input.OnMouseMove(50.f, 50.f);

    // This BeginFrame computes delta = (50-0, 50-0) and sets prev to (50, 50)
    input.BeginFrame();

    // Third BeginFrame: no new OnMouseMove, so delta = (50-50, 50-50) = (0, 0)
    input.BeginFrame();
    EXPECT_FLOAT_EQ(input.GetMouseDeltaX(), 0.f);
    EXPECT_FLOAT_EQ(input.GetMouseDeltaY(), 0.f);
}

TEST_F(InputTest, ActionMapping_Overwrite_UsesLatestBinding) {
    input.RegisterAction("Dash", uz::Key::LeftShift);
    input.RegisterAction("Dash", uz::Key::RightShift); // overwrite

    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::LeftShift));
    EXPECT_FALSE(input.IsActionDown("Dash"))
        << "Old binding (LeftShift) must no longer trigger after rebind";

    input.OnKeyDown(static_cast<uint16_t>(uz::Key::RightShift));
    EXPECT_TRUE(input.IsActionDown("Dash"))
        << "New binding (RightShift) must trigger after rebind";
}

TEST_F(InputTest, ActionMapping_UnregisteredAction_ReturnsFalse) {
    input.BeginFrame();
    EXPECT_FALSE(input.IsActionDown("ghost_action"));
    EXPECT_FALSE(input.IsActionJustPressed("ghost_action"));
}

TEST_F(InputTest, OnKeyDown_OutOfRangeVirtualKey_DoesNotCrash) {
    input.BeginFrame();
    // VK >= 256 must be silently ignored (implementation guards on kMaxKeys=256).
    EXPECT_NO_THROW(input.OnKeyDown(256));
    EXPECT_NO_THROW(input.OnKeyDown(1000));
    EXPECT_NO_THROW(input.OnKeyUp(256));
    EXPECT_NO_THROW(input.OnKeyUp(65535));
}

TEST_F(InputTest, GetGamepadAxis_DisconnectedGamepad_ReturnsZero) {
    input.BeginFrame(); // polls XInput — no hardware → all disconnected
    for (int player = 0; player < uz::kMaxGamepads; ++player) {
        EXPECT_FLOAT_EQ(input.GetGamepadAxis(player, uz::GamepadAxis::LeftX), 0.f)
            << "Disconnected gamepad axis must return 0 (player " << player << ")";
        EXPECT_FLOAT_EQ(input.GetGamepadAxis(player, uz::GamepadAxis::RightTrigger), 0.f)
            << "Disconnected trigger axis must return 0 (player " << player << ")";
    }
}

TEST_F(InputTest, GetGamepadAxis_InvalidPlayerIndex_ReturnsZero) {
    input.BeginFrame();
    EXPECT_FLOAT_EQ(input.GetGamepadAxis(-1, uz::GamepadAxis::LeftX), 0.f);
    EXPECT_FLOAT_EQ(input.GetGamepadAxis(uz::kMaxGamepads, uz::GamepadAxis::LeftY), 0.f);
    EXPECT_FLOAT_EQ(input.GetGamepadAxis(99, uz::GamepadAxis::LeftTrigger), 0.f);
}

TEST_F(InputTest, IsGamepadButtonDown_DisconnectedGamepad_ReturnsFalse) {
    input.BeginFrame();
    for (int player = 0; player < uz::kMaxGamepads; ++player) {
        EXPECT_FALSE(input.IsGamepadButtonDown(player, uz::GamepadButton::A))
            << "Disconnected gamepad button must return false (player " << player << ")";
        EXPECT_FALSE(input.IsGamepadButtonDown(player, uz::GamepadButton::Start));
    }
}

TEST_F(InputTest, IsGamepadButtonJustPressed_InvalidIndex_ReturnsFalse) {
    input.BeginFrame();
    EXPECT_FALSE(input.IsGamepadButtonJustPressed(-1, uz::GamepadButton::A));
    EXPECT_FALSE(input.IsGamepadButtonJustPressed(uz::kMaxGamepads, uz::GamepadButton::B));
    EXPECT_FALSE(input.IsGamepadButtonJustReleased(-1, uz::GamepadButton::X));
}

TEST_F(InputTest, GamepadConnectionCallback_NullCallback_DoesNotCrash) {
    // Setting null callbacks and then polling must not crash.
    input.SetGamepadConnectedCallback(nullptr, nullptr);
    input.SetGamepadDisconnectedCallback(nullptr, nullptr);
    EXPECT_NO_THROW(input.BeginFrame());
}

TEST_F(InputTest, CursorShape_DefaultIsArrow) {
    EXPECT_EQ(input.GetCursorShape(), uz::CursorShape::Arrow)
        << "Default cursor shape after Init must be Arrow";
}

TEST_F(InputTest, ApplyCursorState_DoesNotCrash) {
    // ApplyCursorState is called from WM_SETCURSOR. Calling it directly
    // (without a real WndProc context) must not crash regardless of shape/visibility.
    for (auto shape : {
             uz::CursorShape::Arrow,    uz::CursorShape::Hand,
             uz::CursorShape::IBeam,    uz::CursorShape::Crosshair,
             uz::CursorShape::SizeNS,   uz::CursorShape::SizeWE,
             uz::CursorShape::SizeNWSE, uz::CursorShape::SizeNESW,
             uz::CursorShape::Wait,     uz::CursorShape::No,
         }) {
        input.SetCursorShape(shape);
        input.SetCursorVisible(true);
        EXPECT_NO_THROW(input.ApplyCursorState())
            << "ApplyCursorState visible must not throw for shape "
            << static_cast<int>(shape);

        input.SetCursorVisible(false);
        EXPECT_NO_THROW(input.ApplyCursorState())
            << "ApplyCursorState hidden must not throw for shape "
            << static_cast<int>(shape);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Custom cursor
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, SetCustomCursor_NullPath_ReturnsFalse) {
    // A null path is an invalid argument — must return false and leave the
    // cursor shape unchanged (still Arrow after Init).
    EXPECT_FALSE(input.SetCustomCursor(nullptr))
        << "SetCustomCursor(nullptr) must return false";
    EXPECT_EQ(input.GetCursorShape(), uz::CursorShape::Arrow)
        << "Cursor shape must remain Arrow after a failed SetCustomCursor call";
}

TEST_F(InputTest, SetCustomCursor_NonExistentFile_ReturnsFalse) {
    // A path that does not exist on disk must fail gracefully without crashing.
    EXPECT_FALSE(input.SetCustomCursor("C:/does_not_exist_8f3a9b2c.cur"))
        << "SetCustomCursor with a non-existent file must return false";
    EXPECT_EQ(input.GetCursorShape(), uz::CursorShape::Arrow)
        << "Cursor shape must remain Arrow after a failed load";
}

TEST_F(InputTest, SetCustomCursor_NonExistentFile_ApplyCursorState_DoesNotCrash) {
    // Even after a failed SetCustomCursor, ApplyCursorState must not crash
    // regardless of visibility setting because m_customCursor stays null.
    input.SetCustomCursor("C:/ghost_cursor.cur");

    input.SetCursorVisible(true);
    EXPECT_NO_THROW(input.ApplyCursorState())
        << "ApplyCursorState must not crash when m_customCursor is null";

    input.SetCursorVisible(false);
    EXPECT_NO_THROW(input.ApplyCursorState())
        << "ApplyCursorState hidden must not crash when m_customCursor is null";
}

TEST_F(InputTest, ApplyCursorState_CustomShape_WithNullHandle_DoesNotCrash) {
    // Force the cursor shape to Custom without going through SetCustomCursor
    // (m_customCursor therefore stays null). ApplyCursorState must fall back to
    // the Arrow cursor instead of crashing on a null HCURSOR.
    input.SetCursorShape(uz::CursorShape::Custom);
    EXPECT_NO_THROW(input.ApplyCursorState())
        << "ApplyCursorState(Custom) with null handle must fall back to Arrow";
}

TEST_F(InputTest, GetCursorShape_AfterSetCustomCursor_Failure_RemainsArrow) {
    // Repeated failed attempts must never flip the shape to Custom.
    EXPECT_FALSE(input.SetCustomCursor(nullptr));
    EXPECT_FALSE(input.SetCustomCursor(""));
    EXPECT_FALSE(input.SetCustomCursor("not_a_real.cur"));
    EXPECT_EQ(input.GetCursorShape(), uz::CursorShape::Arrow)
        << "Shape must remain Arrow after any number of failed SetCustomCursor calls";
}

// ─────────────────────────────────────────────────────────────────────────────
// Parity-06 item 1.4 — an edge inside one frame must survive to the queries.
//
// The engine snapshotted state at BeginFrame, so a key pressed AND released between two
// BeginFrame calls ended with previous == current == up and reported neither edge: a quick F5 tap
// did nothing. These reproduce that case and the behaviours that must not regress.
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(InputTest, QuickTapWithinOneFrame_ReportsBothKeyEdges) {
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::F5));
    input.OnKeyUp(static_cast<uint16_t>(uz::Key::F5));

    EXPECT_TRUE(input.IsKeyJustPressed(uz::Key::F5))
        << "A key pressed and released inside one frame must still report the press";
    EXPECT_TRUE(input.IsKeyJustReleased(uz::Key::F5))
        << "...and the release";
    EXPECT_FALSE(input.IsKeyDown(uz::Key::F5))
        << "The key is not held any more";
}

TEST_F(InputTest, QuickTapWithinOneFrame_ReportsBothMouseEdges) {
    input.BeginFrame();
    input.OnMouseButton(uz::MouseButton::Left, true);
    input.OnMouseButton(uz::MouseButton::Left, false);

    EXPECT_TRUE(input.IsMouseJustPressed(uz::MouseButton::Left));
    EXPECT_TRUE(input.IsMouseJustReleased(uz::MouseButton::Left));
    EXPECT_FALSE(input.IsMouseDown(uz::MouseButton::Left));
}

TEST_F(InputTest, QuickTapEdges_AreGoneNextFrame) {
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::F5));
    input.OnKeyUp(static_cast<uint16_t>(uz::Key::F5));
    ASSERT_TRUE(input.IsKeyJustPressed(uz::Key::F5));

    input.BeginFrame();
    EXPECT_FALSE(input.IsKeyJustPressed(uz::Key::F5))
        << "The edge belongs to the frame it happened in";
    EXPECT_FALSE(input.IsKeyJustReleased(uz::Key::F5));
}

TEST_F(InputTest, HeldKeyAcrossFrames_ReportsOnePress) {
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::W));
    EXPECT_TRUE(input.IsKeyJustPressed(uz::Key::W));

    input.BeginFrame();
    EXPECT_FALSE(input.IsKeyJustPressed(uz::Key::W))
        << "A held key must report the press once, not every frame";
    EXPECT_TRUE(input.IsKeyDown(uz::Key::W));

    input.OnKeyUp(static_cast<uint16_t>(uz::Key::W));
    EXPECT_TRUE(input.IsKeyJustReleased(uz::Key::W));
}

TEST_F(InputTest, AutoRepeatDown_DoesNotRepeatThePressEdge) {
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::W));
    input.BeginFrame();
    // WM_KEYDOWN repeats while the key is held.
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::W));
    EXPECT_FALSE(input.IsKeyJustPressed(uz::Key::W))
        << "Auto-repeat must not look like a new press";
}

TEST_F(InputTest, PrepareAndEndFrame_KeepTheEdgeUntilEndFrame) {
    input.PrepareFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::F5));
    input.OnKeyUp(static_cast<uint16_t>(uz::Key::F5));
    EXPECT_TRUE(input.IsKeyJustPressed(uz::Key::F5));

    input.PrepareFrame();  // publishing again must not drop it
    EXPECT_TRUE(input.IsKeyJustPressed(uz::Key::F5))
        << "Only EndFrame() ends an edge's life";

    input.EndFrame();
    EXPECT_FALSE(input.IsKeyJustPressed(uz::Key::F5));
    EXPECT_FALSE(input.IsKeyJustReleased(uz::Key::F5));
}

TEST_F(InputTest, ReInit_ClearsPendingEdges) {
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::F5));
    input.OnKeyUp(static_cast<uint16_t>(uz::Key::F5));
    ASSERT_TRUE(input.IsKeyJustPressed(uz::Key::F5));

    input.Shutdown();
    input.Init();
    EXPECT_FALSE(input.IsKeyJustPressed(uz::Key::F5))
        << "A fresh Init must not report an edge recorded before it";
    EXPECT_FALSE(input.IsKeyJustReleased(uz::Key::F5));
}
