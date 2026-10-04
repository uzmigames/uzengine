/// UzEngine Input System Implementation (Windows)
///
/// Implements InputSystem for keyboard, mouse, gamepad, raw input, and cursor
/// management on Windows. Maintains per-frame state tracking (current vs previous)
/// for detecting transitions. Integrates with WM_* messages from
/// PlatformWindow::WndProc and XInput for gamepad polling.

#include "input.h"

#include "log.h"
#include "profiler.h"

#include <cstring>

#ifdef _WIN32
#include <hidusage.h>
#include <xinput.h>
#endif

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

/// Initializes input state arrays to zero (no keys/buttons pressed).
///
/// # Behavior
/// - Zeros both current and previous key arrays
/// - Zeros both current and previous mouse button arrays
/// - Zeros gamepad state arrays
/// - Mouse position and delta left at default (will update on first move)
void InputSystem::Init() {
    std::memset(m_keyCurrent, 0, sizeof(m_keyCurrent));
    std::memset(m_keyPrevious, 0, sizeof(m_keyPrevious));
    std::memset(m_mouseCurrent, 0, sizeof(m_mouseCurrent));
    std::memset(m_mousePrevious, 0, sizeof(m_mousePrevious));
    std::memset(m_keyPressedEdge, 0, sizeof(m_keyPressedEdge));
    std::memset(m_keyReleasedEdge, 0, sizeof(m_keyReleasedEdge));
    std::memset(m_mousePressedEdge, 0, sizeof(m_mousePressedEdge));
    std::memset(m_mouseReleasedEdge, 0, sizeof(m_mouseReleasedEdge));
    m_gamepadCurrent = {};
    m_gamepadPrevious = {};
    m_rawMouseDX = 0.f;
    m_rawMouseDY = 0.f;
    m_rawInputEnabled = false;
    m_mouseCaptured = false;
    m_cursorVisible = true;
    m_cursorShape = CursorShape::Arrow;
    UZ_LOG(Input, Info, "Input system initialized");
}

/// Clears action mappings and releases raw input.
void InputSystem::Shutdown() {
    m_actions.clear();

    // Release mouse capture if active
    if (m_mouseCaptured) {
#ifdef _WIN32
        ClipCursor(nullptr);
#endif
        m_mouseCaptured = false;
    }

#ifdef _WIN32
    // Destroy any custom cursor loaded via SetCustomCursor() to avoid a GDI handle leak
    if (m_customCursor) {
        DestroyCursor(m_customCursor);
        m_customCursor = nullptr;
    }
#endif

    // Stop vibration on all gamepads
    for (int i = 0; i < kMaxGamepads; ++i) {
        if (m_gamepadCurrent[i].connected) {
            SetVibration(i, 0.f, 0.f);
        }
    }

    UZ_LOG(Input, Info, "Input system shutdown");
}

/// Publishes the frame's input: snapshot, mouse delta, gamepads. Leaves the recorded edges alone.
void InputSystem::PrepareFrame() {
    UZ_PROFILE_FUNCTION();

    // Keyboard snapshot
    std::memcpy(m_keyPrevious, m_keyCurrent, sizeof(m_keyCurrent));

    // Mouse button snapshot
    std::memcpy(m_mousePrevious, m_mouseCurrent, sizeof(m_mouseCurrent));

    // Mouse delta from WM_MOUSEMOVE position
    m_mouseDX = m_mouseX - m_prevMouseX;
    m_mouseDY = m_mouseY - m_prevMouseY;
    m_prevMouseX = m_mouseX;
    m_prevMouseY = m_mouseY;

    // Poll XInput gamepads
    PollGamepads();
}

