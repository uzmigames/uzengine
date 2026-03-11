#pragma once

/// UzEngine Input System
///
/// Unified keyboard, mouse, and gamepad input handling.
/// Wraps Win32 raw input messages and maps them to high-level events.
///
/// # Architecture
/// - **Input source**: WM_KEYDOWN/UP, WM_MOUSEMOVE, WM_*BUTTON messages
/// - **Key mapping**: Virtual key codes (VK_*) → uz::Key enum
/// - **Action mapping**: Input events → gameplay actions (Move, Jump, Attack)
/// - **State tracking**: IsPressed(), GetMouseDelta(), etc.
///
/// # Input Flow
/// ```
/// OS (WM_KEYDOWN) → PlatformWindow::PumpMessages()
///   → InputSystem::HandleMessage()
///   → Update key state + fire callbacks
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

// ── Input state ─────────────────────────────────────────────────────────────

class InputSystem {
  public:
    void Init();
    void Shutdown();

    /// Call once per frame before processing events.
    void BeginFrame();

    /// Key state queries.
    bool IsKeyDown(Key key) const;
    bool IsKeyJustPressed(Key key) const;
    bool IsKeyJustReleased(Key key) const;

    /// Mouse state.
    bool IsMouseDown(MouseButton btn) const;
    bool IsMouseJustPressed(MouseButton btn) const;
    bool IsMouseJustReleased(MouseButton btn) const;
    float GetMouseX() const { return m_mouseX; }
    float GetMouseY() const { return m_mouseY; }
    float GetMouseDeltaX() const { return m_mouseDX; }
    float GetMouseDeltaY() const { return m_mouseDY; }
    float GetScrollDelta() const { return m_scrollDelta; }

    /// Action mapping.
    void RegisterAction(const std::string& name, Key key);
    bool IsActionDown(const std::string& name) const;
    bool IsActionJustPressed(const std::string& name) const;

    /// Called by platform layer — do not call directly.
    void OnKeyDown(uint16_t vk);
    void OnKeyUp(uint16_t vk);
    void OnMouseMove(float x, float y);
    void OnMouseButton(MouseButton btn, bool down);
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
