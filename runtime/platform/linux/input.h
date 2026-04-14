#pragma once

/// UzEngine Input System
///
/// Unified keyboard, mouse, and gamepad input handling.
/// Wraps X11 key/button events and maps them to high-level actions.
///
/// # Architecture
/// - **Input source**: X11 KeyPress/KeyRelease, MotionNotify, ButtonPress/ButtonRelease
/// - **Key mapping**: X11 keysyms are translated to Windows VK_* codes by the platform
///   window layer, so the Key enum values are identical across platforms.
/// - **Action mapping**: Input events -> gameplay actions (Move, Jump, Attack)
/// - **State tracking**: IsPressed(), GetMouseDelta(), etc.
///
/// # Input Flow
/// ```
/// OS (X11 KeyPress) -> PlatformWindow::PumpMessages()
///   -> TranslateKeysym() -> InputSystem::OnKeyDown(vk)
///   -> Update key state + fire callbacks
/// ```
///
/// # Input Contexts
/// Game actions are context-aware:
/// - Menu context: Up/Down/Select (UI navigation)
/// - Gameplay context: WASD/Mouse/Gamepad (movement/camera)
/// Can rebind or disable contexts at runtime.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace uz {

/// Keyboard key codes (physical keys).
///
/// # Mapping
/// - Letters: ASCII codes (A='A', Z='Z')
/// - Numbers: ASCII codes (0='0', 9='9')
/// - Function keys: VK_F1..VK_F12 codes (0x70..0x7B)
/// - Special: Standard Windows VK_* codes (used cross-platform as canonical IDs)
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

    // ──── Punctuation / OEM Keys ────
    Grave = 0xC0, ///< Grave accent / tilde key (` / ~). Mapped to 0xC0 for cross-platform parity.
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

// ── Input state ─────────────────────────────────────────────────────────────

/// Unified input system for keyboard, mouse, and gamepad state tracking.
///
/// Maintains per-frame snapshots of key/button state (current vs previous)
/// to detect press/release transitions. Also tracks mouse position, delta,
/// and scroll wheel accumulation.
///
/// # Lifecycle
/// 1. Call `Init()` once at startup to zero all state
/// 2. Call `BeginFrame()` once per frame before processing OS messages
/// 3. Platform layer calls `OnKey*`, `OnMouse*` during message dispatch
/// 4. Game code queries `IsKeyDown()`, `IsActionDown()`, etc.
/// 5. Call `Shutdown()` to clear action mappings
///
/// # Thread Safety
/// NOT thread-safe. All calls must be from the main (message pump) thread.
///
/// # Example
/// ```cpp
/// InputSystem input;
/// input.Init();
/// input.RegisterAction("jump", Key::Space);
/// // ... per frame:
/// input.BeginFrame();
/// window.PumpMessages(); // triggers OnKey*/OnMouse* callbacks
/// if (input.IsActionJustPressed("jump")) { player.Jump(); }
/// ```
class InputSystem {
  public:
    /// Initializes all key and mouse state arrays to zero.
    ///
    /// Must be called once before any input processing begins.
    void Init();

    /// Clears action mappings and resets state.
    void Shutdown();

    /// Snapshots current state into previous state for transition detection.
    ///
    /// Must be called exactly once per frame, before the platform layer
    /// dispatches new X11 events. Computes mouse delta from position
    /// change since last frame and resets scroll accumulator.
    void BeginFrame();

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

    /// Returns the mouse X movement since last frame (pixels).
    [[nodiscard]] float GetMouseDeltaX() const { return m_mouseDX; }

    /// Returns the mouse Y movement since last frame (pixels).
    [[nodiscard]] float GetMouseDeltaY() const { return m_mouseDY; }

    /// Returns the accumulated scroll wheel delta since last `BeginFrame()`.
    ///
    /// Positive = scroll up, negative = scroll down.
    /// Resets to 0 at each `BeginFrame()`.
    [[nodiscard]] float GetScrollDelta() const { return m_scrollDelta; }

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

    /// Called by the platform layer when a key is pressed.
    ///
    /// # Safety
    /// Do not call directly — invoked by `PlatformWindow::PumpMessages` on KeyPress.
    ///
    /// # Arguments
    /// * `vk` - Translated virtual key code (VK_* equivalent). Values >= 256 are ignored.
    void OnKeyDown(uint16_t vk);

    /// Called by the platform layer when a key is released.
    ///
    /// # Safety
    /// Do not call directly — invoked by `PlatformWindow::PumpMessages` on KeyRelease.
    ///
    /// # Arguments
    /// * `vk` - Translated virtual key code (VK_* equivalent). Values >= 256 are ignored.
    void OnKeyUp(uint16_t vk);

    /// Called by the platform layer when the mouse moves.
    ///
    /// # Safety
    /// Do not call directly — invoked by `PlatformWindow::PumpMessages` on MotionNotify.
    ///
    /// # Arguments
    /// * `x` - New mouse X in window-relative pixels.
    /// * `y` - New mouse Y in window-relative pixels.
    void OnMouseMove(float x, float y);

    /// Called by the platform layer when a mouse button is pressed/released.
    ///
    /// # Safety
    /// Do not call directly — invoked by `PlatformWindow::PumpMessages`.
    ///
    /// # Arguments
    /// * `btn` - Which mouse button changed state.
    /// * `down` - `true` if pressed, `false` if released.
    void OnMouseButton(MouseButton btn, bool down);

    /// Called by the platform layer when the scroll wheel moves.
    ///
    /// # Safety
    /// Do not call directly — invoked by `PlatformWindow::PumpMessages` on scroll events.
    ///
    /// # Arguments
    /// * `delta` - Scroll amount (positive = up, negative = down).
    ///   Accumulated until the next `BeginFrame()`.
    void OnMouseScroll(float delta);

  private:
    static constexpr int kMaxKeys = 256;

    bool m_keyCurrent[kMaxKeys] = {};
    bool m_keyPrevious[kMaxKeys] = {};

    static constexpr int kMaxMouseButtons = static_cast<int>(MouseButton::Count);
    bool m_mouseCurrent[kMaxMouseButtons] = {};
    bool m_mousePrevious[kMaxMouseButtons] = {};

    float m_mouseX = 0.f, m_mouseY = 0.f;
    float m_mouseDX = 0.f, m_mouseDY = 0.f;
    float m_prevMouseX = 0.f, m_prevMouseY = 0.f;
    float m_scrollDelta = 0.f;

    std::unordered_map<std::string, Key> m_actions;
};

} // namespace uz