/// Ends the frame: drops the recorded edges and the per-frame accumulators.
void InputSystem::EndFrame() {
    std::memset(m_keyPressedEdge, 0, sizeof(m_keyPressedEdge));
    std::memset(m_keyReleasedEdge, 0, sizeof(m_keyReleasedEdge));
    std::memset(m_mousePressedEdge, 0, sizeof(m_mousePressedEdge));
    std::memset(m_mouseReleasedEdge, 0, sizeof(m_mouseReleasedEdge));

    m_scrollDelta = 0.f;
    m_rawMouseDX = 0.f;
    m_rawMouseDY = 0.f;
    // NOTE: m_charBuffer is NOT cleared here — it accumulates WM_CHAR events
    // from PumpMessages() which happens BEFORE BeginFrame(). Clearing here
    // would lose all chars typed this frame. Instead, call ClearTypedChars()
    // after all UI consumers have processed the chars.
}

/// Snapshots state and polls gamepads.
///
/// The boundary is exactly where it was: the previous frame's edges and accumulators are dropped and
/// the new snapshot is taken in one call, so every existing call site keeps its timing.
void InputSystem::BeginFrame() {
    EndFrame();
    PrepareFrame();
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard Queries
// ─────────────────────────────────────────────────────────────────────────────

bool InputSystem::IsKeyDown(Key key) const {
    uint16_t k = static_cast<uint16_t>(key);
    return k < kMaxKeys && m_keyCurrent[k];
}

bool InputSystem::IsKeyJustPressed(Key key) const {
    uint16_t k = static_cast<uint16_t>(key);
    // The recorded edge OR the snapshot transition: a press that also ended this frame is reported
    // by the edge, which a comparison of two snapshots cannot see (Parity-06 item 1.4).
    return k < kMaxKeys && (m_keyPressedEdge[k] || (m_keyCurrent[k] && !m_keyPrevious[k]));
}

bool InputSystem::IsKeyJustReleased(Key key) const {
    uint16_t k = static_cast<uint16_t>(key);
    return k < kMaxKeys && (m_keyReleasedEdge[k] || (!m_keyCurrent[k] && m_keyPrevious[k]));
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse Queries
// ─────────────────────────────────────────────────────────────────────────────

bool InputSystem::IsMouseDown(MouseButton btn) const {
    int b = static_cast<int>(btn);
    return b < kMaxMouseButtons && m_mouseCurrent[b];
}

bool InputSystem::IsMouseJustPressed(MouseButton btn) const {
    int b = static_cast<int>(btn);
    return b < kMaxMouseButtons && (m_mousePressedEdge[b] || (m_mouseCurrent[b] && !m_mousePrevious[b]));
}

bool InputSystem::IsMouseJustReleased(MouseButton btn) const {
    int b = static_cast<int>(btn);
    return b < kMaxMouseButtons && (m_mouseReleasedEdge[b] || (!m_mouseCurrent[b] && m_mousePrevious[b]));
}

// ─────────────────────────────────────────────────────────────────────────────
// Raw Mouse Input
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32
/// Registers for raw mouse input on the specified window.
bool InputSystem::EnableRawInput(HWND hwnd) {
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
    rid.usUsage = HID_USAGE_GENERIC_MOUSE;
    rid.dwFlags = 0;
    rid.hwndTarget = hwnd;

    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        UZ_LOG(Input, Error, "Failed to register raw input device: error={}",
               GetLastError());
        return false;
    }

    m_rawInputEnabled = true;
    UZ_LOG(Input, Info, "Raw mouse input enabled");
    return true;
}

/// Processes a WM_INPUT message and accumulates raw mouse deltas.
void InputSystem::OnRawMouseInput(LPARAM lParam) {
    UINT size = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
                    nullptr, &size, sizeof(RAWINPUTHEADER));

    if (size == 0) return;

    // Use stack buffer for typical RAWINPUT size (avoid heap allocation)
    alignas(RAWINPUT) uint8_t buffer[128];
    RAWINPUT* raw = nullptr;

    if (size <= sizeof(buffer)) {
        raw = reinterpret_cast<RAWINPUT*>(buffer);
    } else {
        // Extremely unlikely — RAWINPUT for mouse is ~48 bytes
        return;
    }

    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
                        raw, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
        return;
    }

    if (raw->header.dwType == RIM_TYPEMOUSE) {
        // Only handle relative mouse movement (not absolute, e.g. from tablets)
        if ((raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
            m_rawMouseDX += static_cast<float>(raw->data.mouse.lLastX);
            m_rawMouseDY += static_cast<float>(raw->data.mouse.lLastY);
        }
    }
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Mouse Capture & Cursor
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32
/// Confines cursor to window client area or releases it.
void InputSystem::SetMouseCapture(bool capture, HWND hwnd) {
    m_mouseCaptured = capture;
    if (capture && hwnd) {
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        // Convert client rect to screen coordinates
        POINT topLeft = {clientRect.left, clientRect.top};
        POINT bottomRight = {clientRect.right, clientRect.bottom};
        ClientToScreen(hwnd, &topLeft);
        ClientToScreen(hwnd, &bottomRight);

        RECT screenRect = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
        ClipCursor(&screenRect);
        UZ_LOG(Input, Info, "Mouse captured to window");
    } else {
        ClipCursor(nullptr);
        if (!capture) {
            UZ_LOG(Input, Info, "Mouse capture released");
        }
    }
}

/// Re-clips cursor if capture is active (call on window move/resize).
void InputSystem::UpdateMouseCapture(HWND hwnd) {
    if (!m_mouseCaptured || !hwnd) return;

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);

    POINT topLeft = {clientRect.left, clientRect.top};
    POINT bottomRight = {clientRect.right, clientRect.bottom};
    ClientToScreen(hwnd, &topLeft);
    ClientToScreen(hwnd, &bottomRight);

    RECT screenRect = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    ClipCursor(&screenRect);
}
#endif

