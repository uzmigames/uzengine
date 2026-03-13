#pragma once

/// UzEngine Input System
///
/// Unified keyboard, mouse, and gamepad input handling.
/// Wraps Win32 raw input messages, XInput gamepad polling, and maps them
/// to high-level events.
///
/// # Architecture
/// - **Input source**: WM_KEYDOWN/UP, WM_MOUSEMOVE, WM_*BUTTON, WM_INPUT, XInput
/// - **Key mapping**: Virtual key codes (VK_*) → uz::Key enum
/// - **Action mapping**: Input events → gameplay actions (Move, Jump, Attack)
/// - **State tracking**: IsPressed(), GetMouseDelta(), GetGamepadAxis(), etc.
/// - **Raw mouse**: WM_INPUT for high-precision, unaccelerated mouse deltas
/// - **Gamepad**: XInput polling for up to 4 controllers with deadzone processing
///
/// # Input Flow
/// ```
/// OS (WM_KEYDOWN)  → PlatformWindow::PumpMessages()
///   → InputSystem::HandleMessage()
///   → Update key state + fire callbacks
///
/// XInput (per-frame) → InputSystem::BeginFrame()
///   → PollGamepads() → Update button/axis state + fire callbacks
/// ```
///
/// # Input Contexts
/// Game actions are context-aware:
/// - Menu context: Up/Down/Select (UI navigation)
/// - Gameplay context: WASD/Mouse/Gamepad (movement/camera)
/// Can rebind or disable contexts at runtime.

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace uz {

/// Keyboard key codes (physical keys).
///
/// # Mapping
/// - Letters: ASCII codes (A='A', Z='Z')
/// - Numbers: ASCII codes (0='0', 9='9')
/// - Function keys: VK_F1..VK_F12 codes (0x70..0x7B)
/// - Special: Standard Windows VK_* codes
///
/// # Example
/// ```cpp
/// if (input.IsPressed(Key::W)) { /* move forward */ }
/// if (input.JustPressed(Key::Space)) { /* jump */ }
/// ```
enum class Key : uint16_t {
    Unknown = 0, ///< Unknown/unsupported key

    // ──── Letters (A-Z) ────
    A = 'A',
    B = 'B',
    C = 'C',
    D = 'D',
    E = 'E',
    F = 'F',
    G = 'G',
    H = 'H',
    I = 'I',
    J = 'J',
    K = 'K',
    L = 'L',
    M = 'M',
    N = 'N',
    O = 'O',
    P = 'P',
    Q = 'Q',
    R = 'R',
    S = 'S',
    T = 'T',
    U = 'U',
    V = 'V',
    W = 'W',
    X = 'X',
    Y = 'Y',
    Z = 'Z',

    // ──── Numbers (0-9) ────
    Num0 = '0',
    Num1 = '1',
    Num2 = '2',
    Num3 = '3',
    Num4 = '4',
    Num5 = '5',
    Num6 = '6',
    Num7 = '7',
    Num8 = '8',
    Num9 = '9',

    // ──── Function Keys (F1-F12) ────
    F1 = 0x70,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    // ──── Special Keys ────
    Escape = 0x1B,
    Tab = 0x09,
    Space = 0x20,
    Enter = 0x0D,
    Backspace = 0x08,
    LeftShift = 0xA0,
    RightShift = 0xA1, ///< Shift (left/right)
    LeftCtrl = 0xA2,
    RightCtrl = 0xA3, ///< Control (left/right)
    LeftAlt = 0xA4,
    RightAlt = 0xA5, ///< Alt (left/right)

    // ──── Arrow Keys ────
    Left = 0x25,
    Up = 0x26,
    Right = 0x27,
    Down = 0x28,

    // ──── Misc Keys ────
    Delete = 0x2E,
    Insert = 0x2D,
    Home = 0x24,
    End = 0x23,
    PageUp = 0x21,
    PageDown = 0x22,
};

/// Mouse button codes.
///
/// # Standard buttons
/// Left, Right, Middle (LMB, RMB, MMB)
///
/// # Extended buttons
/// Side buttons (Browser Back/Forward) on gaming mice
enum class MouseButton : uint8_t {
    Left = 0, ///< Left mouse button (primary)
    Right = 1,
    Middle = 2,
    X1 = 3,
    X2 = 4,
    Count = 5,
};

/// Cursor shape for `InputSystem::SetCursorShape()`.
///
/// Maps to standard Win32 cursor resources (IDC_ARROW, IDC_HAND, etc.).
enum class CursorShape : uint8_t {
    Arrow,    ///< Default pointer (IDC_ARROW)
    Hand,     ///< Hand pointer for clickable items (IDC_HAND)
    IBeam,    ///< Text insertion cursor (IDC_IBEAM)
    Crosshair,///< Precision crosshair (IDC_CROSS)
    SizeNS,   ///< Vertical resize (IDC_SIZENS)
    SizeWE,   ///< Horizontal resize (IDC_SIZEWE)
    SizeNWSE, ///< Diagonal resize NW-SE (IDC_SIZENWSE)
    SizeNESW, ///< Diagonal resize NE-SW (IDC_SIZENESW)
    Wait,     ///< Busy/hourglass (IDC_WAIT)
    No,       ///< Not allowed (IDC_NO)
    Custom,   ///< User-supplied cursor loaded via `SetCustomCursor()`
};

// ─────────────────────────────────────────────────────────────────────────────
// Gamepad Types
// ─────────────────────────────────────────────────────────────────────────────

/// XInput gamepad buttons.
///
/// Matches the XINPUT_GAMEPAD_* button masks in layout but uses an enum
/// for type safety. Query via `IsGamepadButtonDown()` etc.
enum class GamepadButton : uint8_t {
    A,          ///< Face button A (bottom)
    B,          ///< Face button B (right)
    X,          ///< Face button X (left)
    Y,          ///< Face button Y (top)
    LB,         ///< Left bumper (shoulder)
    RB,         ///< Right bumper (shoulder)
    Start,      ///< Start / Menu button
    Back,       ///< Back / View button
    LThumb,     ///< Left thumbstick click
    RThumb,     ///< Right thumbstick click
    DPadUp,     ///< D-pad up
    DPadDown,   ///< D-pad down
    DPadLeft,   ///< D-pad left
    DPadRight,  ///< D-pad right
    Count,      ///< Sentinel — number of buttons
};

/// XInput gamepad axes.
///
/// Thumbstick axes range from -1.0 to 1.0.
/// Trigger axes range from 0.0 to 1.0.
/// Deadzones are applied before returning values.
enum class GamepadAxis : uint8_t {
    LeftX,        ///< Left stick horizontal (-1 = left, +1 = right)
    LeftY,        ///< Left stick vertical (-1 = down, +1 = up)
    RightX,       ///< Right stick horizontal
    RightY,       ///< Right stick vertical
    LeftTrigger,  ///< Left trigger (0 = released, 1 = fully pressed)
    RightTrigger, ///< Right trigger (0 = released, 1 = fully pressed)
    Count,        ///< Sentinel — number of axes
};

/// Maximum number of gamepads supported (XInput limit).
static constexpr int kMaxGamepads = 4;

/// Per-gamepad state snapshot.
///
/// Stores button and axis state for a single XInput controller.
/// Two snapshots (current + previous) enable transition detection.
struct GamepadState {
    bool buttons[static_cast<int>(GamepadButton::Count)] = {};
    float axes[static_cast<int>(GamepadAxis::Count)] = {};
    bool connected = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Gamepad Callbacks
// ─────────────────────────────────────────────────────────────────────────────

/// Callback fired when a gamepad is connected or disconnected.
///
/// # Arguments
/// * `playerIndex` — 0-based player index (0..3).
/// * `userData` — Opaque pointer set via `SetGamepadConnectedCallback()`.
using GamepadConnectionCallback = void(*)(int playerIndex, void* userData);

// ── Input state ─────────────────────────────────────────────────────────────

/// Unified input system for keyboard, mouse, and gamepad state tracking.
///
/// Maintains per-frame snapshots of key/button state (current vs previous)
/// to detect press/release transitions. Also tracks mouse position, delta,
/// scroll wheel accumulation, raw mouse input, cursor state, and up to 4
/// XInput gamepads.
///
/// # Lifecycle
/// 1. Call `Init()` once at startup to zero all state
/// 2. Call `BeginFrame()` once per frame before processing OS messages
/// 3. Platform layer calls `OnKey*`, `OnMouse*`, `OnRawMouseInput` during message dispatch
/// 4. Game code queries `IsKeyDown()`, `IsActionDown()`, `GetGamepadAxis()`, etc.
/// 5. Call `Shutdown()` to clear action mappings and release raw input
///
/// # Thread Safety
/// NOT thread-safe. All calls must be from the main (message pump) thread.
///
/// # Example
/// ```cpp
/// InputSystem input;
/// input.Init();
/// input.EnableRawInput(hwnd);
/// input.RegisterAction("jump", Key::Space);
/// // ... per frame:
/// input.BeginFrame();
/// window.PumpMessages(); // triggers OnKey*/OnMouse*/OnRawMouseInput callbacks
/// if (input.IsActionJustPressed("jump")) { player.Jump(); }
/// float rx = input.GetGamepadAxis(0, GamepadAxis::RightX);
/// ```
class InputSystem {
  public:
    /// Initializes all key, mouse, and gamepad state arrays to zero.
    ///
    /// Must be called once before any input processing begins.
    void Init();

    /// Clears action mappings, releases raw input registration, and resets state.
    void Shutdown();

    /// Snapshots current state into previous state for transition detection.
    ///
    /// Must be called exactly once per frame, before the platform layer
    /// dispatches new WM_* messages. Computes mouse delta from position
    /// change since last frame, resets scroll and raw mouse accumulators,
    /// and polls XInput gamepads.
    void BeginFrame();

    // ── Keyboard Queries ────────────────────────────────────────────────

    /// Returns `true` if the key is currently held down.
    ///
    /// # Arguments
    /// * `key` - The key to query.
    ///
    /// # Returns
    /// `true` if the key's virtual key code is in the pressed state.
    [[nodiscard]] bool IsKeyDown(Key key) const;

    /// Returns `true` if the key transitioned from up to down this frame.
    ///
    /// # Arguments
    /// * `key` - The key to query.
    ///
    /// # Returns
    /// `true` if `current[key] && !previous[key]`.
    [[nodiscard]] bool IsKeyJustPressed(Key key) const;

    /// Returns `true` if the key transitioned from down to up this frame.
    ///
    /// # Arguments
    /// * `key` - The key to query.
    ///
    /// # Returns
    /// `true` if `!current[key] && previous[key]`.
    [[nodiscard]] bool IsKeyJustReleased(Key key) const;

    // ── Mouse Queries ───────────────────────────────────────────────────

    /// Returns `true` if the mouse button is currently held down.
    ///
    /// # Arguments
    /// * `btn` - The mouse button to query.
    [[nodiscard]] bool IsMouseDown(MouseButton btn) const;

    /// Returns `true` if the mouse button was just pressed this frame.
    ///
    /// # Arguments
    /// * `btn` - The mouse button to query.
    [[nodiscard]] bool IsMouseJustPressed(MouseButton btn) const;

    /// Returns `true` if the mouse button was just released this frame.
    ///
    /// # Arguments
    /// * `btn` - The mouse button to query.
    [[nodiscard]] bool IsMouseJustReleased(MouseButton btn) const;

    /// Returns the current mouse X position in window-relative pixels.
    [[nodiscard]] float GetMouseX() const { return m_mouseX; }

    /// Returns the current mouse Y position in window-relative pixels.
    [[nodiscard]] float GetMouseY() const { return m_mouseY; }

    /// Returns the mouse X movement since last frame (pixels, from WM_MOUSEMOVE).
    [[nodiscard]] float GetMouseDeltaX() const { return m_mouseDX; }

    /// Returns the mouse Y movement since last frame (pixels, from WM_MOUSEMOVE).
    [[nodiscard]] float GetMouseDeltaY() const { return m_mouseDY; }

    /// Returns the accumulated scroll wheel delta since last `BeginFrame()`.
    ///
    /// Positive = scroll up, negative = scroll down.
    /// Resets to 0 at each `BeginFrame()`.
    [[nodiscard]] float GetScrollDelta() const { return m_scrollDelta; }

    // ── Raw Mouse Input ─────────────────────────────────────────────────

    /// Registers for WM_INPUT raw mouse messages on the given window.
    ///
    /// Raw input bypasses mouse acceleration and provides hardware-level
    /// deltas, ideal for FPS camera control. Call once after window creation.
    ///
    /// # Arguments
    /// * `hwnd` — The window handle to receive WM_INPUT messages.
    ///
    /// # Returns
    /// `true` if registration succeeded, `false` on failure (logged).
    ///
    /// # Side Effects
    /// Calls `RegisterRawInputDevices()`. Logs Input::Info on success,
    /// Input::Error on failure.
#ifdef _WIN32
    bool EnableRawInput(HWND hwnd);
#endif

    /// Returns the raw (unaccelerated) mouse X delta since last `BeginFrame()`.
    ///
    /// Only populated if `EnableRawInput()` was called. Resets each frame.
    [[nodiscard]] float GetRawMouseDeltaX() const { return m_rawMouseDX; }

    /// Returns the raw (unaccelerated) mouse Y delta since last `BeginFrame()`.
    ///
    /// Only populated if `EnableRawInput()` was called. Resets each frame.
    [[nodiscard]] float GetRawMouseDeltaY() const { return m_rawMouseDY; }

    /// Called by WndProc when a WM_INPUT message arrives.
    ///
    /// Extracts mouse delta from the RAWINPUT structure and accumulates
    /// into raw delta accumulators.
    ///
    /// # Safety
    /// Do not call directly — invoked by `PlatformWindow::WndProc` on WM_INPUT.
    ///
    /// # Arguments
    /// * `lParam` — The lParam from the WM_INPUT message (HRAWINPUT handle).
#ifdef _WIN32
    void OnRawMouseInput(LPARAM lParam);
#endif

    // ── Mouse Capture & Cursor ──────────────────────────────────────────

    /// Confines the mouse cursor to the window client area, or releases it.
    ///
    /// When captured, calls `ClipCursor()` with the window's client rect.
    /// The clip rect is automatically updated when the window moves or resizes
    /// (caller must call `UpdateMouseCapture()` on WM_MOVE/WM_SIZE).
    ///
    /// # Arguments
    /// * `capture` — `true` to confine cursor, `false` to release.
    /// * `hwnd` — The window to clip the cursor to.
#ifdef _WIN32
    void SetMouseCapture(bool capture, HWND hwnd);
#endif

    /// Re-clips the cursor to the current window rect if capture is active.
    ///
    /// Call from WM_MOVE and WM_SIZE handlers to keep the clip rect in sync
    /// when the window is repositioned or resized.
    ///
    /// # Arguments
    /// * `hwnd` — The window handle for client rect calculation.
#ifdef _WIN32
    void UpdateMouseCapture(HWND hwnd);
#endif

    /// Returns `true` if mouse capture is currently active.
    [[nodiscard]] bool IsMouseCaptured() const { return m_mouseCaptured; }

    /// Shows or hides the OS mouse cursor.
    ///
    /// Tracks desired visibility state internally. The actual `ShowCursor()`
    /// call happens in the WM_SETCURSOR handler via `ApplyCursorState()`.
    ///
    /// # Arguments
    /// * `visible` — `true` to show, `false` to hide.
    void SetCursorVisible(bool visible);

    /// Returns `true` if the cursor is set to be visible.
    [[nodiscard]] bool IsCursorVisible() const { return m_cursorVisible; }

    /// Sets the cursor shape (arrow, hand, crosshair, etc.).
    ///
    /// The shape is applied in the WM_SETCURSOR handler via `ApplyCursorState()`.
    ///
    /// # Arguments
    /// * `shape` — The desired cursor shape from the `CursorShape` enum.
    void SetCursorShape(CursorShape shape);

    /// Returns the currently configured cursor shape.
    [[nodiscard]] CursorShape GetCursorShape() const { return m_cursorShape; }

    /// Loads a custom cursor from an image file (.cur, .ani).
    ///
    /// Uses `LoadCursorFromFileW` on Windows. The file must be a valid .cur or
    /// .ani format. After a successful call the cursor shape is set to
    /// `CursorShape::Custom` and applied on the next `WM_SETCURSOR` event.
    ///
    /// # Arguments
    /// * `imagePath` — UTF-8 path to a .cur or .ani cursor file.
    /// * `hotspotX`  — Horizontal hotspot pixel offset (ignored for .cur files
    ///                 that embed their own hotspot).
    /// * `hotspotY`  — Vertical hotspot pixel offset (same caveat as hotspotX).
    ///
    /// # Returns
    /// `true` if the cursor was loaded successfully, `false` on failure (error
    /// is logged via `UZ_LOG(Input, Error, ...)`).
    [[nodiscard]] bool SetCustomCursor(const char* imagePath,
                                       int hotspotX = 0, int hotspotY = 0);

    /// Applies the current cursor visibility and shape.
    ///
    /// Called by WndProc in the WM_SETCURSOR handler. Returns `true` if the
    /// cursor was handled (caller should return TRUE from WndProc), `false`
    /// if default processing should continue.
    ///
    /// # Returns
    /// `true` if cursor was set (WndProc should return TRUE).
    /// `false` if WndProc should call DefWindowProc.
    bool ApplyCursorState();

    // ── Gamepad (XInput) ────────────────────────────────────────────────

    /// Returns `true` if the specified gamepad is connected.
    ///
    /// # Arguments
    /// * `player` — Player index (0..3).
    ///
    /// # Returns
    /// `false` if player index is out of range or controller disconnected.
    [[nodiscard]] bool IsGamepadConnected(int player) const;

    /// Returns `true` if the gamepad button is currently held down.
    ///
    /// # Arguments
    /// * `player` — Player index (0..3).
    /// * `button` — The button to query.
    ///
    /// # Returns
    /// `false` if player disconnected or index out of range.
    [[nodiscard]] bool IsGamepadButtonDown(int player, GamepadButton button) const;

    /// Returns `true` if the button transitioned from up to down this frame.
    ///
    /// # Arguments
    /// * `player` — Player index (0..3).
    /// * `button` — The button to query.
    [[nodiscard]] bool IsGamepadButtonJustPressed(int player, GamepadButton button) const;

    /// Returns `true` if the button transitioned from down to up this frame.
    ///
    /// # Arguments
    /// * `player` — Player index (0..3).
    /// * `button` — The button to query.
    [[nodiscard]] bool IsGamepadButtonJustReleased(int player, GamepadButton button) const;

    /// Returns the value of a gamepad axis with deadzone applied.
    ///
    /// Thumbstick axes return values in the range -1.0 to 1.0.
    /// Trigger axes return values in the range 0.0 to 1.0.
    /// Disconnected gamepads or invalid indices return 0.0.
    ///
    /// # Arguments
    /// * `player` — Player index (0..3).
    /// * `axis` — The axis to query.
    ///
    /// # Returns
    /// Deadzone-processed axis value. 0.0 if disconnected or invalid.
    [[nodiscard]] float GetGamepadAxis(int player, GamepadAxis axis) const;

    /// Sets gamepad vibration (rumble) motors.
    ///
    /// # Arguments
    /// * `player` — Player index (0..3).
    /// * `leftMotor` — Low-frequency motor intensity (0.0 to 1.0).
    /// * `rightMotor` — High-frequency motor intensity (0.0 to 1.0).
    ///
    /// # Side Effects
    /// Calls `XInputSetState()`. No-op if player disconnected or index invalid.
    void SetVibration(int player, float leftMotor, float rightMotor);

    /// Sets a callback invoked when a gamepad is connected.
    ///
    /// # Arguments
    /// * `cb` — Callback function, or nullptr to clear.
    /// * `userData` — Opaque pointer forwarded to the callback.
    void SetGamepadConnectedCallback(GamepadConnectionCallback cb, void* userData = nullptr) {
        m_gamepadConnectedCb = cb;
        m_gamepadConnectedUserData = userData;
    }

    /// Sets a callback invoked when a gamepad is disconnected.
    ///
    /// # Arguments
    /// * `cb` — Callback function, or nullptr to clear.
    /// * `userData` — Opaque pointer forwarded to the callback.
    void SetGamepadDisconnectedCallback(GamepadConnectionCallback cb, void* userData = nullptr) {
        m_gamepadDisconnectedCb = cb;
        m_gamepadDisconnectedUserData = userData;
    }

    // ── Action Mapping ──────────────────────────────────────────────────

    /// Binds a named action to a keyboard key.
    ///
    /// # Arguments
    /// * `name` - Human-readable action name (e.g., "jump", "fire").
    /// * `key` - The key that triggers this action.
    ///
    /// # Behavior
    /// Overwrites any existing binding for the same action name.
    void RegisterAction(const std::string& name, Key key);

    /// Returns `true` if the key bound to the named action is held down.
    ///
    /// # Arguments
    /// * `name` - Action name previously registered via `RegisterAction()`.
    ///
    /// # Returns
    /// `false` if the action name is not registered.
    [[nodiscard]] bool IsActionDown(const std::string& name) const;

    /// Returns `true` if the action's bound key was just pressed this frame.
    ///
    /// # Arguments
    /// * `name` - Action name previously registered via `RegisterAction()`.
    ///
    /// # Returns
    /// `false` if the action name is not registered.
    [[nodiscard]] bool IsActionJustPressed(const std::string& name) const;

    // ── Platform Callbacks (called by WndProc) ──────────────────────────

    /// Called by the platform layer when a key is pressed.
    ///
    /// # Safety
    /// Do not call directly — invoked by `PlatformWindow::WndProc` on WM_KEYDOWN.
    ///
    /// # Arguments
    /// * `vk` - Windows virtual key code (VK_*). Values >= 256 are ignored.
    void OnKeyDown(uint16_t vk);

    /// Called by the platform layer when a key is released.
    ///
    /// # Safety
    /// Do not call directly — invoked by `PlatformWindow::WndProc` on WM_KEYUP.
    ///
    /// # Arguments
    /// * `vk` - Windows virtual key code (VK_*). Values >= 256 are ignored.
    void OnKeyUp(uint16_t vk);

    /// Called by the platform layer when the mouse moves.
    ///
    /// # Safety
    /// Do not call directly — invoked by `PlatformWindow::WndProc` on WM_MOUSEMOVE.
    ///
    /// # Arguments
    /// * `x` - New mouse X in window-relative pixels.
    /// * `y` - New mouse Y in window-relative pixels.
    void OnMouseMove(float x, float y);

    /// Called by the platform layer when a mouse button is pressed/released.
    ///
    /// # Safety
    /// Do not call directly — invoked by `PlatformWindow::WndProc`.
    ///
    /// # Arguments
    /// * `btn` - Which mouse button changed state.
    /// * `down` - `true` if pressed, `false` if released.
    void OnMouseButton(MouseButton btn, bool down);

    /// Called by the platform layer when the scroll wheel moves.
    ///
    /// # Safety
    /// Do not call directly — invoked by `PlatformWindow::WndProc` on WM_MOUSEWHEEL.
    ///
    /// # Arguments
    /// * `delta` - Scroll amount (positive = up, negative = down).
    ///   Accumulated until the next `BeginFrame()`.
    void OnMouseScroll(float delta);

  private:
    // ── Keyboard State ──────────────────────────────────────────────────
    static constexpr int kMaxKeys = 256;

    bool m_keyCurrent[kMaxKeys] = {};
    bool m_keyPrevious[kMaxKeys] = {};

    // ── Mouse Button State ──────────────────────────────────────────────
    static constexpr int kMaxMouseButtons = static_cast<int>(MouseButton::Count);
    bool m_mouseCurrent[kMaxMouseButtons] = {};
    bool m_mousePrevious[kMaxMouseButtons] = {};

    // ── Mouse Position & Delta ──────────────────────────────────────────
    float m_mouseX = 0.f, m_mouseY = 0.f;
    float m_mouseDX = 0.f, m_mouseDY = 0.f;
    float m_prevMouseX = 0.f, m_prevMouseY = 0.f;
    float m_scrollDelta = 0.f;

    // ── Raw Mouse Input ─────────────────────────────────────────────────
    float m_rawMouseDX = 0.f;
    float m_rawMouseDY = 0.f;
    bool m_rawInputEnabled = false;

    // ── Mouse Capture & Cursor ──────────────────────────────────────────
    bool m_mouseCaptured = false;
    bool m_cursorVisible = true;
    CursorShape m_cursorShape = CursorShape::Arrow;
#ifdef _WIN32
    HCURSOR m_customCursor = nullptr; ///< Cursor handle loaded by SetCustomCursor(), destroyed in Shutdown().
#endif

    // ── Action Mapping ──────────────────────────────────────────────────
    std::unordered_map<std::string, Key> m_actions;

    // ── Gamepad State (XInput) ──────────────────────────────────────────
    std::array<GamepadState, kMaxGamepads> m_gamepadCurrent = {};
    std::array<GamepadState, kMaxGamepads> m_gamepadPrevious = {};

    // ── Gamepad Callbacks ───────────────────────────────────────────────
    GamepadConnectionCallback m_gamepadConnectedCb = nullptr;
    void* m_gamepadConnectedUserData = nullptr;
    GamepadConnectionCallback m_gamepadDisconnectedCb = nullptr;
    void* m_gamepadDisconnectedUserData = nullptr;

    /// Polls all 4 XInput controllers, updates state, fires connection callbacks.
    void PollGamepads();

    /// Validates a player index is in range [0, kMaxGamepads).
    [[nodiscard]] static bool IsValidPlayer(int player) {
        return player >= 0 && player < kMaxGamepads;
    }
};

} // namespace uz