/// Shows or hides the OS mouse cursor.
void InputSystem::SetCursorVisible(bool visible) {
    m_cursorVisible = visible;
}

/// Sets the cursor shape.
void InputSystem::SetCursorShape(CursorShape shape) {
    m_cursorShape = shape;
}

/// Loads a custom cursor from a .cur or .ani file and sets the shape to Custom.
bool InputSystem::SetCustomCursor(const char* imagePath, int /*hotspotX*/, int /*hotspotY*/) {
#ifdef _WIN32
    if (!imagePath) {
        UZ_LOG(Input, Error, "SetCustomCursor: imagePath is null");
        return false;
    }

    // Convert UTF-8 path to UTF-16 for Win32 API
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, imagePath, -1, nullptr, 0);
    if (wideLen <= 0) {
        UZ_LOG(Input, Error, "SetCustomCursor: failed to convert path to wide string: path='{}'", imagePath);
        return false;
    }

    std::wstring widePath(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, imagePath, -1, widePath.data(), wideLen);

    HCURSOR cursor = LoadCursorFromFileW(widePath.c_str());
    if (!cursor) {
        UZ_LOG(Input, Error, "SetCustomCursor: LoadCursorFromFileW failed: path='{}', error={}", imagePath, GetLastError());
        return false;
    }

    // Destroy any previously loaded custom cursor to avoid a resource leak
    if (m_customCursor) {
        DestroyCursor(m_customCursor);
    }

    m_customCursor = cursor;
    m_cursorShape  = CursorShape::Custom;
    return true;
#else
    (void)imagePath;
    return false;
#endif
}

/// Applies cursor visibility and shape (called from WM_SETCURSOR handler).
bool InputSystem::ApplyCursorState() {
#ifdef _WIN32
    if (!m_cursorVisible) {
        SetCursor(nullptr);
        return true;
    }

    // Map CursorShape to Win32 cursor resource ID
    // Use MAKEINTRESOURCEW explicitly — MinGW defaults to ANSI macros
    LPCWSTR cursorId = MAKEINTRESOURCEW(32512); // IDC_ARROW
    switch (m_cursorShape) {
    case CursorShape::Arrow:    cursorId = MAKEINTRESOURCEW(32512); break;
    case CursorShape::Hand:     cursorId = MAKEINTRESOURCEW(32649); break;
    case CursorShape::IBeam:    cursorId = MAKEINTRESOURCEW(32513); break;
    case CursorShape::Crosshair:cursorId = MAKEINTRESOURCEW(32515); break;
    case CursorShape::SizeNS:   cursorId = MAKEINTRESOURCEW(32645); break;
    case CursorShape::SizeWE:   cursorId = MAKEINTRESOURCEW(32644); break;
    case CursorShape::SizeNWSE: cursorId = MAKEINTRESOURCEW(32642); break;
    case CursorShape::SizeNESW: cursorId = MAKEINTRESOURCEW(32643); break;
    case CursorShape::Wait:     cursorId = MAKEINTRESOURCEW(32514); break;
    case CursorShape::No:       cursorId = MAKEINTRESOURCEW(32648); break;
    case CursorShape::Custom:
        // Use the pre-loaded custom cursor handle if available; fall back to
        // Arrow if SetCustomCursor() was never called successfully.
        SetCursor(m_customCursor ? m_customCursor : LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
        return true;
    }

    SetCursor(LoadCursorW(nullptr, cursorId));
    return true;
#else
    return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Gamepad (XInput)
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32

/// Applies radial deadzone to a thumbstick axis pair.
///
/// If the magnitude of (x, y) is below the deadzone threshold, both are zeroed.
/// Otherwise, the vector is rescaled so the output range starts at 0 just outside
/// the deadzone and reaches 1.0 at full deflection.
static void ApplyRadialDeadzone(float& outX, float& outY,
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

    // Normalize and rescale from deadzone..32767 to 0..1
    constexpr float kMaxStick = 32767.f;
    float normalizedMagnitude = (magnitude - dz) / (kMaxStick - dz);
    // Clamp to prevent exceeding 1.0 (raw values can hit 32768 for negative)
    if (normalizedMagnitude > 1.f) normalizedMagnitude = 1.f;

    // Preserve direction, apply rescaled magnitude
    float scale = normalizedMagnitude / magnitude;
    outX = fx * scale;
    outY = fy * scale;
}

/// Applies linear deadzone to a trigger axis.
///
/// XInput triggers are 0..255. Returns 0..1 with deadzone applied.
static float ApplyTriggerDeadzone(BYTE rawValue, BYTE deadzone) {
    if (rawValue < deadzone) return 0.f;
    return static_cast<float>(rawValue - deadzone) /
           static_cast<float>(255 - deadzone);
}

/// Maps XINPUT_GAMEPAD button mask to our GamepadButton indices.
static void ExtractButtons(bool* buttons, WORD xinputButtons) {
    buttons[static_cast<int>(GamepadButton::A)]        = (xinputButtons & XINPUT_GAMEPAD_A) != 0;
    buttons[static_cast<int>(GamepadButton::B)]        = (xinputButtons & XINPUT_GAMEPAD_B) != 0;
    buttons[static_cast<int>(GamepadButton::X)]        = (xinputButtons & XINPUT_GAMEPAD_X) != 0;
    buttons[static_cast<int>(GamepadButton::Y)]        = (xinputButtons & XINPUT_GAMEPAD_Y) != 0;
    buttons[static_cast<int>(GamepadButton::LB)]       = (xinputButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    buttons[static_cast<int>(GamepadButton::RB)]       = (xinputButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    buttons[static_cast<int>(GamepadButton::Start)]    = (xinputButtons & XINPUT_GAMEPAD_START) != 0;
    buttons[static_cast<int>(GamepadButton::Back)]     = (xinputButtons & XINPUT_GAMEPAD_BACK) != 0;
    buttons[static_cast<int>(GamepadButton::LThumb)]   = (xinputButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    buttons[static_cast<int>(GamepadButton::RThumb)]   = (xinputButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
    buttons[static_cast<int>(GamepadButton::DPadUp)]   = (xinputButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
    buttons[static_cast<int>(GamepadButton::DPadDown)] = (xinputButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    buttons[static_cast<int>(GamepadButton::DPadLeft)] = (xinputButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    buttons[static_cast<int>(GamepadButton::DPadRight)]= (xinputButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
}

/// Polls all 4 XInput controllers.
void InputSystem::PollGamepads() {
    UZ_PROFILE_FUNCTION();

    for (int i = 0; i < kMaxGamepads; ++i) {
        m_gamepadPrevious[i] = m_gamepadCurrent[i];

        XINPUT_STATE state = {};
        DWORD result = XInputGetState(static_cast<DWORD>(i), &state);

        bool wasConnected = m_gamepadPrevious[i].connected;
        bool nowConnected = (result == ERROR_SUCCESS);

        m_gamepadCurrent[i].connected = nowConnected;

        if (nowConnected) {
            const auto& gp = state.Gamepad;

            // Buttons
            ExtractButtons(m_gamepadCurrent[i].buttons, gp.wButtons);

            // Thumbstick axes with radial deadzone
            ApplyRadialDeadzone(
                m_gamepadCurrent[i].axes[static_cast<int>(GamepadAxis::LeftX)],
                m_gamepadCurrent[i].axes[static_cast<int>(GamepadAxis::LeftY)],
                gp.sThumbLX, gp.sThumbLY,
                XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);

            ApplyRadialDeadzone(
                m_gamepadCurrent[i].axes[static_cast<int>(GamepadAxis::RightX)],
                m_gamepadCurrent[i].axes[static_cast<int>(GamepadAxis::RightY)],
                gp.sThumbRX, gp.sThumbRY,
                XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

            // Triggers with linear deadzone
            m_gamepadCurrent[i].axes[static_cast<int>(GamepadAxis::LeftTrigger)] =
                ApplyTriggerDeadzone(gp.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
            m_gamepadCurrent[i].axes[static_cast<int>(GamepadAxis::RightTrigger)] =
                ApplyTriggerDeadzone(gp.bRightTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
        } else {
            // Zero out disconnected pad state
            m_gamepadCurrent[i] = {};
        }

        // Fire connection/disconnection callbacks
        if (!wasConnected && nowConnected) {
            UZ_LOG(Input, Info, "Gamepad {} connected", i);
            if (m_gamepadConnectedCb) {
                m_gamepadConnectedCb(i, m_gamepadConnectedUserData);
            }
        } else if (wasConnected && !nowConnected) {
            UZ_LOG(Input, Info, "Gamepad {} disconnected", i);
            if (m_gamepadDisconnectedCb) {
                m_gamepadDisconnectedCb(i, m_gamepadDisconnectedUserData);
            }
        }
    }
}

#else
// Non-Windows stub
void InputSystem::PollGamepads() {}
#endif

bool InputSystem::IsGamepadConnected(int player) const {
    return IsValidPlayer(player) && m_gamepadCurrent[player].connected;
}

bool InputSystem::IsGamepadButtonDown(int player, GamepadButton button) const {
    if (!IsValidPlayer(player) || !m_gamepadCurrent[player].connected) return false;
    int b = static_cast<int>(button);
    return b < static_cast<int>(GamepadButton::Count) && m_gamepadCurrent[player].buttons[b];
}

bool InputSystem::IsGamepadButtonJustPressed(int player, GamepadButton button) const {
    if (!IsValidPlayer(player)) return false;
    int b = static_cast<int>(button);
    if (b >= static_cast<int>(GamepadButton::Count)) return false;
    return m_gamepadCurrent[player].buttons[b] && !m_gamepadPrevious[player].buttons[b];
}

bool InputSystem::IsGamepadButtonJustReleased(int player, GamepadButton button) const {
    if (!IsValidPlayer(player)) return false;
    int b = static_cast<int>(button);
    if (b >= static_cast<int>(GamepadButton::Count)) return false;
    return !m_gamepadCurrent[player].buttons[b] && m_gamepadPrevious[player].buttons[b];
}

float InputSystem::GetGamepadAxis(int player, GamepadAxis axis) const {
    if (!IsValidPlayer(player) || !m_gamepadCurrent[player].connected) return 0.f;
    int a = static_cast<int>(axis);
    if (a >= static_cast<int>(GamepadAxis::Count)) return 0.f;
    return m_gamepadCurrent[player].axes[a];
}

void InputSystem::SetVibration(int player, float leftMotor, float rightMotor) {
#ifdef _WIN32
    if (!IsValidPlayer(player) || !m_gamepadCurrent[player].connected) return;

    // Clamp to 0..1 then scale to 0..65535
    auto clamp01 = [](float v) -> float {
        return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    };

    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed = static_cast<WORD>(clamp01(leftMotor) * 65535.f);
    vibration.wRightMotorSpeed = static_cast<WORD>(clamp01(rightMotor) * 65535.f);
    XInputSetState(static_cast<DWORD>(player), &vibration);
#else
    (void)player;
    (void)leftMotor;
    (void)rightMotor;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Action Mapping
// ─────────────────────────────────────────────────────────────────────────────

void InputSystem::RegisterAction(const std::string& name, Key key) {
    m_actions[name] = key;
}

bool InputSystem::IsActionDown(const std::string& name) const {
    auto it = m_actions.find(name);
    if (it == m_actions.end())
        return false;
    return IsKeyDown(it->second);
}

bool InputSystem::IsActionJustPressed(const std::string& name) const {
    auto it = m_actions.find(name);
    if (it == m_actions.end())
        return false;
    return IsKeyJustPressed(it->second);
}

// ─────────────────────────────────────────────────────────────────────────────
// Modifier State (Editor Injection)
// ─────────────────────────────────────────────────────────────────────────────

void InputSystem::SetModifierState(bool shift, bool ctrl, bool alt, bool meta) {
    // VK_LSHIFT = 0xA0, VK_RSHIFT = 0xA1, VK_SHIFT = 0x10
    m_keyCurrent[0xA0] = shift;   // VK_LSHIFT
    m_keyCurrent[0x10] = shift;   // VK_SHIFT (generic)

    // VK_LCONTROL = 0xA2, VK_RCONTROL = 0xA3, VK_CONTROL = 0x11
    m_keyCurrent[0xA2] = ctrl;    // VK_LCONTROL
    m_keyCurrent[0x11] = ctrl;    // VK_CONTROL (generic)

    // VK_LMENU = 0xA4, VK_RMENU = 0xA5, VK_MENU = 0x12
    m_keyCurrent[0xA4] = alt;     // VK_LMENU (Alt)
    m_keyCurrent[0x12] = alt;     // VK_MENU (generic)

    // VK_LWIN = 0x5B, VK_RWIN = 0x5C
    m_keyCurrent[0x5B] = meta;    // VK_LWIN
}

// ─────────────────────────────────────────────────────────────────────────────
// Platform Callbacks
// ─────────────────────────────────────────────────────────────────────────────

void InputSystem::OnKeyDown(uint16_t vk) {
    if (vk >= kMaxKeys)
        return;
    // Guarded against WM_KEYDOWN auto-repeat: holding a key must report one press, not one per repeat.
    if (!m_keyCurrent[vk])
        m_keyPressedEdge[vk] = true;
    m_keyCurrent[vk] = true;
}

void InputSystem::OnKeyUp(uint16_t vk) {
    if (vk >= kMaxKeys)
        return;
    if (m_keyCurrent[vk] || m_keyPressedEdge[vk])
        m_keyReleasedEdge[vk] = true;
    m_keyCurrent[vk] = false;
}

void InputSystem::OnMouseMove(float x, float y) {
    m_mouseX = x;
    m_mouseY = y;
}

void InputSystem::OnMouseButton(MouseButton btn, bool down) {
    int b = static_cast<int>(btn);
    if (b >= kMaxMouseButtons)
        return;
    if (down && !m_mouseCurrent[b])
        m_mousePressedEdge[b] = true;
    if (!down && (m_mouseCurrent[b] || m_mousePressedEdge[b]))
        m_mouseReleasedEdge[b] = true;
    m_mouseCurrent[b] = down;
}

void InputSystem::OnMouseScroll(float delta) {
    m_scrollDelta += delta;
}

void InputSystem::OnChar(uint16_t ch) {
    // Accept backspace (0x08), enter/return (0x0D), escape (0x1B),
    // and printable ASCII (0x20-0x7E).
    if (ch == 0x08 || ch == 0x0D || ch == 0x1B || (ch >= 0x20 && ch <= 0x7E)) {
        m_charBuffer += static_cast<char>(ch);
    }
}

} // namespace uz
